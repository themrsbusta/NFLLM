param(
    [string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

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
$diskPath = Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test.raw'
$originalDisk = Join-Path $emulatorRoot 'disks\nfdos502-qemu.raw'
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
$evidencePath = Join-Path $workspace 'NFLLM\build\logs\acceptance-autoexec.json'
$vmExe = Join-Path $emulatorRoot 'app\86Box.exe'
$taskId = "nfllm-update-acceptance-autoexec-$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
$commands = @(
    'REM NFLLM AM486 ACCEPTANCE BEGIN'
    'SET PATH=A:\;%PATH%'
    'A:\COMPAT.EXE C:\STORIES.GGF > C:\COMPAT.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\COMPAT.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\COMPAT.OK'
    'A:\NFLLM.EXE C:\STORIES.GGF -p "Once upon a time" -n 1 > C:\NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\INFER.OK'
    'A:\COMPAT.EXE C:\TINY1M.GGF > C:\T1COMPAT.LOG'
    'IF ERRORLEVEL 1 ECHO COMPAT_FAIL > C:\T1COMPAT.OK'
    'IF NOT ERRORLEVEL 1 ECHO COMPAT_PASS > C:\T1COMPAT.OK'
    'A:\NFLLM.EXE C:\TINY1M.GGF -p "Tom and Jerry are " -n 1 > C:\T1NFLLM.LOG'
    'IF ERRORLEVEL 1 ECHO INFER_FAIL > C:\T1INFER.OK'
    'IF NOT ERRORLEVEL 1 ECHO INFER_PASS > C:\T1INFER.OK'
    'A:\NFLLM.EXE C:\NOFILE.GGF -n 1 > C:\MISSING.LOG'
    'IF ERRORLEVEL 2 ECHO EXPECTED_ERROR2 > C:\MISS.OK'
    'A:\NFLLM.EXE A:\TRUNC.GGF -n 1 > C:\TRUNC.LOG'
    'IF ERRORLEVEL 2 ECHO EXPECTED_ERROR2 > C:\TRUNC.OK'
    'ECHO NFLLM_TEST_FINISHED > C:\NFLLM.DON'
    'REM NFLLM AM486 ACCEPTANCE END'
)
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
$registered = $false
$disk = $null
$exitCode = 1

try {
    if (-not (Test-Path -LiteralPath $diskPath -PathType Leaf) -or -not (Test-Path -LiteralPath $originalDisk -PathType Leaf)) {
        throw 'Original or isolated test disk is missing.'
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
        command_line=$cim.CommandLine;classification='temporary';scope='Update AUTOEXEC only on isolated clone for prompt, success and input-error checks'
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
    $testBlockStart = $text.IndexOf('REM NFLLM AM486 ACCEPTANCE BEGIN', [StringComparison]::OrdinalIgnoreCase)
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
