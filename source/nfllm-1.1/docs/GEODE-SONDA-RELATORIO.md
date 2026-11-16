# NFLLM — Relatório da sonda de emulação Geode

**Data:** 14/11/2026  
**Resultado:** nível 1 — **PROXY_VIAVEL**  
**Etapa 3:** autorizada por Busta em 14/11/2026 pelo handoff schema_version 2; esta sonda permanece como evidência prévia, sem provar execução dos plugins planejados.

## Objetivo e escopo

A sonda avaliou se os emuladores disponíveis oferecem um perfil Geode utilizável e se o NF-DOS e o NFLLM atual executam com 96 MiB de RAM convidada. Um processador genérico com recursos ativados foi classificado apenas como proxy, nunca como Geode emulado.

Não alterei código-fonte do NFLLM, ABI, plugins ou o disco original. O QEMU iniciou uma cópia descartável do disco de aceitação. Antes da inicialização, apaguei somente as entradas antigas dos marcadores de resultado nessa cópia para exigir evidência nova.

## Descobertas sobre os emuladores e as CPUs

A instalação local do QEMU 11.1.0 não lista Geode no resultado de qemu-system-i386.exe -cpu help. O código-fonte x86 do QEMU também não define um modelo chamado Geode. O código do TCG, porém, inclui os recursos MMX, 3DNow! e 3DNow! estendido, e o QEMU permite ativar recursos individuais em modelos nomeados. Isso permite usar um perfil aproximado, mas o Athlon usado neste teste não é um Geode. Consulte o [código x86 do QEMU 11.1](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/target/i386/cpu.c) e a [documentação de modelos e recursos de CPU do QEMU](https://www.qemu.org/docs/master/system/i386/cpu.html).

O código de CPU do 86Box 6.0 contém um caminho chamado CPU_CxGX1, mas o arquivo inspecionado não o identifica como AMD Geode. No caminho Cyrix compartilhado, o código ativa a flag MMX para Cx6x86MX, não para CxGX1. Portanto, essa fonte não comprova um perfil Geode GX1 fiel. Não inicializei NF-DOS sob CPU_CxGX1 neste ensaio. Consulte a [implementação de CPU do 86Box 6.0](https://raw.githubusercontent.com/86Box/86Box/v6.0/src/cpu/cpu.c).

A AMD documenta conjuntos de instruções diferentes para Geode GX1 e LX. A tabela CPUID do GX1 marca MMX e não documenta 3DNow!; a do LX lista MMX, AMD MMX Extensions, 3DNow! e 3DNow! Extensions. Não se deve tratar GX1 e LX como o mesmo alvo. Fontes: [AMD Geode GX1 Data Book](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/datasheets/goede_gx1_databook-rev5.pdf) e [AMD Geode LX Data Book](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/datasheets/33234H_LX_databook.pdf).

## Execução de NF-DOS/NFLLM no proxy

Inicializei uma cópia FAT16 descartável de 528.482.304 bytes no QEMU TCG:

- QEMU 11.1.0-12130-ge470268ff4, máquina pc.
- CPU apresentada: athlon-v1 com flags mmx, 3dnow e 3dnowext.
- RAM convidada: 96 MiB.
- Disco de boot: clone raw em IDE.
- QEMU encerrou com código 0 após receber quit pelo QMP em loopback.
- O log de CPUID do convidado identificou AuthenticAMD, família 6, modelo 2, stepping 3. Esta é a identidade do proxy, não um Geode.

Depois de encerrar o QEMU, o leitor FAT16 confirmou marcadores recém-gerados:

- SELFTEST.OK: SELFTEST_PASS.
- T1COMPAT.LOG: DOS 7.10, FPU presente e maior bloco livre DPMI de 65.669.120 bytes.
- T1INFER.OK: INFER_PASS; Tiny1M gerou token ID 411, texto e.
- INFER.OK: INFER_PASS; o modelo de aceitação chamado STORIES.GGF gerou token ID 426, texto ponto.
- NFLLM.DON: NFLLM_TEST_FINISHED.

O log do NFLLM identifica o caminho como F32 escalar, baseado em ISA 386 e x87; a seleção de backend não depende de CPUID. A execução demonstra que esse caminho escalar existente e a suíte de aceitação do NF-DOS funcionam com 96 MiB sob o perfil Athlon proxy. Ela não executa instruções MMX ou 3DNow! e não valida um plugin específico para Geode.

## Integridade do disco e do processo

- SHA-256 do disco original antes: 32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3.
- SHA-256 do disco original depois: 32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3.
- SHA-256 do clone de teste depois da execução: 1FCED20EC9F21CE0468E33D07755DF6F84666186949F5D4A4806E4716A2F06AA.
- A VM foi encerrada via QMP loopback e o processo foi registrado como encerrado.

## Classificação

| Nível | Significado | Resultado |
|---|---|---|
| 3 | Modelo Geode exato, ISA, sistema e testes de instrução | Não alcançado |
| 2 | Implementação parcial de Geode validada | Não alcançado |
| 1 | Proxy de CPU inicia o sistema e executa NFLLM | Alcançado |
| 0 | Nenhum proxy útil | Não se aplica |

**Conclusão: PROXY_VIAVEL, nível 1.** O perfil serve para continuar o desenvolvimento do NF-DOS e do caminho escalar NFLLM, mas não certifica compatibilidade com Geode GX1 ou LX. As instruções específicas MMX, 3DNow! e AMMX dos Geodes continuam sem teste.

A sonda foi executada antes da autorização da Etapa 3. O handoff schema_version 2 de Busta agora autoriza o início imediato; a sonda continua limitada ao proxy Athlon e não substitui as fases de ISA, plugins, inferência e regressão.

## Supervisão da Mica

Consultei a Mica pela ponte local verificada na tarefa nfllm-geode-probe-review-20261114. Ela respondeu com confiança baixa, alertou para distinguir GX1 de LX e recomendou verificar se os emuladores têm um modelo Geode específico antes de afirmar compatibilidade. Segui essa orientação conferindo a lista local de CPUs do QEMU, os códigos do QEMU 11.1 e 86Box 6.0, a documentação AMD e os logs do guest. A Mica não aprovou tecnicamente a fidelidade Geode e não fez piada nem usou ironia na resposta.

## Evidências locais

- NFLLM/build/logs/geode-probe-20261114/emulator-inventory.txt — versões instaladas e lista de CPUs do QEMU.
- NFLLM/build/logs/geode-probe-20261114/qemu-athlon-proxy-run.json — comando, processo e registro inicial; a leitura ao vivo do marcador falhou porque o QEMU bloqueou o disco.
- NFLLM/build/logs/geode-probe-20261114/qemu-athlon-proxy-postboot-results.json — resultados reconciliados após o encerramento.
- NFLLM/build/logs/geode-probe-20261114/qemu-athlon-proxy-extracted/extraction-evidence.json — arquivos FAT16 extraídos, conteúdo e hashes.
- NFLLM/build/logs/geode-probe-20261114/qemu-athlon-proxy-extracted/ — marcadores e logs do guest.
- emulators/AM486DX4-96MB/disks/nfdos502-geode-qemu-athlon-proxy-20261114.raw — clone descartável usado no teste.

O pedido QMP de recursos de CPU ficou inconclusivo: o cliente PowerShell enviou um BOM UTF-8 na primeira mensagem e o QEMU devolveu erro de parse. Não uso essa consulta como prova dos recursos; a inicialização do guest, o argumento de CPU registrado e o CPUID do guest sustentam somente a identificação do proxy Athlon.

## Referências técnicas primárias

- [AMD Geode GX1 Processor Data Book](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/datasheets/goede_gx1_databook-rev5.pdf)
- [AMD Geode LX Processor Data Book](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/datasheets/33234H_LX_databook.pdf)
- [Código de CPU do 86Box 6.0](https://raw.githubusercontent.com/86Box/86Box/v6.0/src/cpu/cpu.c)
- [Código x86 do QEMU 11.1](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/target/i386/cpu.c)
- [Configuração de CPU no QEMU](https://www.qemu.org/docs/master/system/i386/cpu.html)