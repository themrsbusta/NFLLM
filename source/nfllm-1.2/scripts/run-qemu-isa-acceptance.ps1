param(
    [Parameter(Mandatory=$true)][string]$DiskPath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9-]+$')][string]$RunId,
    [Parameter(Mandatory=$true)][ValidateSet('486','athlon-lx-proxy','atom-n270')][string]$CpuProfile,
    [ValidateSet('3dnow','mmx','sse2','486-safe','quantized-direct','packed-dot-selftest','packed-quant-benchmark')][string]$TestType = '3dnow',
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(0,5)][int]$BenchmarkRepetitions = 0,
    [ValidateRange(0,5)][int]$QuantizedRepetitions = 0,
    [ValidateRange(1,3600)][int]$TimeoutSeconds = 900,
    [ValidateRange(1024,65535)][int]$QmpPort = 62020
)

$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$diskRoot = [IO.Path]::GetFullPath((Join-Path $emulatorRoot 'disks'))
$originalDisk = Join-Path $diskRoot 'nfdos502-qemu.raw'
$qemuExe = [IO.Path]::GetFullPath('C:\Program Files\qemu\qemu-system-i386.exe')
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
$diskPath = [IO.Path]::GetFullPath($DiskPath)
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$logsRoot = [IO.Path]::GetFullPath((Join-Path $workspace 'NFLLM\build\logs'))
$vmName = "NFLLM-STAGE3-$CpuProfile-$RunId"
$taskId = "nfllm-qemu-isa-$RunId-$((Get-Date).ToString('yyyyMMdd-HHmmss-fff'))"
$serialPath = Join-Path $outputDirectory "$RunId-serial.log"
$debugPath = Join-Path $outputDirectory "$RunId-qemu-debug.log"
$stdoutPath = Join-Path $outputDirectory "$RunId-qemu-stdout.log"
$stderrPath = Join-Path $outputDirectory "$RunId-qemu-stderr.log"
$summaryPath = Join-Path $outputDirectory "$RunId-summary.json"
$marker = 'NFLLM_TEST_FINISHED'
$testPrefix = if ($TestType -eq '3dnow') { '3DTEST' } elseif ($TestType -eq 'mmx') { 'MMXTEST' } elseif ($TestType -eq 'sse2') { 'SSE2TEST' } elseif ($TestType -eq 'packed-quant-benchmark') { 'PDBENCH' } else { '' }
$testLogName = if ($testPrefix) { "$testPrefix.LOG" } elseif ($TestType -eq '486-safe') { 'SAFE486.LOG' } elseif ($TestType -in @('quantized-direct','packed-dot-selftest')) { 'SELFTEST.LOG' } else { '3DNOW.LOG' }
$testStateName = if ($testPrefix) { "$testPrefix.OK" } elseif ($TestType -eq '486-safe') { 'SAFE486.OK' } elseif ($TestType -in @('quantized-direct','packed-dot-selftest')) { 'SELFTEST.OK' } else { '3DNOW.OK' }
$inferenceLogName = if ($TestType -eq '486-safe') { 'SAFE486.LOG' } elseif ($TestType -eq 'sse2') { 'SSE2.LOG' } elseif ($TestType -eq 'quantized-direct') { 'Q20NFLLM.LOG' } elseif ($TestType -eq 'packed-dot-selftest') { 'SELFTEST.LOG' } elseif ($TestType -eq 'packed-quant-benchmark') { 'PDBENCH.LOG' } else { '3DNOW.LOG' }
$inferenceStateName = if ($TestType -eq '486-safe') { 'SAFE486.OK' } elseif ($TestType -eq 'sse2') { 'SSE2.OK' } elseif ($TestType -eq 'quantized-direct') { 'Q20INFER.OK' } elseif ($TestType -in @('packed-dot-selftest','packed-quant-benchmark')) { if ($TestType -eq 'packed-quant-benchmark') { 'PDBENCH.OK' } else { 'SELFTEST.OK' } } else { '3DNOW.OK' }
$kernelName = if ($TestType -eq '3dnow') { '3dnow.matvec.f32' } elseif ($TestType -eq 'mmx') { 'mmx.matvec.f32.quantized-int8' } elseif ($TestType -eq 'sse2') { 'sse2.matvec.f32' } else { 'reference.matvec.f32' }
$selftestMarker = if ($TestType -eq '3dnow') { '3DNOW SELFTEST: PASS' } elseif ($TestType -eq 'mmx') { 'MMX SELFTEST: PASS' } elseif ($TestType -eq 'sse2') { 'SSE2 SELFTEST: PASS' } elseif ($TestType -in @('quantized-direct','packed-dot-selftest')) { 'SELFTEST: PASS' } else { '' }
$performanceComparison = $null
$quantizedDirectResults = $null
$quantizedIsaResults = $null
$quantizedIsaBenchmarkResults = $null
$f32IsaBenchmarkResults = $null
$packedQuantBenchmarkResults = $null
$process = $null
$stdoutTask = $null
$stderrTask = $null
$registered = $false
$markerVerified = $false
$failure = $null
$stopReason = 'QMP quit after verified guest completion marker.'
$exitCode = 1
$qemuExitCode = $null
$forcedShutdown = $false
$startedAt = (Get-Date).ToString('o')
$diskHashBefore = $null

function Set-Field($Object, [string]$Name, $Value) {
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        Add-Member -InputObject $Object -MemberType NoteProperty -Name $Name -Value $Value
    } else { $Object.$Name = $Value }
}

function Write-Registry($Registry) {
    $temp = "$registryPath.$PID.tmp"
    [IO.File]::WriteAllText($temp, ($Registry | ConvertTo-Json -Depth 16), [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temp, $registryPath, $true)
}

function Open-RegistryLock() {
    return [IO.File]::Open("$registryPath.lock", [IO.FileMode]::OpenOrCreate,
                           [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}

function Read-SerialText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Test-ExactQemuIdentity([Diagnostics.Process]$Process, [string]$Creation, [string]$Name) {
    if ($null -eq $Process) { return $false }
    try {
        $Process.Refresh()
        if ($Process.HasExited) { return $false }
        $expected = [DateTimeOffset]::Parse($Creation, [Globalization.CultureInfo]::InvariantCulture).LocalDateTime
        $actual = $Process.StartTime
        if ([Math]::Abs(($actual - $expected).TotalSeconds) -gt 1) { return $false }
        if ([IO.Path]::GetFullPath($Process.StartInfo.FileName) -ne $qemuExe) { return $false }
        $arguments = @($Process.StartInfo.ArgumentList)
        $hasName = $arguments -contains $Name
        if (-not $hasName -and $Process.StartInfo.Arguments) {
            $hasName = $Process.StartInfo.Arguments.Contains($Name)
        }
        if (-not $hasName) { return $false }
        try {
            if ([IO.Path]::GetFullPath($Process.MainModule.FileName) -eq $qemuExe) { return $true }
        } catch { }
        for ($attempt = 0; $attempt -lt 5; $attempt++) {
            try {
                $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
                if ($cim -and -not [string]::IsNullOrWhiteSpace($cim.ExecutablePath) -and
                    -not [string]::IsNullOrWhiteSpace($cim.CommandLine)) {
                    $cimCreation = [datetime]$cim.CreationDate
                    if ([IO.Path]::GetFullPath($cim.ExecutablePath) -eq $qemuExe -and
                        [Math]::Abs(($cimCreation - $expected).TotalSeconds) -le 1 -and
                        $cim.CommandLine.Contains($Name)) { return $true }
                }
            } catch { }
            $Process.Refresh()
            if ($Process.HasExited) { return $false }
            Start-Sleep -Milliseconds 150
        }
        return $false
    } catch { return $false }
}

function Send-QmpQuit([int]$Port) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connect = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $connect.AsyncWaitHandle.WaitOne(3000)) { throw 'QMP connection timed out.' }
        $client.EndConnect($connect)
        $client.ReceiveTimeout = 3000
        $client.SendTimeout = 3000
        $stream = $client.GetStream()
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $false, 1024, $true)
        $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::UTF8, 1024, $true)
        $writer.AutoFlush = $true
        [void]$reader.ReadLine() # QMP greeting
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        [void]$reader.ReadLine()
        $writer.WriteLine('{"execute":"quit"}')
    } finally {
        $client.Dispose()
    }
}

