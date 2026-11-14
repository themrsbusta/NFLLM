param(
    [string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path,
    [string]$DiskPath = '',
    [string]$EvidencePath = '',
    [ValidateRange(0,5)][int]$BenchmarkRepetitions = 0,
    [ValidateSet('none','athlon-lx-proxy','atom-n270')][string]$ISALabProfile = 'none',
    [ValidateSet('none','486','geode-gx1','geode-lx','atom-n270')][string]$NFLLMISAProfile = 'none',
    [ValidateSet('auto','reference','mmx','3dnow','sse2')][string]$NFLLMBackend = 'auto',
    [switch]$ISALabOnly,
    [switch]$NFLLMISAOnly,
    [switch]$DirectQuantBenchmarkOnly,
    [switch]$PackedQuantBenchmarkOnly,
    [switch]$SelfTestOnly,
    [switch]$SerialCompletionMarker
)

$ErrorActionPreference = 'Stop'

if ($SelfTestOnly -and ($BenchmarkRepetitions -gt 0 -or $ISALabOnly -or
    $NFLLMISAOnly -or $DirectQuantBenchmarkOnly -or $PackedQuantBenchmarkOnly -or $ISALabProfile -ne 'none' -or
    $NFLLMISAProfile -ne 'none')) {
    throw 'SelfTestOnly cannot be combined with another specialized AUTOEXEC mode.'
}

function Set-Field($Object, [string]$Name, $Value) {
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        Add-Member -InputObject $Object -MemberType NoteProperty -Name $Name -Value $Value
    } else { $Object.$Name = $Value }
}

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -le 0) { throw 'Unexpected end of cloned disk.' }
        $offset += $read
    }
    return ,$bytes
}

function Get-Chain([byte[]]$Fat, [int]$First, [int]$Limit) {
    $chain = [Collections.Generic.List[int]]::new()
    $cluster = $First
    while ($cluster -ge 2 -and $cluster -lt 0xfff8 -and $chain.Count -lt $Limit) {
        if ($chain.Contains($cluster)) { throw 'FAT16 cluster loop detected.' }
        $chain.Add($cluster)
        $cluster = [BitConverter]::ToUInt16($Fat, $cluster * 2)
    }
    if ($cluster -lt 0xfff8) { throw "Invalid FAT16 end marker $cluster." }
    return ,$chain.ToArray()
}

function Get-EntryOffset([byte[]]$Root, [byte[]]$ShortName) {
    for ($offset = 0; $offset -lt $Root.Length; $offset += 32) {
        if ($Root[$offset] -eq 0) { break }
        if ($Root[$offset] -ne 0xe5 -and [Linq.Enumerable]::SequenceEqual([byte[]]$Root[$offset..($offset + 10)], $ShortName)) {
            return $offset
        }
    }
    return -1
}

$workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$diskPath = if ([string]::IsNullOrWhiteSpace($DiskPath)) {
    Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test.raw'
} else {
    [IO.Path]::GetFullPath($DiskPath)
}
$originalDisk = Join-Path $emulatorRoot 'disks\nfdos502-qemu.raw'
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
$evidencePath = if ([string]::IsNullOrWhiteSpace($EvidencePath)) {
    Join-Path $workspace 'NFLLM\source\nfllm\build\logs\acceptance-autoexec.json'
} else {
    [IO.Path]::GetFullPath($EvidencePath)
}
$vmExe = Join-Path $emulatorRoot 'app\86Box.exe'
$taskId = "nfllm-update-acceptance-autoexec-$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
$commands = @(
    'REM NFLLM AM486 ACCEPTANCE BEGIN'
    'DEL C:\NFLLM.DON >NUL'
    'DEL C:\SELFTEST.OK >NUL'
    'DEL C:\COMPAT.OK >NUL'
    'DEL C:\INFER.OK >NUL'
    'DEL C:\T1COMPAT.OK >NUL'
    'DEL C:\T1INFER.OK >NUL'
    'DEL C:\MANUAL.OK >NUL'
    'DEL C:\MISS.OK >NUL'
    'DEL C:\TRUNC.OK >NUL'
    'DEL C:\Q8COMP.OK >NUL'
    'DEL C:\Q8INFER.OK >NUL'
    'DEL C:\Q4COMP.OK >NUL'
    'DEL C:\Q4INFER.OK >NUL'
    'DEL C:\Q20COMP.OK >NUL'
    'DEL C:\Q20INFER.OK >NUL'
    'DEL C:\Q2KCOMP.OK >NUL'
    'DEL C:\Q2KINFER.OK >NUL'
    'DEL C:\I3XCOMP.OK >NUL'
    'DEL C:\I3XINFER.OK >NUL'
    'DEL C:\I3SCOMP.OK >NUL'
    'DEL C:\I3SINFER.OK >NUL'
    'DEL C:\SELFTEST.LOG >NUL'
    'DEL C:\COMPAT.LOG >NUL'
    'DEL C:\NFLLM.LOG >NUL'
    'DEL C:\MANUAL.LOG >NUL'
    'DEL C:\T1COMPAT.LOG >NUL'
    'DEL C:\T1NFLLM.LOG >NUL'
    'DEL C:\MISSING.LOG >NUL'
    'DEL C:\TRUNC.LOG >NUL'
    'DEL C:\Q8COMP.LOG >NUL'
    'DEL C:\Q8NFLLM.LOG >NUL'
    'DEL C:\Q4COMP.LOG >NUL'
    'DEL C:\Q4NFLLM.LOG >NUL'
    'DEL C:\Q20COMP.LOG >NUL'
    'DEL C:\Q20NFLLM.LOG >NUL'
    'DEL C:\Q2KCOMP.LOG >NUL'
    'DEL C:\Q2KNFLLM.LOG >NUL'
    'DEL C:\I3XCOMP.LOG >NUL'
    'DEL C:\I3XNFLLM.LOG >NUL'
    'DEL C:\I3SCOMP.LOG >NUL'
    'DEL C:\I3SNFLLM.LOG >NUL'
    'SET PATH=C:\;%PATH%'
    'ECHO SELFTEST_START > C:\SELFTEST.OK'
    'C:\NFTEST.EXE > C:\SELFTEST.LOG'
    'IF ERRORLEVEL 1 ECHO SELFTEST_FAIL > C:\SELFTEST.OK'
    'IF NOT ERRORLEVEL 1 ECHO SELFTEST_PASS > C:\SELFTEST.OK'
    'ECHO COMPAT_START > C:\COMPAT.OK'
    'C:\COMPAT.EXE C:\STORIES.GGF > C:\COMPAT.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\COMPAT.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\COMPAT.OK'
    'ECHO INFER_START > C:\INFER.OK'
    'C:\NFLLM.EXE C:\STORIES.GGF -p "Once upon a time" -n 1 > C:\NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\INFER.OK'
    'ECHO MANUAL_START > C:\MANUAL.OK'
    'C:\NFLLM.EXE C:\STORIES.GGF -p "Once upon a time" -n 1 --backend reference > C:\MANUAL.LOG'
    'IF ERRORLEVEL 1 ECHO MANUAL_FAIL > C:\MANUAL.OK'
    'IF NOT ERRORLEVEL 1 ECHO MANUAL_PASS > C:\MANUAL.OK'
    'ECHO T1COMPAT_START > C:\T1COMPAT.OK'
    'C:\COMPAT.EXE C:\TINY1M.GGF > C:\T1COMPAT.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\T1COMPAT.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\T1COMPAT.OK'
    'ECHO T1INFER_START > C:\T1INFER.OK'
    'C:\NFLLM.EXE C:\TINY1M.GGF -p "Tom and Jerry are " -n 1 > C:\T1NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\T1INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\T1INFER.OK'
    'ECHO Q8COMPAT_START > C:\Q8COMP.OK'
    'C:\COMPAT.EXE C:\TINYQ8.GGF > C:\Q8COMP.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\Q8COMP.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\Q8COMP.OK'
    'ECHO Q8INFER_START > C:\Q8INFER.OK'
    'C:\NFLLM.EXE C:\TINYQ8.GGF -p "Tom and Jerry are " -n 1 > C:\Q8NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\Q8INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\Q8INFER.OK'
    'ECHO Q4COMPAT_START > C:\Q4COMP.OK'
    'C:\COMPAT.EXE C:\TINYQ4.GGF > C:\Q4COMP.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\Q4COMP.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\Q4COMP.OK'
    'ECHO Q4INFER_START > C:\Q4INFER.OK'
    'C:\NFLLM.EXE C:\TINYQ4.GGF -p "Tom and Jerry are " -n 1 --backend reference > C:\Q4NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\Q4INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\Q4INFER.OK'
    'ECHO Q20COMPAT_START > C:\Q20COMP.OK'
    'C:\COMPAT.EXE C:\TINYQ20.GGF > C:\Q20COMP.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\Q20COMP.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\Q20COMP.OK'
    'ECHO Q20INFER_START > C:\Q20INFER.OK'
    'C:\NFLLM.EXE C:\TINYQ20.GGF -p "Tom and Jerry are " -n 1 > C:\Q20NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\Q20INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\Q20INFER.OK'
    'ECHO Q2KCOMPAT_START > C:\Q2KCOMP.OK'
    'C:\COMPAT.EXE C:\TINYQ2K.GGF > C:\Q2KCOMP.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\Q2KCOMP.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\Q2KCOMP.OK'
    'ECHO Q2KINFER_START > C:\Q2KINFER.OK'
    'C:\NFLLM.EXE C:\TINYQ2K.GGF -p "Tom and Jerry are " -n 1 > C:\Q2KNFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\Q2KINFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\Q2KINFER.OK'
    'ECHO I3XCOMPAT_START > C:\I3XCOMP.OK'
    'C:\COMPAT.EXE C:\TINYI3X.GGF > C:\I3XCOMP.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\I3XCOMP.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\I3XCOMP.OK'
    'ECHO I3XINFER_START > C:\I3XINFER.OK'
    'C:\NFLLM.EXE C:\TINYI3X.GGF -p "Tom and Jerry are " -n 1 > C:\I3XNFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\I3XINFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\I3XINFER.OK'
    'ECHO I3SCOMPAT_START > C:\I3SCOMP.OK'
    'C:\COMPAT.EXE C:\TINYI3S.GGF > C:\I3SCOMP.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\I3SCOMP.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\I3SCOMP.OK'
    'ECHO I3SINFER_START > C:\I3SINFER.OK'
    'C:\NFLLM.EXE C:\TINYI3S.GGF -p "Tom and Jerry are " -n 1 > C:\I3SNFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\I3SINFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\I3SINFER.OK'
    'ECHO MISSING_START > C:\MISS.OK'
    'C:\NFLLM.EXE C:\NOFILE.GGF -n 1 > C:\MISSING.LOG'
    'IF ERRORLEVEL 2 ECHO EXPECTED_ERROR2 > C:\MISS.OK'
    'ECHO TRUNC_START > C:\TRUNC.OK'
    'C:\NFLLM.EXE C:\TRUNC.GGF -n 1 > C:\TRUNC.LOG'
    'IF ERRORLEVEL 2 ECHO EXPECTED_ERROR2 > C:\TRUNC.OK'
    'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON'
    'REM NFLLM AM486 ACCEPTANCE END'
)
if ($SelfTestOnly) {
    $commands = @(
        'REM NFLLM PACKED MATVEC SELFTEST BEGIN'
        'DEL C:\NFLLM.DON >NUL'
        'DEL C:\SELFTEST.OK >NUL'
        'DEL C:\SELFTEST.LOG >NUL'
        'SET PATH=C:\;%PATH%'
        'ECHO SELFTEST_START > C:\SELFTEST.OK'
        'C:\NFTEST.EXE > C:\SELFTEST.LOG'
        'IF ERRORLEVEL 1 ECHO SELFTEST_FAIL > C:\SELFTEST.OK'
        'IF NOT ERRORLEVEL 1 ECHO SELFTEST_PASS > C:\SELFTEST.OK'
        'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON'
        'REM NFLLM PACKED MATVEC SELFTEST END'
    )
}
if ($ISALabProfile -ne 'none') {
    $isaCommands = @(
        'DEL C:\ISA.OK >NUL'
        'DEL C:\ISA.LOG >NUL'
        'ECHO ISA_START > C:\ISA.OK'
        "C:\ISAPROBE.EXE $ISALabProfile > C:\ISA.LOG"
        'IF ERRORLEVEL 1 ECHO ISA_FAIL > C:\ISA.OK'
        'IF NOT ERRORLEVEL 1 ECHO ISA_PASS > C:\ISA.OK'
    )
    $insertIndex = [Array]::IndexOf($commands, 'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
    if ($insertIndex -lt 0) { throw 'Could not locate the NFLLM completion marker insertion point.' }
    $commands = @($commands[0..($insertIndex - 1)] + $isaCommands + $commands[$insertIndex..($commands.Length - 1)])
}
if ($SerialCompletionMarker) {
    $insertIndex = [Array]::IndexOf($commands, 'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
    if ($insertIndex -lt 0) { throw 'Could not locate the NFLLM completion marker for serial output.' }
    $commands = @($commands[0..$insertIndex] + 'ECHO NFLLM_TEST_FINISHED > COM1' + $commands[($insertIndex + 1)..($commands.Length - 1)])
}
if ($BenchmarkRepetitions -gt 0) {
    $benchmarkCommands = [Collections.Generic.List[string]]::new()
    $modes = @(
        [pscustomobject]@{Label='F32';Model='TINY1M.GGF'}
        [pscustomobject]@{Label='Q8';Model='TINYQ8.GGF'}
        [pscustomobject]@{Label='Q4';Model='TINYQ4.GGF'}
    )
    for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
        $suffix = [char]([int][char]'A' + $sample)
        foreach ($mode in $modes) {
            $tag = "$($mode.Label)$suffix"
            $benchmarkCommands.Add(("DEL C:\{0}.OK >NUL" -f $tag))
            $benchmarkCommands.Add(("DEL C:\{0}.LOG >NUL" -f $tag))
        }
    }
    for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
        $suffix = [char]([int][char]'A' + $sample)
        foreach ($mode in $modes) {
            $tag = "$($mode.Label)$suffix"
            $benchmarkCommands.Add(("ECHO START > C:\{0}.OK" -f $tag))
            $benchmarkCommands.Add(("C:\NFLLM.EXE C:\{0} -p `"Tom and Jerry are `" -n 1 > C:\{1}.LOG" -f $mode.Model,$tag))
            $benchmarkCommands.Add(("IF ERRORLEVEL 1 ECHO FAIL > C:\{0}.OK" -f $tag))
            $benchmarkCommands.Add(("IF NOT ERRORLEVEL 1 ECHO PASS > C:\{0}.OK" -f $tag))
        }
    }
    $insertIndex = [Array]::IndexOf($commands, 'ECHO MISSING_START > C:\MISS.OK')
    if ($insertIndex -lt 0) { throw 'Could not locate the benchmark insertion point in AUTOEXEC commands.' }
    $commands = @($commands[0..($insertIndex - 1)] + $benchmarkCommands.ToArray() + $commands[$insertIndex..($commands.Length - 1)])
}
if ($DirectQuantBenchmarkOnly) {
    if ($BenchmarkRepetitions -lt 1) { throw 'DirectQuantBenchmarkOnly requires BenchmarkRepetitions from 1 through 5.' }
    if ($ISALabOnly) { throw 'DirectQuantBenchmarkOnly cannot be combined with ISALabOnly.' }
    if ($ISALabProfile -ne 'none') { throw 'DirectQuantBenchmarkOnly cannot be combined with ISALabProfile.' }
    $commands = [Collections.Generic.List[string]]::new()
    $commands.Add('REM NFLLM DIRECT QUANTIZED MATVEC BENCHMARK BEGIN')
    $commands.Add('DEL C:\NFLLM.DON >NUL')
    $commands.Add('SET PATH=C:\;%PATH%')
    $jobs = @(
        [pscustomobject]@{Label='F32';Model='TINY1M.GGF';Backend=''}
        [pscustomobject]@{Label='Q8';Model='TINYQ8.GGF';Backend=''}
        [pscustomobject]@{Label='Q8R';Model='TINYQ8.GGF';Backend=' --backend reference'}
        [pscustomobject]@{Label='Q4';Model='TINYQ4.GGF';Backend=''}
        [pscustomobject]@{Label='Q4R';Model='TINYQ4.GGF';Backend=' --backend reference'}
    )
    for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
        $suffix = [char]([int][char]'A' + $sample)
        foreach ($job in $jobs) {
            $tag = "$($job.Label)$suffix"
            $commands.Add(("DEL C:\{0}.OK >NUL" -f $tag))
            $commands.Add(("DEL C:\{0}.LOG >NUL" -f $tag))
        }
    }
    for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
        $suffix = [char]([int][char]'A' + $sample)
        foreach ($job in $jobs) {
            $tag = "$($job.Label)$suffix"
            $commands.Add(("ECHO START > C:\{0}.OK" -f $tag))
            $commands.Add(("C:\NFLLM.EXE C:\{0} -p `"Tom and Jerry are `" -n 1{1} > C:\{2}.LOG" -f $job.Model,$job.Backend,$tag))
            $commands.Add(("IF ERRORLEVEL 1 ECHO FAIL > C:\{0}.OK" -f $tag))
            $commands.Add(("IF NOT ERRORLEVEL 1 ECHO PASS > C:\{0}.OK" -f $tag))
        }
    }
    $commands.Add('ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
    if ($SerialCompletionMarker) { $commands.Add('ECHO NFLLM_TEST_FINISHED > COM1') }
    $commands.Add('REM NFLLM DIRECT QUANTIZED MATVEC BENCHMARK END')
}
if ($PackedQuantBenchmarkOnly) {
    if ($BenchmarkRepetitions -lt 1) { throw 'PackedQuantBenchmarkOnly requires BenchmarkRepetitions from 1 through 5.' }
    if ($ISALabOnly -or $NFLLMISAOnly -or $DirectQuantBenchmarkOnly -or
        $ISALabProfile -ne 'none' -or $NFLLMISAProfile -ne 'none') {
        throw 'PackedQuantBenchmarkOnly cannot be combined with another specialized AUTOEXEC mode.'
    }
    $commands = [Collections.Generic.List[string]]::new()
    $commands.Add('REM NFLLM PACKED QUANTIZED MATVEC BENCHMARK BEGIN')
    $commands.Add('DEL C:\NFLLM.DON >NUL')
    $commands.Add('SET PATH=C:\;%PATH%')
    $commands.Add('DEL C:\PDBENCH.OK >NUL')
    $commands.Add('DEL C:\PDBENCH.LOG >NUL')
    $commands.Add('ECHO START > C:\PDBENCH.OK')
    $commands.Add('ECHO PACKED_QUANT_BENCHMARK > C:\PDBENCH.LOG')
    $formats = @(
        [pscustomobject]@{Label='Q20';Model='TINYQ20.GGF'}
        [pscustomobject]@{Label='Q2K';Model='TINYQ2K.GGF'}
        [pscustomobject]@{Label='I3X';Model='TINYI3X.GGF'}
        [pscustomobject]@{Label='I3S';Model='TINYI3S.GGF'}
    )
    for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
        $suffix = [char]([int][char]'A' + $sample)
        foreach ($format in $formats) {
            foreach ($route in @('D','F')) {
                $tag = "$($format.Label)$route$suffix"
                $commands.Add(("DEL C:\{0}.OK >NUL" -f $tag))
                $commands.Add(("DEL C:\{0}.LOG >NUL" -f $tag))
            }
        }
    }
    for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
        $suffix = [char]([int][char]'A' + $sample)
        foreach ($format in $formats) {
            $routes = if (($sample % 2) -eq 0) { @('D','F') } else { @('F','D') }
            foreach ($route in $routes) {
                $tag = "$($format.Label)$route$suffix"
                $mode = if ($route -eq 'D') { 'direct' } else { 'decode' }
                $commands.Add(("ECHO START > C:\{0}.OK" -f $tag))
                $commands.Add(("C:\NFLLM.EXE C:\{0} -p `"Tom and Jerry are `" -n 1 --quantized-matvec {1} > C:\{2}.LOG" -f $format.Model,$mode,$tag))
                $commands.Add(("IF ERRORLEVEL 1 ECHO FAIL > C:\{0}.OK" -f $tag))
                $commands.Add(("IF NOT ERRORLEVEL 1 ECHO PASS > C:\{0}.OK" -f $tag))
            }
        }
    }
    $commands.Add('ECHO PASS > C:\PDBENCH.OK')
    $commands.Add('ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
    if ($SerialCompletionMarker) { $commands.Add('ECHO NFLLM_TEST_FINISHED > COM1') }
    $commands.Add('REM NFLLM PACKED QUANTIZED MATVEC BENCHMARK END')
}
if ($NFLLMISAOnly) {
    if ($NFLLMISAProfile -eq 'none') { throw 'NFLLMISAOnly requires an explicit NFLLMISAProfile.' }
    if (($BenchmarkRepetitions -gt 0 -and $NFLLMBackend -notin @('mmx','sse2','3dnow')) -or
        $ISALabOnly -or $DirectQuantBenchmarkOnly -or $PackedQuantBenchmarkOnly) {
        throw 'NFLLMISAOnly cannot be combined with another specialized AUTOEXEC mode.'
    }
    if ($NFLLMISAProfile -eq 'geode-lx' -and $NFLLMBackend -eq '3dnow') {
        $backendArgument = ' --backend 3dnow'
    } elseif ($NFLLMBackend -eq '3dnow') {
        throw 'The 3DNow! F32 kernel test requires the geode-lx profile.'
    } elseif ($NFLLMBackend -eq 'mmx') {
        $backendArgument = ' --backend mmx'
    } elseif ($NFLLMBackend -eq 'sse2') {
        if ($NFLLMISAProfile -ne 'atom-n270') { throw 'SSE2 test profile requires atom-n270.' }
        $backendArgument = ' --backend sse2'
    } elseif ($NFLLMBackend -eq 'auto') {
        $backendArgument = ''
    } else {
        $backendArgument = ' --backend reference'
    }
    $inferencePrefix = if ($NFLLMISAProfile -eq '486') { 'SAFE486' } elseif ($NFLLMBackend -eq 'sse2') { 'SSE2' } else { '3DNOW' }
    $quantPrefix = if ($NFLLMBackend -eq 'mmx') { 'MMX' } else { 'SSE2' }
    $isaTestExe = if ($NFLLMBackend -eq '3dnow') { 'NF3DNOW.EXE' } elseif ($NFLLMBackend -eq 'mmx') { 'NFMMX.EXE' } elseif ($NFLLMBackend -eq 'sse2') { 'NFSSE2.EXE' } else { '' }
    $isaTestPrefix = if ($NFLLMBackend -eq '3dnow') { '3DTEST' } elseif ($NFLLMBackend -eq 'mmx') { 'MMXTEST' } elseif ($NFLLMBackend -eq 'sse2') { 'SSE2TEST' } else { '' }
    $commands = @(
        'REM NFLLM ISA INFERENCE TEST BEGIN'
        'DEL C:\NFLLM.DON >NUL'
        "DEL C:\$inferencePrefix.OK >NUL"
        "DEL C:\$inferencePrefix.LOG >NUL"
        'SET PATH=C:\;%PATH%'
        "ECHO START > C:\$inferencePrefix.OK"
        "C:\NFLLM.EXE C:\TINY1M.GGF -p `"Tom and Jerry are `" -n 1 --isa $NFLLMISAProfile$backendArgument > C:\$inferencePrefix.LOG"
        "IF ERRORLEVEL 1 ECHO FAIL > C:\$inferencePrefix.OK"
        "IF NOT ERRORLEVEL 1 ECHO PASS > C:\$inferencePrefix.OK"
        'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON'
        'REM NFLLM ISA INFERENCE TEST END'
    )
    if ($NFLLMBackend -eq 'sse2') {
        $pathIndex = [Array]::IndexOf($commands, 'SET PATH=C:\;%PATH%')
        if ($pathIndex -lt 0) { throw 'Could not locate PATH setup for the OSFXSR helper.' }
        $osfxsrCommands = @(
            'DEL C:\OSFXSR.OK >NUL'
            'DEL C:\OSFXSR.LOG >NUL'
            'ECHO START > C:\OSFXSR.OK'
            'C:\OSFXSR.COM > C:\OSFXSR.LOG'
            'IF ERRORLEVEL 1 ECHO FAIL > C:\OSFXSR.OK'
            'IF NOT ERRORLEVEL 1 ECHO PASS > C:\OSFXSR.OK'
        )
        $commands = @($commands[0..$pathIndex] + $osfxsrCommands + $commands[($pathIndex + 1)..($commands.Length - 1)])
    }
    if ($isaTestExe) {
        $testInsertIndex = if ($NFLLMBackend -eq 'sse2') {
            [Array]::IndexOf($commands, 'IF NOT ERRORLEVEL 1 ECHO PASS > C:\OSFXSR.OK')
        } else { 1 }
        if ($testInsertIndex -lt 1) { throw 'Could not locate the safe ISA self-test insertion point.' }
        $commands = @($commands[0..$testInsertIndex] + @(
            "DEL C:\$isaTestPrefix.OK >NUL"
            "DEL C:\$isaTestPrefix.LOG >NUL"
            "ECHO START > C:\$isaTestPrefix.OK"
            "C:\$isaTestExe > C:\$isaTestPrefix.LOG"
            "IF ERRORLEVEL 1 ECHO FAIL > C:\$isaTestPrefix.OK"
            "IF NOT ERRORLEVEL 1 ECHO PASS > C:\$isaTestPrefix.OK"
        ) + $commands[($testInsertIndex + 1)..($commands.Length - 1)])
    }
    if ($NFLLMBackend -in @('mmx','sse2','3dnow')) {
        $compareIndex = [Array]::IndexOf($commands, "ECHO START > C:\$inferencePrefix.OK")
        if ($compareIndex -lt 0) { throw "Could not locate the $NFLLMBackend inference command for its reference comparison." }
        $referencePrefix = if ($NFLLMBackend -eq 'sse2') { 'REFSSE' } elseif ($NFLLMBackend -eq '3dnow') { 'REF3D' } else { 'REFMMX' }
        $referenceCommands = @(
            "DEL C:\$referencePrefix.OK >NUL"
            "DEL C:\$referencePrefix.LOG >NUL"
            "ECHO START > C:\$referencePrefix.OK"
            "C:\NFLLM.EXE C:\TINY1M.GGF -p `"Tom and Jerry are `" -n 1 --isa $NFLLMISAProfile --backend reference > C:\$referencePrefix.LOG"
            "IF ERRORLEVEL 1 ECHO FAIL > C:\$referencePrefix.OK"
            "IF NOT ERRORLEVEL 1 ECHO PASS > C:\$referencePrefix.OK"
        )
        $commands = @($commands[0..($compareIndex - 1)] + $referenceCommands + $commands[$compareIndex..($commands.Length - 1)])

        $completionIndex = [Array]::IndexOf($commands, 'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
        if ($completionIndex -lt 0) { throw 'Could not locate the ISA inference completion marker for benchmarks.' }
        if ($BenchmarkRepetitions -gt 0) {
            $selectedPrefix = if ($NFLLMBackend -eq 'sse2') { 'E' } elseif ($NFLLMBackend -eq '3dnow') { 'D' } else { 'M' }
            $f32BenchmarkCommands = [Collections.Generic.List[string]]::new()
            foreach ($sample in 0..($BenchmarkRepetitions - 1)) {
                $suffix = [char]([int][char]'A' + $sample)
                foreach ($prefix in @("$selectedPrefix`F",'RF')) {
                    $tag = "$prefix$suffix"
                    $f32BenchmarkCommands.Add("DEL C:\$tag.OK >NUL")
                    $f32BenchmarkCommands.Add("DEL C:\$tag.LOG >NUL")
                }
            }
            for ($sample = 0; $sample -lt $BenchmarkRepetitions; $sample++) {
                $suffix = [char]([int][char]'A' + $sample)
                $routes = @(
                    [pscustomobject]@{prefix="$selectedPrefix`F";backend=$NFLLMBackend}
                    [pscustomobject]@{prefix='RF';backend='reference'}
                )
                if (($sample % 2) -eq 1) { [array]::Reverse($routes) }
                foreach ($route in $routes) {
                    $tag = "$($route.prefix)$suffix"
                    $f32BenchmarkCommands.Add("ECHO START > C:\$tag.OK")
                    $f32BenchmarkCommands.Add("C:\NFLLM.EXE C:\TINY1M.GGF -p `"Tom and Jerry are `" -n 4 --isa $NFLLMISAProfile -b $($route.backend) > C:\$tag.LOG")
                    $f32BenchmarkCommands.Add("IF ERRORLEVEL 1 ECHO FAIL > C:\$tag.OK")
                    $f32BenchmarkCommands.Add("IF NOT ERRORLEVEL 1 ECHO PASS > C:\$tag.OK")
                }
            }
            $commands = @($commands[0..($completionIndex - 1)] + $f32BenchmarkCommands.ToArray() + $commands[$completionIndex..($commands.Length - 1)])
        }
        if ($NFLLMBackend -in @('mmx','sse2')) {
            $completionIndex = [Array]::IndexOf($commands, 'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
            if ($completionIndex -lt 0) { throw 'Could not locate the ISA inference completion marker for Q8/Q4 comparisons.' }
        $quantizedCommands = [Collections.Generic.List[string]]::new()
        $referenceTag = if ($NFLLMBackend -eq 'sse2') { 'RSSE2' } else { 'RMMX' }
        foreach ($format in @(
            [pscustomobject]@{name='Q8';model='TINYQ8.GGF'}
            [pscustomobject]@{name='Q4';model='TINYQ4.GGF'}
        )) {
            foreach ($variant in @(
                [pscustomobject]@{tag=$quantPrefix;backend=$NFLLMBackend}
                [pscustomobject]@{tag=$referenceTag;backend='reference'}
            )) {
                $tag = "$($variant.tag)$($format.name)"
                $quantizedCommands.Add("DEL C:\$tag.OK >NUL")
                $quantizedCommands.Add("DEL C:\$tag.LOG >NUL")
                $quantizedCommands.Add("ECHO START > C:\$tag.OK")
                $quantizedCommands.Add("C:\NFLLM.EXE C:\$($format.model) -p `"Tom and Jerry are `" -n 1 --isa $NFLLMISAProfile --backend $($variant.backend) > C:\$tag.LOG")
                $quantizedCommands.Add("IF ERRORLEVEL 1 ECHO FAIL > C:\$tag.OK")
                $quantizedCommands.Add("IF NOT ERRORLEVEL 1 ECHO PASS > C:\$tag.OK")
            }
        }
        if ($BenchmarkRepetitions -gt 0) {
            $selectedPrefix = if ($NFLLMBackend -eq 'sse2') { 'E' } else { 'M' }
            $scalarPrefix = "S$selectedPrefix"
            foreach ($sample in 0..($BenchmarkRepetitions - 1)) {
                $suffix = [char]([int][char]'A' + $sample)
                foreach ($format in @(
                    [pscustomobject]@{name='Q8';model='TINYQ8.GGF'}
                    [pscustomobject]@{name='Q4';model='TINYQ4.GGF'}
                )) {
                    foreach ($prefix in @($selectedPrefix,$scalarPrefix)) {
                        $tag = "$prefix$($format.name)$suffix"
                        $quantizedCommands.Add("DEL C:\$tag.OK >NUL")
                        $quantizedCommands.Add("DEL C:\$tag.LOG >NUL")
                    }
                }
            }
            foreach ($sample in 0..($BenchmarkRepetitions - 1)) {
                $suffix = [char]([int][char]'A' + $sample)
                foreach ($format in @(
                    [pscustomobject]@{name='Q8';model='TINYQ8.GGF'}
                    [pscustomobject]@{name='Q4';model='TINYQ4.GGF'}
                )) {
                    $routes = @(
                        [pscustomobject]@{prefix=$selectedPrefix;mode='direct'}
                        [pscustomobject]@{prefix=$scalarPrefix;mode='scalar'}
                    )
                    if (($sample % 2) -eq 1) { [array]::Reverse($routes) }
                    foreach ($route in $routes) {
                        $tag = "$($route.prefix)$($format.name)$suffix"
                        $quantizedCommands.Add("ECHO START > C:\$tag.OK")
                        $quantizedCommands.Add("C:\NFLLM.EXE C:\$($format.model) -p `"Tom and Jerry are `" -n 4 --isa $NFLLMISAProfile -b $NFLLMBackend --quantized-matvec $($route.mode) > C:\$tag.LOG")
                        $quantizedCommands.Add("IF ERRORLEVEL 1 ECHO FAIL > C:\$tag.OK")
                        $quantizedCommands.Add("IF NOT ERRORLEVEL 1 ECHO PASS > C:\$tag.OK")
                    }
                }
            }
        }
        $commands = @($commands[0..($completionIndex - 1)] + $quantizedCommands.ToArray() + $commands[$completionIndex..($commands.Length - 1)])
        }
    }
    if ($SerialCompletionMarker) {
        $insertIndex = [Array]::IndexOf($commands, 'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
        if ($insertIndex -lt 0) { throw 'Could not locate the NFLLM ISA completion marker for serial output.' }
        $commands = @($commands[0..$insertIndex] + 'ECHO NFLLM_TEST_FINISHED > COM1' + $commands[($insertIndex + 1)..($commands.Length - 1)])
    }
}
if ($ISALabOnly) {
    if ($ISALabProfile -eq 'none') { throw 'ISALabOnly requires an explicit ISA lab profile.' }
    if ($BenchmarkRepetitions -gt 0) { throw 'ISALabOnly cannot be combined with benchmark repetitions.' }
    $commands = @(
        'REM NFLLM ISOLATED ISA PROBE BEGIN'
        'DEL C:\NFLLM.DON >NUL'
        'DEL C:\ISA.OK >NUL'
        'DEL C:\ISA.LOG >NUL'
        'SET PATH=C:\;%PATH%'
        'ECHO ISA_START > C:\ISA.OK'
        "C:\ISAPROBE.EXE $ISALabProfile > C:\ISA.LOG"
        'IF ERRORLEVEL 1 ECHO ISA_FAIL > C:\ISA.OK'
        'IF NOT ERRORLEVEL 1 ECHO ISA_PASS > C:\ISA.OK'
        'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON'
        'REM NFLLM ISOLATED ISA PROBE END'
    )
    if ($SerialCompletionMarker) {
        $insertIndex = [Array]::IndexOf($commands, 'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON')
        $commands = @($commands[0..$insertIndex] + 'ECHO NFLLM_TEST_FINISHED > COM1' + $commands[($insertIndex + 1)..($commands.Length - 1)])
    }
}
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
$registered = $false
$disk = $null
$exitCode = 1

try {
    if (-not (Test-Path -LiteralPath $diskPath -PathType Leaf) -or -not (Test-Path -LiteralPath $originalDisk -PathType Leaf)) {
        throw 'Original or isolated test disk is missing.'
    }
    if ([IO.Path]::GetFullPath($diskPath) -eq [IO.Path]::GetFullPath($originalDisk)) {
        throw 'Refusing to modify the original NF-DOS system disk.'
    }
    $originalHash = (Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
    if ($originalHash -ne '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3') {
        throw 'Original NF-DOS system disk hash differs from the preserved baseline.'
    }
    if (@(Get-CimInstance Win32_Process | Where-Object ExecutablePath -eq $vmExe).Count -gt 0) {
        throw '86Box is active; refusing to edit the guest boot script while mounted.'
    }
    $active = @($registry.entries | Where-Object state -eq 'active').Count + [int]$registry.known_external_active_managed_count
    if ($active -ge [int]$registry.max_active_managed_processes) { throw 'Managed process capacity reached.' }
    $current = [Diagnostics.Process]::GetCurrentProcess()
    $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.Id)"
    $entry = [pscustomobject]@{
        task_id=$taskId;vm_name='NFLLM guest acceptance boot-script update';state='active';pid=$current.Id
        ppid=$cim.ParentProcessId;creation_time=$current.StartTime.ToString('o');executable=$cim.ExecutablePath
        command_line=$cim.CommandLine;classification='temporary';scope='Update AUTOEXEC only on isolated clone for NFLLM regression, Q8/Q4 and optional repeated comparable benchmarks'
        registered_at=(Get-Date).ToString('o');shutdown_policy='write and read back the cloned AUTOEXEC, then reconcile';source_config=$diskPath
        exit_code=$null;stopped_at=$null;stop_reason=$null
    }
    $registry.entries = @($registry.entries) + $entry
    [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    $registered = $true

    $disk = [IO.File]::Open($diskPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $disk.Position = 0
    $mbr = Read-Exact $disk 512
    $partition = $null
    for ($offset = 446; $offset -lt 510; $offset += 16) {
        if ($mbr[$offset + 4] -eq 6) {
            $partition = [pscustomobject]@{Start=[BitConverter]::ToUInt32($mbr,$offset+8);Sectors=[BitConverter]::ToUInt32($mbr,$offset+12)}
            break
        }
    }
    if (-not $partition -or $mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'FAT16 primary partition or MBR signature is missing.' }
    $volumeStart = [int64]$partition.Start * 512
    $disk.Position = $volumeStart
    $bpb = Read-Exact $disk 512
    $bps = [BitConverter]::ToUInt16($bpb, 11)
    $spc = [int]$bpb[13]
    $reserved = [BitConverter]::ToUInt16($bpb, 14)
    $fatCount = [int]$bpb[16]
    $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
    $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
    if ($bps -ne 512 -or $spc -le 0 -or $fatCount -lt 1) { throw 'Unsupported FAT16 BPB in the test clone.' }
    $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bps)
    $fatStart = $volumeStart + ([int64]$reserved * $bps)
    $rootStart = $fatStart + ([int64]$fatCount * $fatSectors * $bps)
    $dataStart = $rootStart + ([int64]$rootSectors * $bps)
    $fatBytes = $fatSectors * $bps
    $clusterBytes = $spc * $bps
    $disk.Position = $fatStart
    $fat = Read-Exact $disk $fatBytes
    for ($copy = 1; $copy -lt $fatCount; $copy++) {
        $disk.Position = $fatStart + ([int64]$copy * $fatBytes)
        $mirror = Read-Exact $disk $fatBytes
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$fat, [byte[]]$mirror)) { throw 'FAT mirrors differ.' }
    }
    $root = [byte[]]::new($rootEntries * 32)
    $disk.Position = $rootStart
    $root = Read-Exact $disk $root.Length
    $autoName = [Text.Encoding]::ASCII.GetBytes('AUTOEXECBAT')
    $autoOffset = Get-EntryOffset $root $autoName
    if ($autoOffset -lt 0) { throw 'AUTOEXEC.BAT is missing.' }
    $oldSize = [BitConverter]::ToUInt32($root, $autoOffset + 28)
    $autoFirst = [BitConverter]::ToUInt16($root, $autoOffset + 26)
    $maxClusters = [int]($fatBytes / 2)
    $chain = Get-Chain $fat $autoFirst $maxClusters
    if ($oldSize -gt ([int64]$chain.Length * $clusterBytes)) { throw 'AUTOEXEC.BAT exceeds its FAT chain.' }
    $old = [byte[]]::new($oldSize)
    $position = 0
    foreach ($cluster in $chain) {
        if ($position -ge $old.Length) { break }
        $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
        $block = Read-Exact $disk $clusterBytes
        $count = [Math]::Min($clusterBytes, $old.Length - $position)
        [Buffer]::BlockCopy($block, 0, $old, $position, $count)
        $position += $count
    }
    $text = [Text.Encoding]::ASCII.GetString($old)
    $testBlockStart = -1
    foreach ($markerText in @(
        'REM NFLLM AM486 ACCEPTANCE BEGIN'
        'REM NFLLM PACKED MATVEC SELFTEST BEGIN'
        'REM NFLLM ISOLATED ISA PROBE BEGIN'
        'REM NFLLM ISA INFERENCE TEST BEGIN'
        'REM NFLLM DIRECT QUANTIZED MATVEC BENCHMARK BEGIN'
        'REM NFLLM PACKED QUANTIZED MATVEC BENCHMARK BEGIN'
    )) {
        $markerIndex = $text.IndexOf($markerText, [StringComparison]::OrdinalIgnoreCase)
        if ($markerIndex -ge 0 -and ($testBlockStart -lt 0 -or $markerIndex -lt $testBlockStart)) {
            $testBlockStart = $markerIndex
        }
    }
    if ($testBlockStart -lt 0) {
        $testBlockStart = $text.IndexOf('SET PATH=A:\;%PATH%', [StringComparison]::OrdinalIgnoreCase)
    }
    if ($testBlockStart -ge 0) {
        $text = $text.Substring(0, $testBlockStart).TrimEnd([char]13, [char]10)
    }
    $newText = $text.TrimEnd([char]13, [char]10) + "`r`n`r`n" + ($commands -join "`r`n") + "`r`n"
    $newBytes = [Text.Encoding]::ASCII.GetBytes($newText)
    if ($newBytes.Length -gt ([int64]$chain.Length * $clusterBytes)) { throw 'AUTOEXEC.BAT has insufficient allocated space.' }
    $padded = [byte[]]::new($chain.Length * $clusterBytes)
    [Buffer]::BlockCopy($newBytes, 0, $padded, 0, $newBytes.Length)
    $position = 0
    foreach ($cluster in $chain) {
        $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
        $disk.Write($padded, $position, $clusterBytes)
        $position += $clusterBytes
    }
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$newBytes.Length), 0, $root, $autoOffset + 28, 4)
    $disk.Position = $rootStart
    $disk.Write($root, 0, $root.Length)
    $disk.Flush($true)
    $disk.Position = $rootStart
    $rootCheck = Read-Exact $disk $root.Length
    if ([BitConverter]::ToUInt32($rootCheck, $autoOffset + 28) -ne $newBytes.Length) { throw 'AUTOEXEC directory size failed read-back.' }
    $disk.Dispose()
    $disk = $null
    $result = [pscustomobject]@{disk=$diskPath;original_disk_sha256=$originalHash;autoexec_old_bytes=$oldSize;autoexec_new_bytes=$newBytes.Length;cluster_bytes=$clusterBytes;commands=$commands;test_disk_sha256=(Get-FileHash -LiteralPath $diskPath -Algorithm SHA256).Hash}
    [IO.File]::WriteAllText($evidencePath, ($result | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    $exitCode = 0
    $result
}
catch {
    if ($registered -and $null -ne $entry) { Set-Field $entry 'stop_reason' $_.Exception.Message }
    throw
}
finally {
    if ($null -ne $disk) { $disk.Dispose() }
    if ($registered) {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $entry = $registry.entries | Where-Object task_id -eq $taskId
        Set-Field $entry 'state' 'exited'
        Set-Field $entry 'exit_code' $exitCode
        Set-Field $entry 'stopped_at' (Get-Date).ToString('o')
        if ($exitCode -ne 0 -and [string]::IsNullOrEmpty($entry.stop_reason)) { Set-Field $entry 'stop_reason' 'AUTOEXEC update did not complete; see PowerShell error output.' }
        [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    }
}
