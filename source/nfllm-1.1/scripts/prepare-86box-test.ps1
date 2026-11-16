param(
    [string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

function Read-Exact([System.IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -le 0) { throw "Unexpected end of disk image after $offset of $Count bytes." }
        $offset += $read
    }
    return ,$bytes
}

$workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$originalDisk = Join-Path $emulatorRoot 'disks\nfdos502-qemu.raw'
$testDisk = Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test.raw'
$machineDir = Join-Path $emulatorRoot 'machine\AM486DX4-96MB-NFLLM-TEST'
$configPath = Join-Path $machineDir '86box.cfg'
$appImage = Join-Path $workspace 'NFLLM\build\NFLLM-APP.IMG'
$modelImage = Join-Path $workspace 'NFLLM\build\NFLLM-MODEL.IMG'
$evidencePath = Join-Path $workspace 'NFLLM\build\logs\86box-test-preparation.json'

foreach ($required in @($originalDisk, $testDisk, $configPath, $appImage, $modelImage)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required test file is missing: $required" }
}
if ([IO.Path]::GetFullPath($originalDisk) -eq [IO.Path]::GetFullPath($testDisk)) {
    throw 'Refusing to write because original and test disk paths are identical.'
}

$liveVms = @(Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -like '*\AM486DX4-96MB\app\86Box.exe'
})
if ($liveVms.Count -gt 0) { throw 'An AM486DX4-96MB 86Box process is active; test disk will not be modified while mounted.' }

$originalHash = (Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
$testHashBefore = (Get-FileHash -LiteralPath $testDisk -Algorithm SHA256).Hash
if ($originalHash -ne $testHashBefore) {
    throw 'The test disk is no longer an untouched clone of the original; refusing the one-time AUTOEXEC patch.'
}

$appendLines = @(
    'SET PATH=A:\;%PATH%'
    'A:\COMPAT.EXE B:\STORIES.GGF > C:\COMPAT.LOG'
    'A:\NFLLM.EXE B:\STORIES.GGF -n 1 > C:\NFLLM.LOG'
    'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON'
)

$disk = [IO.File]::Open($testDisk, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    $disk.Position = 0
    $mbr = Read-Exact $disk 512
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'Test disk MBR signature is invalid.' }

    $partitions = @()
    for ($i = 446; $i -lt 510; $i += 16) {
        if ($mbr[$i + 4] -ne 0) {
            $partitions += [pscustomobject]@{
                Type = $mbr[$i + 4]
                StartLba = [BitConverter]::ToUInt32($mbr, $i + 8)
                Sectors = [BitConverter]::ToUInt32($mbr, $i + 12)
            }
        }
    }
    if ($partitions.Count -ne 1 -or $partitions[0].Type -ne 6) {
        throw 'Expected the cloned NF-DOS disk to contain one FAT16 (type 06) primary partition.'
    }
    $partition = $partitions[0]
    $volumeOffset = [int64]$partition.StartLba * 512
    $disk.Position = $volumeOffset
    $bpb = Read-Exact $disk 512
    if ($bpb[510] -ne 0x55 -or $bpb[511] -ne 0xaa) { throw 'FAT16 boot-sector signature is invalid.' }
    $bytesPerSector = [BitConverter]::ToUInt16($bpb, 11)
    $sectorsPerCluster = [int]$bpb[13]
    $reservedSectors = [BitConverter]::ToUInt16($bpb, 14)
    $fatCount = [int]$bpb[16]
    $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
    $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
    if ($bytesPerSector -ne 512 -or $sectorsPerCluster -le 0 -or $fatCount -lt 1 -or $rootEntries -le 0 -or $fatSectors -le 0) {
        throw 'FAT16 BPB fields are outside the supported test-disk profile.'
    }

    $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bytesPerSector)
    $fatStart = $volumeOffset + ([int64]$reservedSectors * $bytesPerSector)
    $rootStart = $fatStart + ([int64]$fatCount * $fatSectors * $bytesPerSector)
    $rootBytes = $rootEntries * 32
    $disk.Position = $rootStart
    $root = Read-Exact $disk $rootBytes
    $wantedName = [Text.Encoding]::ASCII.GetBytes('AUTOEXECBAT')
    $autoexecOffset = -1
    for ($i = 0; $i -lt $root.Length; $i += 32) {
        if ($root[$i] -eq 0) { break }
        if ($root[$i] -eq 0xe5) { continue }
        if ([Linq.Enumerable]::SequenceEqual([byte[]]$root[$i..($i + 10)], [byte[]]$wantedName)) {
            $autoexecOffset = $i
            break
        }
    }
    if ($autoexecOffset -lt 0) { throw 'AUTOEXEC.BAT was not found in the test disk root.' }

    $firstCluster = [BitConverter]::ToUInt16($root, $autoexecOffset + 26)
    $oldSize = [BitConverter]::ToUInt32($root, $autoexecOffset + 28)
    $fatBytes = $fatSectors * $bytesPerSector
    $disk.Position = $fatStart
    $fat0 = Read-Exact $disk $fatBytes
    if ($fatCount -gt 1) {
        $disk.Position = $fatStart + $fatBytes
        $fat1 = Read-Exact $disk $fatBytes
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$fat0, [byte[]]$fat1)) {
            throw 'FAT mirrors differ; refusing to patch the cloned system disk.'
        }
    }

    $clusterBytes = $sectorsPerCluster * $bytesPerSector
    $chain = @()
    $cluster = $firstCluster
    $maxClusters = [int]($fatBytes / 2)
    while ($cluster -ge 2 -and $cluster -lt 0xfff8 -and $chain.Count -lt $maxClusters) {
        if ($chain -contains $cluster) { throw 'AUTOEXEC.BAT FAT chain contains a loop.' }
        $chain += $cluster
        $cluster = [BitConverter]::ToUInt16($fat0, $cluster * 2)
    }
    if ($chain.Count -eq 0 -or $oldSize -gt ($chain.Count * $clusterBytes)) {
        throw 'AUTOEXEC.BAT has an invalid FAT16 cluster chain.'
    }

    $oldContent = [byte[]]::new($oldSize)
    $readPosition = 0
    foreach ($currentCluster in $chain) {
        if ($readPosition -ge $oldContent.Length) { break }
        $clusterOffset = $volumeOffset + [int64]($reservedSectors + ($fatCount * $fatSectors) + $rootSectors) * $bytesPerSector + ([int64]($currentCluster - 2) * $clusterBytes)
        $disk.Position = $clusterOffset
        $clusterData = Read-Exact $disk $clusterBytes
        $take = [Math]::Min($clusterData.Length, $oldContent.Length - $readPosition)
        [Buffer]::BlockCopy($clusterData, 0, $oldContent, $readPosition, $take)
        $readPosition += $take
    }

    $oldText = [Text.Encoding]::ASCII.GetString($oldContent)
    if ($oldText.Contains('NFLLM_TEST_FINISHED')) { throw 'The NFLLM test commands are already present; refusing a duplicate patch.' }
    $newText = $oldText.TrimEnd([char]13, [char]10) + "`r`n`r`n" + ($appendLines -join "`r`n") + "`r`n"
    $newContent = [Text.Encoding]::ASCII.GetBytes($newText)
    if ($newContent.Length -gt ($chain.Count * $clusterBytes)) { throw 'AUTOEXEC.BAT has insufficient already-allocated cluster space.' }

    $paddedContent = [byte[]]::new($chain.Count * $clusterBytes)
    [Buffer]::BlockCopy($newContent, 0, $paddedContent, 0, $newContent.Length)
    $writePosition = 0
    foreach ($currentCluster in $chain) {
        $clusterOffset = $volumeOffset + [int64]($reservedSectors + ($fatCount * $fatSectors) + $rootSectors) * $bytesPerSector + ([int64]($currentCluster - 2) * $clusterBytes)
        $disk.Position = $clusterOffset
        $disk.Write($paddedContent, $writePosition, $clusterBytes)
        $writePosition += $clusterBytes
    }
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$newContent.Length), 0, $root, $autoexecOffset + 28, 4)
    $disk.Position = $rootStart
    $disk.Write($root, 0, $root.Length)
    $disk.Flush($true)

    $config = Get-Content -LiteralPath $configPath -Raw
    $config = [regex]::Replace($config, '(?m)^hdd_01_fn\s*=.*$', 'hdd_01_fn = ../../disks/nfdos502-nfllm-test.raw')
    if ($config -notmatch '(?m)^hdd_01_fn\s*=\s*\.\./\.\./disks/nfdos502-nfllm-test\.raw\s*$') { throw 'Cloned 86Box config does not point at the cloned system disk.' }
    $floppyBlock = @'
