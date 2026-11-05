/* goose.h -- for C compiled on its own, a package's or a program's sidecar
   C (docs/packages.md): the Goose values an extern fn hands it that C
   cannot spell from the layouts alone (spec §7.10, C.2). A `T[>..]&`
   parameter arrives as a gs_rref, the resizable's header and its data
   stack. C may write its elements and append whole ones at the stack's
   top, moving the top and the header's count together, as gs_bld_append
   does for the bytes of a `u8[>..]&`; nothing else of it is C's to touch.

   The compiler writes this header beside every native part it builds and
   puts it on that part's include path, `#include "goose.h"`, and builds
   the part again whenever the header changes. The generated program
   defines all of it itself (runtime.h, runtime_ext.h), and GOOSE_H with
   it, so an --include header may include this one too. */

#ifndef GOOSE_H
#define GOOSE_H

#include <stdint.h>
#include <string.h>

#ifndef GS_TARGET_32
#if UINTPTR_MAX == 0xffffffffu
#define GS_TARGET_32 1
#else
#define GS_TARGET_32 0
#endif
#endif

#ifndef GS_STATIC_STACKS
#define GS_STATIC_STACKS GS_TARGET_32
#endif

#if GS_TARGET_32
typedef int32_t gs_len_t;
#else
typedef int64_t gs_len_t;
#endif

typedef struct {
    uint8_t *top;
#if GS_STATIC_STACKS
    uint8_t *limit;
#endif
} gs_stack;

#pragma pack(push, 1)
typedef struct { uint8_t *base; gs_len_t len; } gs_rhdr;
typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;
#pragma pack(pop)

#if GS_STATIC_STACKS
#ifdef __cplusplus
extern "C"
#endif
void gs_stack_overflow(const void *at);
#endif

/* Appends n bytes to the u8[>..] a builder reference points at: the
   resizable's elements top its stack, so the bytes go at the stack top and
   the header's count grows. */
static inline void gs_bld_append(gs_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
#if GS_STATIC_STACKS
    if ((uint64_t)(uintptr_t)(b.stk->limit - b.stk->top) < (uint64_t)n)
        gs_stack_overflow(b.stk->top == b.stk->limit ? b.stk->top - 1 : b.stk->top);
#endif
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += (gs_len_t)n;
}

#endif
