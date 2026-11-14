# NFLLM 0.2 Beta — relatório técnico da etapa 2

**Escopo:** primeira biblioteca F32/x87 e os decodificadores quantizados Q8_0
e Q4_0 como plugins estáticos Open Watcom para DOS.

**Resultado técnico:** builds, integração do loader e inferências reais
passaram no perfil 86Box 6.0 build 9001 / ASUS 486SP3C / AMD Am486DX4 100 MHz /
96 MiB / NF-DOS 5.01.3314 / DOS/4GW 1.97. A Mica aprovou o aceite técnico
final em 14/11/2026, confiança alta, consulta
`nfllm-stage2-final-review-20261114`. A aprovação considera a limitação do
probe CPUID explicitamente documentada.

## Artefatos

- `build/plugins/f32_x87.lib`: kernels escalares F32, RMSNorm e RoPE.
- `build/plugins/q8_0_x87.lib`: decodificador Q8_0 por linha.
- `build/plugins/q4_0_x87.lib`: decodificador Q4_0 por linha.
- `build/NFLLM.exe` e `build/NFTEST.exe`: executáveis DOS; mapas de link
  registram as bibliotecas estáticas.
- `plugins/{f32_x87,q8_0,q4_0}/manifest.yaml` e READMEs descrevem ABI,
  requisitos, dependências, licença declarada e resultados medidos.
- `scripts/quantize_tiny1m_test_models.py` gera fixtures derivados do Tiny1M
  local sem sobrescrever o modelo F32 de origem.

Os archives de quantização permanecem separados e cada um fornece só uma
operação de decodificação. O loader mantém o modelo compactado em memória,
decodifica uma linha por vez e usa o matvec F32 de referência. O relatório da
arquitetura descreve o contrato completo.

## Build e testes do guest

`cmd /d /c .\build-dos.cmd` concluiu com sucesso usando Open Watcom. O guest
passou self-test, compatibilidade, regressão F32, inferência Q8_0 e Q4_0,
seleção manual de backend, erros esperados para arquivos ausentes/truncados e
marcador de término.

| Modelo | Bytes GGUF | SHA-256 | Payload de heap estimado* | Token gerado |
|---|---:|---|---:|---|
| Tiny1M F32 | 3.754.048 | `60FA2675B0531735C9724C9FCB871BAA6E40594F451D1454981034BE47D025D4` | 4.805.696 B | 411 (`e`) |
| Tiny1M Q8_0 | 1.010.752 | `7A85BD49EF670386B212B394F0FC46945D77A35C7BB41327E504A3F6FA43EA14` | 2.062.400 B | 411 (`e`) |
| Tiny1M Q4_0 | 543.808 | `E2F0028E7E53A5D9B17C2FA5DA168DF9E03D3A1883C6A5FAFB304677B38D02BE` | 1.595.456 B | 402 (`ch`) |

\* Inclui modelo, KV cache e auxiliares do teste; exclui overhead do runtime e
do alocador. A execução validou que o guest conseguiu alocar e usar cada
configuração; não é uma medição do pico total do processo.

## Benchmark comparável

Três amostras intercaladas por formato, mesmo prompt de 30 tokens, Tiny1M e um
token gerado; relógio DOS a 1000 Hz:

| Formato | Amostras (ticks) | Mediana | Latência vs. F32 |
|---|---|---:|---:|
| F32 | 11.424, 11.480, 11.479 | 11.479 ticks / 11,479 s | 1,00× |
| Q8_0 | 32.406, 32.461, 32.461 | 32.461 ticks / 32,461 s | 2,83× |
| Q4_0 | 29.989, 29.989, 29.990 | 29.989 ticks / 29,989 s | 2,61× |

Q8_0 e Q4_0 diminuem o tamanho do arquivo e o payload estimado de heap, mas a
rota atual é mais lenta porque converte cada linha para F32 antes da
multiplicação. Não há alegação de aceleração. Tiny1M é uma fixture pequena de
validação, não uma recomendação de modelo para uso geral.

