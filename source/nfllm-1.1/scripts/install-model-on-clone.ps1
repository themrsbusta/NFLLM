param(
    [string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ModelPath = '',
    [string]$GuestFilename = 'STORIES.GGF',
    [string]$ExpectedModelSha256 = ''
)

$ErrorActionPreference = 'Stop'

function Set-Field($Object, [string]$Name, $Value) {
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        Add-Member -InputObject $Object -MemberType NoteProperty -Name $Name -Value $Value
    } else {
        $Object.$Name = $Value
    }
}

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

function Get-RootEntry([byte[]]$Root, [byte[]]$Name, [bool]$FindFree) {
    for ($offset = 0; $offset -lt $Root.Length; $offset += 32) {
        if ($FindFree -and ($Root[$offset] -eq 0 -or $Root[$offset] -eq 0xe5)) { return $offset }
        if (-not $FindFree -and $Root[$offset] -ne 0 -and $Root[$offset] -ne 0xe5 -and
            [Linq.Enumerable]::SequenceEqual([byte[]]$Root[$offset..($offset + 10)], $Name)) { return $offset }
    }
    return -1
}

function Get-FatChain([byte[]]$Fat, [int]$FirstCluster, [int]$MaxClusters) {
    $chain = [System.Collections.Generic.List[int]]::new()
    $cluster = $FirstCluster
    while ($cluster -ge 2 -and $cluster -lt 0xfff8 -and $chain.Count -lt $MaxClusters) {
        if ($chain.Contains($cluster)) { throw "FAT16 chain loops at cluster $cluster." }
        $chain.Add($cluster)
        $cluster = [BitConverter]::ToUInt16($Fat, $cluster * 2)
    }
    if ($cluster -lt 0xfff8) { throw "FAT16 chain ended with invalid marker $cluster." }
    return ,$chain.ToArray()
}

function Get-FileContentFromChain([System.IO.Stream]$Stream, [int64]$DataStart, [int]$ClusterBytes, [int[]]$Chain, [int]$Length) {
    if ($Length -gt ([int64]$Chain.Length * $ClusterBytes)) { throw 'FAT16 file length exceeds its cluster chain.' }
    $content = [byte[]]::new($Length)
    $position = 0
    foreach ($cluster in $Chain) {
        if ($position -ge $Length) { break }
        $Stream.Position = $DataStart + ([int64]($cluster - 2) * $ClusterBytes)
        $block = Read-Exact $Stream $ClusterBytes
        $count = [Math]::Min($ClusterBytes, $Length - $position)
        [Buffer]::BlockCopy($block, 0, $content, $position, $count)
        $position += $count
    }
    return ,$content
}

$workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$originalDisk = Join-Path $emulatorRoot 'disks\nfdos502-qemu.raw'
$testDisk = Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test.raw'
$backupDisk = Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test-before-model.raw'
$machineDir = Join-Path $emulatorRoot 'machine\AM486DX4-96MB-NFLLM-TEST'
$configPath = Join-Path $machineDir '86box.cfg'
$guestFile = [IO.Path]::GetFileName($GuestFilename).ToUpperInvariant()
$guestStem = [IO.Path]::GetFileNameWithoutExtension($guestFile)
$guestExtension = [IO.Path]::GetExtension($guestFile).TrimStart('.')
if ($guestFile -ne $GuestFilename.ToUpperInvariant() -or
    $guestFile -notmatch '^[A-Z0-9]{1,8}\.[A-Z0-9]{1,3}$') {
    throw 'GuestFilename must be an 8.3 DOS filename without a directory.'
}
$modelName = [Text.Encoding]::ASCII.GetBytes($guestStem.PadRight(8) + $guestExtension.PadRight(3))
if ([string]::IsNullOrWhiteSpace($ModelPath)) {
    $modelPath = Join-Path $workspace 'NFLLM\models\stories260K-f32.gguf'
} else {
    $modelPath = [IO.Path]::GetFullPath($ModelPath)
}
$expectedModelHash = $ExpectedModelSha256.ToUpperInvariant()
if ([string]::IsNullOrWhiteSpace($expectedModelHash) -and $guestFile -eq 'STORIES.GGF') {
    $expectedModelHash = '270CBA1BD5109F42D03350F60406024560464DB173C0E387D91F0426D3BD256D'
}
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
$evidencePath = Join-Path $workspace "NFLLM\build\logs\model-install-$($guestStem.ToLowerInvariant()).json"
$expectedOriginalHash = '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3'
$vmExe = Join-Path $emulatorRoot 'app\86Box.exe'
$taskId = "nfllm-install-$($guestStem.ToLowerInvariant())-on-clone-$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
$registered = $false
$disk = $null
$exitCode = 1

