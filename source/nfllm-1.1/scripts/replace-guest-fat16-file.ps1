param(
    [Parameter(Mandatory=$true)][string]$DiskPath,
    [Parameter(Mandatory=$true)][string]$SourceFilePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9]{1,8}\.[A-Za-z0-9]{1,3}$')][string]$DosName
)

$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$diskRoot = [IO.Path]::GetFullPath((Join-Path $emulatorRoot 'disks'))
$originalDisk = [IO.Path]::GetFullPath((Join-Path $diskRoot 'nfdos502-qemu.raw'))
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
$diskPath = [IO.Path]::GetFullPath($DiskPath)
$sourceFilePath = [IO.Path]::GetFullPath($SourceFilePath)
$dosName = $DosName.ToUpperInvariant()
$vmExe = [IO.Path]::GetFullPath((Join-Path $emulatorRoot 'app\86Box.exe'))
$qemuExe = [IO.Path]::GetFullPath('C:\Program Files\qemu\qemu-system-i386.exe')
$taskId = "nfllm-fat16-replace-$($dosName.Replace('.','-'))-$((Get-Date).ToString('yyyyMMdd-HHmmss-fff'))"
$disk = $null
$registryRegistered = $false
$exitCode = 1

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -le 0) { throw "Unexpected end of image at $($Stream.Position)." }
        $offset += $read
    }
    return ,$bytes
}

function Get-RootEntry([byte[]]$Root, [byte[]]$Name) {
    for ($offset = 0; $offset -lt $Root.Length; $offset += 32) {
        if ($Root[$offset] -eq 0) { break }
        if ($Root[$offset] -ne 0xe5 -and $Root[$offset + 11] -ne 0x0f -and
            [Linq.Enumerable]::SequenceEqual([byte[]]$Root[$offset..($offset + 10)], $Name)) {
            return $offset
        }
    }
    return -1
}

function Get-Fat16Chain([byte[]]$Fat, [int]$FirstCluster, [int]$MaximumClusters) {
    $chain = [Collections.Generic.List[int]]::new()
    $cluster = $FirstCluster
    while ($cluster -ge 2 -and $cluster -lt 0xfff8 -and $chain.Count -lt $MaximumClusters) {
        if ($chain.Contains($cluster)) { throw "FAT16 chain loops at cluster $cluster." }
        $chain.Add($cluster)
        $offset = $cluster * 2
        if ($offset + 1 -ge $Fat.Length) { throw 'FAT16 chain points outside the FAT.' }
        $cluster = [BitConverter]::ToUInt16($Fat, $offset)
    }
    if ($cluster -lt 0xfff8) { throw "FAT16 chain ended with invalid marker $cluster." }
    return ,$chain.ToArray()
}