## CPUID e compatibilidade

O probe tentou tornar EFLAGS.ID (bit 21) diferente e observá-lo de volta sob
DOS/4GW. Não observou a mudança e, por isso, não executou a instrução CPUID.
Esse resultado comprova somente a observação daquele probe naquele contexto;
não demonstra ausência de CPUID no Am486DX4.

A [nota oficial da AMD](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/application-notes/19720.pdf)
diz que Enhanced Am486 e Am5x86 suportam CPUID e recomenda testar a capacidade
por EFLAGS. O [código-fonte 86Box v6.0](https://github.com/86Box/86Box/blob/v6.0/src/cpu/cpu.c#L1888)
possui uma rotina para `CPU_ENH_Am486DX`. A [documentação Open Watcom](https://ftp.openwatcom.org/ftp/manuals/current/tools.pdf)
descreve a opção `-4` em target 32-bit como premissas de temporização 486,
mantendo instruções do baseline 386. A causa da divergência entre esses dados e
o resultado observado pelo probe DOS/4GW não foi isolada. Como o NFLLM não usa CPUID, o
dispatcher declara o perfil 386+x87 manualmente; o probe atualizado relata a
limitação sem afirmar ausência de recurso.

## ISA e limites do porte

O target é compilado com baseline 386 e x87. Não se criaram plugins MMX,
3DNow!, SSE ou posteriores porque não são recursos do Am486DX4 testado; o
estágio permite que plugins especializados sejam opcionais. A flag Open
Watcom `-4` em modo 32-bit mantém instruções 386 e ajusta expectativas de
temporização 486, portanto não fornece um novo conjunto de instruções útil
para um plugin ISA nesta etapa.

Esta entrega não é o porte completo do `llama.cpp`. Evidência cobre GGUF v3,
arquitetura `llama` e os modelos TinyStories/Tiny1M utilizados. Norms ficam em
F32; tensores quantizados suportados são Q8_0/Q4_0 em matrizes 2D e embeddings,
com linha múltipla de 32. Outros formatos, arquiteturas e layouts ainda são
rejeitados. Não houve teste em Am486 físico. Na data deste relatório, a
licença dos plugins ainda não estava declarada nos manifests. Para a
publicação 1.0, Busta declarou MIT para o código-fonte de autoria NFLLM; a
auditoria de proveniência continua pendente. Nenhuma publicação existia quando
este relatório foi redigido; a publicação atual está descrita em
`docs/releases/v1.0-experimental-research-release.md`.

## Evidências e supervisão

- F32: `build/logs/stage2-f32-x87-acceptance-20261114.json` e
  `build/logs/guest-stage2-f32-x87-20261114/`.
- Q8_0/Q4_0: `build/logs/stage2-q4q8-autoexec-20261114.json` e
  `build/logs/guest-stage2-q4q8-20261114/`.
- Benchmark: `build/logs/stage2-bench3-autoexec-20261114.json` e
  `build/logs/guest-stage2-bench3-20261114/`.
- As duas execuções quantizadas usaram clones isolados; o disco de sistema
  original preservou o SHA-256 baseline
  `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`.
- A Mica revisou a discrepância CPUID na tarefa
  `nfllm-stage2-cpuid-discrepancy-qa-20261114` e aprovou a etapa 2 na consulta
  `nfllm-stage2-final-review-20261114` (confiança alta; `requer_usuario=false`).
  Ela aceitou a limitação porque o probe não prova ausência de CPUID, NFLLM não
  depende dessa instrução, e a discrepância está explicitamente documentada.
- Os logs FAT16 existentes preservam a saída histórica da versão anterior do
  probe, cuja frase “não disponível” extrapolava a observação. O código-fonte,
  README, relatórios e resumos de aceite foram corrigidos; a rotina que decide
  se executa CPUID não mudou.
- Publicação no GitHub segue sob responsabilidade do Argos e exige autorização
  separada de Busta. O envio do pacote ao Argos por SSH não está incluído neste
  relatório técnico.
