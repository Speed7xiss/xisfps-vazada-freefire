#include "EMemory.hpp"
#include <algorithm>
#include <cstring>
#include <TlHelp32.h>
#include "NTDLLDefines.hpp"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((LONG)0xC0000004L)
#endif
#ifndef DUPLICATE_SAME_ACCESS
#define DUPLICATE_SAME_ACCESS 0x00000002
#endif

// Handles para a estratégia DuplicateHandle:
//   s_hWriteSrc    — handle PROCESS_DUP_HANDLE para o processo fonte (não HD-Player).
//   s_srcHandleVal — valor do handle dentro da tabela do processo fonte.
//   s_hWritePersist— handle de escrita duplicado para HD-Player; reutilizado no hot path.
static HANDLE    s_hWriteSrc     = nullptr;
static ULONG_PTR s_srcHandleVal  = 0;
static HANDLE    s_hWritePersist = nullptr;

// Handle pré-adquirida do shellcode (spoolsv.exe):
//   s_preAcquiredSrc    — lsass/winlogon com PROCESS_DUP_HANDLE pré-aberto
//                          dentro da janela do patch — zero EventID 10 no cheat.
//   s_preAcquiredSrcPid — PID do processo de onde hSrc foi obtido.
// AcquireEMemHandle usa esta handle como fonte preferencial (sem OpenProcess).
// Diferente de s_hWriteSrc: não é fechada pelo CleanupWriteContext() —
// persiste para reuso em múltiplas chamadas de AcquireEMemHandle.
static HANDLE s_preAcquiredSrc    = nullptr;
static DWORD  s_preAcquiredSrcPid = 0;

// Fecha todos os handles do contexto de escrita (chamado antes de re-adquirir ou em Shutdown).
// NÃO fecha s_preAcquiredSrc — ela persiste para múltiplas aquisições.
static void CleanupWriteContext() {
    if ( s_hWritePersist ) { CloseHandle( s_hWritePersist ); s_hWritePersist = nullptr; }
    // Fechar s_hWriteSrc apenas se não é a handle pré-adquirida
    if ( s_hWriteSrc && s_hWriteSrc != s_preAcquiredSrc ) {
        CloseHandle( s_hWriteSrc );
    }
    s_hWriteSrc    = nullptr;
    s_srcHandleVal = 0;
}

// Page cache: amortiza RPM lendo 4KB por miss e servindo reads subsequentes
// da mesma página sem syscall adicional. Invalidado por geração — BeginFrameRead()
// incrementa s_cacheGen em O(1) sem memset, custando ~1ns por frame.
namespace {
    // 4096 buckets × 4KB = 16 MB por thread. Antes eram 1024 (4MB) — com o
    // Producer tocando ~30-100 páginas quentes por tick + Draw + Aim + Chams
    // rodando em threads separadas, o direct-mapped de 1024 colidia com
    // frequência não-trivial (~5-10% dos hits perdidos por eviction de mesma-
    // -bucket-diferente-página). Cada colisão vira 1 ReadProcessMemory extra
    // + 4 ReadRaw do VA→PA walk se s_vaGen também bumpou. 4096 buckets
    // praticamente elimina colisão para o working set do jogo. O overhead de
    // 12 MB extras por thread cabe folgado no mesmo AWE do processo.
    static constexpr size_t PCACHE_BUCKETS = 4096; // deve ser potência de 2
    struct alignas(64) PageCacheSlot {
        uintptr_t base;
        uint32_t  gen;
        uint32_t  _pad;
        BYTE      data[4096];
    };
    // -----------------------------------------------------------------------
    // Thread-local state via dynamic TLS — thread_local (static TLS) falha em
    // DLLs mapeadas manualmente porque o loader não registra o índice TLS para
    // threads criadas após a injeção.  TlsAlloc/TlsGetValue/TlsSetValue
    // funcionam em qualquer contexto.
    // -----------------------------------------------------------------------
    static constexpr size_t WBATCH_MAX_ENTRIES = 128;
    static constexpr size_t WBATCH_DATA_BYTES  = WBATCH_MAX_ENTRIES * 128;

    struct WriteBatchState {
        BatchEntry entries[WBATCH_MAX_ENTRIES];
        BYTE       data[WBATCH_DATA_BYTES];
        size_t     count;
        size_t     dataOff;
        int        depth;
    };

    struct EMemTLS {
        PageCacheSlot*   s_pageCache = nullptr;
        uint32_t         s_cacheGen  = 1;
        uint32_t         s_vaGen     = 1;
        WriteBatchState* s_wbatch    = nullptr;
    };

    static DWORD g_tlsEMem = TLS_OUT_OF_INDEXES;

    // Retorna o bloco TLS da thread corrente, alocando-o na primeira chamada.
    static EMemTLS* GetEMemTLS() {
        // Inicializa o slot TLS na primeira chamada (de qualquer thread).
        if ( g_tlsEMem == TLS_OUT_OF_INDEXES ) {
            static volatile LONG s_lock = 0;
            while ( InterlockedCompareExchange( &s_lock, 1, 0 ) != 0 )
                Sleep( 0 );
            if ( g_tlsEMem == TLS_OUT_OF_INDEXES )
                g_tlsEMem = TlsAlloc();
            InterlockedExchange( &s_lock, 0 );
        }
        if ( g_tlsEMem == TLS_OUT_OF_INDEXES ) return nullptr;

        EMemTLS* tls = static_cast<EMemTLS*>( TlsGetValue( g_tlsEMem ) );
        if ( !tls ) {
            tls = new EMemTLS{};
            TlsSetValue( g_tlsEMem, tls );
        }
        return tls;
    }

    static PageCacheSlot* GetCache() {
        EMemTLS* t = GetEMemTLS();
        if ( !t ) return nullptr;
        if ( !t->s_pageCache )
            t->s_pageCache = new PageCacheSlot[PCACHE_BUCKETS](); // zero-init: gen=0, base=0
        return t->s_pageCache;
    }

    static WriteBatchState* GetWBatch() {
        EMemTLS* t = GetEMemTLS();
        if ( !t ) return nullptr;
        if ( !t->s_wbatch ) t->s_wbatch = new WriteBatchState{};
        return t->s_wbatch;
    }

    // Flush interno — WriteProcessMemory por entrada, sem round-trip ao driver.
    // s_hWritePersist é declarado acima do namespace para evitar erro de
    // declaração-antes-do-uso.
    static void FlushWBatch(WriteBatchState* wb) {
        if (!wb || wb->count == 0) return;
        if ( s_hWritePersist ) {
            for ( size_t i = 0; i < wb->count; i++ ) {
                auto& e = wb->entries[i];
                WriteProcessMemory( s_hWritePersist ,
                    reinterpret_cast<LPVOID>( e.Address ) ,
                    reinterpret_cast<LPCVOID>( e.Buffer ) ,
                    static_cast<SIZE_T>( e.Size ) ,
                    nullptr );
            }
        }
        wb->count   = 0;
        wb->dataOff = 0;
    }

}

