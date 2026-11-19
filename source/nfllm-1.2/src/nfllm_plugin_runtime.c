#include "nfllm_plugin_runtime.h"
#include "nfllm_registry.h"
#include <cwdll.h>
#include <dos.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFLLM_RUNTIME_MAX_PATH 128U
#define NFLLM_RUNTIME_LINE 192U
#define NFLLM_RUNTIME_ID 40U
#define NFLLM_RUNTIME_VERSION 32U

struct LoadedModule {
    void *handle;
    const struct nfllm_plugin_bundle_v1 *bundle;
    struct nfllm_plugin_descriptor_v1 registry_plugin;
    void *plugin_state;
    int32_t priority;
    int initialized;
};

struct RuntimeExtension {
    struct nfllm_runtime_extension_v1 public_view;
};

struct PluginManifest {
    uint32_t plugin_id;
    uint32_t abi_version;
    uint32_t required_capabilities;
    int32_t priority;
    char name[NFLLM_RUNTIME_ID];
    char version[NFLLM_RUNTIME_VERSION];
    char entrypoint[40];
};

static struct LoadedModule **g_modules;
static uint32_t g_module_count;
static uint32_t g_module_capacity;
static struct RuntimeExtension *g_extensions;
static uint32_t g_extension_count;
static uint32_t g_extension_capacity;
static const struct nfllm_plugin_host_v1 *g_plugin_host;
static struct nfllm_plugin_host_v1 g_plugin_host_storage;
static uint32_t g_cpu_capabilities;
static int g_runtime_initialized;

static void set_text(char *out, uint32_t capacity, const char *value)
{
    uint32_t i = 0U;
    if (!out || capacity == 0U) return;
    if (value) while (i + 1U < capacity && value[i]) {
        out[i] = value[i]; ++i;
    }
    out[i] = '\0';
}

static void diagnostic(char *out, uint32_t capacity, const char *message)
{
    set_text(out, capacity, message);
}

static int32_t NFLLM_CALL resolve_extension_from_host(
    void *context, uint32_t kind, uint32_t extension_id,
    uint32_t required_capabilities, const void **interface_descriptor)
{
    (void)context;
    if (!interface_descriptor) return NFLLM_E_ARGUMENT;
    if ((required_capabilities & ~g_cpu_capabilities) != 0U)
        return NFLLM_E_CAPABILITY;
    return nfllm_plugin_runtime_find_extension(
        kind, extension_id, g_cpu_capabilities,
        NFLLM_PLUGIN_ID_NONE, interface_descriptor);
}

static int reserve_modules(void)
{
    struct LoadedModule **grown;
    uint32_t capacity;
    if (g_module_count < g_module_capacity) return 1;
    capacity = g_module_capacity ? g_module_capacity * 2U : 16U;
    if (capacity < g_module_capacity ||
        capacity > 0xffffffffUL / sizeof(*g_modules)) return 0;
    grown = (struct LoadedModule **)realloc(
        g_modules, capacity * sizeof(*g_modules));
    if (!grown) return 0;
    g_modules = grown;
    g_module_capacity = capacity;
    return 1;
}

static int reserve_extensions(uint32_t needed)
{
    struct RuntimeExtension *grown;
    uint32_t capacity;
    if (needed <= g_extension_capacity) return 1;
    capacity = g_extension_capacity ? g_extension_capacity : 32U;
    while (capacity < needed) {
        if (capacity > 0x7fffffffUL) return 0;
        capacity *= 2U;
    }
    if (capacity > 0xffffffffUL / sizeof(*g_extensions)) return 0;
    grown = (struct RuntimeExtension *)realloc(
        g_extensions, capacity * sizeof(*g_extensions));
    if (!grown) return 0;
    g_extensions = grown;
    g_extension_capacity = capacity;
    return 1;
}

static int compare_extensions(const void *left, const void *right)
{
    const struct RuntimeExtension *a =
        (const struct RuntimeExtension *)left;
    const struct RuntimeExtension *b =
        (const struct RuntimeExtension *)right;
    if (a->public_view.priority < b->public_view.priority) return -1;
    if (a->public_view.priority > b->public_view.priority) return 1;
    if (a->public_view.plugin_id < b->public_view.plugin_id) return -1;
    if (a->public_view.plugin_id > b->public_view.plugin_id) return 1;
    if (a->public_view.extension_id < b->public_view.extension_id) return -1;
    if (a->public_view.extension_id > b->public_view.extension_id) return 1;
    return 0;
}

