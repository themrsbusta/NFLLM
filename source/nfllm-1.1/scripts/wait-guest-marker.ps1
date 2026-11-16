param(
    [Parameter(Mandatory=$true)][string]$DiskPath,
    [Parameter(Mandatory=$true)][int]$VmPid,
    [Parameter(Mandatory=$true)][string]$VmCreationTime,
    [Parameter(Mandatory=$true)][string]$VmName,
    [string]$MonitorRunId = '',
    [int]$TimeoutSeconds = 1800,
    [int]$PollSeconds = 5,
    [ValidateRange(0,3600)][int]$GuestClockSkewSeconds = 120,
    [string]$MarkerName = 'NFLLM.DON',
    [string]$ExpectedText = 'NFLLM_TEST_FINISHED'
)

$ErrorActionPreference = 'Stop'

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -le 0) { throw 'Unexpected end of guest disk snapshot.' }
        $offset += $read
    }
    return ,$bytes
}

function Get-GuestFileText([string]$Path, [string]$Name) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $mbr = Read-Exact $stream 512
        if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'Invalid guest MBR signature.' }
        $partitionStart = $null
        for ($offset = 446; $offset -lt 510; $offset += 16) {
            if ($mbr[$offset + 4] -eq 6) {
                $partitionStart = [int64][BitConverter]::ToUInt32($mbr, $offset + 8)
                break
            }
        }
        if ($null -eq $partitionStart) { throw 'FAT16 partition type 06h was not found.' }
        $volumeStart = $partitionStart * 512
        $stream.Position = $volumeStart
        $bpb = Read-Exact $stream 512
        $bps = [BitConverter]::ToUInt16($bpb, 11)
        $spc = [int]$bpb[13]
        $reserved = [BitConverter]::ToUInt16($bpb, 14)
        $fatCount = [int]$bpb[16]
        $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
        $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
        if ($bps -ne 512 -or $spc -lt 1 -or $fatCount -lt 1 -or $rootEntries -lt 1 -or $fatSectors -lt 1) {
            throw 'Unsupported FAT16 guest volume parameters.'
        }
        $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bps)
        $fatStart = $volumeStart + ([int64]$reserved * $bps)
        $rootStart = $fatStart + ([int64]$fatCount * $fatSectors * $bps)
        $dataStart = $rootStart + ([int64]$rootSectors * $bps)
        $stream.Position = $rootStart
        $root = Read-Exact $stream ($rootEntries * 32)

        $parts = $Name.ToUpperInvariant().Split('.')
        if ($parts.Count -ne 2 -or $parts[0].Length -gt 8 -or $parts[1].Length -gt 3) { throw "Not a root 8.3 name: $Name" }
        $shortName = [Text.Encoding]::ASCII.GetBytes($parts[0].PadRight(8) + $parts[1].PadRight(3))
        $entryOffset = -1
        for ($offset = 0; $offset -lt $root.Length; $offset += 32) {
            if ($root[$offset] -eq 0) { break }
            if ($root[$offset] -ne 0xe5 -and $root[$offset + 11] -ne 0x0f -and
                [Linq.Enumerable]::SequenceEqual([byte[]]$root[$offset..($offset + 10)], $shortName)) {
                $entryOffset = $offset
                break
            }
        }
        if ($entryOffset -lt 0) { return $null }
        $length = [int][BitConverter]::ToUInt32($root, $entryOffset + 28)
        $firstCluster = [int][BitConverter]::ToUInt16($root, $entryOffset + 26)
        if ($length -lt 1 -or $length -gt 128 -or $firstCluster -lt 2) { return $null }
        $clusterBytes = $spc * $bps
        if ($length -gt $clusterBytes) { throw "$Name exceeds one cluster; marker reader rejects unexpected size." }
        $stream.Position = $dataStart + ([int64]($firstCluster - 2) * $clusterBytes)
        $bytes = Read-Exact $stream $length
        $dosTime = [BitConverter]::ToUInt16($root, $entryOffset + 22)
        $dosDate = [BitConverter]::ToUInt16($root, $entryOffset + 24)
        $timestamp = $null
        $year = 1980 + (($dosDate -shr 9) -band 0x7f)
        $month = ($dosDate -shr 5) -band 0x0f
        $day = $dosDate -band 0x1f
        $hour = ($dosTime -shr 11) -band 0x1f
        $minute = ($dosTime -shr 5) -band 0x3f
        $second = ($dosTime -band 0x1f) * 2
        try { $timestamp = [DateTime]::new($year,$month,$day,$hour,$minute,$second) } catch { }
        return [pscustomobject]@{
            text=[Text.Encoding]::ASCII.GetString($bytes).Trim([char[]]" `r`n`t")
            dos_timestamp=$timestamp
        }
    } finally {
        $stream.Dispose()
    }
}

function Get-VerifiedVmProcess([int]$Id, [string]$Creation, [string]$Name) {
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$Id"
    if (-not $process) { return $null }
    $expectedExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\emulators\AM486DX4-96MB\app\86Box.exe'))
    $creationMatches = $false
    try {
        $actualCreated = [datetime]$process.CreationDate
        $expectedCreated = [datetime]::Parse($Creation)
        $creationMatches = [Math]::Abs(($actualCreated - $expectedCreated).TotalSeconds) -le 1
    } catch { }
    if ([IO.Path]::GetFullPath($process.ExecutablePath) -ne $expectedExe -or
        -not $creationMatches -or
        $process.CommandLine -notlike "*--vmname $Name*") {
        throw "PID $Id no longer matches the registered 86Box identity; refusing process actions."
    }
    return $process
}