static HANDLE AcquireEMemHandle( DWORD targetPid ) {
    if ( !targetPid ) return nullptr;

    // Fecha contexto de escrita anterior antes de abrir novos — evita leak.
    CleanupWriteContext();

    // -----------------------------------------------------------------------
    // Estratégia: DuplicateHandle a partir de um processo que já possui handle
    // aberto para HD-Player. Isso evita chamar OpenProcess diretamente no
    // processo alvo, de modo que o Sysmon não registra Event ID 10 para
    // HD-Player (OB_OPERATION_HANDLE_CREATE não dispara; apenas
    // OB_OPERATION_HANDLE_DUPLICATE, que a maioria das configs não loga para
    // o processo alvo).
    //
    // Ordem de tentativa:
    //  1. Processos de usuário do emulador (LdVBoxHeadless, VBoxSVC, etc.)
    //     → não precisam de SeDebugPrivilege; funcionam mesmo injetados em
    //       Chrome ou qualquer processo sem elevação.
    //  2. Processos SYSTEM (winlogon, wininit, lsass) como fallback.
    //     → requerem SeDebugPrivilege; habilitado antes de tentar.
    // -----------------------------------------------------------------------

    HMODULE hNtdll = GetModuleHandleW( L"ntdll.dll" );
    if ( !hNtdll ) return nullptr;
    auto pNtQSI = reinterpret_cast<NTSTATUS( NTAPI* )( SYSTEM_INFORMATION_CLASS , PVOID , ULONG , PULONG )>(
        GetProcAddress( hNtdll , "NtQuerySystemInformation" ) );
    if ( !pNtQSI ) return nullptr;

    // Lê tabela global de handles uma única vez — reutilizada por todas as tentativas.
    ULONG bufSize = 0; NTSTATUS st;
    std::vector<BYTE> buf;
    st = pNtQSI( SystemExtendedHandleInformation , nullptr , 0 , &bufSize );
    while ( st == STATUS_INFO_LENGTH_MISMATCH ) {
        buf.resize( bufSize );
        st = pNtQSI( SystemExtendedHandleInformation , buf.data( ) , bufSize , &bufSize );
    }
    if ( !NT_SUCCESS( st ) ) return nullptr;
    auto* info = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>( buf.data( ) );

    // Helper: dada a imagem de um processo, retorna seu PID.
    auto findPid = []( const wchar_t* name ) -> DWORD {
        HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );
        if ( snap == INVALID_HANDLE_VALUE ) return 0;
        PROCESSENTRY32W pe = { sizeof( pe ) };
        DWORD pid = 0;
        if ( Process32FirstW( snap , &pe ) )
            do { if ( _wcsicmp( pe.szExeFile , name ) == 0 ) { pid = pe.th32ProcessID; break; } } while ( Process32NextW( snap , &pe ) );
        CloseHandle( snap );
        return pid;
    };

    // Helper: tenta duplicar, a partir de srcPid, um handle que aponte para
    // targetPid. Salva s_hWriteSrc e s_srcHandleVal se bem-sucedido.
    auto tryDupFrom = [&]( DWORD srcPid ) -> HANDLE {
        if ( !srcPid || srcPid == targetPid ) return nullptr;
        HANDLE hSrc = OpenProcess( PROCESS_DUP_HANDLE , FALSE , srcPid );
        if ( !hSrc ) return nullptr;
        for ( ULONG_PTR i = 0; i < info->NumberOfHandles; i++ ) {
            auto* e = &info->Handles[i];
            if ( ( DWORD ) e->UniqueProcessId != srcPid ) continue;
            HANDLE hDup = nullptr;
            if ( !DuplicateHandle( hSrc , ( HANDLE ) e->HandleValue , GetCurrentProcess( ) , &hDup ,
                                   PROCESS_VM_READ | PROCESS_QUERY_INFORMATION , FALSE , 0 ) ) continue;
            if ( GetProcessId( hDup ) != targetPid ) { CloseHandle( hDup ); continue; }
            // Encontrou o handle correto.
            s_hWriteSrc    = hSrc;
            s_srcHandleVal = e->HandleValue;
            return hDup;
        }
        CloseHandle( hSrc );
        return nullptr;
    };

    // Habilita SeDebugPrivilege — necessário para abrir processos SYSTEM
    // (winlogon, wininit, lsass) com PROCESS_DUP_HANDLE.
    HANDLE hToken = nullptr;
    if ( OpenProcessToken( GetCurrentProcess( ) , TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY , &hToken ) ) {
        TOKEN_PRIVILEGES tp = {}; LUID luid = {};
        if ( LookupPrivilegeValueW( nullptr , SE_DEBUG_NAME , &luid ) ) {
            tp.PrivilegeCount = 1; tp.Privileges[0].Luid = luid; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges( hToken , FALSE , &tp , sizeof( tp ) , nullptr , nullptr );
        }
        CloseHandle( hToken );
    }

    // ── Caminho 0: handle pré-adquirida pelo shellcode ──────────────────────
    // Se o shellcode duplicou a handle do lsass/winlogon (PROCESS_DUP_HANDLE)
    // para este processo, usamos ela diretamente — sem OpenProcess(lsass, 0x40)
    // → zero EventID 10 de acesso a processo SYSTEM pelo cheat.
    if ( s_preAcquiredSrc && s_preAcquiredSrcPid ) {
        HANDLE hTmp = nullptr;
        for ( ULONG_PTR i = 0; i < info->NumberOfHandles; i++ ) {
            auto* e = &info->Handles[i];
            if ( ( DWORD ) e->UniqueProcessId != s_preAcquiredSrcPid ) continue;
            if ( !DuplicateHandle( s_preAcquiredSrc , ( HANDLE ) e->HandleValue ,
                                   GetCurrentProcess( ) , &hTmp ,
                                   PROCESS_VM_READ | PROCESS_QUERY_INFORMATION ,
                                   FALSE , 0 ) ) continue;
            if ( GetProcessId( hTmp ) != targetPid ) { CloseHandle( hTmp ); hTmp = nullptr; continue; }
            // Encontrou — s_preAcquiredSrc é a fonte
            s_hWriteSrc    = s_preAcquiredSrc;
            s_srcHandleVal = e->HandleValue;
            break;
        }
        if ( hTmp ) {
            // Cria handle de escrita persistente via mesma fonte
            DuplicateHandle( s_hWriteSrc , ( HANDLE ) s_srcHandleVal ,
                             GetCurrentProcess( ) , &s_hWritePersist ,
                             PROCESS_VM_WRITE | PROCESS_VM_OPERATION , FALSE , 0 );
            return hTmp;
        }
        // Se não encontrou a handle do HD-Player no processo pré-adquirido,
        // o HD-Player pode ter reiniciado em outro PID — cai no fallback abaixo.
    }
    // ────────────────────────────────────────────────────────────────────────

    HANDLE h = tryDupFrom( findPid( L"winlogon.exe" ) );
    if ( !h ) h = tryDupFrom( findPid( L"wininit.exe" ) );
    if ( !h ) h = tryDupFrom( findPid( L"lsass.exe" ) );

    // Cria imediatamente o handle persistente de escrita a partir da mesma
    // fonte — evita custo de DuplicateHandle no hot path de escrita.
    if ( h && s_hWriteSrc && s_srcHandleVal ) {
        DuplicateHandle( s_hWriteSrc , ( HANDLE ) s_srcHandleVal , GetCurrentProcess( ) , &s_hWritePersist ,
                         PROCESS_VM_WRITE | PROCESS_VM_OPERATION , FALSE , 0 );
    }

    return h;
}

