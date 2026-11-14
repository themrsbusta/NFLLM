# Laboratório Atom Ready

## Perfil

QEMU 11.1 TCG single-thread, CPU `n270-v1`, NF-DOS/DOS/4GW e 96 MiB. O ensaio
comprova compatibilidade das instruções executadas pelo perfil selecionado; não
é teste em hardware físico nem garante equivalência microarquitetural completa.

## Resultados funcionais

- SSE2 F32: três pares intercalados de 30 tokens de prompt e quatro tokens
  gerados. Cada par selecionado/referência produziu `[411, 289, 419, 269]`.
- SSE2 Q8_0 e Q4_0: autodiagnóstico e inferência passaram; 176.640 linhas
  diretas por formato; primeiro token igual ao kernel escalar direto.
- MMX no N270: autodiagnóstico passou, incluindo cauda ímpar, tolerâncias,
  validações de entrada, `EMMS`/x87 e fallback 386+x87. Q8_0 e Q4_0 inferiram
  176.640 linhas cada e coincidiram no primeiro token com a referência.
- Perfil 486: a regressão NF-DOS/DOS/4GW passou com `reference.matvec.f32`,
  token 411 (`e`) e sem executar kernels MMX/SSE/SSE2.

## OSFXSR e segurança do estado

O N270 reporta SSE2 por CPUID, mas o guest iniciou com `CR4.OSFXSR=0`. O
dispatcher manteve SSE/SSE2 bloqueados nesse estado. `OSFXSR.COM` foi usado
somente em um clone isolado de QEMU para executar a prova SSE2; ele não deve ser
executado no Am486 nem em DOS real. A experiência não demonstra que um DOS/DPMI
genérico salva o estado XMM entre tarefas, interrupções ou trocas de contexto.
Por isso o plugin SSE2 continua manual e experimental fora do laboratório
isolado.

SSSE3 foi reportado pelo perfil escolhido, mas não há plugin SSSE3 nesta
entrega e ele não é pré-requisito do plugin SSE2.

## Evidências

- SSE2 e benchmarks: `build/logs/stage3-sse2-f32-bench-20261114-v2/`.
- MMX N270: `build/logs/stage3-mmx-atom-n270-20261114/`.
- Sonda OSFXSR: `build/logs/atom-n270-isa-osfxsr-guest-20261114/`.
- Regressão 486 genérica: `build/logs/stage3-486-final-regression-20261114/`.
- Fonte técnica: [Intel Atom N270 datasheet](https://www.intel.cn/content/dam/doc/datasheet/mobile-atom-n270-single-core-datasheet-.pdf)
  e [documentação de CPUs x86 do QEMU](https://www.qemu.org/docs/master/system/i386/cpu.html).

## Limites

Não foram testados Atom físico, outros Atom, SSSE3, DOS/DPMI com salvamento de
XMM em múltiplas tarefas, nem o porte upstream do llama.cpp. A comparação de
ticks deste laboratório não deve ser extrapolada para hardware real.
