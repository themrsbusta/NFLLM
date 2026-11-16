param(
    [Parameter(Mandatory=$true)][string]$DiskPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string[]]$Names = @(
        'NFLLM.DON','SELFTEST.OK','SELFTEST.LOG','COMPAT.OK','COMPAT.LOG',
        'INFER.OK','NFLLM.LOG','MANUAL.OK','MANUAL.LOG','T1COMPAT.OK',
        'T1COMPAT.LOG','T1INFER.OK','T1NFLLM.LOG','MISS.OK','MISSING.LOG',
        'TRUNC.OK','TRUNC.LOG','Q20COMP.OK','Q20COMP.LOG','Q20INFER.OK','Q20NFLLM.LOG',
        'Q2KCOMP.OK','Q2KCOMP.LOG','Q2KINFER.OK','Q2KNFLLM.LOG',
        'I3XCOMP.OK','I3XCOMP.LOG','I3XINFER.OK','I3XNFLLM.LOG',
        'I3SCOMP.OK','I3SCOMP.LOG','I3SINFER.OK','I3SNFLLM.LOG',
        'ISA.OK','ISA.LOG'
    )
)

$ErrorActionPreference = 'Stop'

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($bytes, $offset, $Count - $offset)
        if ($read -le 0) { throw "Unexpected end of disk image at $($Stream.Position)." }
        $offset += $read
    }
    return ,$bytes
}

function Get-RootOffset([byte[]]$Root, [byte[]]$ShortName) {
    for ($offset = 0; $offset -lt $Root.Length; $offset += 32) {
        if ($Root[$offset] -eq 0) { break }
        if ($Root[$offset] -ne 0xe5 -and $Root[$offset + 11] -ne 0x0f -and
            [Linq.Enumerable]::SequenceEqual([byte[]]$Root[$offset..($offset + 10)], $ShortName)) {
            return $offset
        }
    }
    return -1
}