function Write-Registry($Registry) {
    $temporary = "$registryPath.$PID.tmp"
    [IO.File]::WriteAllText($temporary, ($Registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary, $registryPath, $true)
}

try {
    if (-not $diskPath.StartsWith($diskRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $diskPath.Equals($originalDisk, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Only a cloned disk under the emulator disk directory may be modified.'
    }
    if (-not (Test-Path -LiteralPath $diskPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sourceFilePath -PathType Leaf)) {
        throw 'The cloned disk or replacement file is missing.'
    }
    if ((Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash -ne
        '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3') {
        throw 'The preserved original NF-DOS disk hash differs from its baseline.'
    }
    if (@(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -eq $vmExe -or $_.ExecutablePath -eq $qemuExe
    }).Count -gt 0) { throw 'A VM is active; refusing to modify a mounted guest disk.' }

    $registryLock = [IO.File]::Open("$registryPath.lock", [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $active = @($registry.entries | Where-Object state -in @('active','starting')).Count +
                  [int]$registry.known_external_active_managed_count
        if ($active -ge [int]$registry.max_active_managed_processes) {
            throw 'Managed process capacity reached.'
        }
        $current = [Diagnostics.Process]::GetCurrentProcess()
        $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.Id)"
        $entry = [pscustomobject]@{
            task_id=$taskId;vm_name="FAT16 root-file replacement $dosName";state='active'
            pid=$current.Id;ppid=$cim.ParentProcessId;creation_time=$current.StartTime.ToString('o')
            executable=$cim.ExecutablePath;command_line=$cim.CommandLine
            classification='temporary';scope="Replace $dosName in one isolated NF-DOS test clone"
            registered_at=(Get-Date).ToString('o');shutdown_policy='flush guest file data and reconcile registry'
            source_config=$diskPath;exit_code=$null;stopped_at=$null;stop_reason=$null
        }
        $registry.entries = @($registry.entries) + $entry
        Write-Registry $registry
        $registryRegistered = $true
    } finally { $registryLock.Dispose() }

    $replacement = [IO.File]::ReadAllBytes($sourceFilePath)
    $disk = [IO.File]::Open($diskPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $mbr = Read-Exact $disk 512
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'Invalid MBR signature.' }
    $partition = $null
    for ($offset = 446; $offset -lt 510; $offset += 16) {
        if ($mbr[$offset + 4] -eq 6) {
            $partition = [pscustomobject]@{
                start=[BitConverter]::ToUInt32($mbr, $offset + 8)
                sectors=[BitConverter]::ToUInt32($mbr, $offset + 12)
            }
            break
        }
    }
    if (-not $partition) { throw 'A FAT16 primary partition was not found.' }
    $volumeStart = [int64]$partition.start * 512
    $disk.Position = $volumeStart
    $bpb = Read-Exact $disk 512
    $bps = [BitConverter]::ToUInt16($bpb, 11)
    $spc = [int]$bpb[13]
    $reserved = [BitConverter]::ToUInt16($bpb, 14)
    $fatCount = [int]$bpb[16]
    $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
    $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
    if ($bps -ne 512 -or $spc -le 0 -or $fatCount -lt 1 -or $fatSectors -lt 1) {
        throw 'Unsupported FAT16 BPB.'
    }
    $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bps)
    $fatBytes = $fatSectors * $bps
    $clusterBytes = $spc * $bps
    $fatStart = $volumeStart + ([int64]$reserved * $bps)
    $rootStart = $fatStart + ([int64]$fatCount * $fatBytes)
    $dataStart = $rootStart + ([int64]$rootSectors * $bps)
    $disk.Position = $fatStart
    $fat = Read-Exact $disk $fatBytes
    for ($copy = 1; $copy -lt $fatCount; $copy++) {
        $disk.Position = $fatStart + ([int64]$copy * $fatBytes)
        $mirror = Read-Exact $disk $fatBytes
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$fat, [byte[]]$mirror)) {
            throw 'FAT mirrors differ before replacement.'
        }
    }
    $root = [byte[]]::new($rootEntries * 32)
    $disk.Position = $rootStart
    $root = Read-Exact $disk $root.Length
    $parts = $dosName.Split('.')
    $shortName = [Text.Encoding]::ASCII.GetBytes($parts[0].PadRight(8) + $parts[1].PadRight(3))
    $entryOffset = Get-RootEntry $root $shortName
    if ($entryOffset -lt 0) { throw "$dosName is missing from the FAT16 root directory." }
    $oldLength = [BitConverter]::ToUInt32($root, $entryOffset + 28)
    $firstCluster = [BitConverter]::ToUInt16($root, $entryOffset + 26)
    $clusterLimit = [int]($fatBytes / 2)
    $chain = Get-Fat16Chain $fat $firstCluster $clusterLimit
    if ($oldLength -gt ([int64]$chain.Length * $clusterBytes)) { throw 'Existing file exceeds its cluster chain.' }
    if ($replacement.Length -gt ([int64]$chain.Length * $clusterBytes)) {
        throw 'Replacement is larger than the existing FAT16 cluster chain; no file data was changed.'
    }

    $padded = [byte[]]::new($chain.Length * $clusterBytes)
    [Buffer]::BlockCopy($replacement, 0, $padded, 0, $replacement.Length)
    $position = 0
    foreach ($cluster in $chain) {
        $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
        $disk.Write($padded, $position, $clusterBytes)
        $position += $clusterBytes
    }
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$replacement.Length), 0,
        $root, $entryOffset + 28, 4)
    $disk.Position = $rootStart
    $disk.Write($root, 0, $root.Length)
    $disk.Flush($true)

    $readback = [byte[]]::new($replacement.Length)
    $position = 0
    foreach ($cluster in $chain) {
        if ($position -ge $readback.Length) { break }
        $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
        $block = Read-Exact $disk $clusterBytes
        $count = [Math]::Min($clusterBytes, $readback.Length - $position)
        [Buffer]::BlockCopy($block, 0, $readback, $position, $count)
        $position += $count
    }
    if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$replacement, [byte[]]$readback)) {
        throw 'Replacement file failed exact byte-for-byte readback.'
    }
    $disk.Dispose()
    $disk = $null
    $originalHash = (Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
    if ($originalHash -ne '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3') {
        throw 'The original NF-DOS disk changed during the file replacement.'
    }
    $exitCode = 0
    [pscustomobject]@{
        disk=$diskPath;dos_name=$dosName;old_length=$oldLength;new_length=$replacement.Length
        clusters_preserved=$chain.Length;cluster_capacity=$chain.Length * $clusterBytes
        sha256=(Get-FileHash -InputStream ([IO.MemoryStream]::new($readback)) -Algorithm SHA256).Hash
        readback_exact=$true;fat_mirrors_verified=$true;original_disk_sha256=$originalHash
    }
} finally {
    if ($null -ne $disk) { $disk.Dispose() }
    if ($registryRegistered) {
        $registryLock = [IO.File]::Open("$registryPath.lock", [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try {
            $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
            $entry = $registry.entries | Where-Object task_id -eq $taskId
            if ($entry) {
                $entry.state = 'exited'
                $entry.exit_code = $exitCode
                $entry.stopped_at = (Get-Date).ToString('o')
                $entry.stop_reason = if ($exitCode -eq 0) { 'FAT16 file replacement and exact readback complete.' } else { 'FAT16 replacement failed; see task process error.' }
                Write-Registry $registry
            }
        } finally { $registryLock.Dispose() }
    }
}