// Retorna o handle de escrita persistente (cacheado). Se ainda não existe
// (ex.: primeiro uso ou re-inicialização), tenta criar via s_hWriteSrc.
// NUNCA CloseHandle no retorno — é compartilhado, gerenciado por Housekeeping/Shutdown.
HANDLE GetPersistentWriteHandle() {
    if ( s_hWritePersist ) return s_hWritePersist;
    if ( !s_hWriteSrc || !s_srcHandleVal ) return nullptr;
    DuplicateHandle( s_hWriteSrc , ( HANDLE ) s_srcHandleVal , GetCurrentProcess( ) , &s_hWritePersist ,
                     PROCESS_VM_WRITE | PROCESS_VM_OPERATION , FALSE , 0 );
    return s_hWritePersist;
}

// Compat: call-sites antigos ainda chamam AcquireTransientWriteHandle.
// Agora retorna o handle persistente — call-sites NÃO devem CloseHandle no retorno.
HANDLE AcquireTransientWriteHandle() {
    return GetPersistentWriteHandle();
}

void EMemory::BeginFrameRead() {
    EMemTLS* t = GetEMemTLS();
    if ( !t ) return;
    ++t->s_cacheGen;
    if ( t->s_cacheGen == 0 ) t->s_cacheGen = 1;

    // FIX FLICKER: VA→PA gen bumpa em LOCKSTEP com s_cacheGen (comportamento
    // do projeto antigo). Throttle de 32 frames deixava traduções stale por
    // até ~320ms — se guest remapeasse páginas de heap, reads seguiam pra
    // páginas físicas liberadas/reutilizadas retornando lixo → entity some.
    ++t->s_vaGen;
    if ( t->s_vaGen == 0 ) t->s_vaGen = 1;
}

// Exposto para o VA→PA cache em Memory::TranslateCR3Cached — permite invalidar
// entradas em lockstep com o page cache 4KB (mesma s_cacheGen por thread).
uint32_t EMemory::GetReadGen() {
    EMemTLS* t = GetEMemTLS();
    return t ? t->s_cacheGen : 1;
}

uint32_t EMemory::GetVAGen() {
    EMemTLS* t = GetEMemTLS();
    return t ? t->s_vaGen : 1;
}

// No-ops mantidos pela API. O handle persistente já elimina o motivo original
// de existir batch semantics (evitar N DuplicateHandle/CloseHandle em sequência).
void EMemory::BeginWriteBatch() { }
void EMemory::EndWriteBatch()   { }

void EMemory::Housekeeping() {
    // Roda em thread dedicada de baixa prioridade a cada ~10s.
    //
    // 1) Refresh do handle de leitura se invalidou (emulador reiniciou).
    //    Sem isso, todos os reads começariam a falhar após restart do HD-Player
    //    e o cheat só voltaria a funcionar depois de re-injeção.
    bool needReRead = false;
    if ( hProcess ) {
        DWORD ec = 0;
        if ( !GetExitCodeProcess( hProcess , &ec ) || ec != STILL_ACTIVE ) {
            CloseHandle( hProcess );
            hProcess   = nullptr;
            needReRead = true;
        }
    } else if ( ProcessPid ) {
        needReRead = true;
    }
    if ( needReRead && ProcessPid ) {
        // Se os handles vieram do caminho pré-adquirido (shellcode), s_hWriteSrc
        // é null. Nesse caso usa OpenProcess direto — evita varredura de
        // winlogon/lsass. Gera EventID 10 com callstack UNKNOWN, mas é um evento
        // único por reinício do emulador (não o padrão periódico detectável).
        if ( !s_hWriteSrc ) {
            HANDLE h = OpenProcess(
                PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, ProcessPid );
            if ( h ) {
                hProcess = h;
                // Refresh do write handle se também expirou.
                if ( !s_hWritePersist ) {
                    s_hWritePersist = OpenProcess(
                        PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, ProcessPid );
                }
            } else {
                // Fallback completo via winlogon/lsass (último recurso).
                h = AcquireEMemHandle( ProcessPid );
                if ( h ) hProcess = h;
            }
        } else {
            // Caminho original: re-adquire via DuplicateHandle de winlogon/lsass.
            HANDLE h = AcquireEMemHandle( ProcessPid );
            if ( h ) hProcess = h;
        }
    }

    // 2) Refresh do handle de escrita se invalidou. AcquireEMemHandle acima
    //    já reobteve via CleanupWriteContext, mas se só o write handle
    //    ficou stale (raro, mas possível) força reobtenção lazy.
    if ( s_hWritePersist ) {
        DWORD ec = 0;
        if ( !GetExitCodeProcess( s_hWritePersist , &ec ) || ec != STILL_ACTIVE ) {
            CloseHandle( s_hWritePersist );
            s_hWritePersist = nullptr;
        }
    }

    // 3) Se NegativeChunkCache saturou (>50% populado), zera. Cache negativo
    //    é otimização — reduzir hit rate temporariamente é preferível a
    //    servir entries stale por horas.
    int populated = 0;
    for ( int i = 0; i < NEG_CACHE_SIZE; i++ )
        if ( NegativeChunkCache[i] ) populated++;
    if ( populated > NEG_CACHE_SIZE / 2 ) {
        memset( NegativeChunkCache , 0 , sizeof( NegativeChunkCache ) );
    }
}

HANDLE EMemory::hProcess = nullptr;
DWORD EMemory::ProcessPid = 0;
uintptr_t EMemory::VMM = 0;
uintptr_t EMemory::KernelBase = 0xFFFFFFFF80000000ULL;
std::vector<EMemory::RangeCacheEntry> EMemory::RangeCache;
bool EMemory::RangeCacheBuilt = false;
bool EMemory::ChunkTreeFound = false;

// FlatChunkMap: O(1) chunk lookup (matching reference VBoxMemory pattern)
uintptr_t EMemory::FlatChunkMap[MAX_CHUNK_ID] = {};
uint32_t EMemory::FlatChunkMapCount = 0;
uintptr_t EMemory::ChunkAVLTreeRootOffset = 0;
uintptr_t EMemory::ChunkTreeRootNode = 0;
std::unordered_set<uintptr_t> EMemory::ChunkVisited;