$diskPath = [IO.Path]::GetFullPath($DiskPath)
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$originalDisk = Join-Path $PSScriptRoot '..\..\emulators\AM486DX4-96MB\disks\nfdos502-qemu.raw'
$originalDisk = [IO.Path]::GetFullPath($originalDisk)
if ($diskPath -eq $originalDisk) { throw 'Refusing to read or alter the preserved original system disk as a test clone.' }
if (-not (Test-Path -LiteralPath $diskPath -PathType Leaf)) { throw "Guest disk is missing: $diskPath" }
$vmExe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\emulators\AM486DX4-96MB\app\86Box.exe'))
if (@(Get-CimInstance Win32_Process | Where-Object ExecutablePath -eq $vmExe).Count -gt 0) {
    throw '86Box is active; refusing to read a disk that may still be mounted.'
}
if ((Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash -ne
    '32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3') {
    throw 'The preserved original system disk does not match its baseline hash.'
}

$stream = [IO.File]::Open($diskPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
$copied = [Collections.Generic.List[object]]::new()
$missing = [Collections.Generic.List[string]]::new()
try {
    $mbr = Read-Exact $stream 512
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xaa) { throw 'MBR signature is invalid.' }
    $partition = $null
    for ($offset = 446; $offset -lt 510; $offset += 16) {
        if ($mbr[$offset + 4] -eq 6) {
            $partition = [pscustomobject]@{
                start = [BitConverter]::ToUInt32($mbr, $offset + 8)
                sectors = [BitConverter]::ToUInt32($mbr, $offset + 12)
            }
            break
        }
    }
    if (-not $partition) { throw 'FAT16 primary partition type 06h not found.' }
    $volumeStart = [int64]$partition.start * 512
    $stream.Position = $volumeStart
    $bpb = Read-Exact $stream 512
    $bytesPerSector = [BitConverter]::ToUInt16($bpb, 11)
    $sectorsPerCluster = [int]$bpb[13]
    $reservedSectors = [BitConverter]::ToUInt16($bpb, 14)
    $fatCount = [int]$bpb[16]
    $rootEntries = [BitConverter]::ToUInt16($bpb, 17)
    $fatSectors = [BitConverter]::ToUInt16($bpb, 22)
    if ($bytesPerSector -ne 512 -or $sectorsPerCluster -lt 1 -or
        $reservedSectors -lt 1 -or $fatCount -lt 1 -or $rootEntries -lt 1 -or $fatSectors -lt 1) {
        throw 'Unsupported or invalid FAT16 BPB.'
    }
    $rootSectors = [int][Math]::Ceiling(($rootEntries * 32) / [double]$bytesPerSector)
    $fatBytes = $fatSectors * $bytesPerSector
    $clusterBytes = $sectorsPerCluster * $bytesPerSector
    $fatStart = $volumeStart + ([int64]$reservedSectors * $bytesPerSector)
    $rootStart = $fatStart + ([int64]$fatCount * $fatBytes)
    $dataStart = $rootStart + ([int64]$rootSectors * $bytesPerSector)
    $stream.Position = $fatStart
    $fat = Read-Exact $stream $fatBytes
    for ($copy = 1; $copy -lt $fatCount; $copy++) {
        $stream.Position = $fatStart + ([int64]$copy * $fatBytes)
        $mirror = Read-Exact $stream $fatBytes
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$fat, [byte[]]$mirror)) {
            throw 'FAT mirror copies differ.'
        }
    }
    $stream.Position = $rootStart
    $root = Read-Exact $stream ($rootEntries * 32)
    [void](New-Item -ItemType Directory -Path $outputDirectory -Force)

    foreach ($name in $Names) {
        $parts = $name.ToUpperInvariant().Split('.')
        if ($parts.Count -ne 2 -or $parts[0].Length -gt 8 -or $parts[1].Length -gt 3) {
            throw "Only root 8.3 names are supported: $name"
        }
        $shortName = [Text.Encoding]::ASCII.GetBytes($parts[0].PadRight(8) + $parts[1].PadRight(3))
        $entryOffset = Get-RootOffset $root $shortName
        if ($entryOffset -lt 0) { $missing.Add($name); continue }
        $length = [int][BitConverter]::ToUInt32($root, $entryOffset + 28)
        $cluster = [int][BitConverter]::ToUInt16($root, $entryOffset + 26)
        $content = [IO.MemoryStream]::new()
        $remaining = $length
        $seen = [Collections.Generic.HashSet[int]]::new()
        while ($remaining -gt 0) {
            if ($cluster -lt 2 -or $cluster -ge 0xfff0 -or -not $seen.Add($cluster)) {
                throw "$name contains an invalid, reserved or cyclic FAT16 cluster chain."
            }
            $stream.Position = $dataStart + ([int64]($cluster - 2) * $clusterBytes)
            $block = Read-Exact $stream $clusterBytes
            $count = [Math]::Min($remaining, $clusterBytes)
            $content.Write($block, 0, $count)
            $remaining -= $count
            $nextOffset = $cluster * 2
            if ($nextOffset + 1 -ge $fat.Length) { throw "$name FAT16 index exceeds the table." }
            $cluster = [BitConverter]::ToUInt16($fat, $nextOffset)
        }
        $bytes = $content.ToArray()
        $content.Dispose()
        $destination = Join-Path $outputDirectory $name.ToUpperInvariant()
        [IO.File]::WriteAllBytes($destination, $bytes)
        $writeTime = [BitConverter]::ToUInt16($root, $entryOffset + 22)
        $writeDate = [BitConverter]::ToUInt16($root, $entryOffset + 24)
        $time = [DateTime]::new(1980 + (($writeDate -shr 9) -band 0x7f), (($writeDate -shr 5) -band 0x0f), ($writeDate -band 0x1f),
            (($writeTime -shr 11) -band 0x1f), (($writeTime -shr 5) -band 0x3f), (($writeTime -band 0x1f) * 2))
        $copied.Add([pscustomobject]@{
            name=$name.ToUpperInvariant();bytes=$bytes.Length;sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            dos_timestamp=$time.ToString('yyyy-MM-dd HH:mm:ss');host_path=$destination
            contents=[Text.Encoding]::ASCII.GetString($bytes)
        })
    }
} finally {
    $stream.Dispose()
}

$result = [pscustomobject]@{
    disk=$diskPath
    disk_sha256_after_guest=(Get-FileHash -LiteralPath $diskPath -Algorithm SHA256).Hash
    original_disk_sha256=(Get-FileHash -LiteralPath $originalDisk -Algorithm SHA256).Hash
    fat16=[pscustomobject]@{bytes_per_sector=$bytesPerSector;sectors_per_cluster=$sectorsPerCluster;cluster_bytes=$clusterBytes;fat_mirrors_verified=$true}
    output_directory=$outputDirectory
    found=$copied.ToArray()
    missing=$missing.ToArray()
    completion_marker_present=($copied.Name -contains 'NFLLM.DON')
}
$evidencePath = Join-Path $outputDirectory 'extraction-evidence.json'
[IO.File]::WriteAllText($evidencePath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$result
