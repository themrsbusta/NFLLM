param(
    [string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$RunId = '20261114-cboot',
    [string]$AppImagePath = '',
    [string]$F32ModelPath = '',
    [string]$Q8ModelPath = '',
    [string]$Q4ModelPath = '',
    [string]$Q2_0ModelPath = '',
    [string]$Q2_KModelPath = '',
    [string]$IQ3_XXSModelPath = '',
    [string]$IQ3_SModelPath = '',
    [string]$ISAProbePath = '',
    [string]$ExtraFilePath = '',
    [string]$ExtraDosName = ''
)

$ErrorActionPreference = 'Stop'

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -le 0) { throw "Unexpected end of image after $offset of $Count bytes." }
        $offset += $read
    }
    return ,$bytes
}

function Copy-SparseImage([string]$SourcePath, [string]$DestinationPath) {
    if (-not ('NFLLM.SparseRawCopy' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.IO;
namespace NFLLM {
    public static class SparseRawCopy {
        public static long CopyNonZeroPages(FileStream source, FileStream destination) {
            const int pageBytes = 4096;
            byte[] buffer = new byte[1048576];
            long offset = 0;
            long written = 0;
            int read;
            while ((read = source.Read(buffer, 0, buffer.Length)) > 0) {
                int runStart = -1;
                int position;
                for (position = 0; position < read; position += pageBytes) {
                    int count = Math.Min(pageBytes, read - position);
                    bool nonZero = false;
                    int i;
                    for (i = position; i < position + count; ++i) {
                        if (buffer[i] != 0) { nonZero = true; break; }
                    }
                    if (nonZero) {
                        if (runStart < 0) runStart = position;
                    } else if (runStart >= 0) {
                        int length = position - runStart;
                        destination.Position = offset + runStart;
                        destination.Write(buffer, runStart, length);
                        written += length;
                        runStart = -1;
                    }
                }
                if (runStart >= 0) {
                    int length = read - runStart;
                    destination.Position = offset + runStart;
                    destination.Write(buffer, runStart, length);
                    written += length;
                }
                offset += read;
            }
            return written;
        }
    }
}
'@
    }
    $sourceStream = $null
    $destinationStream = $null
    $createdDestination = $false
    try {
        $sourceStream = [IO.File]::OpenRead($SourcePath)
        $destinationStream = [IO.File]::Open($DestinationPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $createdDestination = $true
        $destinationStream.Dispose()
        $destinationStream = $null
        & (Join-Path $env:SystemRoot 'System32\fsutil.exe') sparse setflag $DestinationPath | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not mark the cloned raw image sparse (fsutil exit $LASTEXITCODE)." }
        $destinationStream = [IO.File]::Open($DestinationPath, [IO.FileMode]::Open,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $destinationStream.SetLength($sourceStream.Length)
        $writtenBytes = [NFLLM.SparseRawCopy]::CopyNonZeroPages($sourceStream, $destinationStream)
        $destinationStream.Flush($true)
        if ($destinationStream.Length -ne $sourceStream.Length) { throw 'Sparse raw-image copy ended at an unexpected length.' }
        Write-Output "Sparse clone: $writtenBytes non-zero bytes written, length=$($sourceStream.Length)."
    } catch {
        if ($null -ne $destinationStream) { $destinationStream.Dispose(); $destinationStream = $null }
        if ($createdDestination -and (Test-Path -LiteralPath $DestinationPath)) {
            Remove-Item -LiteralPath $DestinationPath
        }
        throw
    } finally {
        if ($null -ne $sourceStream) { $sourceStream.Dispose() }
        if ($null -ne $destinationStream) { $destinationStream.Dispose() }
    }
}

function Get-RootEntry([byte[]]$Root, [byte[]]$Name) {
    for ($offset = 0; $offset -lt $Root.Length; $offset += 32) {
        if ($Root[$offset] -eq 0) { break }
        if ($Root[$offset] -ne 0xe5 -and
            [Linq.Enumerable]::SequenceEqual([byte[]]$Root[$offset..($offset + 10)], $Name)) {
            return $offset
        }
    }
    return -1
}

function Get-Fat16Chain([byte[]]$Fat, [int]$FirstCluster, [int]$MaxClusters) {
    $chain = [Collections.Generic.List[int]]::new()
    $cluster = $FirstCluster
    while ($cluster -ge 2 -and $cluster -lt 0xfff8 -and $chain.Count -lt $MaxClusters) {
        if ($chain.Contains($cluster)) { throw "FAT16 chain loops at cluster $cluster." }
        $chain.Add($cluster)
        $cluster = [BitConverter]::ToUInt16($Fat, $cluster * 2)
    }
    if ($cluster -lt 0xfff8) { throw "FAT16 chain ended with invalid marker $cluster." }
    return ,$chain.ToArray()
}

function Read-ImageFile([string]$ImagePath, [string]$DosName) {
    $stream = [IO.File]::OpenRead($ImagePath)
    try {
        $boot = Read-Exact $stream 512
        $bps = [BitConverter]::ToUInt16($boot, 11)
        $spc = [int]$boot[13]
        $reserved = [BitConverter]::ToUInt16($boot, 14)
        $fatCount = [int]$boot[16]
        $rootEntries = [BitConverter]::ToUInt16($boot, 17)
        $fatSectors = [BitConverter]::ToUInt16($boot, 22)
        if ($bps -ne 512 -or $spc -lt 1 -or $fatCount -lt 1 -or $fatSectors -lt 1) {
            throw "Unsupported source FAT image: $ImagePath."
        }
        $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bps)
        $fatStart = [int64]$reserved * $bps
        $rootStart = $fatStart + ([int64]$fatCount * $fatSectors * $bps)
        $dataStart = $rootStart + ([int64]$rootSectors * $bps)
        $stream.Position = $rootStart
        $root = Read-Exact $stream ($rootEntries * 32)
        $parts = $DosName.ToUpperInvariant().Split('.')
        $shortName = [Text.Encoding]::ASCII.GetBytes($parts[0].PadRight(8) + $parts[1].PadRight(3))
        $entry = Get-RootEntry $root $shortName
        if ($entry -lt 0) { throw "$DosName is missing from $ImagePath." }
        $size = [int][BitConverter]::ToUInt32($root, $entry + 28)
        $cluster = [int][BitConverter]::ToUInt16($root, $entry + 26)
        $stream.Position = $fatStart
        $fat = Read-Exact $stream ($fatSectors * $bps)
        $clusterBytes = $spc * $bps
        $remaining = $size
        $seen = @{}
        $content = [IO.MemoryStream]::new()
        while ($remaining -gt 0) {
            if ($cluster -lt 2 -or $cluster -ge 0xff8 -or $seen.ContainsKey($cluster)) {
                throw "$DosName has an invalid FAT12 cluster chain."
            }
            $seen[$cluster] = $true
            $stream.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
            $block = Read-Exact $stream $clusterBytes
            $count = [Math]::Min($remaining, $block.Length)
            $content.Write($block, 0, $count)
            $remaining -= $count
            $fatOffset = [int][Math]::Floor($cluster * 1.5)
            if (($cluster % 2) -eq 0) {
                $cluster = [int]$fat[$fatOffset] -bor (([int]$fat[$fatOffset + 1] -band 0x0f) -shl 8)
            } else {
                $cluster = ((([int]$fat[$fatOffset] -shr 4) -bor (([int]$fat[$fatOffset + 1]) -shl 4)) -band 0xfff)
            }
        }
        return ,$content.ToArray()
    } finally {
        $stream.Dispose()
    }
}

function Get-ContentFromChain([IO.Stream]$Stream, [int64]$DataStart, [int]$ClusterBytes, [int[]]$Chain, [int]$Length) {
    if ($Length -gt ([int64]$Chain.Length * $ClusterBytes)) { throw 'File length exceeds its FAT16 chain.' }
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

function Set-Field($Object, [string]$Name, $Value) {
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        Add-Member -InputObject $Object -MemberType NoteProperty -Name $Name -Value $Value
    } else {
        $Object.$Name = $Value
    }
}

$workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
$emulatorRoot = Join-Path $workspace 'emulators\AM486DX4-96MB'
$originalDisk = Join-Path $emulatorRoot 'disks\nfdos502-qemu.raw'
$sourceDisk = Join-Path $emulatorRoot 'disks\nfdos502-nfllm-test.raw'
$destinationDisk = Join-Path $emulatorRoot "disks\nfdos502-nfllm-stage1-cboot-$RunId.raw"
$floppy = if ([string]::IsNullOrWhiteSpace($AppImagePath)) {
    Join-Path $workspace 'NFLLM\build\NFLLM-S1-20261114-033630.IMG'
} else {
    [IO.Path]::GetFullPath($AppImagePath)
}
$expectedOriginalHash = '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3'
$expectedSourceHash = 'F3589DFEA795E47174A93E576A20B951070179E3640FC40E29F00864386AA2D5'
$build = Join-Path $workspace 'NFLLM\build'
$registryPath = Join-Path $emulatorRoot 'process-registry.json'
$evidencePath = Join-Path $workspace "NFLLM\build\logs\stage1-hdd-apps-$RunId.json"
$vmExe = Join-Path $emulatorRoot 'app\86Box.exe'
$taskId = "nfllm-stage1-apps-to-cboot-disk-$RunId-$((Get-Date).ToString('yyyyMMdd-HHmmss-fff'))"
$files = @(
    [pscustomobject]@{Name='NFLLM.EXE'; Path=(Join-Path $build 'NFLLM.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='NFTEST.EXE'; Path=(Join-Path $build 'NFTEST.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='NF3DNOW.EXE'; Path=(Join-Path $build 'NF3DNOW.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='NFMMX.EXE'; Path=(Join-Path $build 'NFMMX.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='NFSSE2.EXE'; Path=(Join-Path $build 'NFSSE2.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='COMPAT.EXE'; Path=(Join-Path $build 'compat_probe.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='DOS4GW.EXE'; Path=(Join-Path $build 'dos4gw.exe'); Bytes=$null; Sha256=$null}
    [pscustomobject]@{Name='TRUNC.GGF'; Path=$null; Bytes=$null; Sha256=$null}
)
if (-not [string]::IsNullOrWhiteSpace($F32ModelPath)) {
    $files += [pscustomobject]@{Name='TINY1M.GGF'; Path=[IO.Path]::GetFullPath($F32ModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($Q8ModelPath)) {
    $files += [pscustomobject]@{Name='TINYQ8.GGF'; Path=[IO.Path]::GetFullPath($Q8ModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($Q4ModelPath)) {
    $files += [pscustomobject]@{Name='TINYQ4.GGF'; Path=[IO.Path]::GetFullPath($Q4ModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($Q2_0ModelPath)) {
    $files += [pscustomobject]@{Name='TINYQ20.GGF'; Path=[IO.Path]::GetFullPath($Q2_0ModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($Q2_KModelPath)) {
    $files += [pscustomobject]@{Name='TINYQ2K.GGF'; Path=[IO.Path]::GetFullPath($Q2_KModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($IQ3_XXSModelPath)) {
    $files += [pscustomobject]@{Name='TINYI3X.GGF'; Path=[IO.Path]::GetFullPath($IQ3_XXSModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($IQ3_SModelPath)) {
    $files += [pscustomobject]@{Name='TINYI3S.GGF'; Path=[IO.Path]::GetFullPath($IQ3_SModelPath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($ISAProbePath)) {
    $files += [pscustomobject]@{Name='ISAPROBE.EXE'; Path=[IO.Path]::GetFullPath($ISAProbePath); Bytes=$null; Sha256=$null}
}
if (-not [string]::IsNullOrWhiteSpace($ExtraFilePath) -or
    -not [string]::IsNullOrWhiteSpace($ExtraDosName)) {
    if ([string]::IsNullOrWhiteSpace($ExtraFilePath) -or
        [string]::IsNullOrWhiteSpace($ExtraDosName) -or
        $ExtraDosName -notmatch '^[A-Za-z0-9_$!#%&''()@^{}~-]{1,8}\.[A-Za-z0-9_$!#%&''()@^{}~-]{1,3}$') {
        throw 'ExtraFilePath and a valid 8.3 ExtraDosName must be supplied together.'
    }
    $files += [pscustomobject]@{Name=$ExtraDosName.ToUpperInvariant(); Path=[IO.Path]::GetFullPath($ExtraFilePath); Bytes=$null; Sha256=$null}
}
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
$disk = $null
$registered = $false
$exitCode = 1

try {
    if ((Get-FileHash $originalDisk -Algorithm SHA256).Hash -ne $expectedOriginalHash) {
        throw 'Original NF-DOS system disk hash differs from preserved baseline.'
    }
    if ((Get-FileHash $sourceDisk -Algorithm SHA256).Hash -ne $expectedSourceHash) {
        throw 'Base test clone differs from the preserved source hash.'
    }
    if (Test-Path -LiteralPath $destinationDisk) { throw "Destination already exists: $destinationDisk." }
    if (!(Test-Path $floppy)) { throw 'Stage1 app floppy image is missing.' }
    if (@(Get-CimInstance Win32_Process | Where-Object ExecutablePath -eq $vmExe).Count -gt 0) {
        throw '86Box is active; refusing to write a guest disk.'
    }
    foreach ($file in $files) {
        if ($file.Path -and !(Test-Path -LiteralPath $file.Path)) { throw "Missing binary $($file.Path)." }
        if ($file.Name -eq 'TRUNC.GGF') { $file.Bytes = Read-ImageFile $floppy 'TRUNC.GGF' }
        else { $file.Bytes = [IO.File]::ReadAllBytes($file.Path) }
        $file.Sha256 = (Get-FileHash -InputStream ([IO.MemoryStream]::new([byte[]]$file.Bytes)) -Algorithm SHA256).Hash
    }
    $active = @($registry.entries | Where-Object state -eq 'active').Count + [int]$registry.known_external_active_managed_count
    if ($active -ge [int]$registry.max_active_managed_processes) { throw 'Managed process capacity reached.' }
    if (@($registry.entries | Where-Object task_id -eq $taskId).Count -gt 0) {
        throw "Managed task ID already exists: $taskId"
    }
    $current = [Diagnostics.Process]::GetCurrentProcess()
    $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.Id)"
    $entry = [pscustomobject]@{
        task_id=$taskId;vm_name='NFLLM stage1 hard-disk app staging';state='active';pid=$current.Id
        ppid=$cim.ParentProcessId;creation_time=$current.StartTime.ToString('o');executable=$cim.ExecutablePath
        command_line=$cim.CommandLine;classification='temporary';scope='Clone base test disk then stage acceptance binaries on isolated FAT16 image'
        registered_at=(Get-Date).ToString('o');shutdown_policy='verify file hashes and FAT mirrors before exit'
        source_config=$sourceDisk;exit_code=$null;stopped_at=$null;stop_reason=$null
    }
    $registry.entries = @($registry.entries) + $entry
    [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    $registered = $true

    Copy-SparseImage $sourceDisk $destinationDisk
    if ((Get-FileHash $destinationDisk -Algorithm SHA256).Hash -ne $expectedSourceHash) {
        throw 'Fresh destination clone did not match the base clone hash.'
    }
    $disk = [IO.File]::Open($destinationDisk, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $disk.Position = 0
    $mbr = Read-Exact $disk 512
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'MBR signature is invalid.' }
    $partition = $null
    for ($offset = 446; $offset -lt 510; $offset += 16) {
        if ($mbr[$offset + 4] -eq 6) {
            $partition = [pscustomobject]@{
                Start=[BitConverter]::ToUInt32($mbr, $offset + 8)
                Sectors=[BitConverter]::ToUInt32($mbr, $offset + 12)
            }
            break
        }
    }
    if (!$partition) { throw 'No FAT16 type-06 primary partition found.' }
    $volumeStart = [int64]$partition.Start * 512
    $disk.Position = $volumeStart
    $bpb = Read-Exact $disk 512
    $bps = [BitConverter]::ToUInt16($bpb, 11)
    $spc = [int]$bpb[13]
    $reserved = [BitConverter]::ToUInt16($bpb, 14)
    $fatCount = [int]$bpb[16]
    $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
    $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
    $total16 = [BitConverter]::ToUInt16($bpb, 19)
    $totalSectors = if ($total16) { [int64]$total16 } else { [int64][BitConverter]::ToUInt32($bpb, 32) }
    if ($bps -ne 512 -or $spc -lt 1 -or $fatCount -lt 1 -or $fatSectors -lt 1 -or $bpb[510] -ne 0x55 -or $bpb[511] -ne 0xaa) {
        throw 'FAT16 BPB is unsupported or invalid.'
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
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$fat, [byte[]]$mirror)) { throw 'FAT mirrors differ before app staging.' }
    }
    $disk.Position = $rootStart
    $root = Read-Exact $disk ($rootEntries * 32)
    $freeRootSlots = [Collections.Generic.List[int]]::new()
    $freeClusters = [Collections.Generic.List[int]]::new()
    $reservedClusters = [Collections.Generic.HashSet[int]]::new()
    for ($offset = 0; $offset -lt $root.Length; $offset += 32) {
        if ($root[$offset] -eq 0 -or $root[$offset] -eq 0xe5) { $freeRootSlots.Add($offset) }
    }
    if ($freeRootSlots.Count -lt $files.Count) { throw 'FAT16 root directory has insufficient free slots.' }
    for ($cluster = 2; $cluster -lt [Math]::Min($dataClusters + 2, ($fatBytes / 2)); $cluster++) {
        if ([BitConverter]::ToUInt16($fat, $cluster * 2) -eq 0) { $freeClusters.Add($cluster) }
    }
    $plans = [Collections.Generic.List[object]]::new()
    $slotIndex = 0
    $clusterIndex = 0
    foreach ($file in $files) {
        $parts = $file.Name.Split('.')
        $shortName = [Text.Encoding]::ASCII.GetBytes($parts[0].ToUpperInvariant().PadRight(8) + $parts[1].ToUpperInvariant().PadRight(3))
        if ((Get-RootEntry $root $shortName) -ge 0) { throw "Guest file already exists: $($file.Name)." }
        $needed = [int][Math]::Ceiling($file.Bytes.Length / [double]$clusterBytes)
        if ($clusterIndex + $needed -gt $freeClusters.Count) { throw "FAT16 has insufficient free clusters for $($file.Name)." }
        $chain = [int[]]$freeClusters.GetRange($clusterIndex, $needed).ToArray()
        $clusterIndex += $needed
        for ($i = 0; $i -lt $chain.Length; $i++) {
            $next = if ($i -eq $chain.Length - 1) { 0xffff } else { [uint16]$chain[$i + 1] }
            [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$next), 0, $fat, $chain[$i] * 2, 2)
        }
        $padded = [byte[]]::new($chain.Length * $clusterBytes)
        [Buffer]::BlockCopy([byte[]]$file.Bytes, 0, $padded, 0, $file.Bytes.Length)
        $plans.Add([pscustomobject]@{
            Name=$file.Name; ShortName=$shortName; Bytes=$file.Bytes; Sha256=$file.Sha256
            Chain=$chain; Padded=$padded; RootOffset=$freeRootSlots[$slotIndex]
        })
        $slotIndex++
    }
    foreach ($plan in $plans) {
        $position = 0
        foreach ($cluster in $plan.Chain) {
            $disk.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
            $disk.Write($plan.Padded, $position, $clusterBytes)
            $position += $clusterBytes
        }
        $entryBytes = [byte[]]::new(32)
        [Buffer]::BlockCopy($plan.ShortName, 0, $entryBytes, 0, 11)
        $entryBytes[11] = 0x20
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0), 0, $entryBytes, 22, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0x5d6e), 0, $entryBytes, 24, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$plan.Chain[0]), 0, $entryBytes, 26, 2)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$plan.Bytes.Length), 0, $entryBytes, 28, 4)
        [Buffer]::BlockCopy($entryBytes, 0, $root, $plan.RootOffset, 32)
    }
    for ($copy = 0; $copy -lt $fatCount; $copy++) {
        $disk.Position = $fatStart + ([int64]$copy * $fatBytes)
        $disk.Write($fat, 0, $fat.Length)
    }
    $disk.Position = $rootStart
    $disk.Write($root, 0, $root.Length)
    $disk.Flush($true)

    $disk.Position = $fatStart
    $checkFat = Read-Exact $disk $fatBytes
    for ($copy = 1; $copy -lt $fatCount; $copy++) {
        $disk.Position = $fatStart + ([int64]$copy * $fatBytes)
        $mirror = Read-Exact $disk $fatBytes
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$checkFat, [byte[]]$mirror)) { throw 'FAT mirror readback failed.' }
    }
    $disk.Position = $rootStart
    $checkRoot = Read-Exact $disk ($rootEntries * 32)
    $verified = @()
    foreach ($plan in $plans) {
        $entryOffset = Get-RootEntry $checkRoot $plan.ShortName
        if ($entryOffset -lt 0) { throw "$($plan.Name) root entry readback failed." }
        $length = [int][BitConverter]::ToUInt32($checkRoot, $entryOffset + 28)
        $first = [int][BitConverter]::ToUInt16($checkRoot, $entryOffset + 26)
        $chain = Get-Fat16Chain $checkFat $first $dataClusters
        $bytes = Get-ContentFromChain $disk $dataStart $clusterBytes $chain $length
        $hash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($bytes)) -Algorithm SHA256).Hash
        if ($length -ne $plan.Bytes.Length -or $hash -ne $plan.Sha256) { throw "$($plan.Name) failed size/hash readback." }
        $verified += [pscustomobject]@{name=$plan.Name;bytes=$length;sha256=$hash;clusters=$chain}
    }
    $disk.Flush($true)
    $disk.Dispose()
    $disk = $null
    $exitCode = 0
    $result = [pscustomobject]@{
        source_disk=$sourceDisk
        source_sha256=$expectedSourceHash
        staged_disk=$destinationDisk
        staged_disk_sha256=(Get-FileHash $destinationDisk -Algorithm SHA256).Hash
        original_disk_sha256=(Get-FileHash $originalDisk -Algorithm SHA256).Hash
        cluster_bytes=$clusterBytes
        fat_mirrors_verified=$true
        files=$verified
    }
    [IO.File]::WriteAllText($evidencePath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $result
}
catch {
    if ($registered) {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $entry = $registry.entries | Where-Object task_id -eq $taskId
        if ($entry) { Set-Field $entry 'stop_reason' $_.Exception.Message }
        [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    }
    throw
}
finally {
    if ($null -ne $disk) { $disk.Dispose() }
    if ($registered) {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $entry = $registry.entries | Where-Object task_id -eq $taskId
        if ($entry) {
            Set-Field $entry 'state' 'exited'
            Set-Field $entry 'stopped_at' (Get-Date).ToString('o')
            Set-Field $entry 'exit_code' $exitCode
            [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
        }
    }
}