$diskPath = [IO.Path]::GetFullPath($DiskPath)
if (-not (Test-Path -LiteralPath $diskPath -PathType Leaf)) { throw "Guest disk is missing: $diskPath" }
if ($TimeoutSeconds -lt 1 -or $PollSeconds -lt 1) { throw 'TimeoutSeconds and PollSeconds must be positive.' }
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$minGuestTimestamp = [DateTimeOffset]::Parse($VmCreationTime).LocalDateTime.AddSeconds(-$GuestClockSkewSeconds)
$seenStart = $false
$previousMarker = $null
$emulatorRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\emulators\AM486DX4-96MB'))
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
if ([string]::IsNullOrWhiteSpace($MonitorRunId)) { $MonitorRunId = Get-Date -Format 'yyyyMMdd-HHmmss' }
$taskId = "nfllm-stage2-marker-wait-$VmPid-$MonitorRunId"
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
if (@($registry.entries | Where-Object task_id -eq $taskId).Count -gt 0) { throw "Task ID already exists: $taskId" }
$activeCount = @($registry.entries | Where-Object state -eq 'active').Count + [int]$registry.known_external_active_managed_count
if ($activeCount -ge [int]$registry.max_active_managed_processes) { throw 'Managed process capacity reached.' }
$current = [Diagnostics.Process]::GetCurrentProcess()
$self = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.Id)"
$monitorEntry = [pscustomobject]@{
    task_id=$taskId;vm_name="Marker monitor for $VmName";state='active';pid=$current.Id
    ppid=$self.ParentProcessId;creation_time=$current.StartTime.ToString('o');executable=$self.ExecutablePath
    command_line=$self.CommandLine;classification='temporary';scope="Read only NFLLM guest marker until marker, VM exit or bounded timeout"
    registered_at=(Get-Date).ToString('o');started_at=$current.StartTime.ToString('o')
    shutdown_policy='monitor exits itself; does not stop the VM';source_config=$diskPath
    exit_code=$null;stopped_at=$null;stop_reason=$null
}
$registry.entries = @($registry.entries) + $monitorEntry
[IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
$exitCode = 1
$completion = $null

try {
    while ([DateTime]::UtcNow -lt $deadline) {
        $process = Get-VerifiedVmProcess $VmPid $VmCreationTime $VmName
        try {
            $startFile = Get-GuestFileText $diskPath 'SELFTEST.OK'
            $markerFile = Get-GuestFileText $diskPath $MarkerName
        } catch [IO.IOException] {
            $startFile = $null
            $markerFile = $null
            Write-Output "$(Get-Date -Format o) snapshot=temporarily_unavailable"
        }
        $startText = if ($startFile) { $startFile.text } else { $null }
        $markerText = if ($markerFile) { $markerFile.text } else { $null }
        $startIsFresh = $startFile -and $startFile.dos_timestamp -and $startFile.dos_timestamp -ge $minGuestTimestamp
        $markerIsFresh = $markerFile -and $markerFile.dos_timestamp -and $markerFile.dos_timestamp -ge $minGuestTimestamp
        if ($startIsFresh -and $startText -in @('SELFTEST_START', 'SELFTEST_PASS')) { $seenStart = $true }
        if ($markerText -eq $ExpectedText -and $markerIsFresh -and $seenStart) {
            if ($previousMarker -eq $ExpectedText) {
                $stillRunning = Get-VerifiedVmProcess $VmPid $VmCreationTime $VmName
                $completion = [pscustomobject]@{
                    result='completion_marker_stable'
                    marker=$MarkerName
                    value=$markerText
                    observed_twice=$true
                    vm_pid=$VmPid
                    vm_still_running=($null -ne $stillRunning)
                    observed_at=(Get-Date).ToString('o')
                    guest_marker_timestamp=$markerFile.dos_timestamp.ToString('o')
                    disk=$diskPath
                }
                $exitCode = 0
                break
            }
            $previousMarker = $markerText
        } else {
            $previousMarker = $null
        }
        $status = if ($markerText -eq $ExpectedText -and -not $markerIsFresh) { 'stale_marker_ignored' }
            elseif ($markerText -eq $ExpectedText -and -not $seenStart) { 'waiting_for_fresh_selftest' }
            elseif ($markerText) { $markerText }
            elseif ($startText -and -not $startIsFresh) { 'stale_selftest_ignored' }
            elseif ($startText) { $startText }
            else { 'waiting_for_guest_start' }
        Write-Output "$(Get-Date -Format o) selftest=$status vm_running=$($null -ne $process)"
        if (-not $process -and -not ($markerText -eq $ExpectedText -and $markerIsFresh -and $seenStart)) { throw '86Box exited before a fresh expected guest completion marker appeared.' }
        Start-Sleep -Seconds $PollSeconds
    }
    if ($exitCode -ne 0) { throw "Timed out after $TimeoutSeconds seconds waiting for $MarkerName=$ExpectedText; VM was not stopped by this script." }
    $completion
} finally {
    $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
    $monitorEntry = $registry.entries | Where-Object task_id -eq $taskId
    if ($monitorEntry) {
        $monitorEntry.state = 'exited'
        $monitorEntry.exit_code = $exitCode
        $monitorEntry.stopped_at = (Get-Date).ToString('o')
        if ($exitCode -ne 0 -and [string]::IsNullOrEmpty($monitorEntry.stop_reason)) { $monitorEntry.stop_reason = 'Marker monitor ended without verified completion.' }
        [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    }
}
