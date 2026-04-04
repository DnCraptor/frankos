/*
 * FRANK OS - C library stubs for Video Player
 *
 * pl_mpeg uses standard libc functions. This file provides implementations
 * forwarding to the MOS2 sys_table.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* sys_table base */
#ifndef M_OS_API_SYS_TABLE_BASE
#define M_OS_API_SYS_TABLE_BASE ((void*)(0x10000000ul + (16 << 20) - (4 << 10)))
static const unsigned long * const _sys_table_ptrs =
    (const unsigned long * const)M_OS_API_SYS_TABLE_BASE;
#endif

/* --- String functions --- */

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

/* --- Memory allocation: hybrid SRAM/PSRAM ---
 * Small allocs (< 4KB) -> SRAM (fast, for hot data structures)
 * Large allocs (>= 4KB) -> PSRAM (spacious, for frame/stream buffers) */

#define SRAM_THRESHOLD 4096

static void *_hybrid_malloc(size_t sz) {
    typedef void *(*fn_t)(size_t);
    if (sz < SRAM_THRESHOLD) {
        void *p = ((fn_t)_sys_table_ptrs[529])(sz);  /* pvPortMalloc (SRAM) */
        if (p) return p;
    }
    return ((fn_t)_sys_table_ptrs[32])(sz);  /* __malloc (PSRAM) */
}

static void _hybrid_free(void *p) {
    typedef void (*fn_t)(void *);
    ((fn_t)_sys_table_ptrs[33])(p);  /* __free handles both SRAM and PSRAM */
}

void *malloc(size_t sz) {
    return _hybrid_malloc(sz);
}

void free(void *p) {
    _hybrid_free(p);
}

void *calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void *p = _hybrid_malloc(total);
    if (p) {
        unsigned char *d = (unsigned char *)p;
        for (size_t i = 0; i < total; i++) d[i] = 0;
    }
    return p;
}

void *realloc(void *p, size_t sz) {
    if (!p) return _hybrid_malloc(sz);
    if (sz == 0) { _hybrid_free(p); return (void *)0; }
    void *np = _hybrid_malloc(sz);
    if (np) {
        const unsigned char *s = (const unsigned char *)p;
        unsigned char *d = (unsigned char *)np;
        for (size_t i = 0; i < sz; i++) d[i] = s[i];
        _hybrid_free(p);
    }
    return np;
}

/* --- _impure_ptr (newlib reentrancy, unused) --- */
void *_impure_ptr = 0;

/* --- Math stubs via sys_table --- */

typedef double (*math_d_d_t)(double);
typedef double (*math_dd_d_t)(double, double);

double floor(double x) { return ((math_d_d_t)_sys_table_ptrs[201])(x); }
double pow(double x, double y) { return ((math_dd_d_t)_sys_table_ptrs[202])(x, y); }
double sin(double x) { return ((math_d_d_t)_sys_table_ptrs[204])(x); }
double cos(double x) { return ((math_d_d_t)_sys_table_ptrs[205])(x); }
double log(double x) { return ((math_d_d_t)_sys_table_ptrs[208])(x); }

int abs(int x) { return (x < 0) ? -x : x; }
