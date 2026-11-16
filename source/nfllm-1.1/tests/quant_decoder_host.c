#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/nfllm_registry.h"

static int read_file(const char *path, uint8_t **data, uint32_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *buffer;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 0; }
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return 0; }
    buffer = (uint8_t *)malloc((size_t)length);
    if (!buffer) { fclose(file); return 0; }
    if (fread(buffer, 1U, (size_t)length, file) != (size_t)length) {
        free(buffer); fclose(file); return 0;
    }
    fclose(file);
    *data = buffer;
    *size = (uint32_t)length;
    return 1;
}

int main(int argc, char **argv)
{
    struct nfllm_host_api_v1 host;
    uint8_t *packed = 0;
    uint32_t packed_bytes = 0U, columns;
    float *output = 0;
    nfllm_status_v1 status;
    FILE *file;

    if (argc != 5) return 2;
    columns = (uint32_t)strtoul(argv[2], 0, 10);
    if (columns == 0U || !read_file(argv[3], &packed, &packed_bytes)) return 3;
    output = (float *)malloc((size_t)columns * sizeof(float));
    if (!output) { free(packed); return 4; }
    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    status = nfllm_registry_initialize(&host);
    if (status == NFLLM_OK) status = nfllm_register_q2_plugin();
    if (status == NFLLM_OK) status = nfllm_register_iq3_plugin();
    if (status == NFLLM_OK && strcmp(argv[1], "Q2_0") == 0) {
        struct nfllm_q2_0_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args); args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed; args.packed_bytes = packed_bytes;
        args.columns = columns; args.output = output;
        status = nfllm_dispatch_dequantize_q2_0_row(&args);
    } else if (status == NFLLM_OK && strcmp(argv[1], "Q2_K") == 0) {
        struct nfllm_q2_k_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args); args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed; args.packed_bytes = packed_bytes;
        args.columns = columns; args.output = output;
        status = nfllm_dispatch_dequantize_q2_k_row(&args);
    } else if (status == NFLLM_OK && strcmp(argv[1], "IQ3_XXS") == 0) {
        struct nfllm_iq3_xxs_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args); args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed; args.packed_bytes = packed_bytes;
        args.columns = columns; args.output = output;
        status = nfllm_dispatch_dequantize_iq3_xxs_row(&args);
    } else if (status == NFLLM_OK && strcmp(argv[1], "IQ3_S") == 0) {
        struct nfllm_iq3_s_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args); args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed; args.packed_bytes = packed_bytes;
        args.columns = columns; args.output = output;
        status = nfllm_dispatch_dequantize_iq3_s_row(&args);
    } else if (status == NFLLM_OK) {
        status = NFLLM_E_ARGUMENT;
    }
    if (status != NFLLM_OK) {
        fprintf(stderr, "decoder status %ld\n", (long)status);
        nfllm_registry_shutdown();
        free(output); free(packed);
        return 5;
    }
    file = fopen(argv[4], "wb");
    if (!file) { nfllm_registry_shutdown(); free(output); free(packed); return 6; }
    if (fwrite(output, sizeof(float), columns, file) != columns) {
        fclose(file); nfllm_registry_shutdown(); free(output); free(packed); return 7;
    }
    fclose(file);
    nfllm_registry_shutdown();
    free(output); free(packed);
    return 0;
}
