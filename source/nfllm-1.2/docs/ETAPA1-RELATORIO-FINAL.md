# NFLLM 0.2 Beta — Relatório final da Etapa 1

**Estado:** concluída e aprovada pela Mica em 14/11/2026 (consulta `nfllm-stage1-final-contract-review-20261114`, confiança alta).

## Objetivo entregue

A etapa implementa a base lógica independente de extensões de CPU, a ABI C v1 e o registry/dispatcher estático para futuras implementações. A inferência F32/x87 de compatibilidade do NFLLM 0.1 foi preservada. Nenhum plugin especializado `.lib` nem carregador dinâmico foi criado.

## Arquitetura

- `src/nfllm_logic.c` e `.h`: primitivas booleanas e operações u32 determinísticas; limites/erros definidos para shifts, overflow, underflow e divisão por zero; conversões entre u32 e bits IEEE-754 binary32.
- `src/nfllm_abi.h`: contratos `nfllm_host_api_v1`, `nfllm_plugin_descriptor_v1`, `nfllm_kernel_descriptor_v1`, `nfllm_quant_descriptor_v1` e `nfllm_extension_hooks_v1`; fronteira C, campos versionados, tipos fixos e `__cdecl` no Open Watcom.
- `src/nfllm_registry.c/.h` e `src/nfllm_builtin_plugins.c`: registry estático com limite padrão de 8 plugins × 8 kernels por plugin, validação e lifecycle explícito.
- `src/nfllm_reference_kernels.c`: backend de compatibilidade x87 para matvec, RMSNorm e RoPE.
- `NFLLM.cpp`: integração gradual pelo dispatcher, preservando o caminho de inferência F32 existente; seleção automática com fallback e seleção manual diagnóstica.
- `tests/nfllm_selftest.c`: testes unitários de lógica, ABI, limites, lifecycle, dispatcher e equivalência.
- `build-dos.cmd` e `build-modules.cmd`: build DOS/Open Watcom modular para vinculação estática.

## Validação

### Build e limites

Open Watcom compilou `NFLLM.EXE`, `NFTEST.EXE` e `COMPAT.EXE`. Os SHA-256 de `NFLLM.EXE`, `NFTEST.EXE`, `COMPAT.EXE`, `DOS4GW.EXE` e `TRUNC.GGF` foram relidos do clone FAT16 e coincidiram com os arquivos de origem.

O self-test aceitou os 8 plugins com 8 kernels cada, recusou o nono kernel e o nono plugin com `NFLLM_E_FULL`, e confirmou que os registros previamente aceitos continuaram resolvendo. As comparações por bits (`memcmp`) de matvec, RMSNorm e RoPE contra NFLLM 0.1 passaram. Também passaram os vetores de lógica u32, conversões IEEE-754, erros de ABI, fallback, seleção manual, lifecycle e validação de argumentos.

### NF-DOS no 86Box

A execução final ocorreu no 86Box 6.0 build 9001 com `machine=486sp3c`, `cpu_family=am486dx4`, 100 MHz, multiplicador 3 e 98304 KiB. O clone FAT16 começou em SHA-256 `5CB82A739CDAA8CF1F652250BD67D205B6840308A0E1DD7527953187FAB44C03` e terminou em `0372712AF0EF066E56C2618F5CC92F68263C43C30A6414E173BA566D5227FE5B`.

Os marcadores recém-gravados após o início da VM confirmam o término do AUTOEXEC: `SELFTEST.OK=SELFTEST_PASS`, `COMPAT.OK=COMPAT_PASS`, `INFER.OK=INFER_PASS`, `MANUAL.OK=MANUAL_PASS`, `T1COMPAT.OK=COMPAT_PASS`, `T1INFER.OK=INFER_PASS`, `MISS.OK=EXPECTED_ERROR2`, `TRUNC.OK=EXPECTED_ERROR2` e `NFLLM.DON=NFLLM_TEST_FINISHED`.

- `STORIES.GGF` (1.185.376 bytes): gerou 1 token, id 426 (`.`); seleção manual `reference` gerou o mesmo token.
- `TINY1M.GGF` (3.754.048 bytes): compatibilidade aprovada e 1 token gerado, id 411 (`e`).
- Probe de compatibilidade: DOS 7.10, x87 `5.25`, maior bloco DPMI reportado de 65.669.120 bytes. Sob DOS/4GW, o probe não observou mudança em EFLAGS.ID e, com segurança, não executou CPUID; isso não confirma ausência de CPUID no processador nem na VM. A AMD documenta CPUID no Enhanced Am486DX4; a disponibilidade no contexto DOS/4GW deste perfil permanece não verificada.
- Payload de heap estimado para Stories: 3.817.056 bytes; Tiny1M: 4.805.696 bytes. Esses valores excluem overhead do runtime/alocador.

O ensaio diagnóstico no QEMU 11.1.0 com `-cpu 486` e 96 MiB também passou, mas é registrado separadamente e não substitui o resultado 86Box.

## Integridade e limitações

O HD de sistema original manteve o SHA-256 baseline `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`; todos os testes ocorreram em clones. O probe não identificou o fabricante porque não executou CPUID; o modelo exato do ensaio é comprovado pelo perfil do 86Box. O executável NFLLM não usa CPUID e declara o perfil 386+x87 manualmente. Não houve execução em hardware físico. A captura visual do 86Box falhou, por isso a evidência final usa os marcadores e logs FAT16 relidos após a execução. A VM foi encerrada após o lote; como o fechamento normal não terminou o processo, o encerramento foi confirmado pelo PID exato e o disco foi lido/hashado depois.

## Interfaces e preparação futura

As interfaces C v1 e o registro estático estão prontos para módulos vinculados estaticamente. A etapa 1 não inclui plugins especializados `.lib`; o backend de referência continua disponível para manter a inferência funcional enquanto extensões futuras forem escolhidas.

## Evidências

- `build/logs/stage1-86box-acceptance-20261114.json` — logs completos, marcadores, configuração e hashes do ensaio 86Box.
- `build/logs/stage1-qemu-acceptance-20261114.json` — cross-check separado.
- `handoffs/progresso.yaml` — progresso e decisão da Mica.
