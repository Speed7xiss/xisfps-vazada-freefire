#pragma once
// Stub: O projeto ativo não usa OLLVM/obfuscação clang.
// Quando compilado com MSVC, atributos [[clang::annotate(...)]] são ignorados.
// Este header define macros no-op para compatibilidade com kdmapper upstream.

#if defined(__clang__)
// Clang: passa os atributos reais para eventual obfuscação
#  define OBFUSCATE_SPLIT    [[clang::annotate("split")]]
#  define OBFUSCATE_FLATTEN  [[clang::annotate("flatten")]]
#  define OBFUSCATE_OPAQUE   [[clang::annotate("opaque")]]
#  define OBFUSCATE_SUB      [[clang::annotate("sub")]]
#else
// MSVC: no-op
#  define OBFUSCATE_SPLIT
#  define OBFUSCATE_FLATTEN
#  define OBFUSCATE_OPAQUE
#  define OBFUSCATE_SUB
#endif