uint32_t EMemory::NegativeChunkCache[NEG_CACHE_SIZE] = {};

uintptr_t EMemory::PagesArrayOffset = 0;
bool EMemory::PagesArrayOffsetValidated = false;

uintptr_t EMemory::ActiveBstLeft = 0;
uintptr_t EMemory::ActiveBstRight = 0;
uintptr_t EMemory::ActiveBstRoot = 0;

uint32_t EMemory::CHUNK_SIZE = 0;
uint32_t EMemory::PAGE_ENTRY_SIZE = 0;
size_t EMemory::CHUNKNODE_PV_OFF = 0;
size_t EMemory::CHUNKNODE_KEY_OFF = 0;
size_t EMemory::CHUNKNODE_LEFT_OFF = 0;
size_t EMemory::CHUNKNODE_RIGHT_OFF = 0;
size_t EMemory::CHUNKNODE_SIZE = 0;
uint64_t EMemory::PAGE_ADDR_MASK = 0;

uint64_t EMemory::DmapBase = 0;
uint64_t EMemory::DmapEnd = 0;
uint64_t EMemory::KtextBase = 0;
uint64_t EMemory::KtextEnd = 0;

EMemory::BSTConfig EMemory::ConfigA = {};
EMemory::BSTConfig EMemory::ConfigB = {};

void EMemory::SetChunkParams(uint32_t pvOff, uint32_t pageEntrySz, uint32_t chunkSz) {
    CHUNKNODE_PV_OFF = pvOff;
    PAGE_ENTRY_SIZE = pageEntrySz;
    CHUNK_SIZE = chunkSz;
}

void EMemory::SetBSTConfigs(
    uintptr_t rootA, uintptr_t leftA, uintptr_t rightA, uintptr_t pageA,
    uintptr_t rootB, uintptr_t leftB, uintptr_t rightB, uintptr_t pageB
) {
    ConfigA = { rootA, leftA, rightA, pageA };
    ConfigB = { rootB, leftB, rightB, pageB };
}

void EMemory::SetTranslateParams(uint64_t dmapBase, uint64_t dmapEnd, uint64_t ktextBase, uint64_t ktextEnd) {
    DmapBase = dmapBase;
    DmapEnd = dmapEnd;
    KtextBase = ktextBase;
    KtextEnd = ktextEnd;
}

void EMemory::SetChunkNodeParams(uint32_t keyOff, uint32_t leftOff, uint32_t rightOff, uint32_t nodeSize, uint64_t pageMask) {
    CHUNKNODE_KEY_OFF = keyOff;
    CHUNKNODE_LEFT_OFF = leftOff;
    CHUNKNODE_RIGHT_OFF = rightOff;
    CHUNKNODE_SIZE = nodeSize;
    PAGE_ADDR_MASK = pageMask;
}

uint64_t EMemory::StatChunkMapHits = 0;
uint64_t EMemory::StatAVLFallbacks = 0;
uint64_t EMemory::StatReadFails = 0;

void EMemory::Init(DWORD pid, uintptr_t kernelBase) {
    ProcessPid = pid;

    // ── Handle cache: evita re-adquirir handles se já válidos ────────────────
    // AcquireEMemHandle usa s_preAcquiredSrc (handle do lsass/winlogon passada
    // pelo shellcode) como fonte preferencial — zero EventID 10 no cheat.
    // Se os handles de leitura e escrita já estão válidos para este PID,
    // reutiliza-os sem nova chamada de acesso (ciclos de retry do CheatManager).
    {
        bool rOk = false, wOk = false;
        if ( hProcess ) {
            DWORD ec = 0;
            rOk = GetExitCodeProcess( hProcess, &ec ) && ec == STILL_ACTIVE
                  && GetProcessId( hProcess ) == pid;
        }
        HANDLE hW = GetWriteHandle();
        if ( hW ) {
            DWORD ec = 0;
            wOk = GetExitCodeProcess( hW, &ec ) && ec == STILL_ACTIVE;
        }
        if ( !rOk || !wOk ) {
            // Fecha handle de leitura inválido antes de re-adquirir.
            // AcquireEMemHandle já chama CleanupWriteContext() internamente.
            if ( !rOk && hProcess ) { CloseHandle( hProcess ); hProcess = nullptr; }
            HANDLE h = AcquireEMemHandle( pid );
            if ( h ) hProcess = h;
        }
        // Se ambos válidos: reutiliza handles existentes — sem EventID 10.
    }
    // ─────────────────────────────────────────────────────────────────────────

    KernelBase = kernelBase;
    RangeCache.clear();
    RangeCacheBuilt = false;
    ChunkTreeFound = false;
    FlatChunkMapCount = 0;
    ChunkAVLTreeRootOffset = 0;
    ChunkTreeRootNode = 0;
    ChunkVisited.clear();
    memset(FlatChunkMap, 0, sizeof(FlatChunkMap));
    memset(NegativeChunkCache, 0, sizeof(NegativeChunkCache));
    PagesArrayOffsetValidated = false;
    StatChunkMapHits = 0;
    StatAVLFallbacks = 0;
    StatReadFails = 0;
    BeginFrameRead();
}

void EMemory::Shutdown() {
    ProcessPid = 0;
    RangeCache.clear();
    ChunkVisited.clear();
    memset(FlatChunkMap, 0, sizeof(FlatChunkMap));
    if ( hProcess ) { CloseHandle( hProcess ); hProcess = nullptr; }
    CleanupWriteContext();
    // Fecha a handle pré-adquirida do shellcode — o processo (lsass/winlogon)
    // continua rodando, mas não precisaremos mais dela após o destruct.
    if ( s_preAcquiredSrc ) { CloseHandle( s_preAcquiredSrc ); s_preAcquiredSrc = nullptr; }
    s_preAcquiredSrcPid = 0;
}

void EMemory::SetVMM(uintptr_t vmm) {
    VMM = vmm;
}

uintptr_t EMemory::TranslateKernelVA(uintptr_t va) {
    if (va >= DmapBase && va < DmapEnd) {
        return va - DmapBase;
    }
    if (va >= KtextBase && va < KtextEnd) {
        return va - KtextBase;
    }
    return 0;
}

// ==================== Range Cache ====================

