#pragma once

/*
 * Zero-cost source annotation markers for the VMP policy frontend.
 *
 * Clang/GCC: expand to annotate(...) attributes that are visible to the AST plugin.
 * MSVC C++: fall back to standard-style vendor attributes that are ignored by the
 * compiler but remain visible in source text for the fallback scanner.
 * MSVC C: leave the token in source as an empty macro/comment marker for the
 * fallback scanner; no code is emitted.
 */

#if defined(__clang__) || defined(__GNUC__)
#define VMP_VM_FUNC __attribute__((annotate("vmp_vm_func")))
#define VMP_VM_STRING __attribute__((annotate("vmp_vm_string")))
#elif defined(_MSC_VER)
#  ifdef __cplusplus
#    define VMP_VM_FUNC [[vmp::vm_func]]
#    define VMP_VM_STRING [[vmp::vm_string]]
#  else
#    define VMP_VM_FUNC /* vmp_vm_func */
#    define VMP_VM_STRING /* vmp_vm_string */
#  endif
#else
#define VMP_VM_FUNC
#define VMP_VM_STRING
#endif
