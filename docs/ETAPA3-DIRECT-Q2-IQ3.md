# Etapa 3 — matvec packed direto Q2/IQ3

**Status:** kernels, inferência de um token e benchmark comparativo validados no
QEMU 486 genérico. O efeito medido depende do formato; não há um ganho uniforme.

## Implementação

As bibliotecas estáticas `q2_x87.lib` e `iq3_x87.lib` agora registram operações
ABI C v1 de matvec por linha para os quatro formatos solicitados:

| Formato | Elementos/bloco | Bytes/bloco | Dequant operação | Matvec packed |
|---|---:|---:|---:|---:|
| Q2_0 | 64 | 18 | `0x203` | `0x303` |
| Q2_K | 256 | 84 | `0x204` | `0x304` |
| IQ3_XXS | 256 | 98 | `0x205` | `0x305` |
| IQ3_S | 256 | 110 | `0x206` | `0x306` |

O kernel percorre os dados compactados, reconstrói cada peso escalar e o
consome imediatamente no produto interno com a ativação F32. Assim, evita
materializar o vetor F32 temporário do caminho legado. A implementação é C
escalar com requisitos 386+x87; não usa MMX, 3DNow! ou SSE.

Q2_0 extrai quatro pesos de 2 bits por byte e aplica `(q - 1) * d`. Q2_K
combina os quants de 2 bits com os subscales e submínimos por grupo, mais os
fatores FP16 `d` e `dmin`. IQ3_XXS e IQ3_S usam as tabelas de grid licenciadas
no código de referência GGML, aplicando índices, sinais e escalas diretamente
ao vetor de entrada. IQ3_S também incorpora os bits altos dos índices e as
escalas por subgrupo.

As quatro rotas validam tamanho, alinhamento, versão da ABI e todos os fatores
FP16 do row antes de escrever a saída. A opção `--quantized-matvec decode`
desliga os caminhos diretos e força o decodificador por linha seguido do
matvec F32; `direct` é o padrão e usa o caminho packed quando registrado.

## Validação funcional

- `NFLLM/build-dos.cmd` compilou os alvos DOS modulares, incluindo Q2, IQ3,
  MMX, 3DNow!, SSE2, F32/x87, Q8_0 e Q4_0.
- A comparação diferencial dos decodificadores Q2/IQ3 contra GGML registrou
  erro máximo zero nos quatro formatos em
  `NFLLM/build/logs/q2-iq3-differential/summary.json`.
- O self-test no QEMU 11.1, CPU genérica `486`, TCG single-thread e 96 MiB
  passou os vetores diretos de Q2_0, Q2_K, IQ3_XXS e IQ3_S, a ordem de grids,
  sinais, escalas e a rejeição de fatores FP16 não finitos tardios sem escrita
  parcial da saída. Também verificou ABI, tamanho e operação desconhecida.
- Inferências Tiny1M de um token passaram nos quatro formatos no mesmo perfil.
  Cada log reporta 291.840 linhas executadas pelo kernel direto e o token da
  referência anterior:

| Formato | Token | Texto | Ticks do primeiro ensaio |
|---|---:|---|---:|
| Q2_0 | 413 | `t` | 25.980 |
| Q2_K | 414 | `o` | 33.834 |
| IQ3_XXS | 317 | ` Lily` | 38.063 |
| IQ3_S | 353 | ` on` | 36.910 |

Os ticks dessa tabela vêm de uma execução por formato e servem como evidência
funcional, não como benchmark comparativo. O primeiro AUTOEXEC de aceitação
terminou com falha no harness do self-test, que ainda não inicializava o
dispatcher de referência. O harness foi corrigido; a execução isolada
posterior passou. Os logs de inferência daquela execução completa demonstram
os quatro tokens e os contadores diretos, mas o resumo do runner daquela
tentativa permanece marcado como falho e não é apresentado como aprovação da
aceitação completa.

## Benchmark A/B

O ensaio usa três amostras de cada rota, com a ordem alternada por repetição,
prompt `Tom and Jerry are `, um token gerado e o relógio DOS em 1000 ticks/s.
Cada par produziu o mesmo token. A rota direta reportou linhas packed positivas;
`decode` reportou zero linhas diretas, como esperado.

| Formato | Direto (ticks) | Decode + F32 (ticks) | Mediana direta | Mediana fallback | Relação decode/direto |
|---|---|---|---:|---:|---:|
| Q2_0 | 29495, 29440, 29604 | 26365, 26858, 26748 | 29495 | 26748 | 0,907 |
| Q2_K | 33669, 34164, 33944 | 34328, 34548, 34493 | 33944 | 34493 | 1,016 |
| IQ3_XXS | 37789, 37953, 38009 | 42128, 42293, 42348 | 37953 | 42293 | 1,114 |
| IQ3_S | 36800, 36800, 36800 | 40974, 41029, 41139 | 36800 | 41029 | 1,115 |

Nestas amostras, o matvec direto de Q2_0 foi cerca de 10,3% mais lento que o
fallback. Q2_K foi cerca de 1,6% mais rápido; IQ3_XXS e IQ3_S foram cerca de
10,2% e 10,3% mais rápidos. São medições deste guest e desta carga; não provam
ganho em hardware AMD real nem aceleração geral para outros modelos.

O QEMU concluiu a sequência e terminou normalmente. A primeira validação do
runner recusou o log `decode` porque a linha informativa do modo era impressa
antes de processar os argumentos. Os 24 logs foram validados separadamente por
status, token, ticks e contador de linhas, junto com os argumentos registrados
no AUTOEXEC. O diagnóstico foi movido para depois do parser de argumentos.

## Ambiente e limites

- Guest: NF-DOS/DOS-4GW no QEMU 11.1.0, CPU genérica `486`, TCG em uma thread,
  96 MiB. O perfil não identifica nem reproduz completamente um AMD Am486DX4
  físico.
- O ensaio usa Tiny1M reempacotado em cada tipo GGUF; prompt de 30 tokens e
  geração de um token.
- Nenhum resultado de hardware físico foi obtido.
- Nenhuma inferência Q2/IQ3 foi executada em 86Box nesta etapa incremental;
  as evidências desta implementação direta são do QEMU genérico.
- O disco original NF-DOS continua inalterado, SHA-256
  `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`.

## Evidências

- Self-test QEMU: `NFLLM/build/logs/stage3-q2-iq3-direct-selftest-v4/`.
- Inferências e regressões coletadas: `NFLLM/build/logs/stage3-q2-iq3-direct-486-v4/`.
- Ensaio A/B e resumo validado: `NFLLM/build/logs/stage3-q2-iq3-direct-benchmark-v4/packed-quant-benchmark-reviewed-summary.json`.
- Kernels e manifests: `NFLLM/plugins/q2/`, `NFLLM/plugins/iq3/`.
