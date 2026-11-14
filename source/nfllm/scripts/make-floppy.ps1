param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter(Mandatory = $true)]
    [string[]]$Files
)

$ErrorActionPreference = 'Stop'

$BytesPerSector = 512
$SectorsPerFloppy = 2880
$SectorsPerFat = 9
$RootEntryCount = 224
$RootDirectorySectors = 14
$DataStartSector = 1 + (2 * $SectorsPerFat) + $RootDirectorySectors
$ClusterBytes = $BytesPerSector

function Set-U16([byte[]]$Buffer, [int]$Offset, [int]$Value) {
    [Buffer]::BlockCopy([BitConverter]::GetBytes([UInt16]$Value), 0, $Buffer, $Offset, 2)
}

function Set-U32([byte[]]$Buffer, [int]$Offset, [long]$Value) {
    [Buffer]::BlockCopy([BitConverter]::GetBytes([UInt32]$Value), 0, $Buffer, $Offset, 4)
}

function Set-Fat12Entry([byte[]]$Fat, [int]$Cluster, [int]$Value) {
    $offset = $Cluster + [Math]::Floor($Cluster / 2)
    if (($Cluster -band 1) -eq 0) {
        $Fat[$offset] = [byte]($Value -band 0xff)
        $Fat[$offset + 1] = [byte](($Fat[$offset + 1] -band 0xf0) -bor (($Value -shr 8) -band 0x0f))
    } else {
        $Fat[$offset] = [byte](($Fat[$offset] -band 0x0f) -bor (($Value -shl 4) -band 0xf0))
        $Fat[$offset + 1] = [byte](($Value -shr 4) -band 0xff)
    }
}

if ($Files.Count -gt $RootEntryCount) {
    throw "Too many files for a FAT12 floppy root: $($Files.Count)"
}

$items = @()
foreach ($file in $Files) {
    $item = Get-Item -LiteralPath $file
    if (-not $item.PSIsContainer) {
        $dosName = $item.Name.ToUpperInvariant()
        if ($dosName -notmatch '^[A-Z0-9_$~!#%&''()@^`{}-]{1,8}(\.[A-Z0-9_$~!#%&''()@^`{}-]{1,3})?$') {
            throw "Filename must use DOS 8.3 format: $($item.Name)"
        }
        $items += [pscustomobject]@{ Path = $item.FullName; DosName = $dosName; Length = [long]$item.Length }
    }
}

$maxDataClusters = $SectorsPerFloppy - $DataStartSector
$totalClusters = 0
foreach ($item in $items) {
    $totalClusters += [int][Math]::Ceiling($item.Length / [double]$ClusterBytes)
}
if ($totalClusters -gt $maxDataClusters) {
    throw "Files need $totalClusters clusters; FAT12 floppy has $maxDataClusters data clusters."
}

$disk = [byte[]]::new($BytesPerSector * $SectorsPerFloppy)
$boot = [byte[]]::new($BytesPerSector)
$boot[0] = 0xeb; $boot[1] = 0x3c; $boot[2] = 0x90
[Text.Encoding]::ASCII.GetBytes('NFLLM   ').CopyTo($boot, 3)
Set-U16 $boot 11 $BytesPerSector
$boot[13] = 1
Set-U16 $boot 14 1
$boot[16] = 2
Set-U16 $boot 17 $RootEntryCount
Set-U16 $boot 19 $SectorsPerFloppy
$boot[21] = 0xf0
Set-U16 $boot 22 $SectorsPerFat
Set-U16 $boot 24 18
Set-U16 $boot 26 2
Set-U32 $boot 28 0
Set-U32 $boot 32 0
$boot[36] = 0
$boot[38] = 0x29
Set-U32 $boot 39 0x4e464c4d
[Text.Encoding]::ASCII.GetBytes('NFLLM DEV  ').CopyTo($boot, 43)
[Text.Encoding]::ASCII.GetBytes('FAT12   ').CopyTo($boot, 54)
$boot[510] = 0x55; $boot[511] = 0xaa
[Buffer]::BlockCopy($boot, 0, $disk, 0, $BytesPerSector)

