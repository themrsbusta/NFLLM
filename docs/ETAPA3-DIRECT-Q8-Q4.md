# Etapa 3 — matvec direto Q8_0/Q4_0

**Status:** incremento implementado e validado no perfil escalar 386+x87.

## Implementação

Os plugins Q8_0 e Q4_0 agora registram uma operação ABI v1 para produto
interno de uma linha compactada contra um vetor F32. O novo caminho lê os
valores empacotados diretamente e não materializa uma linha F32 temporária.
Os decodificadores por linha continuam disponíveis. Os dois kernels diretos
atuais são C escalar com requisito 386+x87; não são kernels MMX, 3DNow! ou SSE.

No Q8_0, cada bloco contém 32 pesos assinados e uma escala FP16: calcula-se a
soma de `peso_quantizado * ativação` do bloco e depois aplica-se a escala. No
Q4_0, cada bloco contém 32 valores de 4 bits; o nibble baixo cobre os índices
0–15, o alto os índices 16–31, e cada peso é centralizado em 8 antes do
produto. O resultado final é escrito somente após validar os limites e todas
as escalas do tensor.

NFLLM tenta a operação direta por linha. Se ela não estiver registrada, usa o
caminho anterior de desquantização seguida pelo matvec F32. A opção
`--backend reference` força o caminho legado usado na comparação. A ABI C v1
e o fallback geral foram preservados. Q2_0, Q2_K, IQ3_XXS e IQ3_S continuam
como decodificadores por linha; este incremento não converte seus kernels.

## Testes

Os testes de host Open Watcom passaram com vetores independentes, escalas
FP16 inválidas/subnormais, tamanhos incorretos e comparações diferenciais
entre direto e desquantiza-mais-dot. O self-test DOS passou no guest QEMU, e
os testes de inferência e compatibilidade Q8/Q4 mantiveram os tokens: Q8_0
`411:e`, Q4_0 `402:ch`. Cada inferência quantizada executou 176.640 linhas pelo
caminho direto. O caminho de referência gerou os mesmos tokens.

## Benchmark A/B

Três amostras intercaladas, prompt de 30 tokens e um token gerado; o relógio
DOS usado pelo teste foi 1000 ticks/s.

| Modo | Amostras (ticks) | Mediana | Comparação |
|---|---:|---:|---|
| F32 | 6152, 6262, 6262 | 6262 | referência de contexto |
| Q8_0 direto | 6481, 6481, 6591 | 6481 | 1,61x mais rápido que Q8_0 legado; 3,5% mais lento que F32 |
| Q8_0 legado | 10271, 10436, 10491 | 10436 | desquantização por linha + matvec F32 |
| Q4_0 direto | 6316, 6372, 6426 | 6372 | 1,63x mais rápido que Q4_0 legado; 1,8% mais lento que F32 |
| Q4_0 legado | 10271, 10381, 10381 | 10381 | desquantização por linha + matvec F32 |

Portanto, o resultado medido é uma redução de latência versus o antigo caminho
de quantização no mesmo guest. Ele ainda não demonstra que as inferências
Q8_0/Q4_0 ultrapassem a latência F32, nem prova desempenho de um Am486 físico.

## Ambiente e identidade dos dados

- QEMU 11.1.0, modelo genérico `486`, TCG em thread única, 96 MiB, NF-DOS e
  DOS/4GW. O modelo de CPU não é uma emulação identificada como AMD
  Am486DX4 exata.
- Tiny1M; prompt `Tom and Jerry are `; 30 tokens de entrada; um token de saída.
- SHA-256 dos modelos: F32
  `60FA2675B0531735C9724C9FCB871BAA6E40594F451D1454981034BE47D025D4`, Q8_0
  `7A85BD49EF670386B212B394F0FC46945D77A35C7BB41327E504A3F6FA43EA14`, Q4_0
  `E2F0028E7E53A5D9B17C2FA5DA168DF9E03D3A1883C6A5FAFB304677B38D02BE`.
- O disco original de NF-DOS permanece com SHA-256
  `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`.
  O ensaio usou um clone independente, SHA-256
  `02107603F14BFE7B30F706B9ABB37575BCEFB5F6B3E0CB1B98C8EA6C610AB508`.

## Evidências

- Resumo das amostras, tokens, hashes e ambiente:
  `build/logs/stage3-direct-q8q4-ab-summary-20261114.json`.
- Extração dos logs do guest:
  `build/logs/stage3-direct-q8q4-ab-guest-20261114/`.
- AUTOEXEC preparado e verificado:
  `build/logs/stage3-direct-q8q4-ab-autoexec-20261114.json`.
- Self-test host:
  `build/logs/stage3-direct-dot-host-selftest-20261114.log`.
- Código e bibliotecas: `plugins/q8_0/`, `plugins/q4_0/`,
  `build/plugins/q8_0_x87.lib`, `build/plugins/q4_0_x87.lib`.

## Extensões ISA posteriores

Ensaios posteriores adicionaram kernels MMX Q8_0/Q4_0 e SSE2 Q8_0/Q4_0 em
plugins estáticos separados. Eles passaram testes funcionais no proxy Athlon e
no Atom N270 isolado, respectivamente. Os benchmarks SIMD comparam a matriz
direta contra o kernel escalar direto por linha e estão em
`docs/BENCHMARKS-ETAPA3.md`; não são uma comparação com o caminho de
desquantização descrito neste arquivo. O teste SSE2 habilitou OSFXSR somente em
um clone QEMU; isso não estabelece suporte geral a XMM por DOS/DPMI.
