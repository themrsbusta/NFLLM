# Etapa 3 — kernel 3DNow! F32

**Status:** implementação, verificação funcional e medição da fase de geração
concluídas no proxy QEMU; não validado em hardware Geode físico.

## Implementação

`plugins/3dnow/3dnow.c` oferece um kernel ABI C v1 para matvec F32. O produto
interno usa `PFMUL`, acumulação em pares com `PFADD` e redução com `PFACC`;
colunas ímpares usam a cauda escalar. A rotina executa `EMMS` antes de voltar
ao DOS/4GW para deixar o estado x87 utilizável.

O objeto é compilado com alvo 386. Como o assembler Open Watcom configurado
para `-3` rejeita as mnemônicas 3DNow!, os opcodes ficam expressos em bytes no
bloco `#pragma aux`. A seleção exige perfil manual com MMX e 3DNow!. O perfil
486 padrão continua 386+x87 e não chama o kernel. RMSNorm e RoPE usam o
backend x87 de referência.

## Verificação

O build DOS incluiu `3dnow.lib`, `NF3DNOW.EXE` e NFLLM. O teste isolado passou
no QEMU 11.1 com `athlon-v1,+mmx,+3dnow,+3dnowext`, TCG single-thread e 96 MiB:

- selecionou `3dnow.matvec.f32` pelo dispatcher;
- comparou duas linhas de comprimento ímpar com os valores esperados;
- verificou que operações x87 funcionam depois de `EMMS`;
- NFLLM carregou Tiny1M F32 e gerou token 411 (`e`), com `Status: PASS` e
  `matvec=3dnow.matvec.f32`.

Este é um proxy de instruções Athlon, não uma identificação do Geode LX, não
uma validação em Geode físico e não uma medição de desempenho comparável ao
Am486. O teste não executou o binário 3DNow! no perfil 486.

## Benchmark de geração

Um ensaio posterior executou três pares intercalados com o mesmo Tiny1M F32,
30 tokens de prompt e quatro tokens gerados por rota. As sequências foram
idênticas nos três pares: `[411, 289, 419, 269]`.

| Rota | Amostras da geração (ticks) | Mediana |
|---|---:|---:|
| 3DNow! | 660, 659, 714 | 660 |
| x87 | 8.294, 9.447, 10.930 | 9.447 |

O guest não gravou ticks da avaliação do prompt nessa bateria. Portanto, estes
valores cobrem somente a fase de geração, não o prompt nem a inferência total.
O resumo original do runner encerrou com processo e runner em código 0,
marcador do guest verificado e disco original preservado. O clone usado foi
`nfdos502-nfllm-stage1-cboot-20261114-dnow-r2.raw`.

## Evidências

- Saída extraída do teste: `build/logs/stage3-dnow-geodeproxy-20261114-r2/3DTEST.LOG`.
- Inferência e token: `build/logs/stage3-dnow-geodeproxy-20261114-r2/3DNOW.LOG`.
- Resumo QEMU, CPU, saída, desligamento e hashes: `build/logs/stage3-dnow-geodeproxy-20261114-r2/stage3-dnow-geodeproxy-20261114-r2-summary.json`.
- Benchmark repetido após correção da extração: `build/logs/stage3-3dnow-f32-bench-20261114-v2/stage3-3dnow-f32-bench-20261114-v2-summary.json`.
- Extração FAT16: `build/logs/stage3-dnow-geodeproxy-20261114-r2/extraction-evidence.json`.
- AUTOEXEC: `build/logs/stage3-dnow-autoexec-20261114-r3.json`.
- O disco original de NF-DOS permaneceu em SHA-256
  `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`.

## Limitações remanescentes

O proxy não demonstra identidade Geode nem compatibilidade em hardware físico.
As AMD MMX Extensions específicas de Geode não foram implementadas. A
configuração de SSE2 continua experimental fora do guest isolado que habilita
`CR4.OSFXSR`; com `OSFXSR=0`, SSE/SSE2 deve permanecer bloqueado. O porte do
llama.cpp upstream para DOS é um objetivo posterior e distinto.
