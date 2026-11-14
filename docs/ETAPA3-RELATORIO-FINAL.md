# NFLLM — relatório final da Etapa 3

**Estado:** critérios obrigatórios da Etapa 3 verificados em ambientes virtuais
de compatibilidade por ISA. Nenhum hardware Geode ou Atom físico foi testado.
O repositório GitHub não foi alterado.

## Escopo executado

- Laboratório NF-DOS/DOS/4GW com 96 MiB em QEMU 11.1 TCG single-thread.
- Geode compatível validado por proxy Athlon-v1 com MMX e 3DNow!/3DNowExt; o
  proxy não prova identidade Geode.
- Atom N270 (`n270-v1`) para SSE2 e MMX; o estado OSFXSR foi preparado apenas
  no clone isolado de teste.
- Formatos Q8_0/Q4_0 com kernels diretos x87 e kernels de matriz MMX/SSE2.
- Q2_0, Q2_K, IQ3_XXS e IQ3_S com dot products packed x87 e referência de
  decodificação preservada.

## Critérios e evidências

| Critério | Resultado verificado | Limite |
|---|---|---|
| Baseline 486 e fallback x87 | PASS: NF-DOS/DOS/4GW, perfil QEMU `486`, Tiny1M F32, `reference.matvec.f32`, token 411; MMX/SSE/SSE2 não executados | QEMU genérico 486, não Am486 físico |
| ABI v1 e bibliotecas estáticas | bibliotecas Open Watcom OMF integradas e kernels registrados no dispatcher | seleção ISA continua manual |
| MMX | self-test, caudas, tolerâncias, invalidação, `EMMS`/x87 e fallback passam; Q8_0/Q4_0 inferem no Athlon proxy e N270 | MMX F32 experimental por divergência em 4 tokens e lentidão |
| 3DNow! | self-test, caudas ímpares, `EMMS`, inferência e três pares de quatro tokens passam no proxy Athlon | ticks do QEMU não representam Geode físico |
| SSE2 | F32, Q8_0 e Q4_0 passam no perfil N270 isolado; F32 tem sequência de quatro tokens idêntica ao x87 | OSFXSR/DOS-DPMI genérico não comprovado |
| Q8_0/Q4_0 direto x87 | self-test, inferência e comparação A/B contra decode+F32 no 486 QEMU | ambos ficaram ligeiramente mais lentos que F32 neste teste |
| Q2_0/Q2_K/IQ3_XXS/IQ3_S | diferencial host máximo 0; self-test e inferência packed x87 passam em QEMU 486 | resultados de velocidade dependem do modelo e guest |
| Seleção manual e fallback | perfil 386+x87 voltou à referência; perfis especializados exigem declaração explícita | probe CPUID do DOS/4GW permaneceu inconclusivo |
| Integridade do disco | SHA-256 do original permaneceu `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3` | testes foram feitos em clones |

## Documentos e resumos principais

- Laboratórios: `docs/GEODE-READY.md`, `docs/ATOM-READY.md`.
- Matriz de plugins: `docs/PLUGIN-COMPATIBILITY.md`.
- Medições: `docs/BENCHMARKS-ETAPA3.md`.
- Kernels: `docs/ETAPA3-3DNOW.md`, `docs/ETAPA3-DIRECT-Q8-Q4.md`,
  `docs/ETAPA3-DIRECT-Q2-IQ3.md`.
- SSE2: `build/logs/stage3-sse2-f32-bench-20261114-v2/`.
- MMX no N270: `build/logs/stage3-mmx-atom-n270-20261114/`.
- 3DNow! repetido: `build/logs/stage3-3dnow-f32-bench-20261114-v2/`.
- Regressão 486: `build/logs/stage3-486-final-regression-20261114/`.
- Comparação direta Q8/Q4: `build/logs/stage3-direct-q8q4-ab-summary-20261114.json`.
- Benchmark packed Q2/IQ3: `build/logs/stage3-q2-iq3-direct-benchmark-v4/`.

## Limitações e próximos objetivos

- Nenhum resultado comprova compatibilidade em Geode/Atom físico.
- Não há `amd_mmx_ext.lib` nem plugin SSSE3; são extensões opcionais e não
  foram requeridas pelos kernels aceitos.
- SSE2 permanece manual e experimental fora do clone em que o guest habilitou
  `CR4.OSFXSR`; o contrato de salvamento XMM por DOS/DPMI ainda precisa de
  implementação/validação geral.
- MMX F32 não é aceito como geração fiel; manter a rota experimental fora do
  caminho padrão.
- Tiny1M é a carga usada; isso não prova cobertura de todos os GGUF ou layouts.
- O objetivo maior de portar o llama.cpp upstream para NF-DOS continua em aberto.
- Na data deste relatório, ainda não havia publicação no GitHub. A publicação
  1.0 atual está descrita em
  `docs/releases/v1.0-experimental-research-release.md`.
