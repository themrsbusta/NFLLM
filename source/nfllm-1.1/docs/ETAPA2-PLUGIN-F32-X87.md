# Etapa 2 — primeira biblioteca F32/x87

## Objetivo

Empacotar o backend escalar F32/x87 já validado na etapa 1 como uma biblioteca
estática Open Watcom independente e vinculá-la ao host NFLLM e ao self-test,
sem alterar a ABI C v1 nem a ordem matemática do backend.

## Conteúdo

- Fonte: `src/nfllm_reference_kernels.c`.
- Arquivo produzido: `build/plugins/f32_x87.lib` (formato OMF).
- Registro estático: `nfllm_register_reference_plugin()` chamado pelo módulo
  built-in durante a inicialização do host.
- Manifesto: `plugins/f32_x87/manifest.yaml`, copiado para o diretório do
  artefato no build.
- Seleção manual: `--backend reference`; seleção automática continua usando a
  ordem determinística do registry.
- Requisitos declarados: ISA 386 e x87. O host declara capacidades manualmente
  e não enumera a CPU via CPUID. O probe sob DOS/4GW não observou mudança em
  EFLAGS.ID e não executou CPUID; isso não prova que o Am486DX4 carece da
  instrução. A disponibilidade de CPUID no contexto do guest segue não
  verificada.

## Critérios de verificação

1. WLIB cria um archive OMF novo contendo o objeto do plugin.
2. `NFLLM.EXE` e `NFTEST.EXE` vinculam o `.lib`, sem adicionar diretamente o
   objeto do plugin na linha de link; mapas de link devem registrar os símbolos.
3. Self-test DOS confirma seleção automática/manual, rejeição de plugin
   desconhecido ou incompatível e igualdade bit a bit das três rotinas em
   relação às fórmulas de NFLLM 0.1.
4. Guest limpo no 86Box confirma self-test, compatibilidade e token esperado por
   inferência real usando seleção manual; a execução automática é comparada.
5. Nenhuma alegação de aceleração é feita sem benchmark medido nas mesmas
   condições. Este marco extrai o backend existente e não é uma otimização;
   portanto, ganho de desempenho não é critério de aceitação funcional.

O manifesto não é carregado em runtime. A implementação atual usa vínculo e
registro estáticos. Essa primeira biblioteca não implementa Q8_0, Q4_0, MMX,
3DNow! ou carregamento dinâmico.

## Resultado da validação incremental

Em 2026-11-14, a biblioteca foi ligada e executada no perfil 86Box 6.0 build
9001 / 486sp3c / Am486DX4 100 MHz / 96 MiB. O build e os mapas confirmam o
archive no caminho de link dos dois executáveis. O guest aprovou o self-test,
seleção automática e manual, equivalência dos kernels e inferência real:
Stories token 426 (`.`) e Tiny1M token 411 (`e`). O log inclui as falhas
esperadas de arquivo ausente/truncado e termina com `NFLLM.DON`.

Evidência detalhada: `build/logs/stage2-f32-x87-acceptance-20261114.json`;
logs FAT16 extraídos em modo somente leitura:
`build/logs/guest-stage2-f32-x87-20261114/`. A licença do plugin continua
explicitamente não declarada nos arquivos do projeto; o manifesto não atribui
uma licença por inferência. O host NFLLM não executa CPUID, e os testes não
alegam aceleração. A limitação do probe EFLAGS.ID está descrita em
`ETAPA2-RELATORIO-FINAL.md`.