$fatLength = $SectorsPerFat * $BytesPerSector
$fat = [byte[]]::new($fatLength)
$fat[0] = 0xf0; $fat[1] = 0xff; $fat[2] = 0xff
$rootOffset = (1 + (2 * $SectorsPerFat)) * $BytesPerSector
$nextCluster = 2
$rootIndex = 0

foreach ($item in $items) {
    $baseName, $extension = $item.DosName.Split('.', 2)
    $nameBytes = [Text.Encoding]::ASCII.GetBytes($baseName.PadRight(8) + $extension.PadRight(3))
    $dirOffset = $rootOffset + ($rootIndex * 32)
    [Buffer]::BlockCopy($nameBytes, 0, $disk, $dirOffset, 11)
    $disk[$dirOffset + 11] = 0x20
    Set-U16 $disk ($dirOffset + 22) 0
    Set-U16 $disk ($dirOffset + 24) 0x21

    $clustersForFile = [int][Math]::Ceiling($item.Length / [double]$ClusterBytes)
    $firstCluster = if ($clustersForFile -gt 0) { $nextCluster } else { 0 }
    Set-U16 $disk ($dirOffset + 26) $firstCluster
    Set-U32 $disk ($dirOffset + 28) $item.Length

    $content = [IO.File]::ReadAllBytes($item.Path)
    $remaining = $content.Length
    $sourceOffset = 0
    for ($clusterIndex = 0; $clusterIndex -lt $clustersForFile; $clusterIndex++) {
        $cluster = $nextCluster + $clusterIndex
        $next = if ($clusterIndex -eq $clustersForFile - 1) { 0xfff } else { $cluster + 1 }
        Set-Fat12Entry $fat $cluster $next
        $sector = $DataStartSector + ($cluster - 2)
        $diskOffset = $sector * $BytesPerSector
        $copyLength = [Math]::Min($BytesPerSector, $remaining)
        [Buffer]::BlockCopy($content, $sourceOffset, $disk, $diskOffset, $copyLength)
        $sourceOffset += $copyLength
        $remaining -= $copyLength
    }
    $nextCluster += $clustersForFile
    $rootIndex++
}

[Buffer]::BlockCopy($fat, 0, $disk, $BytesPerSector, $fatLength)
[Buffer]::BlockCopy($fat, 0, $disk, $BytesPerSector * (1 + $SectorsPerFat), $fatLength)

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
$parent = [IO.Path]::GetDirectoryName($resolvedOutput)
if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
$temporaryOutput = "$resolvedOutput.tmp"
[IO.File]::WriteAllBytes($temporaryOutput, $disk)
Move-Item -LiteralPath $temporaryOutput -Destination $resolvedOutput -Force

# Read the directory entries and file chains back from the finished image.
$check = [IO.File]::ReadAllBytes($resolvedOutput)
if ($check.Length -ne ($BytesPerSector * $SectorsPerFloppy) -or $check[510] -ne 0x55 -or $check[511] -ne 0xaa) {
    throw 'FAT12 image length or boot-sector signature failed verification.'
}
foreach ($item in $items) {
    $entryName = $item.DosName.Split('.', 2)[0].PadRight(8) + $(if ($item.DosName.Contains('.')) { $item.DosName.Split('.', 2)[1].PadRight(3) } else { '   ' })
    $bytes = [Text.Encoding]::ASCII.GetBytes($entryName)
    $found = $false
    for ($i = 0; $i -lt $RootEntryCount; $i++) {
        $offset = $rootOffset + ($i * 32)
        if ([Linq.Enumerable]::SequenceEqual([byte[]]$check[$offset..($offset + 10)], [byte[]]$bytes)) {
            $size = [BitConverter]::ToUInt32($check, $offset + 28)
            if ($size -ne $item.Length) { throw "FAT12 directory size mismatch for $($item.DosName)" }
            $found = $true
            break
        }
    }
    if (-not $found) { throw "FAT12 directory entry missing: $($item.DosName)" }
}

Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256 | Select-Object Path, Hash
Get-Item -LiteralPath $resolvedOutput | Select-Object FullName, Length
