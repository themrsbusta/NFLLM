# NFLLM — requisitos das etapas 1, 2 e 3

Este documento consolida os handoffs `handoffs/etapa1.yaml`,
`handoffs/etapa2.yaml` e `handoffs/etapa3.yaml`. Ele registra o escopo técnico
do protótipo NFLLM e não declara concluído o porte do llama.cpp upstream.

## Etapa 1 — núcleo e contratos

**Objetivo:** manter o NFLLM independente de extensões de CPU e preparar a
substituição de operações por plugins futuros.

Requisitos principais:

- Implementação de referência determinística para operações matemáticas usadas
  na inferência, com limites, overflow e precisão definidos.
- ABI C v1 versionada; registry, dispatcher, contratos de operações e
  diagnósticos.
- Seleção manual, validação de requisitos, rejeição de implementações
  incompatíveis e fallback para a referência.
- Build DOS com Open Watcom e DOS/4GW, preparado para vínculo estático.
- Preservar leitura GGUF, tokenização, inferência F32, geração de tokens e
  compatibilidade com o perfil 386+x87 do Am486.
- Validar primitivas, equivalência numérica, registry, dispatcher, fallback,
  arquivos GGUF inválidos/truncados e regressão DOS.
- Não criar plugins especializados `.lib` nesta etapa.

**Estado registrado no handoff da Etapa 3:** aprovada tecnicamente; núcleo,
ABI C v1, registry/dispatcher, fallback e inferência F32 foram levados adiante.

## Etapa 2 — bibliotecas estáticas

**Objetivo:** fornecer plugins `.lib` OMF independentes, com manifesto,
requisitos ISA, ABI v1, testes e documentação.

Requisitos principais:

- Vínculo estático e registro no build; seleção manual pelo usuário.
- F32/x87, Q8_0 e Q4_0 como primeiras bibliotecas; MMX, 3DNow! e outras ISAs
  quando viáveis no toolchain; formatos quantizados validados contra a
  referência.
- Preservar ABI, fallback e inferência sem plugins especializados.
- Testar registro, incompatibilidade de ISA, resultados numéricos, inferência
  com e sem plugins, comportamento DOS/486 e desempenho antes de alegar ganho.
- Documentar manifestos, requisitos, build, combinação e limitações dos plugins
  entregues. Não é necessário implementar todas as ISAs planejadas.

**Estado registrado no handoff da Etapa 3:** aprovada tecnicamente; as
bibliotecas F32/x87, Q8_0/x87 e Q4_0/x87 são listadas como entregues.

## Etapa 3 — compatibilidade Geode e Atom

**Objetivo:** validar no NF-DOS, com 96 MiB, kernels compatíveis com as ISAs
selecionadas sem exigir que QEMU reproduza a microarquitetura física.

| Perfil | Base ISA | Requisitos verificáveis | Não presumir |
|---|---|---|---|
| Geode GX1 | x86, x87, MMX | caminho escalar e kernels MMX | 3DNow!, SSE ou SSE2 |
| Geode LX | x86, x87, MMX, 3DNow! | MMX e 3DNow! testados separadamente | identidade Geode em proxy Athlon |
| Atom N270 | x86, x87, MMX, SSE2 | SSE2, kernels MMX reutilizáveis, requisitos do estado OSFXSR | extensões não exigidas pelo plugin |

Requisitos obrigatórios do aceite:

- Preservar o baseline Am486/486+x87, ABI C v1 e fallback.
- Manter NF-DOS e DOS/4GW funcionais nos laboratórios.
- Integrar plugins estáticos MMX, 3DNow! e SSE2 e comprovar inferência real.
- Comprovar seleção manual e recusa/fallback quando uma ISA ou estado do
  processador não é utilizável.
- Testar regressão 486, resultados numéricos e formatos GGUF cobertos.
- Comparar benchmarks nas mesmas condições, separar prompt de geração e não
  presumir aceleração.
- Documentar laboratórios, CPUs/proxies, bibliotecas, configuração, testes,
  benchmarks e limitações.

SSSE3 e AMD MMX Extensions são condicionais às instruções documentadas e
testáveis. Não há plugin SSSE3 nem `amd_mmx_ext.lib` nesta entrega. O MMX F32
permanece experimental: mostrou custo alto e sequência de tokens diferente em
geração longa. Os resultados aceitos do MMX são os kernels quantizados Q8_0 e
Q4_0.

## O que deve permanecer disponível para continuar o projeto

- O checkout local `NFLLM` e o repositório remoto
  [`themrsbusta/NFLLM`](https://github.com/themrsbusta/NFLLM). O remoto foi
  verificado somente por leitura; não foi alterado. O diretório local atual não
  contém `.git`.
- O disco principal `emulators/AM486DX4-96MB/disks/nfdos502-qemu.raw`, a
  configuração `machine/AM486DX4-96MB/86box.cfg`, ROMs e executável 86Box.
- QEMU e clones de referência reproduzíveis para cada laboratório: `nfdos502-nfllm-stage1-86box-20261114.raw` (Etapa 1), `nfdos502-nfllm-stage1-cboot-stage2-q4q8-20261114.raw` (Etapa 2), `nfdos502-nfllm-stage1-cboot-stage3-q2-iq3-86box-20261114.raw` (Etapa 3/86Box), `nfdos502-geode-qemu-athlon-proxy-20261114.raw` e `nfdos502-nfllm-stage1-cboot-20261114-dnow-r2.raw` (proxy Geode/3DNow!), e `nfdos502-nfllm-stage1-cboot-stage3-simd-atom-20261114.raw` (Atom N270).
- Fontes/build NFLLM, Open Watcom/DOS/4GW e Tiny1M F32/Q8_0/Q4_0 usados nas
  evidências. Os logs, resumos, hashes e extrações permanecem em
  `build/logs/`.
- `tools/llama.cpp`, HAME e Mica Bridge continuam úteis para o objetivo maior e
  para a supervisão solicitada. Ferramentas de Android, Ghidra e análise de APK
  foram preservadas porque podem servir a outros trabalhos.

## Limite do objetivo maior

NFLLM já é um protótipo DOS de inferência para um subconjunto de GGUF. A
Etapa 3 não equivale ao porte do llama.cpp upstream para DOS nem demonstra
suporte a todos os modelos. Essa continuação permanece como trabalho próprio.