static char *trim(char *text)
{
    char *end;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
           end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
    return text;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;
    if (!text || !*text || !value) return 0;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (*end != '\0' || errno == ERANGE) return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_i32(const char *text, int32_t *value)
{
    char *end;
    long parsed;
    if (!text || !*text || !value) return 0;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (*end != '\0' || errno == ERANGE) return 0;
    *value = (int32_t)parsed;
    return 1;
}

static int read_manifest(const char *path, struct PluginManifest *manifest)
{
    FILE *file;
    char line[NFLLM_RUNTIME_LINE];
    uint32_t seen = 0U;
    memset(manifest, 0, sizeof(*manifest));
    manifest->priority = 100;
    file = fopen(path, "rt");
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        char *key = trim(line), *value, *equals;
        if (!*key || *key == '#') continue;
        equals = strchr(key, '=');
        if (!equals) { fclose(file); return 0; }
        *equals = '\0';
        key = trim(key);
        value = trim(equals + 1);
        if (strcmp(key, "plugin_id") == 0) {
            if (!parse_u32(value, &manifest->plugin_id)) { fclose(file); return 0; }
            seen |= 1U;
        } else if (strcmp(key, "name") == 0) {
            set_text(manifest->name, sizeof(manifest->name), value); seen |= 2U;
        } else if (strcmp(key, "version") == 0) {
            set_text(manifest->version, sizeof(manifest->version), value); seen |= 4U;
        } else if (strcmp(key, "abi_version") == 0) {
            if (!parse_u32(value, &manifest->abi_version)) { fclose(file); return 0; }
            seen |= 8U;
        } else if (strcmp(key, "required_capabilities") == 0) {
            if (!parse_u32(value, &manifest->required_capabilities)) { fclose(file); return 0; }
            seen |= 16U;
        } else if (strcmp(key, "priority") == 0) {
            if (!parse_i32(value, &manifest->priority)) { fclose(file); return 0; }
            seen |= 32U;
        } else if (strcmp(key, "entrypoint") == 0) {
            set_text(manifest->entrypoint, sizeof(manifest->entrypoint), value);
            seen |= 64U;
        } else {
            fclose(file); return 0;
        }
        if (strlen(value) >= NFLLM_RUNTIME_LINE - 1U) { fclose(file); return 0; }
    }
    fclose(file);
    return seen == 127U && manifest->plugin_id != NFLLM_PLUGIN_ID_NONE &&
           manifest->name[0] && manifest->version[0] &&
           strcmp(manifest->entrypoint, NFLLM_PLUGIN_QUERY_V1) == 0;
}