try {
    foreach ($path in @($originalDisk, $testDisk, $configPath, $modelPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required input is missing: $path" }
    }
    $vmProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $vmExe })
    if ($vmProcesses.Count -gt 0) { throw 'An 86Box instance is active; refusing to modify the mounted test disk.' }
    $originalHash = (Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
    if ($originalHash -ne $expectedOriginalHash) { throw 'The NF-DOS original disk hash changed; refusing to continue.' }

    $active = @($registry.entries | Where-Object state -eq 'active').Count + [int]$registry.known_external_active_managed_count
    if ($active -ge [int]$registry.max_active_managed_processes) { throw 'Managed process capacity reached.' }
    $current = [Diagnostics.Process]::GetCurrentProcess()
    $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.Id)"
    $processEntry = [pscustomobject]@{
        task_id=$taskId;vm_name='NFLLM model installation in isolated clone';state='active';pid=$current.Id
        ppid=$cim.ParentProcessId;creation_time=$current.StartTime.ToString('o');executable=$cim.ExecutablePath
        command_line=$cim.CommandLine;classification='temporary';scope='Copy the verified GGUF model into C: of the isolated NF-DOS test clone and update test commands'
        registered_at=(Get-Date).ToString('o');shutdown_policy='finish disk write, flush, validate, and reconcile';source_config=$configPath
        exit_code=$null;stopped_at=$null;stop_reason=$null
    }
    $registry.entries = @($registry.entries) + $processEntry
    [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    $registered = $true

    if (-not (Test-Path -LiteralPath $backupDisk)) {
        Copy-Item -LiteralPath $testDisk -Destination $backupDisk
        $backupHash = (Get-FileHash -LiteralPath $backupDisk -Algorithm SHA256).Hash
        $sourceCloneHash = (Get-FileHash -LiteralPath $testDisk -Algorithm SHA256).Hash
        if ($backupHash -ne $sourceCloneHash) { throw 'Pre-model backup does not match the current isolated test clone.' }
    }

    $modelBytes = [IO.File]::ReadAllBytes($modelPath)
    if ($modelBytes.Length -lt 8 -or [Text.Encoding]::ASCII.GetString($modelBytes, 0, 4) -ne 'GGUF' -or
        [BitConverter]::ToUInt32($modelBytes, 4) -ne 3) { throw 'Model file failed size/magic/version validation.' }
    $modelHash = (Get-FileHash -LiteralPath $modelPath -Algorithm SHA256).Hash
    if (-not [string]::IsNullOrWhiteSpace($expectedModelHash) -and $modelHash -ne $expectedModelHash) {
        throw 'Model SHA-256 differs from the expected source artifact.'
    }

    $disk = [IO.File]::Open($testDisk, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $disk.Position = 0
    $mbr = Read-Exact $disk 512
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'Test clone MBR signature is invalid.' }
    $partitions = @()
    for ($offset = 446; $offset -lt 510; $offset += 16) {
        if ($mbr[$offset + 4] -ne 0) {
            $partitions += [pscustomobject]@{Type=$mbr[$offset+4];Start=[BitConverter]::ToUInt32($mbr,$offset+8);Sectors=[BitConverter]::ToUInt32($mbr,$offset+12)}
        }
    }
    if ($partitions.Count -ne 1 -or $partitions[0].Type -ne 6) { throw 'Expected one FAT16 type-06 primary partition.' }
    $partition = $partitions[0]
    $volumeStart = [int64]$partition.Start * 512
    $disk.Position = $volumeStart
    $bpb = Read-Exact $disk 512
    $bps = [BitConverter]::ToUInt16($bpb, 11)
    $spc = [int]$bpb[13]
    $reserved = [BitConverter]::ToUInt16($bpb, 14)
    $fatCount = [int]$bpb[16]
    $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
    $total16 = [BitConverter]::ToUInt16($bpb, 19)
    $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
    $totalSectors = if ($total16 -ne 0) { [int64]$total16 } else { [int64][BitConverter]::ToUInt32($bpb, 32) }
    if ($bpb[510] -ne 0x55 -or $bpb[511] -ne 0xaa -or $bps -ne 512 -or $spc -le 0 -or $fatCount -lt 1 -or $fatSectors -le 0) {
        throw 'FAT16 boot parameters failed validation.'
    }
    $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bps)
    $fatBytes = $fatSectors * $bps
    $clusterBytes = $spc * $bps
    $fatStart = $volumeStart + ([int64]$reserved * $bps)
    $rootStart = $fatStart + ([int64]$fatCount * $fatBytes)
    $dataStart = $rootStart + ([int64]$rootSectors * $bps)
    $dataClusters = [int][Math]::Floor(($totalSectors - $reserved - ($fatCount * $fatSectors) - $rootSectors) / $spc)
    $disk.Position = $fatStart
    $fat = Read-Exact $disk $fatBytes
    for ($copy = 1; $copy -lt $fatCount; $copy++) {
        $disk.Position = $fatStart + ([int64]$copy * $fatBytes)
        $mirror = Read-Exact $disk $fatBytes
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$fat, [byte[]]$mirror)) { throw 'FAT mirrors differ; refusing disk modification.' }
    }
    $root = [byte[]]::new($rootEntries * 32)
    $disk.Position = $rootStart
    $root = Read-Exact $disk $root.Length
    $modelEntry = Get-RootEntry $root $modelName $false
    $autoexecName = [Text.Encoding]::ASCII.GetBytes('AUTOEXECBAT')
    $autoexecEntry = Get-RootEntry $root $autoexecName $false
    if ($autoexecEntry -lt 0) { throw 'AUTOEXEC.BAT is missing from the cloned root directory.' }

    if ($modelEntry -ge 0) {
        $first = [BitConverter]::ToUInt16($root, $modelEntry + 26)
        $size = [BitConverter]::ToUInt32($root, $modelEntry + 28)
        $chain = Get-FatChain $fat $first $dataClusters
        $existing = Get-FileContentFromChain $disk $dataStart $clusterBytes $chain $size
        if ((Get-FileHash -InputStream ([IO.MemoryStream]::new($existing)) -Algorithm SHA256).Hash -ne $modelHash) {
            throw "A different C:\$guestFile already exists in the clone; no overwrite performed."
        }
    } else {
        $needed = [int][Math]::Ceiling($modelBytes.Length / [double]$clusterBytes)
        $free = [System.Collections.Generic.List[int]]::new()
        for ($cluster = 2; $cluster -lt [Math]::Min($dataClusters + 2, ($fatBytes / 2)); $cluster++) {
            if ([BitConverter]::ToUInt16($fat, $cluster * 2) -eq 0) {
                $free.Add($cluster)
                if ($free.Count -eq $needed) { break }
            }
        }
        if ($free.Count -ne $needed) { throw "FAT16 has only $($free.Count) free clusters; $needed required." }
        $chain = $free.ToArray()
        for ($i = 0; $i -lt $chain.Length; $i++) {
            $next = if ($i -eq $chain.Length - 1) { 0xffff } else { $chain[$i + 1] }
            [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$next), 0, $fat, $chain[$i] * 2, 2)
        }
        $padded = [byte[]]::new($chain.Length * $clusterBytes)
        [Buffer]::BlockCopy($modelBytes, 0, $padded, 0, $modelBytes.Length)
        $writeOffset = 0
        foreach ($cluster in $chain) {
            $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
            $disk.Write($padded, $writeOffset, $clusterBytes)
            $writeOffset += $clusterBytes
        }
        for ($copy = 0; $copy -lt $fatCount; $copy++) {
            $disk.Position = $fatStart + ([int64]$copy * $fatBytes)
            $disk.Write($fat, 0, $fat.Length)
        }
        $modelEntry = Get-RootEntry $root $modelName $true
        if ($modelEntry -lt 0) { throw "The FAT16 root directory has no free entry for $guestFile." }
        [Buffer]::BlockCopy($modelName, 0, $root, $modelEntry, 11)
        $root[$modelEntry + 11] = 0x20
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0), 0, $root, $modelEntry + 22, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0x5a21), 0, $root, $modelEntry + 24, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0), 0, $root, $modelEntry + 20, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$chain[0]), 0, $root, $modelEntry + 26, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$modelBytes.Length), 0, $root, $modelEntry + 28, 4)
    }

    $autoSize = [BitConverter]::ToUInt32($root, $autoexecEntry + 28)
    $autoFirst = [BitConverter]::ToUInt16($root, $autoexecEntry + 26)
    $autoChain = Get-FatChain $fat $autoFirst $dataClusters
    $autoBytes = Get-FileContentFromChain $disk $dataStart $clusterBytes $autoChain $autoSize
    $autoText = [Text.Encoding]::ASCII.GetString($autoBytes)
    $legacyModelPath = 'B:\' + $guestFile
    $guestModelPath = 'C:\' + $guestFile
    $autoText = $autoText.Replace($legacyModelPath, $guestModelPath)
    if ($autoText.Contains($legacyModelPath)) {
        throw "AUTOEXEC.BAT retains an unavailable B: model path for $guestFile."
    }
    $autoNewBytes = [Text.Encoding]::ASCII.GetBytes($autoText)
    if ($autoNewBytes.Length -gt ($autoChain.Length * $clusterBytes)) { throw 'AUTOEXEC.BAT has insufficient cluster space.' }
    $autoPadded = [byte[]]::new($autoChain.Length * $clusterBytes)
    [Buffer]::BlockCopy($autoNewBytes, 0, $autoPadded, 0, $autoNewBytes.Length)
    $writeOffset = 0
    foreach ($cluster in $autoChain) {
        $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
        $disk.Write($autoPadded, $writeOffset, $clusterBytes)
        $writeOffset += $clusterBytes
    }
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$autoNewBytes.Length), 0, $root, $autoexecEntry + 28, 4)
    $disk.Position = $rootStart
    $disk.Write($root, 0, $root.Length)
    $disk.Flush($true)

    $config = Get-Content -LiteralPath $configPath -Raw
    $config = [regex]::Replace($config, '(?m)^fdd_02_fn\s*=.*\r?\n?', '')
    $config = [regex]::Replace($config, '(?m)^fdd_02_type\s*=.*\r?\n?', '')
    $tempConfig = "$configPath.nfllm.tmp"
    [IO.File]::WriteAllText($tempConfig, $config, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $tempConfig -Destination $configPath -Force

    $disk.Position = $rootStart
    $checkRoot = Read-Exact $disk $root.Length
    $checkEntry = Get-RootEntry $checkRoot $modelName $false
    if ($checkEntry -lt 0 -or [BitConverter]::ToUInt32($checkRoot, $checkEntry + 28) -ne $modelBytes.Length) {
        throw "$guestFile directory entry failed read-back verification."
    }
    $checkChain = Get-FatChain $fat ([BitConverter]::ToUInt16($checkRoot, $checkEntry + 26)) $dataClusters
    $checkContent = Get-FileContentFromChain $disk $dataStart $clusterBytes $checkChain $modelBytes.Length
    $checkHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($checkContent)) -Algorithm SHA256).Hash
    if ($checkHash -ne $modelHash) { throw 'STORIES.GGF bytes failed read-back SHA-256 verification.' }
    $disk.Flush($true)
    $disk.Dispose()
    $disk = $null
    $exitCode = 0

    $result = [pscustomobject]@{
        original_disk = $originalDisk
        original_disk_sha256 = $originalHash
        test_disk = $testDisk
        test_disk_sha256 = (Get-FileHash -LiteralPath $testDisk -Algorithm SHA256).Hash
        backup_disk = $backupDisk
        model_file = $guestModelPath
        model_bytes = $modelBytes.Length
        model_sha256 = $modelHash
        model_clusters = $chain
        cluster_bytes = $clusterBytes
        autoexec_bytes = $autoNewBytes.Length
        autoexec_model_paths = $guestModelPath
        fdd2_removed_from_test_config = $true
    }
    [IO.File]::WriteAllText($evidencePath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $result
}
catch {
    if ($registered -and $null -ne $processEntry) { Set-Field $processEntry 'stop_reason' $_.Exception.Message }
    throw
}
finally {
    if ($null -ne $disk) { $disk.Dispose() }
    if ($registered) {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $processEntry = $registry.entries | Where-Object task_id -eq $taskId
        Set-Field $processEntry 'state' 'exited'
        Set-Field $processEntry 'exit_code' $exitCode
        Set-Field $processEntry 'stopped_at' (Get-Date).ToString('o')
        if ($exitCode -ne 0 -and [string]::IsNullOrEmpty($processEntry.stop_reason)) {
            Set-Field $processEntry 'stop_reason' 'Disk/model installation did not complete; see PowerShell error output.'
        }
        [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    }
}