void EMemory::WalkBST(uintptr_t nodePtr) {
    // Iterativo (pilha explícita): substituição da versão recursiva que causava
    // stack overflow quando o emulador está aberto — a árvore BST do VirtualBox
    // pode ter milhares de nós (um por range de RAM), e a recursão sem limite de
    // profundidade estourava a stack de 1MB da thread do Cheat::Initialize.
    std::vector<uintptr_t> stack;
    std::unordered_set<uintptr_t> visited;
    stack.reserve(256);
    if (nodePtr != 0 && nodePtr >= 0x10000 && nodePtr < 0xFFFF800000000000ULL)
        stack.push_back(nodePtr);

    while (!stack.empty()) {
        uintptr_t cur = stack.back(); stack.pop_back();
        if (cur == 0 || cur < 0x10000 || cur >= 0xFFFF800000000000ULL) continue;
        if (!visited.insert(cur).second) continue;  // já visitado

        PGMRAMRANGE range{};
        if (!InternalReadRaw(cur, &range, sizeof(PGMRAMRANGE))) continue;
        if (range.cb == 0 || range.GCPhys > 0x200000000ULL) continue;

        uintptr_t left  = InternalRead<uintptr_t>(cur + ActiveBstLeft);
        uintptr_t right = InternalRead<uintptr_t>(cur + ActiveBstRight);

        RangeCacheEntry entry;
        entry.GCPhys    = range.GCPhys;
        entry.End       = range.GCPhys + range.cb;
        entry.RangeAddr = cur;
        entry.Range     = range;
        RangeCache.push_back(entry);

        if (right && right >= 0x10000 && right < 0xFFFF800000000000ULL) stack.push_back(right);
        if (left  && left  >= 0x10000 && left  < 0xFFFF800000000000ULL) stack.push_back(left);
    }
}

void EMemory::BuildRangeCache() {
    struct OffsetConfig {
        uintptr_t bstRoot;
        uintptr_t bstLeft;
        uintptr_t bstRight;
        uintptr_t pageBase;
    };

    OffsetConfig configs[2];
    configs[0].bstRoot = ConfigA.root;
    configs[0].bstLeft = ConfigA.left;
    configs[0].bstRight = ConfigA.right;
    configs[0].pageBase = ConfigA.page;

    configs[1].bstRoot = ConfigB.root;
    configs[1].bstLeft = ConfigB.left;
    configs[1].bstRight = ConfigB.right;
    configs[1].pageBase = ConfigB.page;

    int bestConfigIdx = -1;
    int bestRangeCount = 0;

    for (int ci = 0; ci < 2; ci++) {
        ActiveBstRoot = configs[ci].bstRoot;
        ActiveBstLeft = configs[ci].bstLeft;
        ActiveBstRight = configs[ci].bstRight;

        uintptr_t root = InternalRead<uintptr_t>(VMM + ActiveBstRoot);

        if (root == 0 || root < 0x10000 || root >= 0xFFFF800000000000ULL)
            continue;

        RangeCache.clear();
        RangeCacheBuilt = false;
        WalkBST(root);

        int validRanges = 0;
        for (auto& entry : RangeCache) {
            if (entry.Range.cb > 0 && entry.Range.cb <= 0x200000000ULL)
                validRanges++;
        }

        if (validRanges > bestRangeCount) {
            bestRangeCount = validRanges;
            bestConfigIdx = ci;
        }
    }

    if (bestConfigIdx < 0) {
        RangeCacheBuilt = true;
        return;
    }

    ActiveBstRoot = configs[bestConfigIdx].bstRoot;
    ActiveBstLeft = configs[bestConfigIdx].bstLeft;
    ActiveBstRight = configs[bestConfigIdx].bstRight;
    PagesArrayOffset = configs[bestConfigIdx].pageBase;

    RangeCache.clear();
    memset(FlatChunkMap, 0, sizeof(FlatChunkMap));
    FlatChunkMapCount = 0;

    uintptr_t root = InternalRead<uintptr_t>(VMM + ActiveBstRoot);
    WalkBST(root);

    std::sort(RangeCache.begin(), RangeCache.end(),
        [](const RangeCacheEntry& a, const RangeCacheEntry& b) {
            return a.GCPhys < b.GCPhys;
        });

    RangeCacheBuilt = true;

    bool needChunks = false;
    for (auto& entry : RangeCache) {
        if (entry.Range.pvR3 == 0 && entry.Range.cb > 0x100000) needChunks = true;
    }

    if (needChunks) {
        DiscoverChunkTree();
        PagesArrayOffsetValidated = true;
    }
}

// ==================== Chunk Discovery ====================

int EMemory::CountChunkNodes(uintptr_t nodePtr, std::unordered_set<uintptr_t>& visited) {
    // Iterativo: mesma razão de WalkBST — a árvore de chunks do VirtualBox
    // pode ter milhares de nós (1 por MB de RAM da VM) e a versão recursiva
    // estourava a stack quando o emulador estava aberto.
    std::vector<uintptr_t> stack;
    stack.reserve(256);
    if (nodePtr != 0 && nodePtr >= 0x10000 && nodePtr < 0xFFFF800000000000ULL)
        stack.push_back(nodePtr);

    int count = 0;
    while (!stack.empty()) {
        uintptr_t cur = stack.back(); stack.pop_back();
        if (cur == 0 || cur < 0x10000 || cur >= 0xFFFF800000000000ULL) continue;
        if (!visited.insert(cur).second) continue;

        uint8_t nodeBuf[64] = {};
        if (!InternalReadRaw(cur, nodeBuf, 64)) continue;

        uint32_t  nodeId = *reinterpret_cast<uint32_t*> (nodeBuf + CHUNKNODE_KEY_OFF);
        uintptr_t pvR3   = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_PV_OFF);
        uintptr_t pLeft  = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_LEFT_OFF);
        uintptr_t pRight = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_RIGHT_OFF);

        if (nodeId >= MAX_CHUNK_ID || pvR3 == 0 || pvR3 >= 0xFFFF800000000000ULL || (pvR3 & 0xFFF) != 0)
            continue;

        count++;
        if (pRight && pRight >= 0x10000 && pRight < 0xFFFF800000000000ULL) stack.push_back(pRight);
        if (pLeft  && pLeft  >= 0x10000 && pLeft  < 0xFFFF800000000000ULL) stack.push_back(pLeft);
    }
    return count;
}

void EMemory::PopulateFlatChunkMap(uintptr_t nodePtr) {
    // Iterativo: mesma razão de WalkBST e CountChunkNodes.
    std::vector<uintptr_t> stack;
    stack.reserve(256);
    if (nodePtr != 0 && nodePtr >= 0x10000 && nodePtr < 0xFFFF800000000000ULL)
        stack.push_back(nodePtr);

    while (!stack.empty()) {
        uintptr_t cur = stack.back(); stack.pop_back();
        if (cur == 0 || cur < 0x10000 || cur >= 0xFFFF800000000000ULL) continue;
        if (!ChunkVisited.insert(cur).second) continue;

        uint8_t nodeBuf[64] = {};
        if (!InternalReadRaw(cur, nodeBuf, 64)) continue;

        uint32_t  nodeId = *reinterpret_cast<uint32_t*> (nodeBuf + CHUNKNODE_KEY_OFF);
        uintptr_t pvR3   = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_PV_OFF);
        uintptr_t pLeft  = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_LEFT_OFF);
        uintptr_t pRight = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_RIGHT_OFF);

        if (nodeId < MAX_CHUNK_ID && pvR3 != 0 && pvR3 < 0xFFFF800000000000ULL && (pvR3 & 0xFFF) == 0) {
            FlatChunkMap[nodeId] = pvR3;
            FlatChunkMapCount++;
        }

        if (pRight && pRight >= 0x10000 && pRight < 0xFFFF800000000000ULL) stack.push_back(pRight);
        if (pLeft  && pLeft  >= 0x10000 && pLeft  < 0xFFFF800000000000ULL) stack.push_back(pLeft);
    }
}

