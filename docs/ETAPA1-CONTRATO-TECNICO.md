# NFLLM 0.2 Beta — contrato técnico da etapa 1

**Estado:** aprovado pela Mica em 2026-11-14, após revisão final com confiança alta. Consulta: `nfllm-stage1-final-contract-review-20261114`.
**Alvo principal:** NF-DOS 5.02, DOS/4GW, 386 ISA mínima e x87 no AMD Am486DX4.
**Gate:** nenhuma ampliação da implementação até a Mica aprovar esta proposta.

## 1. Fronteira matemática

`nfllm_logic` é uma referência inteira de 32 bits sem dependência de FPU ou de
extensões de ISA. As funções booleanas operam sobre palavras `uint32_t`. As
operações derivadas têm estas semânticas:

| Operação | Resultado | Erro/limite |
|---|---|---|
| AND, OR, XOR, NOT | Operação bit a bit em 32 bits | Nenhum |
| SHL, SHR | Deslocamento lógico; o bit descartado não é sinal estendido | Contagem de 0 a 31; caso contrário `NFLLM_E_SHIFT_RANGE` e saída zero |
| ADD | 32 bits baixos, módulo 2^32 | `NFLLM_E_OVERFLOW` se houver carry |
| SUB | 32 bits baixos, módulo 2^32 | `NFLLM_E_OVERFLOW` se houver borrow/underflow |
| MUL | 32 bits baixos, módulo 2^32 | `NFLLM_E_OVERFLOW` se o produto matemático exceder 32 bits |
| DIV u32 | Quociente e resto sem sinal | Divisor zero retorna `NFLLM_E_DIV_ZERO` e zera ambas as saídas |
| Comparação u32 | `-1`, `0` ou `1` | Sem interpretação signed |
| u32 → bits F32 | IEEE-754 binary32, arredondamento para o mais próximo, empate para par | Conversão de representação; não é aritmética F32 |
| bits F32 → u32 | Truncamento para zero | Negativo não nulo e NaN/Inf retornam `NFLLM_E_NUMERIC`; fora da faixa retorna `NFLLM_E_OVERFLOW` |

As rotinas retornam `NFLLM_E_ARGUMENT` para ponteiro de saída nulo. Exceto nos
casos em que a tabela define saída zero, um erro não autoriza o chamador a usar
um resultado parcial. Os tipos unsigned e as operações bit a bit evitam
overflow signed indefinido.

Esta camada implementa operações inteiras e conversões de bits. Ela **não**
implementa soma, multiplicação, divisão, exponencial, raiz, seno, cosseno ou
potência em F32 por portas lógicas. O compilador pode traduzir operadores
booleanos para instruções escalares da ISA 386; a implementação não simula
transistores nem exige MMX, SSE, 3DNow! ou AVX.

## 2. Fronteira da inferência F32

O backend `reference-x87` preserva a matemática F32/x87 que já funciona no
NFLLM 0.1. A etapa 1 encaminha matvec, RMSNorm e RoPE por contratos de kernel;
seus corpos mantêm a ordem das operações anterior. O restante da inferência,
incluindo a matemática inline de atenção e ativação em `NFLLM.cpp`, continua no
caminho escalar existente.

Portanto, logs e documentação devem identificar explicitamente `reference-x87`
como backend de compatibilidade. Nunca chamar o conjunto da inferência de
“aritmética implementada por portas lógicas”. Testes de equivalência comparam os
três kernels com as fórmulas da versão 0.1; testes DOS com TinyStories verificam
que a integração preserva os IDs dos tokens nos cenários de regressão.

## 3. ABI C v1

- ABI em processo, prioritariamente para DOS/4GW x86 de 32 bits com Open Watcom.
- Campos escalares de contratos usam larguras fixas; ponteiros usam a largura
  nativa do alvo e não são formato de arquivo ou protocolo de rede.
- Cada struct pública começa por `struct_size` e `abi_version`. A versão precisa
  ser exatamente 1; o tamanho precisa ser pelo menos o tamanho conhecido pelo
  host.
- Cabeçalho C compatível com C++; a fronteira exportada é C, sem exceptions,
  RTTI ou layout de classes C++.
- Callbacks retornam `nfllm_status_v1`. A revisão deve explicitar e aplicar a
  convenção de chamada C do alvo (`__cdecl` no Open Watcom) em todos os tipos de
  função da ABI.
- Descritores, nomes e tabelas de kernels são emprestados e devem permanecer
  válidos e imutáveis até `nfllm_registry_shutdown`. O registry não os copia nem
  libera memória.
