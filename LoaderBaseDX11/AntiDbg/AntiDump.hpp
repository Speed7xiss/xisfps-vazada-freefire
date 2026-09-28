#pragma once

// ─── AntiDbg::Dump ────────────────────────────────────────────────────────────
// Técnicas de anti-dump aplicadas uma vez no Init().
// Objetivo: impedir que ferramentas como KsDumper, Scylla ou PE-sieve
// consigam reconstruir a DLL a partir da memória do processo.
//
// Técnicas:
//   1. ErasePEHeader    — zera os primeiros 0x1000 bytes do próprio módulo
//                         (IMAGE_DOS_HEADER + IMAGE_NT_HEADERS ficam inválidos)
//   2. CorruptLDREntry  — corrompe SizeOfImage e DllBase na entrada do PEB LDR
//                         (dumpers que leem tamanho via LDR recebem valor errado)
//   3. EraseSectionHdrs — zera os IMAGE_SECTION_HEADER no NT headers na memória
//                         (impede reconstrução da layout de seções)

#include <Windows.h>

namespace AntiDbg::Dump {

    // Apaga o PE header completo (0x1000 bytes) do próprio módulo em memória.
    void ErasePEHeader( HMODULE hSelf );

    // Corrompe a entrada LDR do PEB: SizeOfImage → lixo, DllBase → valor falso.
    void CorruptLDREntry( HMODULE hSelf );

    // Zera os section headers (IMAGE_SECTION_HEADER[]) dentro do NT header.
    void EraseSectionHeaders( HMODULE hSelf );

} // namespace AntiDbg::Dump
