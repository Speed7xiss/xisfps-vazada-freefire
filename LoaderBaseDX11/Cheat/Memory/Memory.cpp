#include "Memory.hpp"
#include <windows.h>

uintptr_t Memory::GuestCR3 = 0;
Memory* g_Memory = nullptr;

// -------------------------------------------------------------------------
// VA→PA translation cache (dynamic TLS, direct-mapped)
// -------------------------------------------------------------------------
// Cada Memory::Read<T> chama ConvertCR3 → TranslateCR3, que emite 4 EMemory::
// ReadRaw (PML4, PDPT, PD, PT) antes do 5º ReadRaw dos bytes finais. Após
// warmup, esses 4 hops normalmente batem no page cache 4KB de EMemory, mas
// (a) BeginFrameRead invalida esse cache a cada frame e (b) colisões no bucket
// direct-mapped podem despejar a PT, transformando 1 Read<T> lógico em 5 RPMs.
//
// Este cache guarda a tradução final (VA→PA) já resolvida, indexada pela
// página guest. Em hit, pulamos o walk inteiro. A invalidação é atômica com
// EMemory::s_cacheGen (bumpado por BeginFrameRead) — impossível servir uma
// tradução obsoleta que induziria EMemory a devolver bytes de página antiga.
// cr3 é embedded no slot: SetCR3 (troca de emulador/processo) causa miss
// natural, sem flush explícito.
//
// NOTA: thread_local (static TLS) falha em DLLs mapeadas manualmente porque
// o loader não expande o bloco TLS para threads criadas após a injeção.
// Usamos TlsAlloc/TlsGetValue/TlsSetValue (dynamic TLS) que funciona sempre.
namespace {
    struct alignas(32) VASlot {
        uintptr_t vaBase;   // guestVA & ~0xFFF; 0 = slot vazio
        uintptr_t paBase;   // guestPA & ~0xFFF
        uintptr_t cr3;      // GuestCR3 no momento da populacao
        uint32_t  gen;      // EMemory::GetReadGen() no momento da populacao
        uint32_t  _pad;
    };

    static constexpr size_t VA_CACHE_ENTRIES = 512;

    struct MemTLS {
        VASlot g_vaCache[VA_CACHE_ENTRIES]{};
    };

    static DWORD g_tlsMem = TLS_OUT_OF_INDEXES;

    static MemTLS* GetMemTLS() {
        if ( g_tlsMem == TLS_OUT_OF_INDEXES ) {
            static volatile LONG s_lock = 0;
            while ( InterlockedCompareExchange( &s_lock, 1, 0 ) != 0 )
                Sleep( 0 );
            if ( g_tlsMem == TLS_OUT_OF_INDEXES )
                g_tlsMem = TlsAlloc();
            InterlockedExchange( &s_lock, 0 );
        }
        if ( g_tlsMem == TLS_OUT_OF_INDEXES ) return nullptr;

        MemTLS* tls = static_cast<MemTLS*>( TlsGetValue( g_tlsMem ) );
        if ( !tls ) {
            tls = new MemTLS{};
            TlsSetValue( g_tlsMem, tls );
        }
        return tls;
    }
}

bool Memory::TranslateCR3Cached(uintptr_t guestVA, uintptr_t cr3, uintptr_t& guestPA) {
    const uintptr_t vaBase = guestVA & ~uintptr_t(0xFFF);
    const uint32_t  gen    = EMemory::GetVAGen();
    const size_t    bucket = (vaBase >> 12) & (VA_CACHE_ENTRIES - 1);

    // Sem cache TLS → fall-through para walk completo (seguro, apenas mais lento).
    MemTLS* mt = GetMemTLS();
    if ( mt ) {
        VASlot* slot = &mt->g_vaCache[bucket];

        // Hit: mesma página, mesmo CR3, mesma geração de page cache. paBase != 0
        // descarta slots residuais de population parcial.
        if (slot->vaBase == vaBase && slot->gen == gen && slot->cr3 == cr3 && slot->paBase != 0) {
            guestPA = slot->paBase | (guestVA & uintptr_t(0xFFF));
            return true;
        }

        // Miss: walk completo.
        uintptr_t pa = 0;
        if (!TranslateCR3(guestVA, cr3, pa))
            return false;

        slot->vaBase = vaBase;
        slot->paBase = pa & ~uintptr_t(0xFFF);
        slot->cr3    = cr3;
        slot->gen    = gen;
        guestPA = pa;
        return true;
    }

    // Fallback sem cache (primeiro uso antes de TlsAlloc ou falha de alocação).
    return TranslateCR3(guestVA, cr3, guestPA);
}
