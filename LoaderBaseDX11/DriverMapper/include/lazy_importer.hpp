#pragma once
// Redirect: mapeia lazy_importer.hpp para LazyImporter do projeto ativo.
// LazyImporter.hpp define SafeCall(name) — hash-based lazy PEB walk.
// LI_FN é um alias para manter compatibilidade com o kdmapper upstream.
#include "../../XorStr/LazyImporter.hpp"

// LI_FN — kdmapper chama e.g. LI_FN(GetModuleHandleA)("ntdll.dll").
// SafeCall provê semântica idêntica: resolução lazy via PEB, sem strings visíveis.
#ifndef LI_FN
#  define LI_FN(name) SafeCall(name)
#endif
