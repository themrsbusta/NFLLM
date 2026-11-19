# Benchmarks da Etapa 3

## Método

Os testes usam NF-DOS/DOS/4GW, QEMU 11.1, TCG single-thread e 96 MiB. Os
benchmarks repetidos têm três amostras pareadas ou intercaladas. O relógio do
guest reporta 1.000 ticks/s. Resultados são observações do QEMU e da carga
Tiny1M; não são desempenho previsto em hardware físico.

## SSE2 no Atom N270

O teste F32 usa prompt de 30 tokens e quatro tokens gerados; as três sequências
SSE2 e as três sequências x87 foram `[411, 289, 419, 269]`.

| Operação | Comparação | Mediana do prompt (ticks) | Mediana da geração (ticks) | Tokens pareados |
|---|---|---:|---:|---|
| F32 | SSE2 vs x87 | 549 vs 6.316 | 110 vs 605 | iguais em 3/3 pares |
| Q8_0 | matriz SSE2 direta vs kernel escalar direto por linha | 1.208 vs 6.646 | 110 vs 714 | iguais em 3/3 pares |
| Q4_0 | matriz SSE2 direta vs kernel escalar direto por linha | 1.043 vs 6.591 | 110 vs 660 | iguais em 3/3 pares |

Para Q8_0/Q4_0, cada amostra processou 194.304 linhas no teste de quatro
tokens. A inferência de aceitação de um token processou 176.640 linhas para
cada formato. A comparação quantizada isola kernel de matriz contra o kernel
escalar direto por linha; não é comparação contra desquantização F32.

## MMX e 3DNow!

- MMX Q8_0/Q4_0: no N270, cada rota inferiu um token igual à referência e
  processou 176.640 linhas. O teste isolado validou caudas, tolerâncias,
  entrada inválida, saída não parcial, `EMMS` e fallback. A amostra de um token
  não é benchmark comparativo.
- MMX F32: a comparação exploratória no proxy Athlon foi mais lenta que x87 e
  divergiu após o primeiro token na geração de quatro tokens. Nenhum ganho é
  reivindicado; o caminho segue experimental.
- 3DNow! F32: três pares de quatro tokens iguais à referência. Reportar somente
  os ticks da fase de geração, pois o guest não registrou ticks de prompt nesse
  ensaio. As medianas foram 660 ticks para 3DNow! e 9.447 para x87; amostras
  individuais e o resumo runner/QEMU com saída 0 estão em
  `build/logs/stage3-3dnow-f32-bench-20261114-v2/`.

## Matvec x87 direto no QEMU 486

Três amostras intercaladas, prompt de 30 tokens e um token gerado:

| Modo | Mediana (ticks) | Interpretação |
|---|---:|---|
| F32 | 6.262 | referência de contexto |
| Q8_0 direto | 6.481 | 1,61× menor latência que Q8_0 decode+F32; 3,5% mais lento que F32 |
| Q8_0 decode+F32 | 10.436 | rota quantizada anterior |
| Q4_0 direto | 6.372 | 1,63× menor latência que Q4_0 decode+F32; 1,8% mais lento que F32 |
| Q4_0 decode+F32 | 10.381 | rota quantizada anterior |

## Q2 e IQ3 x87 diretos no QEMU 486

Três amostras por formato e rota, em ordem alternada:

| Formato | Direto (ticks) | Decode+F32 (ticks) | Resultado no ensaio |
|---|---:|---:|---|
| Q2_0 | 29.495 | 26.748 | direto 10,3% mais lento |
| Q2_K | 33.944 | 34.493 | direto 1,6% mais rápido |
| IQ3_XXS | 37.953 | 42.293 | direto 10,2% mais rápido |
| IQ3_S | 36.800 | 41.029 | direto 10,3% mais rápido |

O diferencial host teve erro absoluto máximo 0 nos quatro formatos. A medida
reflete Tiny1M e QEMU genérico 486; não se generaliza para outros modelos.

## Baseline e limitações

A regressão x87 de um token no perfil QEMU `486` passou: `reference.matvec.f32`,
token 411 (`e`), sem linhas Q8/Q4/Q2/IQ3 diretas nesse teste F32. Os testes
anteriores cobrem separadamente os formatos quantizados. O disco original
NF-DOS permaneceu com SHA-256
`32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`.

Benchmarks de proxy, perfis Atom/Athlon do QEMU e execução em 86Box são ambientes
distintos. Não somar ou comparar diretamente os ticks entre eles. Nenhum
resultado demonstra desempenho em Am486, Geode ou Atom físico.