function Get-MetricSummary([double[]]$Values) {
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { throw 'Cannot summarize an empty metric sample.' }
    $middle = [int][Math]::Floor($sorted.Count / 2)
    $median = if (($sorted.Count % 2) -eq 1) {
        [double]$sorted[$middle]
    } else {
        ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
    }
    $deviations = @($Values | ForEach-Object { [Math]::Abs([double]$_ - $median) } | Sort-Object)
    $madMiddle = [int][Math]::Floor($deviations.Count / 2)
    $mad = if (($deviations.Count % 2) -eq 1) {
        [double]$deviations[$madMiddle]
    } else {
        ([double]$deviations[$madMiddle - 1] + [double]$deviations[$madMiddle]) / 2.0
    }
    return [pscustomobject]@{
        median=[Math]::Round($median,3);minimum=[Math]::Round([double]$sorted[0],3)
        maximum=[Math]::Round([double]$sorted[-1],3)
        range=[Math]::Round(([double]$sorted[-1] - [double]$sorted[0]),3)
        median_absolute_deviation=[Math]::Round($mad,3)
        samples=@($Values)
    }
}

if (-not $diskPath.StartsWith($diskRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $diskPath.Equals([IO.Path]::GetFullPath($originalDisk), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'QEMU ISA tests may use only a cloned disk inside the emulator disks directory.'
}
if (-not $outputDirectory.StartsWith($logsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence output must stay below NFLLM/build/logs.'
}
if ($TestType -eq '486-safe' -and $CpuProfile -ne '486') {
    throw 'The 486-safe validation requires the QEMU 486 CPU profile.'
}
if ($TestType -eq 'quantized-direct' -and $CpuProfile -ne '486') {
    throw 'The direct Q2/IQ3 quantized validation requires the safe QEMU 486 CPU profile.'
}
if ($TestType -eq 'packed-dot-selftest' -and $CpuProfile -ne '486') {
    throw 'The packed-dot unit self-test requires the safe QEMU 486 CPU profile.'
}
if ($TestType -eq 'packed-quant-benchmark' -and $CpuProfile -ne '486') {
    throw 'The packed quantized matvec benchmark requires the safe QEMU 486 CPU profile.'
}
if ($TestType -eq 'sse2' -and $CpuProfile -ne 'atom-n270') {
    throw 'The experimental SSE2 validation requires the QEMU Atom N270 profile.'
}
if ($TestType -eq 'mmx' -and $CpuProfile -notin @('athlon-lx-proxy','atom-n270')) {
    throw 'The MMX validation requires a QEMU profile that exposes MMX (Athlon LX proxy or Atom N270).'
}
if ($TestType -eq '3dnow' -and $CpuProfile -ne 'athlon-lx-proxy') {
    throw 'The 3DNow! validation requires the Geode LX compatible Athlon proxy.'
}
if ($BenchmarkRepetitions -gt 0 -and $TestType -notin @('mmx','sse2','3dnow')) {
    throw 'Repeated F32 ISA benchmarks require TestType mmx, sse2, or 3dnow.'
}
if ($QuantizedRepetitions -gt 0 -and $TestType -notin @('mmx','sse2')) {
    throw 'Repeated Q8_0/Q4_0 ISA benchmarks require TestType mmx or sse2.'
}
foreach ($path in @($diskPath, $originalDisk, $qemuExe, $registryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file is missing: $path" }
}
if ((Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash -ne
    '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3') {
    throw 'The preserved NF-DOS original disk hash does not match its recorded baseline.'
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
foreach ($path in @($serialPath, $debugPath, $stdoutPath, $stderrPath, $summaryPath)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite prior evidence: $path" }
}
$diskHashBefore = (Get-FileHash -LiteralPath $diskPath -Algorithm SHA256).Hash

$cpuArg = switch ($CpuProfile) {
    '486' { '486' }
    'athlon-lx-proxy' { 'athlon-v1,+mmx,+3dnow,+3dnowext' }
    'atom-n270' { 'n270-v1' }
}
$qemuArgs = @(
    '-machine','pc','-accel','tcg,thread=single','-cpu',$cpuArg,
    '-m','96M','-nic','none','-drive',"file=$diskPath,format=raw,if=ide,index=0",
    '-boot','c','-rtc','base=localtime','-display','none','-no-reboot',
    '-name',$vmName,'-serial',"file:$serialPath",
    '-qmp',"tcp:127.0.0.1:$QmpPort,server=on,wait=off",
    '-D',$debugPath,'-d','guest_errors'
)
$portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $QmpPort)
try { $portProbe.Start() } catch { throw "QMP port $QmpPort is unavailable." } finally { $portProbe.Stop() }

$quotedArgs = foreach ($argument in $qemuArgs) {
    if ($argument -match '\s' -or $argument.Contains('"')) { '"' + $argument.Replace('"','\"') + '"' }
    else { $argument }
}
$expectedCommandLine = '"' + $qemuExe + '" ' + ($quotedArgs -join ' ')
$reservation = [pscustomobject]@{
    task_id=$taskId;vm_name=$vmName;state='starting';pid=$null;ppid=$PID
    creation_time=$null;executable=$qemuExe;command_line=$expectedCommandLine
    classification='temporary';scope="QEMU ISA acceptance test $RunId on $CpuProfile with a cloned NF-DOS disk"
    registered_at=(Get-Date).ToString('o');started_at=$null
    shutdown_policy='QMP quit after verified guest marker; wait 30 seconds, then force only for this verified task-owned PID if graceful exit fails'
    source_config=$diskPath;source_disk_sha256=$diskHashBefore;qmp_port=$QmpPort
    exit_code=$null;stopped_at=$null;stop_reason=$null
}
$registryLock = Open-RegistryLock
try {
    $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
    $activeCount = @($registry.entries | Where-Object { $_.state -in @('active','starting') }).Count + [int]$registry.known_external_active_managed_count
    if ($activeCount -ge [int]$registry.max_active_managed_processes) { throw 'Managed process capacity reached; QEMU was not launched.' }
    if (@(Get-CimInstance Win32_Process | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_.ExecutablePath) -and
        [IO.Path]::GetFullPath($_.ExecutablePath) -eq $qemuExe
    }).Count -gt 0) {
        throw 'A QEMU process is already active; refusing to create an overlapping VM.'
    }
    if (@($registry.entries | Where-Object task_id -eq $taskId).Count -ne 0) {
        throw "Task identifier is already registered: $taskId"
    }
    $registry.entries = @($registry.entries) + $reservation
    Write-Registry $registry
    $registered = $true
} finally { $registryLock.Dispose() }

try {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $qemuExe
    $start.WorkingDirectory = $emulatorRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $qemuArgs) { [void]$start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    if ($null -eq $process) { throw 'QEMU process creation failed.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.Refresh()
    if ($process.HasExited) { throw 'QEMU exited before its process identity could be registered.' }
    $processCreation = $process.StartTime.ToString('o')
    $actualParentId = $PID
    $actualExecutable = $qemuExe
    $actualCommandLine = $expectedCommandLine
    try {
        $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
        if ($cim -and $cim.ParentProcessId) { $actualParentId = [int]$cim.ParentProcessId }
        if ($cim -and -not [string]::IsNullOrWhiteSpace($cim.CommandLine) -and $cim.CommandLine.Contains($vmName)) {
            $actualCommandLine = $cim.CommandLine
        }
        if ($cim -and -not [string]::IsNullOrWhiteSpace($cim.ExecutablePath)) {
            $actualExecutable = [IO.Path]::GetFullPath($cim.ExecutablePath)
        }
    } catch { }
    $registryLock = Open-RegistryLock
    try {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $rows = @($registry.entries | Where-Object task_id -eq $taskId)
        if ($rows.Count -ne 1) { throw 'Reserved QEMU process entry is missing or duplicated.' }
        Set-Field $rows[0] 'state' 'active'
        Set-Field $rows[0] 'pid' $process.Id
        Set-Field $rows[0] 'ppid' $actualParentId
        Set-Field $rows[0] 'creation_time' $processCreation
        Set-Field $rows[0] 'executable' $actualExecutable
        Set-Field $rows[0] 'command_line' $actualCommandLine
        Set-Field $rows[0] 'started_at' $processCreation
        Write-Registry $registry
    } finally { $registryLock.Dispose() }
    if ($actualExecutable -ne $qemuExe) { throw 'The process image path differs from the requested QEMU executable.' }
    if (-not (Test-ExactQemuIdentity $process $processCreation $vmName)) {
        throw 'Launched QEMU identity failed verification; the registered process will be shut down through QMP.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $seen = $false
    $previousLength = -1L
    $lastReport = [DateTime]::UtcNow
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) { throw "QEMU exited early with code $($process.ExitCode)." }
        $text = Read-SerialText $serialPath
        $length = if (Test-Path -LiteralPath $serialPath) { (Get-Item -LiteralPath $serialPath).Length } else { 0L }
        if ($text.Contains($marker) -and $seen -and $length -ge $previousLength) {
            $markerVerified = $true
            break
        }
        if ($text.Contains($marker)) { $seen = $true }
        $previousLength = $length
        if (([DateTime]::UtcNow - $lastReport).TotalSeconds -ge 30) {
            Write-Output "QEMU $vmName running; serial bytes=$length; waiting for guest completion marker."
            $lastReport = [DateTime]::UtcNow
        }
        Start-Sleep -Seconds 2
    }
    if (-not $markerVerified) { throw "Guest completion marker was not seen within $TimeoutSeconds seconds." }

    Send-QmpQuit $QmpPort
    if (-not $process.WaitForExit(30000)) {
        if (-not (Test-ExactQemuIdentity $process $process.StartTime.ToString('o') $vmName)) {
            throw 'QEMU did not exit after QMP; process identity changed, so no force action was taken.'
        }
        $process.Kill()
        if (-not $process.WaitForExit(5000)) { throw 'Verified task-owned QEMU process did not exit after force shutdown.' }
        $forcedShutdown = $true
        $stopReason = 'QMP quit was sent; verified task-owned process required exact-PID termination after a 30-second grace period.'
    }
    $qemuExitCode = $process.ExitCode
    if ($qemuExitCode -ne 0 -and -not $forcedShutdown) { throw "QEMU exited with code $qemuExitCode after guest completion." }

    $extractor = Join-Path $PSScriptRoot 'read-guest-fat16-files.ps1'
    $extractNames = @('NFLLM.DON')
    if ($TestType -eq 'quantized-direct') {
        $extractNames += @(
            'SELFTEST.OK','SELFTEST.LOG',
            'INFER.OK','NFLLM.LOG','T1INFER.OK','T1NFLLM.LOG',
            'Q8INFER.OK','Q8NFLLM.LOG','Q4INFER.OK','Q4NFLLM.LOG',
            'Q20COMP.OK','Q20COMP.LOG','Q20INFER.OK','Q20NFLLM.LOG',
            'Q2KCOMP.OK','Q2KCOMP.LOG','Q2KINFER.OK','Q2KNFLLM.LOG',
            'I3XCOMP.OK','I3XCOMP.LOG','I3XINFER.OK','I3XNFLLM.LOG',
            'I3SCOMP.OK','I3SCOMP.LOG','I3SINFER.OK','I3SNFLLM.LOG'
        )
    } elseif ($TestType -eq 'packed-quant-benchmark') {
        $extractNames += @('PDBENCH.OK','PDBENCH.LOG')
        foreach ($prefix in @('Q20','Q2K','I3X','I3S')) {
            foreach ($route in @('D','F')) {
                foreach ($sample in @('A','B','C')) {
                    $tag = "$prefix$route$sample"
                    $extractNames += @("$tag.OK","$tag.LOG")
                }
            }
        }
    } else {
        $extractNames += @($inferenceStateName,$inferenceLogName)
        if ($TestType -ne '486-safe') { $extractNames += @($testStateName,$testLogName) }
    }
    if ($TestType -eq 'mmx') { $extractNames += @('REFMMX.OK','REFMMX.LOG','MMXQ8.OK','MMXQ8.LOG','MMXQ4.OK','MMXQ4.LOG','RMMXQ8.OK','RMMXQ8.LOG','RMMXQ4.OK','RMMXQ4.LOG') }
    if ($TestType -eq 'sse2') { $extractNames += @('OSFXSR.OK','OSFXSR.LOG','REFSSE.OK','REFSSE.LOG','SSE2Q8.OK','SSE2Q8.LOG','SSE2Q4.OK','SSE2Q4.LOG','RSSE2Q8.OK','RSSE2Q8.LOG','RSSE2Q4.OK','RSSE2Q4.LOG') }
    if ($TestType -eq '3dnow') { $extractNames += @('REF3D.OK','REF3D.LOG') }
    if ($QuantizedRepetitions -gt 0) {
        $benchmarkPrefix = if ($TestType -eq 'sse2') { 'E' } else { 'M' }
        $scalarPrefix = "S$benchmarkPrefix"
        for ($sample = 0; $sample -lt $QuantizedRepetitions; $sample++) {
            $suffix = [char]([int][char]'A' + $sample)
            foreach ($format in @('Q8','Q4')) {
                foreach ($prefix in @($benchmarkPrefix,$scalarPrefix)) {
                    $tag = "$prefix$format$suffix"
                    $extractNames += @("$tag.OK","$tag.LOG")
                }
            }
        }
    }
    if ($BenchmarkRepetitions -gt 0) {
        $f32Prefix = if ($TestType -eq 'sse2') { 'E' } elseif ($TestType -eq '3dnow') { 'D' } else { 'M' }
        for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
            $suffix = [char]([int][char]'A' + $sample)
            foreach ($prefix in @("$f32Prefix`F",'RF')) {
                $tag = "$prefix$suffix"
                $extractNames += @("$tag.OK","$tag.LOG")
            }
        }
    }
    $extraction = & $extractor -DiskPath $diskPath -OutputDirectory $outputDirectory -Names $extractNames
    if (-not $extraction.completion_marker_present) { throw 'Guest disk is missing NFLLM.DON after QEMU exit.' }
    $testState = [IO.File]::ReadAllText((Join-Path $outputDirectory $testStateName)).Trim()
    $inferState = [IO.File]::ReadAllText((Join-Path $outputDirectory $inferenceStateName)).Trim()
    $testLog = [IO.File]::ReadAllText((Join-Path $outputDirectory $testLogName))
    $inferLog = [IO.File]::ReadAllText((Join-Path $outputDirectory $inferenceLogName))
    if ($TestType -eq 'quantized-direct') {
        if ($testState -ne 'SELFTEST_PASS' -or -not $testLog.Contains($selftestMarker)) {
            throw 'The DOS/4GW unit self-test, including direct Q2/IQ3 kernels, did not pass.'
        }
        if ($inferState -ne 'INFER_PASS' -or -not $inferLog.Contains("matvec=$kernelName") -or
            -not $inferLog.Contains('Status: PASS')) {
            throw 'Q2_0 direct packed-matvec inference did not pass on the 486 profile.'
        }
        $quantizedSpecs = @(
            [pscustomobject]@{name='Q2_0';prefix='Q20';row='Q2_0';token=413}
            [pscustomobject]@{name='Q2_K';prefix='Q2K';row='Q2_K';token=414}
            [pscustomobject]@{name='IQ3_XXS';prefix='I3X';row='IQ3_XXS';token=317}
            [pscustomobject]@{name='IQ3_S';prefix='I3S';row='IQ3_S';token=353}
        )
        $quantizedDirectResults = @()
        foreach ($spec in $quantizedSpecs) {
            $compatState = [IO.File]::ReadAllText((Join-Path $outputDirectory "$($spec.prefix)COMP.OK")).Trim()
            $compatLog = [IO.File]::ReadAllText((Join-Path $outputDirectory "$($spec.prefix)COMP.LOG"))
            $inferStatePath = Join-Path $outputDirectory "$($spec.prefix)INFER.OK"
            $inferLogPath = Join-Path $outputDirectory "$($spec.prefix)NFLLM.LOG"
            $quantState = [IO.File]::ReadAllText($inferStatePath).Trim()
            $quantLog = [IO.File]::ReadAllText($inferLogPath)
            $rowMatch = [regex]::Match($quantLog, "Direct packed matvec rows:.*$($spec.row)=(\d+)")
            $tokenMatch = [regex]::Match($quantLog, 'TOKEN\[0\] id=(\d+)')
            if ($compatState -ne 'COMPAT_PASS' -or -not $compatLog.Contains('Probe result: PASS')) {
                throw "$($spec.name) GGUF compatibility probe failed."
            }
            if ($quantState -ne 'INFER_PASS' -or -not $quantLog.Contains('Status: PASS') -or
                -not $rowMatch.Success -or [int]$rowMatch.Groups[1].Value -le 0) {
                throw "$($spec.name) inference did not report a passing direct packed matvec row count."
            }
            if (-not $tokenMatch.Success -or [int]$tokenMatch.Groups[1].Value -ne $spec.token) {
                throw "$($spec.name) generated a token different from the validated decoded-row baseline."
            }
            $quantizedDirectResults += [pscustomobject]@{
                format=$spec.name;compatibility=$compatState;inference=$quantState
                direct_rows=[int]$rowMatch.Groups[1].Value;token_id=[int]$tokenMatch.Groups[1].Value
                status='PASS'
            }
        }
        foreach ($regression in @(
            [pscustomobject]@{name='Stories F32';state='INFER.OK';log='NFLLM.LOG';token=426}
            [pscustomobject]@{name='Tiny1M F32';state='T1INFER.OK';log='T1NFLLM.LOG';token=411}
            [pscustomobject]@{name='Tiny1M Q8_0';state='Q8INFER.OK';log='Q8NFLLM.LOG';token=411}
            [pscustomobject]@{name='Tiny1M Q4_0';state='Q4INFER.OK';log='Q4NFLLM.LOG';token=402}
        )) {
            $state = [IO.File]::ReadAllText((Join-Path $outputDirectory $regression.state)).Trim()
            $log = [IO.File]::ReadAllText((Join-Path $outputDirectory $regression.log))
            $token = [regex]::Match($log, 'TOKEN\[0\] id=(\d+)')
            if ($state -ne 'INFER_PASS' -or -not $log.Contains('Status: PASS') -or
                -not $token.Success -or [int]$token.Groups[1].Value -ne $regression.token) {
                throw "$($regression.name) 486 baseline regression failed."
            }
        }
    } elseif ($TestType -eq 'packed-quant-benchmark') {
        if ($testState -ne 'PASS' -or -not $testLog.Contains('PACKED_QUANT_BENCHMARK')) {
            throw 'The guest did not complete the packed quantized matvec benchmark mode.'
        }
        $packedSpecs = @(
            [pscustomobject]@{name='Q2_0';prefix='Q20';row='Q2_0';token=413}
            [pscustomobject]@{name='Q2_K';prefix='Q2K';row='Q2_K';token=414}
            [pscustomobject]@{name='IQ3_XXS';prefix='I3X';row='IQ3_XXS';token=317}
            [pscustomobject]@{name='IQ3_S';prefix='I3S';row='IQ3_S';token=353}
        )
        $packedQuantBenchmarkResults = @()
        foreach ($spec in $packedSpecs) {
            $directTicks = [Collections.Generic.List[int]]::new()
            $decodeTicks = [Collections.Generic.List[int]]::new()
            foreach ($sample in @('A','B','C')) {
                foreach ($route in @('D','F')) {
                    $tag = "$($spec.prefix)$route$sample"
                    $state = [IO.File]::ReadAllText((Join-Path $outputDirectory "$tag.OK")).Trim()
                    $log = [IO.File]::ReadAllText((Join-Path $outputDirectory "$tag.LOG"))
                    $expectedPath = if ($route -eq 'D') { 'direct packed when available' } else { 'decode+F32' }
                    $ticks = [regex]::Match($log, 'Generation tokens=1 ticks=(\d+)')
                    $token = [regex]::Match($log, 'TOKEN\[0\] id=(\d+)')
                    $rows = [regex]::Match($log, "Direct packed matvec rows:.*$($spec.row)=(\d+)")
                    if ($state -ne 'PASS' -or -not $log.Contains('Status: PASS') -or
                        -not $log.Contains("Quantized matvec path: $expectedPath") -or
                        -not $ticks.Success -or -not $token.Success -or -not $rows.Success) {
                        throw "$($spec.name) $route$sample benchmark log or status is incomplete."
                    }
                    $rowCount = [int]$rows.Groups[1].Value
                    if (($route -eq 'D' -and $rowCount -le 0) -or ($route -eq 'F' -and $rowCount -ne 0)) {
                        throw "$($spec.name) $route$sample did not use the requested packed/decode route."
                    }
                    if ([int]$token.Groups[1].Value -ne $spec.token) {
                        throw "$($spec.name) $route$sample token differs from the validated baseline."
                    }
                    if ($route -eq 'D') { $directTicks.Add([int]$ticks.Groups[1].Value) }
                    else { $decodeTicks.Add([int]$ticks.Groups[1].Value) }
                }
            }
            [int[]]$directSorted = @($directTicks | Sort-Object)
            [int[]]$decodeSorted = @($decodeTicks | Sort-Object)
            $directMedian = $directSorted[[int][Math]::Floor($directSorted.Length / 2)]
            $decodeMedian = $decodeSorted[[int][Math]::Floor($decodeSorted.Length / 2)]
            $packedQuantBenchmarkResults += [pscustomobject]@{
                format=$spec.name;samples=3
                direct_ticks=$directTicks.ToArray();decode_f32_ticks=$decodeTicks.ToArray()
                direct_median_ticks=$directMedian;decode_f32_median_ticks=$decodeMedian
                decode_over_direct_ratio=[Math]::Round(($decodeMedian / [double]$directMedian),3)
                token_id=$spec.token;route_checks='direct rows > 0; decode direct rows = 0';status='PASS'
            }
        }
    } elseif ($TestType -eq 'packed-dot-selftest') {
        if ($testState -ne 'SELFTEST_PASS' -or -not $testLog.Contains($selftestMarker)) {
            throw 'The 386+x87 packed-dot unit self-test did not pass on the 486 profile.'
        }
    } elseif ($TestType -ne '486-safe' -and ($testState -ne 'PASS' -or -not $testLog.Contains($selftestMarker))) {
        throw "The isolated $TestType numerical/cleanup self-test did not pass."
    }
    if ($TestType -eq 'sse2') {
        $osfxsrState = [IO.File]::ReadAllText((Join-Path $outputDirectory 'OSFXSR.OK')).Trim()
        $osfxsrLog = [IO.File]::ReadAllText((Join-Path $outputDirectory 'OSFXSR.LOG'))
        if ($osfxsrState -ne 'PASS' -or -not $osfxsrLog.Contains('OSFXSR SET AND VERIFIED')) {
            throw 'The test-only real-mode helper did not verify CR4.OSFXSR before SSE2 tests.'
        }
    }
    if ($TestType -notin @('quantized-direct','packed-dot-selftest','packed-quant-benchmark') -and ($inferState -ne 'PASS' -or -not $inferLog.Contains("matvec=$kernelName") -or
        -not $inferLog.Contains('Status: PASS'))) {
        throw "NFLLM did not complete inference through the selected $TestType matvec kernel."
    }
    if ($TestType -in @('mmx','sse2','3dnow')) {
        $referencePrefix = if ($TestType -eq 'sse2') { 'REFSSE' } elseif ($TestType -eq '3dnow') { 'REF3D' } else { 'REFMMX' }
        $referenceState = [IO.File]::ReadAllText((Join-Path $outputDirectory "$referencePrefix.OK")).Trim()
        $referenceLog = [IO.File]::ReadAllText((Join-Path $outputDirectory "$referencePrefix.LOG"))
        if ($referenceState -ne 'PASS' -or -not $referenceLog.Contains('matvec=reference.matvec.f32') -or
            -not $referenceLog.Contains('Status: PASS')) {
            throw "The scalar reference comparison for $TestType did not pass."
        }
        $mmxTicks = [regex]::Match($inferLog, 'Prompt evaluation tokens=\d+ ticks=(\d+)')
        $referenceTicks = [regex]::Match($referenceLog, 'Prompt evaluation tokens=\d+ ticks=(\d+)')
        $mmxToken = [regex]::Match($inferLog, 'TOKEN\[0\] id=(\d+)')
        $referenceToken = [regex]::Match($referenceLog, 'TOKEN\[0\] id=(\d+)')
        if (-not $mmxToken.Success -or -not $referenceToken.Success -or
            [int]$mmxToken.Groups[1].Value -ne [int]$referenceToken.Groups[1].Value) {
            throw "Could not extract matching $TestType/reference token IDs."
        }
        if ($mmxTicks.Success -and $referenceTicks.Success) {
            $performanceComparison = [pscustomobject]@{
                method='one sequential run per backend; prompt-evaluation ticks; exploratory only, not a benchmark'
                backend=$TestType
                backend_prompt_ticks=[int]$mmxTicks.Groups[1].Value
                reference_prompt_ticks=[int]$referenceTicks.Groups[1].Value
                backend_token_id=[int]$mmxToken.Groups[1].Value
                reference_token_id=[int]$referenceToken.Groups[1].Value
                slower_prompt_evaluation_than_reference=([int]$mmxTicks.Groups[1].Value -gt [int]$referenceTicks.Groups[1].Value)
            }
        } else {
            $performanceComparison = [pscustomobject]@{
                method='single inference token IDs only; prompt timing absent; exploratory and not a benchmark'
                backend=$TestType;timing_status='not_available'
                reason='One-shot guest logs did not contain Prompt evaluation ticks.'
                backend_prompt_ticks=$null;reference_prompt_ticks=$null
                backend_token_id=[int]$mmxToken.Groups[1].Value
                reference_token_id=[int]$referenceToken.Groups[1].Value
                identical_token=$true
            }
        }
        if ($BenchmarkRepetitions -gt 0) {
            $f32IsaBenchmarkResults = @()
            $f32Prefix = if ($TestType -eq 'sse2') { 'E' } elseif ($TestType -eq '3dnow') { 'D' } else { 'M' }
            $selectedSamples = [Collections.Generic.List[object]]::new()
            $referenceSamples = [Collections.Generic.List[object]]::new()
            $pairedTokenResults = [Collections.Generic.List[object]]::new()
            for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
                $suffix = [char]([int][char]'A' + $sample)
                $samplePair = @{}
                foreach ($route in @(
                    [pscustomobject]@{name='selected_isa';prefix="$f32Prefix`F";kernel=$kernelName;target=$selectedSamples}
                    [pscustomobject]@{name='reference';prefix='RF';kernel='reference.matvec.f32';target=$referenceSamples}
                )) {
                    $tag = "$($route.prefix)$suffix"
                    $state = [IO.File]::ReadAllText((Join-Path $outputDirectory "$tag.OK")).Trim()
                    $log = [IO.File]::ReadAllText((Join-Path $outputDirectory "$tag.LOG"))
                    $promptMatch = [regex]::Match($log, 'Prompt evaluation tokens=(\d+) ticks=(\d+)')
                    $generationMatch = [regex]::Match($log, 'Generation tokens=(\d+) ticks=(\d+) clock_rate=(\d+)')
                    $tokenMatches = [regex]::Matches($log, 'TOKEN\[\d+\] id=(\d+)')
                    $tokenIds = @($tokenMatches | ForEach-Object { [int]$_.Groups[1].Value })
                    if ($state -ne 'PASS' -or -not $log.Contains("matvec=$($route.kernel)") -or
                        -not $log.Contains('Status: PASS') -or
                        ($promptMatch.Success -and ([int]$promptMatch.Groups[1].Value -le 0 -or [int]$promptMatch.Groups[2].Value -le 0)) -or
                        -not $generationMatch.Success -or
                        [int]$generationMatch.Groups[1].Value -lt 2 -or
                        [int]$generationMatch.Groups[2].Value -le 0 -or
                        [int]$generationMatch.Groups[3].Value -le 0 -or
                        $tokenIds.Count -lt 2 -or $tokenIds[0] -ne 411) {
                        throw "F32 $($route.name) sample $suffix failed, missed multiple tokens, or differed from the validated first token."
                    }
                    $record = [pscustomobject]@{
                        sample=$suffix;route=$route.name;status=$state
                        prompt_tokens=$(if ($promptMatch.Success) { [int]$promptMatch.Groups[1].Value } else { $null })
                        prompt_ticks=$(if ($promptMatch.Success) { [int]$promptMatch.Groups[2].Value } else { $null })
                        generated_tokens=[int]$generationMatch.Groups[1].Value
                        generation_ticks=[int]$generationMatch.Groups[2].Value
                        clock_rate=[int]$generationMatch.Groups[3].Value
                        prompt_ticks_per_token=$(if ($promptMatch.Success -and [int]$promptMatch.Groups[1].Value -gt 0) { [double]$promptMatch.Groups[2].Value / [int]$promptMatch.Groups[1].Value } else { $null })
                        prompt_tokens_per_second=$(if ($promptMatch.Success -and [int]$promptMatch.Groups[2].Value -gt 0) { [double]$promptMatch.Groups[1].Value * [int]$generationMatch.Groups[3].Value / [int]$promptMatch.Groups[2].Value } else { $null })
                        generation_ticks_per_token=([double]$generationMatch.Groups[2].Value / [int]$generationMatch.Groups[1].Value)
                        generation_tokens_per_second=([double]$generationMatch.Groups[3].Value * [int]$generationMatch.Groups[1].Value / [int]$generationMatch.Groups[2].Value)
                        token_ids=$tokenIds
                    }
                    $route.target.Add($record)
                    $samplePair[$route.name] = $record
                }
                $identicalTokens = (($samplePair.selected_isa.token_ids -join ',') -eq ($samplePair.reference.token_ids -join ','))
                if (-not $identicalTokens) { throw "F32 selected ISA and reference token sequences differ in sample $suffix." }
                $pairedTokenResults.Add([pscustomobject]@{
                    sample=$suffix
                    identical_token_sequence=$identicalTokens
                    selected_token_ids=$samplePair.selected_isa.token_ids
                    reference_token_ids=$samplePair.reference.token_ids
                })
            }
            foreach ($route in @(
                [pscustomobject]@{name=$TestType;target=$selectedSamples}
                [pscustomobject]@{name='reference';target=$referenceSamples}
            )) {
                $samples = $route.target
                $promptTicks = @($samples | Where-Object { $null -ne $_.prompt_ticks } | ForEach-Object { [double]$_.prompt_ticks })
                $promptPerToken = @($samples | Where-Object { $null -ne $_.prompt_ticks_per_token } | ForEach-Object { [double]$_.prompt_ticks_per_token })
                $promptTokensPerSecond = @($samples | Where-Object { $null -ne $_.prompt_tokens_per_second } | ForEach-Object { [double]$_.prompt_tokens_per_second })
                $generationTicks = @($samples | ForEach-Object { $_.generation_ticks })
                $generationPerToken = @($samples | ForEach-Object { [double]$_.generation_ticks_per_token })
                $generationTokensPerSecond = @($samples | ForEach-Object { [double]$_.generation_tokens_per_second })
                $f32IsaBenchmarkResults += [pscustomobject]@{
                    backend=$route.name;format='F32';repetitions=$BenchmarkRepetitions
                    prompt_ticks=$(if ($promptTicks.Count) { Get-MetricSummary $promptTicks } else { $null })
                    prompt_ticks_per_token=$(if ($promptPerToken.Count) { Get-MetricSummary $promptPerToken } else { $null })
                    prompt_tokens_per_second=$(if ($promptTokensPerSecond.Count) { Get-MetricSummary $promptTokensPerSecond } else { $null })
                    prompt_timing_status=$(if ($promptTicks.Count -eq $BenchmarkRepetitions) { 'PASS' } elseif ($promptTicks.Count -eq 0) { 'UNAVAILABLE' } else { 'PARTIAL' })
                    generation_ticks=(Get-MetricSummary $generationTicks)
                    generation_ticks_per_token=(Get-MetricSummary $generationPerToken)
                    generation_tokens_per_second=(Get-MetricSummary $generationTokensPerSecond)
                    samples=@($samples.ToArray())
                }
            }
            $f32IsaBenchmarkResults += [pscustomobject]@{
                backend=$TestType;format='F32';comparison='selected ISA vs reference; interleaved repeated samples'
                paired_token_results=@($pairedTokenResults.ToArray());status='PASS'
            }
        }
        if ($TestType -in @('mmx','sse2')) {
        $selectedPrefix = if ($TestType -eq 'sse2') { 'SSE2' } else { 'MMX' }
        $referenceQuantPrefix = if ($TestType -eq 'sse2') { 'RSSE2' } else { 'RMMX' }
        $quantizedIsaResults = @()
        foreach ($format in @(
            [pscustomobject]@{name='Q8_0';suffix='Q8';row='Q8_0';token=411}
            [pscustomobject]@{name='Q4_0';suffix='Q4';row='Q4_0';token=402}
        )) {
            $selectedTag = "$selectedPrefix$($format.suffix)"
            $referenceTag = "$referenceQuantPrefix$($format.suffix)"
            $selectedState = [IO.File]::ReadAllText((Join-Path $outputDirectory "$selectedTag.OK")).Trim()
            $selectedLog = [IO.File]::ReadAllText((Join-Path $outputDirectory "$selectedTag.LOG"))
            $referenceQuantState = [IO.File]::ReadAllText((Join-Path $outputDirectory "$referenceTag.OK")).Trim()
            $referenceQuantLog = [IO.File]::ReadAllText((Join-Path $outputDirectory "$referenceTag.LOG"))
            $selectedToken = [regex]::Match($selectedLog, 'TOKEN\[0\] id=(\d+)')
            $referenceQuantToken = [regex]::Match($referenceQuantLog, 'TOKEN\[0\] id=(\d+)')
            $directRows = [regex]::Match($selectedLog, "Direct packed matvec rows:.*$($format.row)=(\d+)")
            $referenceRows = [regex]::Match($referenceQuantLog, "Direct packed matvec rows:.*$($format.row)=(\d+)")
            if ($selectedState -ne 'PASS' -or -not $selectedLog.Contains('Status: PASS') -or
                -not $selectedLog.Contains("matvec=$kernelName") -or
                $referenceQuantState -ne 'PASS' -or -not $referenceQuantLog.Contains('Status: PASS') -or
                -not $referenceQuantLog.Contains('matvec=reference.matvec.f32') -or
                -not $selectedToken.Success -or -not $referenceQuantToken.Success -or
                [int]$selectedToken.Groups[1].Value -ne [int]$referenceQuantToken.Groups[1].Value -or
                [int]$selectedToken.Groups[1].Value -ne $format.token -or
                -not $directRows.Success -or [int]$directRows.Groups[1].Value -le 0 -or
                -not $referenceRows.Success -or [int]$referenceRows.Groups[1].Value -ne 0) {
                throw "$($format.name) $TestType inference failed, differed from the reference, or did not use the expected direct/fallback routes."
            }
            $quantizedIsaResults += [pscustomobject]@{
                format=$format.name;backend=$TestType;backend_status=$selectedState
                backend_token_id=[int]$selectedToken.Groups[1].Value
                backend_direct_rows=[int]$directRows.Groups[1].Value
                reference_status=$referenceQuantState
                reference_token_id=[int]$referenceQuantToken.Groups[1].Value
                reference_direct_rows=[int]$referenceRows.Groups[1].Value
                identical_token=$true;status='PASS'
            }
        }
        if ($QuantizedRepetitions -gt 0) {
            $quantizedIsaBenchmarkResults = @()
            $benchmarkPrefix = if ($TestType -eq 'sse2') { 'E' } else { 'M' }
            $scalarPrefix = "S$benchmarkPrefix"
            foreach ($format in @(
                [pscustomobject]@{name='Q8_0';suffix='Q8';row='Q8_0'}
                [pscustomobject]@{name='Q4_0';suffix='Q4';row='Q4_0'}
            )) {
                $matrixSamples = [Collections.Generic.List[object]]::new()
                $scalarSamples = [Collections.Generic.List[object]]::new()
                $pairedTokenResults = [Collections.Generic.List[object]]::new()
                for ($sample = 0; $sample -lt $QuantizedRepetitions; $sample++) {
                    $suffix = [char]([int][char]'A' + $sample)
                    $samplePair = @{}
                    foreach ($route in @(
                        [pscustomobject]@{name='matrix';prefix=$benchmarkPrefix;path='direct';target=$matrixSamples}
                        [pscustomobject]@{name='scalar';prefix=$scalarPrefix;path='direct scalar row kernel';target=$scalarSamples}
                    )) {
                        $tag = "$($route.prefix)$($format.suffix)$suffix"
                        $state = [IO.File]::ReadAllText((Join-Path $outputDirectory "$tag.OK")).Trim()
                        $log = [IO.File]::ReadAllText((Join-Path $outputDirectory "$tag.LOG"))
                        $promptMatch = [regex]::Match($log, 'Prompt evaluation tokens=(\d+) ticks=(\d+)')
                        $generationMatch = [regex]::Match($log, 'Generation tokens=(\d+) ticks=(\d+) clock_rate=(\d+)')
                        $rowMatch = [regex]::Match($log, "Direct packed matvec rows:.*$($format.row)=(\d+)")
                        $tokenMatches = [regex]::Matches($log, 'TOKEN\[\d+\] id=(\d+)')
                        $tokenIds = @($tokenMatches | ForEach-Object { [int]$_.Groups[1].Value })
                        $expectedPath = if ($route.name -eq 'matrix') { 'direct packed when available' } else { $route.path }
                        if ($state -ne 'PASS' -or -not $log.Contains('Status: PASS') -or
                            -not $log.Contains("Quantized matvec path: $expectedPath") -or
                            -not $promptMatch.Success -or -not $generationMatch.Success -or
                            [int]$promptMatch.Groups[1].Value -le 0 -or
                            [int]$promptMatch.Groups[2].Value -le 0 -or
                            [int]$generationMatch.Groups[1].Value -lt 2 -or
                            [int]$generationMatch.Groups[2].Value -le 0 -or
                            $tokenIds.Count -lt 2 -or -not $rowMatch.Success -or
                            [int]$rowMatch.Groups[1].Value -le 0) {
                            throw "$($format.name) $($route.name) sample $suffix failed, did not generate multiple tokens, or did not use direct rows."
                        }
                        $record = [pscustomobject]@{
                            sample=$suffix;route=$route.name;status=$state
                            prompt_tokens=[int]$promptMatch.Groups[1].Value
                            prompt_ticks=[int]$promptMatch.Groups[2].Value
                            generated_tokens=[int]$generationMatch.Groups[1].Value
                            generation_ticks=[int]$generationMatch.Groups[2].Value
                            clock_rate=[int]$generationMatch.Groups[3].Value
                            generation_ticks_per_token=([double]$generationMatch.Groups[2].Value / [int]$generationMatch.Groups[1].Value)
                            generation_tokens_per_second=([double]$generationMatch.Groups[3].Value * [int]$generationMatch.Groups[1].Value / [int]$generationMatch.Groups[2].Value)
                            prompt_ticks_per_token=([double]$promptMatch.Groups[2].Value / [int]$promptMatch.Groups[1].Value)
                            prompt_tokens_per_second=([double]$promptMatch.Groups[1].Value * [int]$generationMatch.Groups[3].Value / [int]$promptMatch.Groups[2].Value)
                            direct_rows=[int]$rowMatch.Groups[1].Value;token_ids=$tokenIds
                        }
                        $route.target.Add($record)
                        $samplePair[$route.name] = $record
                    }
                    $pairedTokenResults.Add([pscustomobject]@{
                        sample=$suffix
                        identical_token_sequence=(($samplePair.matrix.token_ids -join ',') -eq ($samplePair.scalar.token_ids -join ','))
                        matrix_token_ids=$samplePair.matrix.token_ids
                        scalar_token_ids=$samplePair.scalar.token_ids
                    })
                }
                $matrixPrompt = @($matrixSamples | ForEach-Object { $_.prompt_ticks })
                $matrixGeneration = @($matrixSamples | ForEach-Object { $_.generation_ticks })
                $matrixPerToken = @($matrixSamples | ForEach-Object { [double]$_.generation_ticks_per_token })
                $matrixTokensPerSecond = @($matrixSamples | ForEach-Object { [double]$_.generation_tokens_per_second })
                $matrixPromptPerToken = @($matrixSamples | ForEach-Object { [double]$_.prompt_ticks_per_token })
                $matrixPromptTokensPerSecond = @($matrixSamples | ForEach-Object { [double]$_.prompt_tokens_per_second })
                $scalarPrompt = @($scalarSamples | ForEach-Object { $_.prompt_ticks })
                $scalarGeneration = @($scalarSamples | ForEach-Object { $_.generation_ticks })
                $scalarPerToken = @($scalarSamples | ForEach-Object { [double]$_.generation_ticks_per_token })
                $scalarTokensPerSecond = @($scalarSamples | ForEach-Object { [double]$_.generation_tokens_per_second })
                $scalarPromptPerToken = @($scalarSamples | ForEach-Object { [double]$_.prompt_ticks_per_token })
                $scalarPromptTokensPerSecond = @($scalarSamples | ForEach-Object { [double]$_.prompt_tokens_per_second })
                $quantizedIsaBenchmarkResults += [pscustomobject]@{
                    format=$format.name;backend=$TestType;comparison='SIMD matrix direct vs scalar direct row kernel'
                    repetitions=$QuantizedRepetitions
                    matrix=[pscustomobject]@{
                        prompt_ticks=(Get-MetricSummary $matrixPrompt)
                        prompt_ticks_per_token=(Get-MetricSummary $matrixPromptPerToken)
                        prompt_tokens_per_second=(Get-MetricSummary $matrixPromptTokensPerSecond)
                        generation_ticks=(Get-MetricSummary $matrixGeneration)
                        generation_ticks_per_token=(Get-MetricSummary $matrixPerToken)
                        generation_tokens_per_second=(Get-MetricSummary $matrixTokensPerSecond)
                        samples=@($matrixSamples.ToArray())
                    }
                    scalar=[pscustomobject]@{
                        prompt_ticks=(Get-MetricSummary $scalarPrompt)
                        prompt_ticks_per_token=(Get-MetricSummary $scalarPromptPerToken)
                        prompt_tokens_per_second=(Get-MetricSummary $scalarPromptTokensPerSecond)
                        generation_ticks=(Get-MetricSummary $scalarGeneration)
                        generation_ticks_per_token=(Get-MetricSummary $scalarPerToken)
                        generation_tokens_per_second=(Get-MetricSummary $scalarTokensPerSecond)
                        samples=@($scalarSamples.ToArray())
                    }
                    paired_token_results=@($pairedTokenResults.ToArray())
                    status='PASS'
                }
            }
        }
        }
    }
    if ($forcedShutdown) {
        $stopReason = "Guest marker verified twice; QMP quit did not exit within 30 seconds; exact task-owned PID was stopped; extracted $TestType tests and inference passed."
    } else {
        $stopReason = "Guest marker verified twice; QMP quit sent; process exit verified; extracted $TestType tests and inference passed."
    }
    $exitCode = 0
}
catch {
    $failure = $_.Exception.Message
    if ($process -and -not $process.HasExited) { $stopReason = "Failure cleanup after: $failure" }
    throw
}
finally {
    $processAlive = $false
    if ($process) {
        try { $process.Refresh(); $processAlive = -not $process.HasExited } catch { }
    }
    if ($processAlive) {
        $creation = $process.StartTime.ToString('o')
        if (Test-ExactQemuIdentity $process $creation $vmName) {
            try { Send-QmpQuit $QmpPort } catch { }
            if (-not $process.WaitForExit(30000) -and
                (Test-ExactQemuIdentity $process $creation $vmName)) {
                $process.Kill()
                [void]$process.WaitForExit(5000)
                $forcedShutdown = $true
            }
        } else {
            $stopReason = 'QEMU remains registered active because its exact process identity could not be verified; no signal was sent.'
        }
    }
    if ($process) {
        try { $process.Refresh(); $processAlive = -not $process.HasExited } catch { }
    }
    if ($process -and -not $processAlive) {
        try {
            if ($stdoutTask) { [IO.File]::WriteAllText($stdoutPath, $stdoutTask.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false)) }
            if ($stderrTask) { [IO.File]::WriteAllText($stderrPath, $stderrTask.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false)) }
        } catch {
            $stopReason = "$stopReason Standard stream capture failed: $($_.Exception.Message)"
        }
    }
    if ($registered) {
        $registryLock = Open-RegistryLock
        try {
            $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
            $row = @($registry.entries | Where-Object task_id -eq $taskId)
            if ($row.Count -eq 1) {
                if ($processAlive) {
                    Set-Field $row[0] 'state' 'active'
                    Set-Field $row[0] 'exit_code' $null
                    if ($failure) { Set-Field $row[0] 'stop_reason' $stopReason }
                } else {
                    Set-Field $row[0] 'state' 'exited'
                    if ($process) {
                        try { Set-Field $row[0] 'exit_code' $process.ExitCode } catch { Set-Field $row[0] 'exit_code' $exitCode }
                    } else {
                        Set-Field $row[0] 'exit_code' $exitCode
                    }
                    Set-Field $row[0] 'stopped_at' (Get-Date).ToString('o')
                    Set-Field $row[0] 'stop_reason' $stopReason
                }
                Write-Registry $registry
            }
        } finally { $registryLock.Dispose() }
    }
    if ($diskHashBefore) {
        $originalHashAfter = (Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
        $summary = [pscustomobject]@{
            schema_version=1;run_id=$RunId;task_id=$taskId;cpu_profile=$CpuProfile
            qemu_version=(((& $qemuExe -version 2>&1) | Select-Object -First 1).ToString())
            qemu_arguments=$qemuArgs;memory_mib=96;disk=$diskPath;disk_sha256_before=$diskHashBefore
            disk_sha256_after=(Get-FileHash -LiteralPath $diskPath -Algorithm SHA256).Hash
            original_disk_sha256_before='32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3'
            original_disk_sha256_after=$originalHashAfter;original_preserved=($originalHashAfter -eq '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3')
            guest_marker_verified=$markerVerified;process_exit_code=$qemuExitCode;runner_exit_code=$exitCode
            forced_shutdown=$forcedShutdown;failure=$failure;stop_reason=$stopReason
            serial_log=$serialPath;serial_sha256=$(if (Test-Path $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { $null })
            performance_comparison=$performanceComparison
            quantized_direct_results=$quantizedDirectResults
            quantized_isa_results=$quantizedIsaResults
            quantized_isa_benchmark_results=$quantizedIsaBenchmarkResults
            f32_isa_benchmark_results=$f32IsaBenchmarkResults
            packed_quant_benchmark_results=$packedQuantBenchmarkResults
            debug_log=$debugPath;stdout_log=$stdoutPath;stderr_log=$stderrPath;output_directory=$outputDirectory;finished_at=(Get-Date).ToString('o')
        }
        [IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    }
}

if ($exitCode -ne 0) { throw "QEMU ISA acceptance failed; see $summaryPath" }
Get-Content -LiteralPath $summaryPath -Raw