uintptr_t EMemory::WalkAVLTreeForChunk(uint32_t chunkId) {
    if (ChunkTreeRootNode == 0 || !ChunkTreeFound) return 0;

    uintptr_t nodePtr = InternalRead<uintptr_t>(VMM + ChunkAVLTreeRootOffset);
    if (nodePtr == 0 || nodePtr < 0x10000 || nodePtr >= 0xFFFF800000000000ULL)
        return 0;

    int depth = 0;
    while (nodePtr != 0 && depth < 32) {
        if (nodePtr < 0x10000 || nodePtr >= 0xFFFF800000000000ULL)
            return 0;

        uint8_t nodeBuf[64] = {};
        if (!InternalReadRaw(nodePtr, nodeBuf, 64))
            return 0;

        uint32_t nodeId = *reinterpret_cast<uint32_t*>(nodeBuf + CHUNKNODE_KEY_OFF);
        uintptr_t pvR3 = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_PV_OFF);

        if (nodeId == chunkId) {
            if (chunkId < MAX_CHUNK_ID && pvR3 != 0 && pvR3 < 0xFFFF800000000000ULL && (pvR3 & 0xFFF) == 0) {
                FlatChunkMap[chunkId] = pvR3;
            }
            return pvR3;
        }

        if (chunkId < nodeId)
            nodePtr = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_LEFT_OFF);
        else
            nodePtr = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_RIGHT_OFF);

        depth++;
    }
    return 0;
}

uintptr_t EMemory::LookupChunkPv(uint32_t idChunk) {
    if (idChunk < MAX_CHUNK_ID && FlatChunkMap[idChunk] != 0) {
        StatChunkMapHits++;
        return FlatChunkMap[idChunk];
    }

    uint32_t negSlot = idChunk % NEG_CACHE_SIZE;
    if (NegativeChunkCache[negSlot] == idChunk && idChunk != 0) {
        return 0;
    }

    StatAVLFallbacks++;
    uintptr_t pv = WalkAVLTreeForChunk(idChunk);
    if (pv != 0) {
        return pv;
    }

    if (idChunk != 0) {
        NegativeChunkCache[negSlot] = idChunk;
    }
    return 0;
}

struct ChunkCandidate {
    uintptr_t offset;
    uintptr_t ptr;
    int reachable;
};

static bool CandidateCompare(const ChunkCandidate& a, const ChunkCandidate& b) {
    return a.reachable > b.reachable;
}

bool EMemory::DiscoverChunkTree() {
    if (RangeCache.empty()) return false;

    memset(FlatChunkMap, 0, sizeof(FlatChunkMap));
    FlatChunkMapCount = 0;
    ChunkVisited.clear();
    ChunkTreeRootNode = 0;
    ChunkAVLTreeRootOffset = 0;
    memset(NegativeChunkCache, 0, sizeof(NegativeChunkCache));

    std::vector<ChunkCandidate> candidates;
    std::unordered_set<uintptr_t> testedPtrs;

    for (uintptr_t off = 0x800; off < 0x8000; off += 8) {
        uintptr_t ptr = InternalRead<uintptr_t>(VMM + off);
        if (ptr == 0 || ptr < 0x10000 || ptr >= 0xFFFF800000000000ULL) continue;
        if (testedPtrs.count(ptr)) continue;
        testedPtrs.insert(ptr);

        uint8_t nodeBuf[64] = {};
        if (!InternalReadRaw(ptr, nodeBuf, 64)) continue;

        uint32_t nodeId = *reinterpret_cast<uint32_t*>(nodeBuf + CHUNKNODE_KEY_OFF);
        uintptr_t pvR3 = *reinterpret_cast<uintptr_t*>(nodeBuf + CHUNKNODE_PV_OFF);

        if (nodeId < MAX_CHUNK_ID &&
            pvR3 > 0x100000 && pvR3 < 0xFFFF800000000000ULL &&
            (pvR3 & 0xFFF) == 0)
        {
            std::unordered_set<uintptr_t> tempVisited;
            int count = CountChunkNodes(ptr, tempVisited);
            if (count > 0) {
                ChunkCandidate c;
                c.offset = off;
                c.ptr = ptr;
                c.reachable = count;
                candidates.push_back(c);
            }
        }
    }

    if (candidates.empty()) return false;

    std::sort(candidates.begin(), candidates.end(), CandidateCompare);

    ChunkAVLTreeRootOffset = candidates[0].offset;
    ChunkTreeRootNode = candidates[0].ptr;

    PopulateFlatChunkMap(candidates[0].ptr);

    ChunkTreeFound = (FlatChunkMapCount > 0);
    return ChunkTreeFound;
}

// ==================== PagesArrayOffset Auto-Detection ====================

bool EMemory::AutoDetectPagesArrayOffset() {
    if (FlatChunkMapCount == 0) return false;

    uintptr_t mainRangeAddr = 0;
    uint64_t mainRangeGCPhys = 0;
    uint64_t mainRangeCb = 0;
    for (size_t ri = 0; ri < RangeCache.size(); ri++) {
        if (RangeCache[ri].Range.pvR3 == 0 && RangeCache[ri].Range.cb > 0x1000000) {
            mainRangeAddr = RangeCache[ri].RangeAddr;
            mainRangeGCPhys = RangeCache[ri].Range.GCPhys;
            mainRangeCb = RangeCache[ri].Range.cb;
            break;
        }
    }
    if (mainRangeAddr == 0) return false;

    const uint32_t testPages[] = { 0x1000, 0x1100, 0x1200, 0x2000, 0x2100, 0x3000, 0x4000, 0x5000 };
    const int numTestPages = 8;

    uintptr_t bestOffset = PagesArrayOffset;
    int bestScoreA = 0;
    int bestScoreB = 0;

    for (uintptr_t candidateOff = 0x38; candidateOff <= 0x200; candidateOff += 8) {
        int scoreA = 0;
        int scoreB = 0;

        for (int ti = 0; ti < numTestPages; ti++) {
            uint32_t pageIdx = testPages[ti];
            uintptr_t pageEntryAddr = mainRangeAddr + candidateOff + (uintptr_t)pageIdx * PAGE_ENTRY_SIZE;

            PGMPAGE page = {};
            if (!InternalReadRaw(pageEntryAddr, &page, sizeof(page)))
                continue;

            uint32_t state = PageGetState(page.uStateY);
            uint32_t idChunk = page.idxAndId >> 9;
            uint32_t pageInChunk = page.idxAndId & 0x1FF;

            if ((state == PGM_PAGE_STATE_ALLOCATED || state == PGM_PAGE_STATE_WRITE_MONITORED) &&
                idChunk < (uint32_t)MAX_CHUNK_ID &&
                pageInChunk < PAGES_PER_CHUNK &&
                FlatChunkMap[idChunk] != 0)
            {
                scoreA++;
            }

            uint64_t hcPhys = page.uStateY & PAGE_ADDR_MASK;
            if (page.uStateY != 0 && hcPhys != 0 && hcPhys < 0x800000000ULL &&
                state != PGM_PAGE_STATE_ZERO)
            {
                scoreB++;
            }
        }

        if (scoreA > bestScoreA) {
            bestScoreA = scoreA;
            bestOffset = candidateOff;
        }
        if (scoreA == 0 && scoreB > bestScoreB) {
            bestScoreB = scoreB;
            if (bestScoreA == 0) {
                bestOffset = candidateOff;
            }
        }
    }

    PagesArrayOffset = bestOffset;
    PagesArrayOffsetValidated = (bestScoreA > 0);

    return bestScoreA > 0 || bestScoreB > 0;
}