[Floppy and CD-ROM drives]
fdd_01_type = 35_2hd
fdd_01_fn = ../../../../NFLLM/build/NFLLM-APP.IMG
fdd_02_type = 35_2hd
fdd_02_fn = ../../../../NFLLM/build/NFLLM-MODEL.IMG
'@
    if ($config -notmatch '(?m)^\[Floppy and CD-ROM drives\]') { throw 'Cloned 86Box floppy section is missing.' }
    $config = [regex]::Replace($config, '(?ms)^\[Floppy and CD-ROM drives\].*?(?=^\[|\z)', $floppyBlock + "`r`n")
    $temporaryConfig = "$configPath.nfllm.tmp"
    [IO.File]::WriteAllText($temporaryConfig, $config, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporaryConfig -Destination $configPath -Force

    foreach ($relative in @('../../disks/nfdos502-nfllm-test.raw', '../../../../NFLLM/build/NFLLM-APP.IMG', '../../../../NFLLM/build/NFLLM-MODEL.IMG')) {
        $resolved = [IO.Path]::GetFullPath((Join-Path $machineDir $relative))
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "86Box config path does not resolve: $relative -> $resolved" }
    }
    $disk.Flush($true)
}
finally {
    $disk.Dispose()
}

$testHashAfter = (Get-FileHash -LiteralPath $testDisk -Algorithm SHA256).Hash
$result = [pscustomobject]@{
    disk = $testDisk
    original_disk_unchanged_hash = $originalHash
    test_disk_hash_before = $testHashBefore
    test_disk_hash_after = $testHashAfter
    partition_type = ('0x{0:X2}' -f $partition.Type)
    fat16_cluster_bytes = $clusterBytes
    autoexec_old_bytes = $oldSize
    autoexec_new_bytes = $newContent.Length
    autoexec_clusters = $chain
    commands = $appendLines
    config = $configPath
    app_floppy = $appImage
    model_floppy = $modelImage
}
[IO.File]::WriteAllText($evidencePath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$result
