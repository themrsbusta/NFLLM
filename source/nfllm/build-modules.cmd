@echo off
rem Optional Open Watcom object and static-library inputs for additional modules.
rem The built-in reference backend is packaged separately as build\plugins\f32_x87.lib.
if not defined NFLLM_EXTRA_OBJECTS set "NFLLM_EXTRA_OBJECTS="
if not defined NFLLM_EXTRA_LIBRARIES set "NFLLM_EXTRA_LIBRARIES="
if not defined NFLLM_REGISTRY_MAX_PLUGINS set "NFLLM_REGISTRY_MAX_PLUGINS=8"
if not defined NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN set "NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN=8"
set "NFLLM_CONFIG_FLAGS=-dNFLLM_REGISTRY_MAX_PLUGINS=%NFLLM_REGISTRY_MAX_PLUGINS% -dNFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN=%NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN%"
exit /b 0