static int make_package_paths(const char *directory, const char *filename,
                              char *manifest_path, char *plugin_path,
                              char *disabled_path)
{
    char base[16];
    const char *dot = strrchr(filename, '.');
    uint32_t length, dirlen;
    if (!dot || (uint32_t)(dot - filename) == 0U ||
        (uint32_t)(dot - filename) >= sizeof(base)) return 0;
    length = (uint32_t)(dot - filename);
    memcpy(base, filename, length); base[length] = '\0';
    dirlen = (uint32_t)strlen(directory);
    if (dirlen + length + 6U >= NFLLM_RUNTIME_MAX_PATH) return 0;
    sprintf(manifest_path, "%s\\%s.NFM", directory, base);
    sprintf(plugin_path, "%s\\%s.NFD", directory, base);
    sprintf(disabled_path, "%s\\%s.OFF", directory, base);
    return 1;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int validate_bundle(const struct PluginManifest *manifest,
                           const struct nfllm_plugin_bundle_v1 *bundle)
{
    uint32_t i, j;
    if (!bundle || bundle->struct_size < sizeof(*bundle) ||
        bundle->abi_version != NFLLM_PLUGIN_ABI_V1 ||
        bundle->plugin_id != manifest->plugin_id || !bundle->name ||
        strcmp(bundle->name, manifest->name) != 0 || !bundle->version ||
        strcmp(bundle->version, manifest->version) != 0 ||
        (bundle->required_capabilities & ~manifest->required_capabilities) != 0U ||
        (bundle->kernel_count && !bundle->kernels) ||
        (!bundle->kernel_count && bundle->kernels) ||
        (bundle->extension_count && !bundle->extensions) ||
        (!bundle->extension_count && bundle->extensions) ||
        bundle->kernel_count > NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN)
        return 0;
    for (i = 0U; i < bundle->kernel_count; ++i) {
        const struct nfllm_kernel_descriptor_v1 *kernel = &bundle->kernels[i];
        if (kernel->struct_size < sizeof(*kernel) ||
            kernel->abi_version != NFLLM_ABI_VERSION_1 ||
            !kernel->operation_id || !kernel->name || !kernel->execute ||
            (kernel->required_capabilities &
             ~manifest->required_capabilities) != 0U) return 0;
    }
    for (i = 0U; i < bundle->extension_count; ++i) {
        const struct nfllm_extension_v1 *extension = &bundle->extensions[i];
        if (extension->struct_size < sizeof(*extension) ||
            extension->abi_version != NFLLM_PLUGIN_ABI_V1 ||
            extension->kind < NFLLM_EXT_OPERATOR ||
            extension->kind > NFLLM_EXT_HARDWARE_OPTIMIZATION ||
            !extension->extension_id || !extension->name ||
            !extension->interface_descriptor ||
            (extension->required_capabilities &
             ~manifest->required_capabilities) != 0U) return 0;
        for (j = 0U; j < i; ++j)
            if (bundle->extensions[j].kind == extension->kind &&
                bundle->extensions[j].extension_id == extension->extension_id)
                return 0;
    }
    return 1;
}

static int add_extension(uint32_t plugin_id, uint32_t kind, uint32_t id,
                         uint32_t required_capabilities, int32_t priority,
                         const char *plugin_name, const char *name,
                         const void *interface_descriptor)
{
    struct RuntimeExtension *entry;
    if (!reserve_extensions(g_extension_count + 1U)) return 0;
    entry = &g_extensions[g_extension_count++];
    entry->public_view.plugin_id = plugin_id;
    entry->public_view.kind = kind;
    entry->public_view.extension_id = id;
    entry->public_view.required_capabilities = required_capabilities;
    entry->public_view.priority = priority;
    entry->public_view.plugin_name = plugin_name;
    entry->public_view.name = name;
    entry->public_view.interface_descriptor = interface_descriptor;
    return 1;
}

static void unload_unregistered_module(struct LoadedModule *module)
{
    if (!module) return;
    if (module->initialized && module->bundle && module->bundle->shutdown)
        module->bundle->shutdown(module->plugin_state);
    if (module->handle) FreeLibrary(module->handle);
    free(module);
}

static nfllm_status_v1 load_one_manifest(const char *directory,
    const char *filename, uint32_t cpu_capabilities)
{
    char manifest_path[NFLLM_RUNTIME_MAX_PATH];
    char plugin_path[NFLLM_RUNTIME_MAX_PATH];
    char disabled_path[NFLLM_RUNTIME_MAX_PATH];
    struct PluginManifest manifest;
    struct LoadedModule *module;
    nfllm_plugin_query_v1_fn query;
    uint32_t i;
    nfllm_status_v1 status;

    if (!make_package_paths(directory, filename, manifest_path, plugin_path,
                            disabled_path)) {
        printf("Plugin skipped: invalid DOS 8.3 package name %s\n", filename);
        return NFLLM_E_ARGUMENT;
    }
    if (file_exists(disabled_path)) {
        printf("Plugin disabled: %s\n", filename);
        return NFLLM_OK;
    }
    if (!read_manifest(manifest_path, &manifest)) {
        printf("Plugin skipped: invalid manifest %s\n", manifest_path);
        return NFLLM_E_ARGUMENT;
    }
    if (manifest.abi_version != NFLLM_PLUGIN_ABI_V1) {
        printf("Plugin skipped: ABI mismatch %s\n", manifest.name);
        return NFLLM_E_ABI;
    }
    if ((manifest.required_capabilities & ~cpu_capabilities) != 0U) {
        printf("Plugin skipped: CPU capability mismatch %s\n", manifest.name);
        return NFLLM_E_CAPABILITY;
    }
    module = (struct LoadedModule *)calloc(1U, sizeof(*module));
    if (!module) return NFLLM_E_MEMORY;
    module->handle = LoadLibrary(plugin_path);
    if (!module->handle) {
        printf("Plugin load failed: %s\n", plugin_path);
        unload_unregistered_module(module);
        return NFLLM_E_NOT_FOUND;
    }
    query = (nfllm_plugin_query_v1_fn)GetProcAddress(
        module->handle, manifest.entrypoint);
    if (!query || query(g_plugin_host, &module->bundle) != NFLLM_OK ||
        !validate_bundle(&manifest, module->bundle)) {
        printf("Plugin ABI/query failed: %s\n", manifest.name);
        unload_unregistered_module(module);
        return NFLLM_E_ABI;
    }
    module->priority = manifest.priority;
    if (module->bundle->initialize) {
        status = module->bundle->initialize(g_plugin_host, &module->plugin_state);
        if (status != NFLLM_OK) {
            printf("Plugin initialization failed: %s (%ld)\n",
                   manifest.name, (long)status);
            unload_unregistered_module(module);
            return status;
        }
        module->initialized = 1;
    }
    module->registry_plugin.struct_size = sizeof(module->registry_plugin);
    module->registry_plugin.abi_version = NFLLM_ABI_VERSION_1;
    module->registry_plugin.plugin_id = module->bundle->plugin_id;
    module->registry_plugin.required_capabilities =
        module->bundle->required_capabilities;
    module->registry_plugin.flags = 0U;
    module->registry_plugin.name = module->bundle->name;
    module->registry_plugin.initialize = 0;
    module->registry_plugin.shutdown = 0;
    if (!reserve_modules()) {
        unload_unregistered_module(module);
        return NFLLM_E_MEMORY;
    }
    status = nfllm_registry_register_priority(
        &module->registry_plugin, module->bundle->kernels,
        module->bundle->kernel_count, module->priority);
    if (status != NFLLM_OK) {
        printf("Plugin registry rejected %s (%ld)\n",
               manifest.name, (long)status);
        unload_unregistered_module(module);
        return status;
    }
    g_modules[g_module_count++] = module;

    for (i = 0U; i < module->bundle->kernel_count; ++i) {
        const struct nfllm_kernel_descriptor_v1 *kernel =
            &module->bundle->kernels[i];
        if (!add_extension(module->bundle->plugin_id, NFLLM_EXT_OPERATOR,
                kernel->operation_id,
                module->bundle->required_capabilities |
                    kernel->required_capabilities,
                module->priority, module->bundle->name, kernel->name, kernel))
            return NFLLM_E_MEMORY;
    }
    for (i = 0U; i < module->bundle->extension_count; ++i) {
        const struct nfllm_extension_v1 *extension =
            &module->bundle->extensions[i];
        int32_t priority = module->priority;
        if ((extension->priority > 0 && priority > 2147483647L - extension->priority) ||
            (extension->priority < 0 && priority < (-2147483647L - 1L) - extension->priority))
            priority = 2147483647L;
        else priority += extension->priority;
        if (!add_extension(module->bundle->plugin_id, extension->kind,
                extension->extension_id,
                module->bundle->required_capabilities |
                    extension->required_capabilities,
                priority, module->bundle->name, extension->name,
                extension->interface_descriptor)) return NFLLM_E_MEMORY;
    }
    printf("Plugin loaded: %s %s (priority %ld)\n", module->bundle->name,
           module->bundle->version, (long)module->priority);
    return NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_load(
    const char *plugin_directory,
    const struct nfllm_host_api_v1 *registry_host,
    const struct nfllm_plugin_host_v1 *plugin_host,
    uint32_t cpu_capabilities)
{
    struct find_t found;
    char pattern[NFLLM_RUNTIME_MAX_PATH];
    unsigned error;
    if (!plugin_directory || !registry_host || !plugin_host)
        return NFLLM_E_ARGUMENT;
    nfllm_plugin_runtime_shutdown();
    g_plugin_host_storage = *plugin_host;
    g_plugin_host_storage.struct_size = sizeof(g_plugin_host_storage);
    g_plugin_host_storage.abi_version = NFLLM_PLUGIN_ABI_V1;
    g_plugin_host_storage.cpu_capabilities = cpu_capabilities;
    g_plugin_host_storage.context = 0;
    g_plugin_host_storage.resolve_extension = resolve_extension_from_host;
    g_plugin_host = &g_plugin_host_storage;
    g_cpu_capabilities = cpu_capabilities;
    error = (unsigned)nfllm_registry_initialize(registry_host);
    if (error != NFLLM_OK) return (nfllm_status_v1)error;
    if (strlen(plugin_directory) + 7U >= sizeof(pattern)) {
        nfllm_plugin_runtime_shutdown(); return NFLLM_E_SIZE;
    }
    sprintf(pattern, "%s\\*.NFM", plugin_directory);
    error = _dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_HIDDEN |
                           _A_SYSTEM | _A_ARCH, &found);
    if (error != 0U) {
        g_runtime_initialized = 1;
        return NFLLM_E_NOT_FOUND;
    }
    g_runtime_initialized = 1;
    do {
        error = (unsigned)load_one_manifest(plugin_directory, found.name,
                                             cpu_capabilities);
        if ((nfllm_status_v1)error == NFLLM_E_MEMORY) {
            (void)_dos_findclose(&found);
            nfllm_plugin_runtime_shutdown();
            return NFLLM_E_MEMORY;
        }
        error = _dos_findnext(&found);
    } while (error == 0U);
    (void)_dos_findclose(&found);
    if (g_extension_count > 1U)
        qsort(g_extensions, g_extension_count, sizeof(*g_extensions),
              compare_extensions);
    return g_module_count ? NFLLM_OK : NFLLM_E_NOT_FOUND;
}

void NFLLM_CALL nfllm_plugin_runtime_shutdown(void)
{
    uint32_t i;
    if (g_runtime_initialized)
        nfllm_registry_shutdown();
    for (i = g_module_count; i > 0U; --i) {
        struct LoadedModule *module = g_modules[i - 1U];
        if (module->initialized && module->bundle && module->bundle->shutdown)
            module->bundle->shutdown(module->plugin_state);
        if (module->handle) FreeLibrary(module->handle);
        free(module);
    }
    free(g_modules); g_modules = 0;
    g_module_count = 0U; g_module_capacity = 0U;
    free(g_extensions); g_extensions = 0;
    g_extension_count = 0U; g_extension_capacity = 0U;
    g_plugin_host = 0;
    memset(&g_plugin_host_storage, 0, sizeof(g_plugin_host_storage));
    g_cpu_capabilities = 0U;
    g_runtime_initialized = 0;
}

uint32_t NFLLM_CALL nfllm_plugin_runtime_plugin_count(void)
{
    return g_module_count;
}

uint32_t NFLLM_CALL nfllm_plugin_runtime_extension_count(uint32_t kind)
{
    uint32_t count = 0U, i;
    for (i = 0U; i < g_extension_count; ++i)
        if (g_extensions[i].public_view.kind == kind) ++count;
    return count;
}

nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_extension_at(
    uint32_t kind, uint32_t cpu_capabilities, uint32_t index,
    struct nfllm_runtime_extension_v1 *extension)
{
    uint32_t i, seen = 0U;
    if (!extension) return NFLLM_E_ARGUMENT;
    for (i = 0U; i < g_extension_count; ++i) {
        const struct nfllm_runtime_extension_v1 *candidate =
            &g_extensions[i].public_view;
        if (candidate->kind != kind ||
            (candidate->required_capabilities & ~cpu_capabilities) != 0U)
            continue;
        if (seen++ == index) { *extension = *candidate; return NFLLM_OK; }
    }
    return NFLLM_E_NOT_FOUND;
}

nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_find_extension(
    uint32_t kind, uint32_t extension_id, uint32_t cpu_capabilities,
    uint32_t preferred_plugin_id, const void **interface_descriptor)
{
    uint32_t i;
    const struct nfllm_runtime_extension_v1 *best = 0;
    if (!interface_descriptor) return NFLLM_E_ARGUMENT;
    *interface_descriptor = 0;
    for (i = 0U; i < g_extension_count; ++i) {
        const struct nfllm_runtime_extension_v1 *candidate =
            &g_extensions[i].public_view;
        if (candidate->kind != kind || candidate->extension_id != extension_id ||
            (preferred_plugin_id != NFLLM_PLUGIN_ID_NONE &&
             candidate->plugin_id != preferred_plugin_id) ||
            (candidate->required_capabilities & ~cpu_capabilities) != 0U)
            continue;
        if (!best || candidate->priority < best->priority) best = candidate;
    }
    if (!best) return NFLLM_E_NOT_FOUND;
    *interface_descriptor = best->interface_descriptor;
    return NFLLM_OK;
}

static int read_model_file(const char *path, uint8_t **data, uint32_t *size)
{
    FILE *file;
    long length;
    uint8_t *buffer;
    *data = 0; *size = 0U;
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) { fclose(file); return 0; }
    buffer = (uint8_t *)malloc(length ? (uint32_t)length : 1U);
    if (!buffer) { fclose(file); return 0; }
    if (length && fread(buffer, 1U, (uint32_t)length, file) !=
                  (uint32_t)length) {
        free(buffer); fclose(file); return 0;
    }
    fclose(file);
    *data = buffer; *size = (uint32_t)length;
    return 1;
}

nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_generate_external(
    const char *path, const char *prompt, uint32_t max_new_tokens,
    nfllm_emit_token_v1 emit_token, void *emit_context,
    uint32_t *generated_tokens, char *diag, uint32_t diag_capacity)
{
    struct nfllm_model_file_view_v1 file_view;
    struct nfllm_generation_options_v1 options;
    uint8_t *file_bytes = 0;
    uint32_t file_size = 0U, format_index = 0U, arch_index;
    struct nfllm_runtime_extension_v1 format_entry;
    nfllm_status_v1 status = NFLLM_E_NOT_FOUND;
    int claimed = 0;

    if (!path || !prompt || !emit_token || !generated_tokens)
        return NFLLM_E_ARGUMENT;
    *generated_tokens = 0U;
    if (!read_model_file(path, &file_bytes, &file_size)) {
        diagnostic(diag, diag_capacity, "could not read model file for plugins");
        return NFLLM_E_MEMORY;
    }
    memset(&file_view, 0, sizeof(file_view));
    file_view.struct_size = sizeof(file_view);
    file_view.abi_version = NFLLM_PLUGIN_ABI_V1;
    file_view.path = path;
    file_view.bytes = file_bytes;
    file_view.byte_count = file_size;
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.abi_version = NFLLM_PLUGIN_ABI_V1;
    options.max_new_tokens = max_new_tokens;

    while (nfllm_plugin_runtime_extension_at(NFLLM_EXT_MODEL_FORMAT,
            g_cpu_capabilities, format_index++, &format_entry) == NFLLM_OK) {
        const struct nfllm_model_format_v1 *format =
            (const struct nfllm_model_format_v1 *)format_entry.interface_descriptor;
        struct nfllm_model_view_v1 model_view;
        void *format_state = 0;
        int32_t probe;
        if (!format || format->struct_size < sizeof(*format) ||
            format->abi_version != NFLLM_PLUGIN_ABI_V1 || !format->format_id ||
            !format->probe || !format->open || !format->close) continue;
        probe = format->probe(g_plugin_host, &file_view);
        if (probe <= 0) continue;
        claimed = 1;
        memset(&model_view, 0, sizeof(model_view));
        model_view.struct_size = sizeof(model_view);
        model_view.abi_version = NFLLM_PLUGIN_ABI_V1;
        status = format->open(g_plugin_host, &file_view, &model_view,
                              &format_state);
        if (status != NFLLM_OK) {
            format->close(format_state);
            diagnostic(diag, diag_capacity, "model format plugin rejected file");
            free(file_bytes);
            return status;
        }
        if (model_view.struct_size < sizeof(model_view) ||
            model_view.abi_version != NFLLM_PLUGIN_ABI_V1 ||
            !model_view.architecture_id) {
            format->close(format_state); free(file_bytes);
            diagnostic(diag, diag_capacity, "invalid model view from format plugin");
            return NFLLM_E_ABI;
        }
        arch_index = 0U;
        while (nfllm_plugin_runtime_extension_at(
            NFLLM_EXT_NEURAL_ARCHITECTURE, g_cpu_capabilities,
            arch_index++, &format_entry) == NFLLM_OK) {
            const struct nfllm_neural_architecture_v1 *architecture =
                (const struct nfllm_neural_architecture_v1 *)
                    format_entry.interface_descriptor;
            if (!architecture ||
                architecture->struct_size < sizeof(*architecture) ||
                architecture->abi_version != NFLLM_PLUGIN_ABI_V1 ||
                !architecture->architecture_id || !architecture->generate ||
                strcmp(architecture->architecture_id,
                       model_view.architecture_id) != 0) continue;
            if (architecture->supports &&
                architecture->supports(g_plugin_host, &model_view) <= 0)
                continue;
            status = architecture->generate(g_plugin_host, &model_view,
                prompt, &options, emit_token, emit_context, generated_tokens);
            format->close(format_state);
            free(file_bytes);
            diagnostic(diag, diag_capacity,
                       status == NFLLM_OK ? "external format and architecture plugins completed" :
                                            "architecture plugin failed during generation");
            return status;
        }
        format->close(format_state);
        if (claimed) {
            free(file_bytes);
            diagnostic(diag, diag_capacity,
                       "model format was recognized but no compatible architecture plugin was installed");
            return NFLLM_E_UNSUPPORTED;
        }
    }
    free(file_bytes);
    if (!claimed) diagnostic(diag, diag_capacity,
                              "no installed model-format plugin claimed file");
    return status;
}