// ==================== Range Lookup ====================

uintptr_t EMemory::GetRangeAtOrAbove(uint64_t gcPhys, PGMRAMRANGE* outRange) {
    *outRange = {};

    if (RangeCacheBuilt) {
        int lo = 0, hi = (int)RangeCache.size() - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            auto& entry = RangeCache[mid];
            if (gcPhys >= entry.GCPhys && gcPhys < entry.End) {
                *outRange = entry.Range;
                return entry.RangeAddr;
            }
            else if (gcPhys < entry.GCPhys) {
                hi = mid - 1;
            }
            else {
                lo = mid + 1;
            }
        }
        if (lo < (int)RangeCache.size()) {
            *outRange = RangeCache[lo].Range;
            return RangeCache[lo].RangeAddr;
        }
        return 0;
    }

    uintptr_t nodePtr = InternalRead<uintptr_t>(VMM + ActiveBstRoot);
    uintptr_t candidate = 0;

    while (nodePtr) {
        if (nodePtr < 0x10000 || nodePtr >= 0xFFFF800000000000ULL)
            return 0;
        if (!InternalReadRaw(nodePtr, outRange, sizeof(PGMRAMRANGE)))
            return 0;

        int64_t off = (int64_t)(gcPhys - outRange->GCPhys);
        if ((uint64_t)off < outRange->cb) {
            return nodePtr;
        }

        if (off < 0) {
            candidate = nodePtr;
            nodePtr = InternalRead<uintptr_t>(nodePtr + ActiveBstLeft);
        }
        else {
            nodePtr = InternalRead<uintptr_t>(nodePtr + ActiveBstRight);
        }
    }

    if (candidate)
        InternalReadRaw(candidate, outRange, sizeof(PGMRAMRANGE));
    return candidate;
}

// ==================== ReadRaw ====================

bool EMemory::ReadRawKernel(uint64_t kernelVA, void* outBuf, size_t cbRead) {
    return ReadRaw(TranslateKernelVA(kernelVA), outBuf, cbRead);
}

bool EMemory::ReadRaw(uint64_t GCPhys, void* outBuf, size_t cbRead) {
    uint8_t* out = (uint8_t*)outBuf;

    while (cbRead > 0) {
        PGMRAMRANGE range{};
        uintptr_t rangeAddr = GetRangeAtOrAbove(GCPhys, &range);

        if (!rangeAddr) {
            memset(out, 0xFF, cbRead);
            return false;
        }

        if (GCPhys < range.GCPhys) {
            size_t gap = (size_t)(range.GCPhys - GCPhys);
            if (gap >= cbRead) {
                memset(out, 0xFF, cbRead);
                return true;
            }
            memset(out, 0xFF, gap);
            out += gap;
            cbRead -= gap;
            GCPhys = range.GCPhys;
        }

        uint64_t off = GCPhys - range.GCPhys;

        while (off < range.cb && cbRead > 0) {
            uint32_t pageOffset = (uint32_t)(off & 0xFFF);
            size_t cb = (4096 - pageOffset < cbRead) ? (4096 - pageOffset) : cbRead;
            uint64_t hostPtr = 0;

            if (range.pvR3 != 0) {
                hostPtr = range.pvR3 + off;
            }
            else {
                uint32_t pageIndex = (uint32_t)(off >> 12);
                uintptr_t pageEntryAddr = rangeAddr + PagesArrayOffset + (uintptr_t)pageIndex * PAGE_ENTRY_SIZE;

                PGMPAGE page{};
                InternalReadRaw(pageEntryAddr, &page, sizeof(page));

                uint32_t idChunk = page.idxAndId >> 9;
                uint32_t pageInChunk = page.idxAndId & 0x1FF;

                uintptr_t chunkPv = LookupChunkPv(idChunk);
                if (chunkPv != 0) {
                    uint64_t offsetInChunk = ((uint64_t)pageInChunk << 12) | pageOffset;
                    if (offsetInChunk < CHUNK_SIZE) {
                        hostPtr = chunkPv + offsetInChunk;
                    }
                }

                if (hostPtr == 0) {
                    StatReadFails++;
                    memset(out, 0x00, cb);
                    out += cb;
                    cbRead -= cb;
                    off += cb;
                    continue;
                }
            }

            if (!InternalReadRaw(hostPtr, out, cb)) {
                memset(out, 0xFF, cb);
            }

            out += cb;
            cbRead -= cb;
            off += cb;
        }

        GCPhys = range.GCPhysLast + 1;
    }

    return true;
}

