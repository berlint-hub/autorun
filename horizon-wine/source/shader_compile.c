#include <stdbool.h>
#include <stdint.h>
#include <switch.h>
#include "shader_alloc_cache.h"

/* The shaders NAK, the compiler under NVK and Zink on it, made and the time it
 * took, on any thread: the [FRAMES] line (frame_pacing.c) names them in a slow
 * frame. */
unsigned long long wine_nx_shader_compile_ns;
unsigned int wine_nx_shader_compiles;

struct nir_shader;
struct nak_compiler;
struct nak_fs_key;
struct nak_shader_bin;

struct nak_shader_bin *__real_nak_compile_shader(struct nir_shader *, bool, const struct nak_compiler *,
                                               uint32_t, const struct nak_fs_key *, bool);
struct nak_shader_bin *__wrap_nak_compile_shader(struct nir_shader *nir, bool dump,
        const struct nak_compiler *nak, uint32_t robust, const struct nak_fs_key *fs, bool task)
{
    u64 start = armGetSystemTick();
    wine_nx_shader_alloc_begin();
    struct nak_shader_bin *result = __real_nak_compile_shader(nir, dump, nak, robust, fs, task);
    wine_nx_shader_alloc_end();
    __atomic_add_fetch(&wine_nx_shader_compile_ns, armTicksToNs(armGetSystemTick() - start), __ATOMIC_RELAXED);
    __atomic_add_fetch(&wine_nx_shader_compiles, 1, __ATOMIC_RELAXED);
    return result;
}
