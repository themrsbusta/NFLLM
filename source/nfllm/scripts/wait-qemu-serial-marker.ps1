param(
    [Parameter(Mandatory=$true)][string]$SerialLogPath,
    [Parameter(Mandatory=$true)][int]$VmPid,
    [Parameter(Mandatory=$true)][string]$VmCreationTime,
    [Parameter(Mandatory=$true)][string]$VmName,
    [string]$MonitorRunId = '',
    [string]$Marker = 'NFLLM_TEST_FINISHED',
    [int]$TimeoutSeconds = 1800,
    [int]$PollSeconds = 5
)

$ErrorActionPreference = 'Stop'
$serialLog = [IO.Path]::GetFullPath($SerialLogPath)
$qemuExe = [IO.Path]::GetFullPath('C:\Program Files\qemu\qemu-system-i386.exe')
$registryPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\emulators\AM486DX4-96MB\process-registry.json'))
if ($TimeoutSeconds -lt 1 -or $PollSeconds -lt 1) { throw 'TimeoutSeconds and PollSeconds must be positive.' }
if ([string]::IsNullOrWhiteSpace($MonitorRunId)) { $MonitorRunId = Get-Date -Format 'yyyyMMdd-HHmmss' }
$taskId = "nfllm-qemu-serial-marker-$VmPid-$MonitorRunId"

function Get-VerifiedQemu([int]$Id, [string]$Creation, [string]$Name) {
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$Id"
    if (-not $process) { return $null }
    $expectedTime = [DateTimeOffset]::Parse($Creation, [Globalization.CultureInfo]::InvariantCulture).LocalDateTime
    $actualTime = [datetime]$process.CreationDate
    $quotedNameArgument = '"-name" "' + $Name + '"'
    $legacyNameArgument = '-name ' + $Name
    if ([IO.Path]::GetFullPath($process.ExecutablePath) -ne $qemuExe -or
        [Math]::Abs(($actualTime - $expectedTime).TotalSeconds) -gt 1 -or
        (-not $process.CommandLine.Contains($quotedNameArgument) -and
         -not $process.CommandLine.Contains($legacyNameArgument))) {
        throw "PID $Id no longer matches the registered QEMU identity; refusing process actions."
    }
    return $process
}

function Read-SerialText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
if (@($registry.entries | Where-Object task_id -eq $taskId).Count -gt 0) { throw "Task ID already exists: $taskId" }
$activeCount = @($registry.entries | Where-Object state -eq 'active').Count + [int]$registry.known_external_active_managed_count
if ($activeCount -ge [int]$registry.max_active_managed_processes) { throw 'Managed process capacity reached.' }
$vm = Get-VerifiedQemu $VmPid $VmCreationTime $VmName
if (-not $vm) { throw 'QEMU exited before the serial-marker monitor started.' }

$current = [Diagnostics.Process]::GetCurrentProcess()
$self = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.Id)"
$entry = [pscustomobject]@{
    task_id=$taskId;vm_name="QEMU serial-marker monitor for $VmName";state='active';pid=$current.Id
    ppid=$self.ParentProcessId;creation_time=$current.StartTime.ToString('o');executable=$self.ExecutablePath
    command_line=$self.CommandLine;classification='temporary';scope="Read QEMU serial log until a fresh guest completion marker or timeout"
    registered_at=(Get-Date).ToString('o');started_at=$current.StartTime.ToString('o')
    shutdown_policy='monitor exits itself; does not stop the VM';source_config=$serialLog
    exit_code=$null;stopped_at=$null;stop_reason=$null
}
$registry.entries = @($registry.entries) + $entry
[IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
$exitCode = 1

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $seen = $false
    $previousLength = -1L
    while ([DateTime]::UtcNow -lt $deadline) {
        $vm = Get-VerifiedQemu $VmPid $VmCreationTime $VmName
        if (-not $vm) { throw 'QEMU exited before the completion marker appeared in its serial log.' }
        $text = Read-SerialText $serialLog
        $length = if (Test-Path -LiteralPath $serialLog) { (Get-Item -LiteralPath $serialLog).Length } else { 0L }
        if ($text.Contains($Marker) -and $seen -and $length -ge $previousLength) {
            $exitCode = 0
            [pscustomobject]@{result='serial_marker_stable';marker=$Marker;observed_twice=$true;vm_pid=$VmPid;vm_still_running=$true;serial_log=$serialLog;observed_at=(Get-Date).ToString('o')}
            break
        }
        if ($text.Contains($Marker)) { $seen = $true }
        $previousLength = $length
        $status = if ($seen) { 'marker_seen_waiting_for_stable_read' } else { 'waiting_for_guest_marker' }
        Write-Output "$(Get-Date -Format o) serial=$status vm_running=True bytes=$length"
        Start-Sleep -Seconds $PollSeconds
    }
    if ($exitCode -ne 0) { throw "Timed out after $TimeoutSeconds seconds waiting for serial marker $Marker; QEMU was not stopped." }
} finally {
    $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
    $entry = $registry.entries | Where-Object task_id -eq $taskId
    if ($entry) {
        $entry.state = 'exited'
        $entry.exit_code = $exitCode
        $entry.stopped_at = (Get-Date).ToString('o')
        if ($exitCode -ne 0 -and [string]::IsNullOrEmpty($entry.stop_reason)) { $entry.stop_reason = 'Serial-marker monitor ended without verified completion.' }
        [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    }
}
