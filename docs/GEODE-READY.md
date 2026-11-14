# Laboratório Geode Ready

## Escopo

O aceite é por conjunto de instruções. O laboratório usa NF-DOS com 96 MiB em
QEMU 11.1 TCG single-thread e o proxy `athlon-v1,+mmx,+3dnow,+3dnowext`.
Esse perfil executa os opcodes usados nos kernels MMX e 3DNow!, mas não é uma
emulação identificada como Geode GX1/LX e não representa teste em hardware
físico.

## Perfis e compatibilidade

| Família alvo | Perfil documentado | Seleção segura |
|---|---|---|
| Geode GX1 | x86, x87 e MMX | scalar/x87 e MMX; não selecionar 3DNow!, SSE ou SSE2 sem prova do modelo |
| Geode LX | x86, x87, MMX e 3DNow! | MMX ou 3DNow! somente por perfil manual que declare as capacidades |

O plugin `plugins/mmx/mmx.c` contém kernels MMX para Q8_0/Q4_0 e um matvec F32
experimental. `plugins/3dnow/3dnow.c` implementa matvec F32 com `PFMUL`,
`PFADD` e `PFACC`, trata cauda ímpar e executa `EMMS`. RMSNorm e RoPE permanecem
no backend de referência. A ABI C v1, o registro estático `.lib`, a seleção
manual e o fallback x87 são mantidos.

## Evidências

- MMX isolado: valida grupos completos e cauda ímpar, tolerância numérica,
  escalas e entradas inválidas, ausência de saída parcial, Q8_0/Q4_0 com várias
  linhas, retorno x87 após `EMMS` e fallback do perfil 386+x87.
- MMX Tiny1M Q8_0/Q4_0: 176.640 linhas diretas por formato, primeiro token
  igual à referência; também executado no proxy Athlon e no Atom N270.
- 3DNow! isolado: seleção do kernel, valores numéricos, cauda ímpar e x87 após
  `EMMS` passaram.
- 3DNow! Tiny1M F32: três pares de execução de quatro tokens; sequência
  `[411, 289, 419, 269]` igual à referência em cada par. O ensaio repetido com
  extração corrigida está em
  `build/logs/stage3-3dnow-f32-bench-20261114-v2/`.
- O ensaio MMX F32 de quatro tokens é experimental; divergiu da referência
  depois do primeiro token e foi mais lento. Não é aceito como caminho fiel.

## Itens não afirmados

- Nenhum Geode GX1/LX físico foi testado.
- O proxy Athlon não verifica ausência ou presença das AMD MMX Extensions
  específicas de Geode. `amd_mmx_ext.lib` não foi implementada.
- O perfil do GX1 não deve herdar 3DNow! do perfil LX.
- Resultados de ticks do QEMU são específicos desse proxy e do TCG.

## Referências técnicas

- [AMD Geode GX1 databook](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/datasheets/goede_gx1_databook-rev5.pdf)
- [AMD Geode LX databook](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/datasheets/33234H_LX_databook.pdf)
- [Modelos de CPU no QEMU](https://www.qemu.org/docs/master/system/i386/cpu.html)