bool EMemory::WriteRaw(uint64_t GCPhys, const void* inBuf, size_t cbWrite) {
    if (!inBuf || cbWrite == 0) return false;

    const uint8_t* src = (const uint8_t*)inBuf;

    while (cbWrite > 0) {
        PGMRAMRANGE range{};
        uintptr_t rangeAddr = GetRangeAtOrAbove(GCPhys, &range);
        if (!rangeAddr) return false;
        if (GCPhys < range.GCPhys) return false;

        uint64_t off = GCPhys - range.GCPhys;

        while (off < range.cb && cbWrite > 0) {
            uint32_t pageOffset = (uint32_t)(off & 0xFFF);
            size_t cb = (4096 - pageOffset < cbWrite) ? (4096 - pageOffset) : cbWrite;
            uint64_t hostPtr = 0;

            if (range.pvR3 != 0) {
                hostPtr = range.pvR3 + off;
            }
            else {
                uint32_t pageIndex = (uint32_t)(off >> 12);
                uintptr_t pageEntryAddr = rangeAddr + PagesArrayOffset + (uintptr_t)pageIndex * PAGE_ENTRY_SIZE;

                PGMPAGE page{};
                InternalReadRaw(pageEntryAddr, &page, sizeof(page));

                uint32_t idChunk = page.idxAndId >> 9;
                uint32_t pageInChunk = page.idxAndId & 0x1FF;

                uintptr_t chunkPv = LookupChunkPv(idChunk);
                if (chunkPv != 0) {
                    uint64_t offsetInChunk = ((uint64_t)pageInChunk << 12) | pageOffset;
                    if (offsetInChunk < CHUNK_SIZE)
                        hostPtr = chunkPv + offsetInChunk;
                }

                if (hostPtr == 0) return false;
            }

            // Handle persistente — sem DuplicateHandle/CloseHandle por write.
            HANDLE hW = GetPersistentWriteHandle();
            if ( !hW ) return false;
            WriteProcessMemory( hW , ( LPVOID ) hostPtr , ( PVOID ) src , cb , nullptr );
            src += cb;
            cbWrite -= cb;
            off += cb;
        }

        GCPhys = range.GCPhysLast + 1;
    }

    return true;
}

bool EMemory::InternalReadRaw(uintptr_t addr, void* out, size_t size) {
    if ( !size ) return true;
    if ( !hProcess ) return false;

    uintptr_t pageBase = addr & ~(uintptr_t)0xFFF;
    size_t    pageOff  = addr & 0xFFF;

    // Fast path: leitura cabe inteiramente em uma página 4 KB.
    // Cache hit = memcpy local (~ns). Miss = 1 ReadProcessMemory de 4 KB.
    if ( pageOff + size <= 4096 ) {
        EMemTLS*       etls   = GetEMemTLS();
        uint32_t       cGen   = etls ? etls->s_cacheGen : 1;
        PageCacheSlot* cache  = GetCache();
        if ( !cache ) goto slow_path;
        {
            size_t         bucket = (pageBase >> 12) & (PCACHE_BUCKETS - 1);
            PageCacheSlot* slot   = &cache[bucket];

            if ( slot->gen == cGen && slot->base == pageBase ) {
                memcpy( out , slot->data + pageOff , size );
                return true;
            }

            // Miss — ReadProcessMemory da página inteira (4 KB) e cacheia.
            // FIX FLICKER: Só cacheia se a leitura retornar EXATAMENTE 4096 bytes.
            // Aceitar leitura parcial (bytesRead > 0) envenena o cache: bytes não
            // preenchidos ficam com o conteúdo da página anterior no bucket ou com
            // zeros de inicialização — reads subsequentes daquela região retornam
            // lixo e fazem entity chain falhar → pisca.
            SIZE_T bytesRead = 0;
            if ( ReadProcessMemory( hProcess , (LPCVOID)pageBase ,
                                    slot->data , 4096 , &bytesRead )
                 && bytesRead == 4096 ) {
                slot->base = pageBase;
                slot->gen  = cGen;
                memcpy( out , slot->data + pageOff , size );
                return true;
            }
        }
        return false;
    }
    slow_path:

    // Slow path: leitura multi-página (raro — reads > 4 KB, ex: ReadBytes grande).
    SIZE_T bytesRead = 0;
    return ReadProcessMemory( hProcess , (LPCVOID)addr , out , size , &bytesRead )
           && bytesRead == size;
}

HANDLE EMemory::GetWriteHandle() {
    return s_hWritePersist;
}

// Retorna um handle de acesso COMPLETO para o processo alvo.
// O caller é responsável por fechar o handle com CloseHandle.
// Dois caminhos:
//  1. Via s_hWriteSrc (processo SYSTEM) — sem novo EventID 10.
//     Disponível quando AcquireEMemHandle encontrou a handle via s_preAcquiredSrc
//     (shellcode) ou via fallback OpenProcess.
//  2. Via DuplicateHandle de hProcess dentro do próprio processo — sem acesso
//     a processo SYSTEM, sem EventID 10. Usado quando s_hWriteSrc é null.
HANDLE EMemory::AcquireChamsHandle() {
    // Caminho 1: via processo SYSTEM já aberto (sem novo EventID 10)
    if ( s_hWriteSrc && s_srcHandleVal ) {
        HANDLE hDup = nullptr;
        DuplicateHandle( s_hWriteSrc , ( HANDLE ) s_srcHandleVal ,
                         GetCurrentProcess( ) , &hDup ,
                         0 , FALSE , DUPLICATE_SAME_ACCESS );
        return hDup;
    }
    // Caminho 2: duplica hProcess dentro do próprio processo — sem abrir
    // winlogon/lsass, sem novo EventID 10 via processo SYSTEM.
    if ( hProcess ) {
        HANDLE hDup = nullptr;
        DuplicateHandle( GetCurrentProcess( ) , hProcess ,
                         GetCurrentProcess( ) , &hDup ,
                         0 , FALSE , DUPLICATE_SAME_ACCESS );
        return hDup;
    }
    return nullptr;
}

// Armazena a handle do processo fonte (lsass/winlogon com PROCESS_DUP_HANDLE)
// pré-adquirida pelo shellcode (spoolsv.exe) durante a janela do patch Sysmon.
// AcquireEMemHandle tentará essa handle antes de abrir winlogon/lsass — zero
// EventID 10 com access 0x40 no processo do cheat.
// A handle NÃO é fechada em Shutdown() de forma agressiva: o lsass/winlogon
// vive enquanto o sistema estiver rodando, e fechar antecipadamente remove a
// capacidade de reuso caso o emulador reinicie.
void EMemory::SetPreAcquiredSrcHandle( HANDLE hSrc, DWORD srcPid ) {
    if ( s_preAcquiredSrc ) {
        CloseHandle( s_preAcquiredSrc );
        s_preAcquiredSrc = nullptr;
    }
    s_preAcquiredSrc    = hSrc;
    s_preAcquiredSrcPid = srcPid;
}

// Fallback: adquire s_preAcquiredSrc uma única vez quando não foi passada pelo
// injetor. Deve ser chamado de dentro da janela do patch Sysmon (ETW suprimido)
// para que o OpenProcess(PROCESS_DUP_HANDLE) não gere EventID 10.
// No-op se já definida — preserva a handle passada pelo injetor.
void EMemory::AcquireSrcHandleOnce( DWORD pid ) {
    if ( s_preAcquiredSrc || !pid ) return;  // já tem (injetor ou chamada anterior)
    HANDLE h = OpenProcess( PROCESS_DUP_HANDLE, FALSE, pid );
    if ( !h ) return;
    s_preAcquiredSrc    = h;
    s_preAcquiredSrcPid = pid;
}

void EMemory::DiagnoseRead(uint64_t testGPA) {
    (void)testGPA;
}
