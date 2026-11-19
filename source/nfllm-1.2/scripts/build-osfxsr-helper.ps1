param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\build\OSFXSR.COM')
)

$ErrorActionPreference = 'Stop'
$prefix = [byte[]]@(
    0x66,0x0f,0x20,0xe0,                  # mov eax, cr4
    0x66,0x0d,0x00,0x02,0x00,0x00,        # or eax, 0200h
    0x66,0x0f,0x22,0xe0,                  # mov cr4, eax
    0x66,0x0f,0x20,0xe0,                  # mov eax, cr4
    0x66,0xa9,0x00,0x02,0x00,0x00         # test eax, 0200h
)
$branch = [byte[]]@(0x74,0x0c)           # jz past the 12-byte success exit
$passExit = [byte[]]@(0xba,0,0,0xb4,0x09,0xcd,0x21,0xb8,0x00,0x4c,0xcd,0x21)
$failExit = [byte[]]@(0xba,0,0,0xb4,0x09,0xcd,0x21,0xb8,0x01,0x4c,0xcd,0x21)
$passText = [Text.Encoding]::ASCII.GetBytes("OSFXSR SET AND VERIFIED`r`n`$")
$failText = [Text.Encoding]::ASCII.GetBytes("OSFXSR READBACK FAILED`r`n`$")
$passOffset = 0x100 + $prefix.Length + $branch.Length + $passExit.Length + $failExit.Length
$failOffset = $passOffset + $passText.Length
[Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$passOffset),0,$passExit,1,2)
[Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$failOffset),0,$failExit,1,2)

$bytes = [Collections.Generic.List[byte]]::new()
$bytes.AddRange($prefix)
$bytes.AddRange($branch)
$bytes.AddRange($passExit)
$bytes.AddRange($failExit)
$bytes.AddRange($passText)
$bytes.AddRange($failText)
if ($bytes.Count -gt 0xff00) { throw 'COM helper exceeds the DOS COM size limit.' }

$resolved = [IO.Path]::GetFullPath($OutputPath)
$parent = [IO.Path]::GetDirectoryName($resolved)
if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
[IO.File]::WriteAllBytes($resolved,$bytes.ToArray())
$check = [IO.File]::ReadAllBytes($resolved)
if ($check.Length -ne $bytes.Count -or
    -not [Linq.Enumerable]::SequenceEqual([byte[]]$check[0..($prefix.Length-1)],[byte[]]$prefix)) {
    throw 'OSFXSR COM opcode/readback validation failed.'
}
[pscustomobject]@{
    file=$resolved
    bytes=$check.Length
    sha256=(Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
    cr4_bit_set=9
    success_text=[Text.Encoding]::ASCII.GetString($passText)
    failure_text=[Text.Encoding]::ASCII.GetString($failText)
} | ConvertTo-Json -Depth 3
