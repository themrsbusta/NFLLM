# Compatibilidade de plugins e formatos

> Histórico NFLLM 1.1: o texto a seguir descreve a antiga integração estática. Para o loader de módulos `.NFD` do NFLLM 1.2, veja [Plugin SDK](PLUGIN-SDK.md).

Todos os plugins DOS são bibliotecas estáticas Open Watcom OMF e respeitam a
ABI C v1. O dispatcher usa perfis declarados manualmente, não CPUID automático.
`--backend reference` e o perfil 386+x87 mantêm o caminho de compatibilidade.

| Backend/formatos | Operação | Resultado verificado | Estado e limites |
|---|---|---|---|
| x87 F32 | referência e fallback | inferência no baseline 486 QEMU; token 411 | padrão no perfil 386+x87 |
| x87 Q8_0/Q4_0 | matvec direto compactado | inferência e comparação numérica; melhoria medida sobre decode+F32 no QEMU 486 | não é SIMD; ainda ligeiramente mais lento que F32 completo no benchmark registrado |
| x87 Q2_0/Q2_K/IQ3_XXS/IQ3_S | leitura direta de blocos e dot product | differential host com erro absoluto máximo 0; self-test e inferência de um token no QEMU 486 | Q2_0 ficou mais lento que decode; ganhos dos demais são específicos do Tiny1M/QEMU |
| MMX F32 | entrada/pesos F32 quantizados a int8 por linha, `PMADDWD`, escalas e `EMMS` | self-test e um token passaram em proxy; sequência de quatro tokens divergiu após o primeiro | experimental; aproximação, lenta neste ensaio e não aceita para fidelidade de geração |
| MMX Q8_0/Q4_0 | matvec direto por matriz | self-test, validações e inferência de um token no Athlon proxy e N270; 176.640 linhas por formato | funcional por ISA no proxy; não afirmar velocidade sem medição repetida aceita |
| 3DNow! F32 | `PFMUL`/`PFADD`/`PFACC` com cauda escalar | self-test, `EMMS`, inferência e benchmark de quatro tokens no proxy Athlon; sequência pareada igual à referência | compatibilidade de opcode medida em proxy Athlon, sem alegação de Geode físico |
| SSE2 F32 | matvec com quatro lanes e cauda escalar | três sequências de quatro tokens iguais à referência no N270 isolado | experimental fora do clone: requer OSFXSR e contexto DOS/DPMI ainda não comprovado |
| SSE2 Q8_0/Q4_0 | matvec direto por matriz | 176.640 linhas por formato, inferência e sequências de quatro tokens iguais à rota escalar direta | benchmark repetido no mesmo QEMU N270 contra kernel escalar direto por linha |

## Extensões não entregues

- SSSE3 é condicional e não necessária para SSE2; não foi implementada.
- AMD MMX Extensions são condicionais às instruções documentadas e testáveis;
  não há `amd_mmx_ext.lib` nesta entrega.
- Não se presume 3DNow! no Geode GX1 nem no Atom.
- Nenhum teste nesta matriz representa hardware físico.
