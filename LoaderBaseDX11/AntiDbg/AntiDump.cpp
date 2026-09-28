#include "AntiDump.hpp"

#include <Windows.h>
#include <cstring>

// ─── Helpers internos ─────────────────────────────────────────────────────────
namespace {
    // Torna uma região de memória gravável, executa o callback e restaura proteção.
    template<typename Fn>
    static void WithWriteAccess( void* base, SIZE_T size, Fn&& fn ) {
        DWORD old = 0;
        if ( VirtualProtect( base, size, PAGE_READWRITE, &old ) ) {
            fn();
            VirtualProtect( base, size, old, &old );
        }
    }
} // anonymous namespace

// ─── 1. ErasePEHeader ─────────────────────────────────────────────────────────
// Apaga os primeiros 0x1000 bytes do módulo (cobre DOS header + NT headers).
// Após isso, IMAGE_DOS_HEADER.e_magic != MZ → qualquer dumper que valide o
// header recusa a imagem como PE inválida.
void AntiDbg::Dump::ErasePEHeader( HMODULE hSelf ) {
    if ( !hSelf ) return;
    auto base = reinterpret_cast<void*>( hSelf );
    WithWriteAccess( base, 0x1000, [base]() {
        SecureZeroMemory( base, 0x1000 );
    } );
}

// ─── 2. CorruptLDREntry ───────────────────────────────────────────────────────
// Caminha a InLoadOrderModuleList do PEB, localiza nossa entrada pelo DllBase
// e corrompe SizeOfImage (→ 0) e DllBase (→ nullptr).
// KsDumper/Scylla leem SizeOfImage do LDR para determinar quantos bytes copiar
// → com zero eles não conseguem extrair nada.
void AntiDbg::Dump::CorruptLDREntry( HMODULE hSelf ) {
    if ( !hSelf ) return;

    // PEB (x64: GS:[0x60])
    const auto peb = reinterpret_cast<PBYTE>( __readgsqword( 0x60 ) );
    if ( !peb ) return;

    // PEB_LDR_DATA está em PEB+0x18
    const auto ldr = *reinterpret_cast<PBYTE*>( peb + 0x18 );
    if ( !ldr ) return;

    // InLoadOrderModuleList começa em PEB_LDR_DATA+0x10
    const auto listHead = reinterpret_cast<PLIST_ENTRY>( ldr + 0x10 );
    PLIST_ENTRY entry   = listHead->Flink;

    while ( entry && entry != listHead ) {
        // LDR_DATA_TABLE_ENTRY layout (x64):
        //   +0x00  InLoadOrderLinks   (LIST_ENTRY, 16 bytes)
        //   +0x10  InMemoryOrderLinks (LIST_ENTRY, 16 bytes)
        //   +0x20  InInitOrderLinks   (LIST_ENTRY, 16 bytes)
        //   +0x30  DllBase            (PVOID)
        //   +0x38  EntryPoint         (PVOID)
        //   +0x40  SizeOfImage        (ULONG)
        const auto ldrEntry = reinterpret_cast<PBYTE>( entry );
        PVOID*  pDllBase      = reinterpret_cast<PVOID*>( ldrEntry + 0x30 );
        PULONG  pSizeOfImage  = reinterpret_cast<PULONG>(  ldrEntry + 0x40 );

        if ( *pDllBase == static_cast<PVOID>( hSelf ) ) {
            // Encontrou nossa entrada — corrompe
            WithWriteAccess( pDllBase, sizeof( PVOID ) + sizeof( ULONG ), [&]() {
                *pSizeOfImage = 0UL;          // dumper vai copiar 0 bytes
                *pDllBase     = nullptr;      // base inválida
            } );
            break;
        }

        entry = entry->Flink;
    }
}

// ─── 3. EraseSectionHeaders ───────────────────────────────────────────────────
// Localiza IMAGE_SECTION_HEADER[] dentro do NT headers na memória e zera tudo.
// Sem a tabela de seções, reconstruir a layout do PE fica inviável.
void AntiDbg::Dump::EraseSectionHeaders( HMODULE hSelf ) {
    if ( !hSelf ) return;

    const auto base = reinterpret_cast<PBYTE>( hSelf );

    // Precisamos de um snapshot dos offsets ANTES de apagar o header.
    // Lemos IMAGE_DOS_HEADER.e_lfanew com VirtualQuery pra confirmar que
    // a região ainda está acessível.
    MEMORY_BASIC_INFORMATION mbi {};
    if ( !VirtualQuery( base, &mbi, sizeof( mbi ) ) ) return;
    if ( mbi.State != MEM_COMMIT ) return;

    // e_lfanew está em offset 0x3C do DOS header. Se o header já foi apagado
    // por ErasePEHeader(), esse valor virou zero — nada a fazer.
    const DWORD e_lfanew = *reinterpret_cast<const DWORD*>( base + 0x3C );
    if ( !e_lfanew || e_lfanew > 0x400 ) return;  // apagado ou suspeito

    const auto ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>( base + e_lfanew );

    // IMAGE_FIRST_SECTION macro: logo após o Optional Header
    const auto firstSection = reinterpret_cast<IMAGE_SECTION_HEADER*>(
        reinterpret_cast<PBYTE>( &ntHeaders->OptionalHeader ) +
        ntHeaders->FileHeader.SizeOfOptionalHeader );

    const WORD numSections = ntHeaders->FileHeader.NumberOfSections;
    if ( !numSections || numSections > 96 ) return;

    const SIZE_T shdrsSize = numSections * sizeof( IMAGE_SECTION_HEADER );

    WithWriteAccess( firstSection, shdrsSize, [firstSection, shdrsSize]() {
        SecureZeroMemory( firstSection, shdrsSize );
    } );
}
