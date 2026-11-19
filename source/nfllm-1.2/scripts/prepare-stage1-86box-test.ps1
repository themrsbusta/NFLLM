param(
    [string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$RunId = (Get-Date).ToString('yyyyMMdd-HHmmss')
)

$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$sourceDisk = Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test.raw'
$originalDisk = Join-Path $emulatorRoot 'disks\nfdos502-qemu.raw'
$vmExe = Join-Path $emulatorRoot 'app\86Box.exe'
$sourceMachineDir = Join-Path $emulatorRoot 'machine\AM486DX4-96MB-NFLLM-TEST'
$sourceNvr = Join-Path $sourceMachineDir 'nvr\486sp3c.nvr'
$runName = "AM486DX4-96MB-NFLLM-STAGE1-$RunId"
$testDisk = Join-Path $emulatorRoot "disks\nfdos502-nfllm-stage1-$RunId.raw"
$machineDir = Join-Path $emulatorRoot "machine\$runName"
$configPath = Join-Path $machineDir '86box.cfg'
$appImage = Join-Path $workspace "NFLLM\build\NFLLM-S1-$RunId.IMG"
$appDiskDir = Join-Path $workspace 'NFLLM\build\APPDISK'
$evidencePath = Join-Path $workspace "NFLLM\build\logs\stage1-86box-preparation-$RunId.json"

foreach ($path in @($sourceDisk, $originalDisk, $vmExe,
                    (Join-Path $sourceMachineDir '86box.cfg'),
                    $sourceNvr,
                    (Join-Path $workspace 'NFLLM\build\NFLLM.exe'),
                    (Join-Path $workspace 'NFLLM\build\NFTEST.exe'),
                    (Join-Path $workspace 'NFLLM\build\compat_probe.exe'),
                    (Join-Path $workspace 'NFLLM\build\dos4gw.exe'),
                    (Join-Path $appDiskDir 'TRUNC.GGF'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file is missing: $path" }
}
if (@(Get-CimInstance Win32_Process | Where-Object ExecutablePath -eq $vmExe).Count -gt 0) {
    throw 'An 86Box instance is active; refusing to clone or change test configuration while a disk may be mounted.'
}

$originalHash = (Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
if ($originalHash -ne '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3') {
    throw 'The preserved NF-DOS system disk differs from its recorded baseline.'
}
$sourceHash = (Get-FileHash -LiteralPath $sourceDisk -Algorithm SHA256).Hash
if (Test-Path -LiteralPath $testDisk) {
    if ((Get-FileHash -LiteralPath $testDisk -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "An existing stage-1 disk differs from the current isolated source; refusing to overwrite it: $testDisk"
    }
}
if (Test-Path -LiteralPath $machineDir) {
    $existingMachineFiles = @(Get-ChildItem -LiteralPath $machineDir -Force)
    if ($existingMachineFiles.Count -gt 0 -and
        -not ($existingMachineFiles.Count -eq 1 -and $existingMachineFiles[0].Name -eq '86box.cfg')) {
        throw "The stage-1 machine directory contains unexpected files: $machineDir"
    }
}

# Build a dedicated FAT12 application disk from the freshly built binaries.
Copy-Item -LiteralPath (Join-Path $workspace 'NFLLM\build\NFLLM.exe') -Destination (Join-Path $appDiskDir 'NFLLM.EXE') -Force
Copy-Item -LiteralPath (Join-Path $workspace 'NFLLM\build\NFTEST.exe') -Destination (Join-Path $appDiskDir 'NFTEST.EXE') -Force
Copy-Item -LiteralPath (Join-Path $workspace 'NFLLM\build\compat_probe.exe') -Destination (Join-Path $appDiskDir 'COMPAT.EXE') -Force
Copy-Item -LiteralPath (Join-Path $workspace 'NFLLM\build\dos4gw.exe') -Destination (Join-Path $appDiskDir 'DOS4GW.EXE') -Force
$appFiles = @(
    (Join-Path $appDiskDir 'COMPAT.EXE'),
    (Join-Path $appDiskDir 'DOS4GW.EXE'),
    (Join-Path $appDiskDir 'NFLLM.EXE'),
    (Join-Path $appDiskDir 'NFTEST.EXE'),
    (Join-Path $appDiskDir 'TRUNC.GGF')
)
& (Join-Path $workspace 'NFLLM\scripts\make-floppy.ps1') -OutputPath $appImage -Files $appFiles | Out-Null
if ((Get-Item -LiteralPath $appImage).Length -ne 1474560) { throw 'The stage-1 FAT12 application disk has an invalid size.' }

# Run only on a new copy of the already isolated NF-DOS test clone. A failed
# preparation can resume only when its existing target still hashes to source.
if (-not (Test-Path -LiteralPath $testDisk)) { Copy-Item -LiteralPath $sourceDisk -Destination $testDisk }
$cloneHash = (Get-FileHash -LiteralPath $testDisk -Algorithm SHA256).Hash
if ($cloneHash -ne $sourceHash) { throw 'The stage-1 hard-disk clone failed its source-hash check.' }

if (-not (Test-Path -LiteralPath $machineDir)) {
    New-Item -ItemType Directory -Path $machineDir | Out-Null
}
$machineNvrDir = Join-Path $machineDir 'nvr'
$machineNvr = Join-Path $machineNvrDir '486sp3c.nvr'
if (-not (Test-Path -LiteralPath $machineNvrDir)) {
    New-Item -ItemType Directory -Path $machineNvrDir | Out-Null
}
$sourceNvrHash = (Get-FileHash -LiteralPath $sourceNvr -Algorithm SHA256).Hash
if (Test-Path -LiteralPath $machineNvr) {
    if ((Get-FileHash -LiteralPath $machineNvr -Algorithm SHA256).Hash -ne $sourceNvrHash) {
        throw "The test-machine CMOS image already exists with different settings: $machineNvr"
    }
} else {
    Copy-Item -LiteralPath $sourceNvr -Destination $machineNvr
}
$config = Get-Content -LiteralPath (Join-Path $sourceMachineDir '86box.cfg') -Raw
$config = [regex]::Replace($config, '(?m)^hdd_01_fn\s*=.*$', "hdd_01_fn = ../../disks/nfdos502-nfllm-stage1-$RunId.raw")
$floppyBlock = @'
[Floppy and CD-ROM drives]
fdd_01_type = 35_2hd
fdd_01_fn = ../../../../NFLLM/build/NFLLM-S1-$RunId.IMG
'@
$floppyBlock = $floppyBlock.Replace('$RunId', $RunId)
$config = [regex]::Replace($config, '(?ms)^\[Floppy and CD-ROM drives\].*?(?=^\[|\z)', $floppyBlock + "`r`n")
$hddLine = ($config -split "`r?`n" | Where-Object { $_ -match '^\s*hdd_01_fn\s*=' } | Select-Object -First 1).Trim()
$fddLine = ($config -split "`r?`n" | Where-Object { $_ -match '^\s*fdd_01_fn\s*=' } | Select-Object -First 1).Trim()
if ($config -notmatch '(?m)^cpu_family\s*=\s*am486dx4\s*$' -or
    $config -notmatch '(?m)^cpu_speed\s*=\s*100000000\s*$' -or
    $config -notmatch '(?m)^mem_size\s*=\s*98304\s*$' -or
    $hddLine -ne "hdd_01_fn = ../../disks/nfdos502-nfllm-stage1-$RunId.raw" -or
    $fddLine -ne "fdd_01_fn = ../../../../NFLLM/build/NFLLM-S1-$RunId.IMG") {
    throw 'The cloned 86Box config did not retain the Am486DX4/96 MiB profile and isolated media paths.'
}
[IO.File]::WriteAllText($configPath, $config, [Text.UTF8Encoding]::new($false))

foreach ($relative in @("../../disks/nfdos502-nfllm-stage1-$RunId.raw", "../../../../NFLLM/build/NFLLM-S1-$RunId.IMG")) {
    $resolved = [IO.Path]::GetFullPath((Join-Path $machineDir $relative))
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "86Box config path does not resolve: $relative -> $resolved" }
}
$result = [pscustomobject]@{
    run_id = $RunId
    disk = $testDisk
    source_test_disk = $sourceDisk
    source_test_disk_sha256 = $sourceHash
    stage1_disk_sha256_before_boot = $cloneHash
    original_system_disk_sha256 = $originalHash
    application_floppy = $appImage
    application_floppy_sha256 = (Get-FileHash -LiteralPath $appImage -Algorithm SHA256).Hash
    application_files = @($appFiles | ForEach-Object { [pscustomobject]@{ name = [IO.Path]::GetFileName($_); bytes = (Get-Item -LiteralPath $_).Length; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } })
    machine_directory = $machineDir
    config = $configPath
    cmos_source = $sourceNvr
    cmos_sha256 = $sourceNvrHash
    cpu = 'AMD Am486DX4 at 100 MHz'
    ram_mib = 96
}
[IO.File]::WriteAllText($evidencePath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$result