- O host copia a struct `host_api`, mas o ponteiro `context` é emprestado e deve
  continuar válido até o encerramento dos plugins.
- Ponteiros de argumentos pertencem ao chamador e só podem ser usados durante a
  chamada do kernel; o plugin não pode retê-los. Estado retornado por
  `initialize` pertence ao plugin, que o libera em `shutdown`. Se `initialize`
  falhar após alocar recursos, ele limpa seu estado parcial antes de retornar.
- A validação deve recusar versão/tamanho incorretos, IDs duplicados, callbacks
  obrigatórios ausentes, tipos incompatíveis, requisitos de capacidade
  desconhecidos e dados desalinhados para `float`.

## 4. Registry, limites e seleção

- O registry mantém no máximo **8 plugins** e cada plugin pode registrar no
  máximo **8 kernels**: teto de **64 kernels registrados**.
- Esses limites são macros de compilação. O padrão é 8/8; alterá-los no
  cabeçalho e reconstruir aumenta a tabela e seu consumo de memória estática.
- Os descritores e arrays dos módulos continuam na memória estática do módulo;
  o registry mantém somente 8 entradas com ponteiros, contadores e estado. Não
  aloca nem redimensiona heap durante descoberta/registro.
- A tentativa de exceder qualquer limite retorna `NFLLM_E_FULL`, rejeita o
  registro inteiro e não trunca a tabela silenciosamente.
- O modo automático percorre a ordem determinística de registro, escolhe a
  primeira implementação que atende capacidades e contrato, e registra o
  motivo da seleção/fallback. Seleção manual exige o plugin solicitado; se ele
  não existir, não suportar a operação ou não atender capacidades, retorna erro
  sem fallback silencioso.
- O alvo declara capacidades 386+x87 de forma explícita. O NFLLM não enumera
  capacidades via CPUID; configurações que não declarem x87 não podem selecionar
  o backend `reference-x87`. O probe tentou alterar EFLAGS.ID sob DOS/4GW, não
  observou mudança e não executou CPUID; essa limitação do probe não prova que o
  processador carece de CPUID.
- Testes de fronteira devem preencher 8 entradas e 8 kernels, confirmar sucesso,
  tentar a nona entrada e o nono kernel, confirmar `NFLLM_E_FULL` e verificar
  que os registros anteriores continuam íntegros.

## 5. Vinculação estática e lifecycle

O ciclo é `initialize registry → register modules → validate → resolve →
execute → shutdown em ordem inversa`. Cada módulo futuro deve fornecer seu
registro explícito em `nfllm_builtin_plugins.c`. `build-modules.cmd` configura
objetos e bibliotecas estáticas adicionais. Não há carregador dinâmico nesta
etapa e nenhum plugin especializado `.lib` será entregue nela.

## 6. Evidência exigida antes de concluir

1. Self-tests DOS para semântica inteira, conversões, capacidades, erros ABI,
   fallback e os limites 8/8.
2. Equivalência bit a bit de matvec, RMSNorm e RoPE contra as fórmulas de 0.1.
3. Rebuild Open Watcom modular e execução dos self-tests em NF-DOS no 86Box.
4. Regressões GGUF válido, inválido e truncado e inferências TinyStories com IDs
   esperados no Am486DX4; confirmar que o disco de sistema original permanece
   inalterado.
5. Revisão final da Mica e atualização de `progresso.yaml` com evidência real.

Nenhum dos itens desta lista é declarado aprovado apenas por compilar o rascunho.

## 7. Aprovação e evidência de validação

A Mica aprovou o contrato v4 e confirmou que os critérios técnicos da etapa 1 estão atendidos. A revisão considerou os limites 8×8 do registry, a recusa do nono plugin/kernel, a preservação dos registros aceitos, as equivalências bit a bit de matvec/RMSNorm/RoPE e a execução dos testes no 86Box.

- Relatório completo do ensaio 86Box: `NFLLM/build/logs/stage1-86box-acceptance-20261114.json`.
- Cross-check separado no QEMU: `NFLLM/build/logs/stage1-qemu-acceptance-20261114.json`.
- Perfil validado: 86Box 6.0 build 9001, `486sp3c`, `am486dx4`, 100 MHz, 98304 KiB.
- O probe não observou mudança em EFLAGS.ID no contexto DOS/4GW e não executou CPUID; isso não confirma ausência de CPUID. O modelo do processador vem da configuração do 86Box. O probe x87 retornou 5.25.
- O hash do HD original permaneceu `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`.
