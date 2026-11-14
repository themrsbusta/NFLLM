param(
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$logsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build\logs'))
if (-not $EvidenceDirectory.StartsWith($logsRoot, [StringComparison]::OrdinalIgnoreCase) -or
    -not $OutputPath.StartsWith($logsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Benchmark evidence and summary must stay below NFLLM/source/nfllm/build/logs.'
}
$autoexecEvidencePath = Join-Path $EvidenceDirectory 'autoexec-update-retry.json'
if (-not (Test-Path -LiteralPath $autoexecEvidencePath -PathType Leaf)) {
    throw 'The saved AUTOEXEC update evidence is required to validate benchmark route arguments.'
}
$autoexecCommands = @((Get-Content -LiteralPath $autoexecEvidencePath -Raw | ConvertFrom-Json).commands)
$autoexecText = $autoexecCommands -join "`n"
if ([regex]::Matches($autoexecText, '--quantized-matvec direct').Count -ne 12 -or
    [regex]::Matches($autoexecText, '--quantized-matvec decode').Count -ne 12) {
    throw 'The benchmark AUTOEXEC must request each route exactly twelve times.'
}

$specs = @(
    [pscustomobject]@{name='Q2_0';prefix='Q20';row='Q2_0';token=413}
    [pscustomobject]@{name='Q2_K';prefix='Q2K';row='Q2_K';token=414}
    [pscustomobject]@{name='IQ3_XXS';prefix='I3X';row='IQ3_XXS';token=317}
    [pscustomobject]@{name='IQ3_S';prefix='I3S';row='IQ3_S';token=353}
)
$formats = @()
foreach ($spec in $specs) {
    $directTicks = [Collections.Generic.List[int]]::new()
    $decodeTicks = [Collections.Generic.List[int]]::new()
    foreach ($sample in @('A','B','C')) {
        foreach ($route in @('D','F')) {
            $tag = "$($spec.prefix)$route$sample"
            $statePath = Join-Path $EvidenceDirectory "$tag.OK"
            $logPath = Join-Path $EvidenceDirectory "$tag.LOG"
            if (-not (Test-Path -LiteralPath $statePath -PathType Leaf) -or
                -not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
                throw "Missing benchmark evidence for $tag."
            }
            $state = [IO.File]::ReadAllText($statePath).Trim()
            $log = [IO.File]::ReadAllText($logPath)
            $ticks = [regex]::Match($log, 'Generation tokens=1 ticks=(\d+)')
            $token = [regex]::Match($log, 'TOKEN\[0\] id=(\d+)')
            $rows = [regex]::Match($log, "Direct packed matvec rows:.*$($spec.row)=(\d+)")
            if ($state -ne 'PASS' -or -not $log.Contains('Status: PASS') -or
                -not $ticks.Success -or -not $token.Success -or -not $rows.Success) {
                throw "Incomplete status, token, timing or route counter in $tag."
            }
            if ([int]$token.Groups[1].Value -ne $spec.token) {
                throw "Unexpected token in $tag."
            }
            $rowCount = [int]$rows.Groups[1].Value
            if (($route -eq 'D' -and $rowCount -le 0) -or
                ($route -eq 'F' -and $rowCount -ne 0)) {
                throw "The packed-row counter does not match the requested route in $tag."
            }
            if ($route -eq 'D') { $directTicks.Add([int]$ticks.Groups[1].Value) }
            else { $decodeTicks.Add([int]$ticks.Groups[1].Value) }
        }
    }
    [int[]]$directSorted = @($directTicks | Sort-Object)
    [int[]]$decodeSorted = @($decodeTicks | Sort-Object)
    if ($directSorted.Length -ne 3 -or $decodeSorted.Length -ne 3) {
        throw "Expected exactly three samples per route for $($spec.name)."
    }
    $directMedian = $directSorted[1]
    $decodeMedian = $decodeSorted[1]
    $formats += [pscustomobject]@{
        format=$spec.name;sample_count=3;token_id=$spec.token
        direct_ticks=$directTicks.ToArray();decode_f32_ticks=$decodeTicks.ToArray()
        direct_median_ticks=$directMedian;decode_f32_median_ticks=$decodeMedian
        decode_over_direct_ratio=[Math]::Round(($decodeMedian / [double]$directMedian),3)
        direct_route='packed-row counter > 0'
        fallback_route='packed-row counter = 0; AUTOEXEC requested --quantized-matvec decode'
        status='PASS'
    }
}
$result = [pscustomobject]@{
    schema_version=1
    status='PASS'
    method='Three samples per format and route; order alternated per repetition; median of three.'
    environment='QEMU 11.1 generic 486; TCG single thread; 96 MiB; NF-DOS/DOS-4GW'
    prompt='Tom and Jerry are '
    generated_tokens=1
    clock_rate_ticks_per_second=1000
    format_results=$formats
    evidence_directory=$EvidenceDirectory
    autoexec_evidence=$autoexecEvidencePath
    route_proof='Direct row counters are positive for direct runs and zero for decode runs; the saved AUTOEXEC update evidence records the decode argument.'
    original_disk_sha256='32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3'
    generated_at=(Get-Date).ToString('o')
}
$outputDirectory = Split-Path -Parent $OutputPath
[void](New-Item -ItemType Directory -Path $outputDirectory -Force)
[IO.File]::WriteAllText($OutputPath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$result
