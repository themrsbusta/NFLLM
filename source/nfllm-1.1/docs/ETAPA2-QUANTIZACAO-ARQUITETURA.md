# Etapa 2 — integração Q8_0/Q4_0

**Estado:** implementada e validada no guest 86Box Am486DX4/96 MiB; aprovação
final da Mica da etapa 2 está pendente.

## Contrato implementado

- Q8_0 e Q4_0 têm IDs, estruturas de argumentos e operações ABI v1 separados.
- `q8_0_x87.lib` e `q4_0_x87.lib` são arquivos OMF independentes; cada um
  registra apenas o decodificador de linha do seu formato. O vínculo é estático.
- O parser preserva o tipo e o tamanho compactado de cada tensor GGUF. A
  aplicação valida dimensões, blocos, limites e tamanho do arquivo antes de
  acessar os pesos. Este recorte aceita matrizes 2D e embeddings Q8_0/Q4_0 com
  dimensão de linha divisível por 32; vetores de RMSNorm permanecem F32.
- A inferência decodifica uma linha de pesos por vez para até 1024 floats e
  chama o matvec F32 de referência. A linha de embedding é processada pelo
  mesmo decodificador. O arquivo completo não é expandido para F32 na RAM.
- `--backend auto` e `--backend reference` selecionam a aritmética F32. O tipo
  GGUF seleciona a operação do decodificador. Um formato desconhecido é
  rejeitado. Ausência de decoder compatível retorna erro explícito no caminho
  do loader; dados compactados não são reinterpretados como `float`.
- Modelos F32 continuam executáveis sem depender de Q8_0/Q4_0.

## Verificações realizadas

- Build Open Watcom criou as três bibliotecas independentes e os executáveis
  NFLLM/NFTEST; os mapas e listagens confirmam o vínculo dos archives.
- Self-tests dos decodificadores verificam valores assinados, escalas half
  normais, zero e subnormais, nibbles Q4, contrato ABI, limites e rejeição de
  escalas não finitas.
- Em dois boots limpos de clones no 86Box 6.0 build 9001, passaram self-test,
  compatibilidade, inferência F32/Q8_0/Q4_0, seleção manual do backend F32,
  erro esperado para modelo ausente/truncado e marcador final de execução.
- Tiny1M F32 e Q8_0 produziram token 411 (`e`); Q4_0 produziu token 402
  (`ch`). A diferença para Q4_0 é resultado quantizado, não falha de execução.
- A estimativa de payload de heap, sem overhead do DOS runtime/alocador, foi
  4.805.696 B em F32, 2.062.400 B em Q8_0 e 1.595.456 B em Q4_0.
- Um benchmark intercalado de 3 amostras mediu uma geração de token após o
  mesmo prompt de 30 tokens. Medianas: F32 11.479 ticks (11,479 s), Q8_0
  32.461 ticks (32,461 s), Q4_0 29.989 ticks (29,989 s), com relógio de
  1000 Hz. Q8_0 levou 2,83× e Q4_0 2,61× a latência F32. A desquantização
  por linha seguida de matvec F32 reduz payload, mas não acelera esta rota.

Evidências completas, hashes e limitações estão em
[`ETAPA2-RELATORIO-FINAL.md`](ETAPA2-RELATORIO-FINAL.md).

## CPU e ISA

O build usa baseline 386 e x87; as instruções especializadas não são
obrigatórias. Não foi criado plugin MMX, 3DNow!, SSE ou posterior: essas ISAs
não pertencem ao Am486DX4 usado na aceitação. A documentação Open Watcom
consultada descreve `-4` em target 32-bit como geração de código 386 com
premissas de temporização 486, não como um backend que acrescente instruções
486. Nenhuma melhoria de desempenho foi presumida.

O NFLLM não enumera CPU via CPUID; declara 386+x87 manualmente. O probe tentou
alternar EFLAGS.ID sob DOS/4GW, não observou mudança e não executou CPUID. A
AMD documenta CPUID para Enhanced Am486DX4 e o código 86Box v6.0 contém a
implementação do modelo `CPU_ENH_Am486DX`; portanto, a saída antiga “CPUID não
disponível neste CPU” era excessiva e foi corrigida. A causa da diferença do
probe no contexto DOS/4GW permanece não verificada.

## Limites

Esta validação prova os caminhos F32/Q8_0/Q4_0 no modelo Tiny1M, no perfil
configurado de 86Box. Não prova suporte à implementação upstream completa do
`llama.cpp`, outros tipos GGML, outras arquiteturas, todos os layouts GGUF ou
execução em Am486 físico. Licença do código do plugin segue não declarada nas
fontes do projeto. Não foi feita publicação.
