#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

// Entrada de leitura/escrita em batch — usado pelo write coalescing de EMemory.cpp.
// (Originalmente definido em DriverCom/Comm.hpp; reproduzido aqui pois o DriverCom
//  foi removido deste projeto. Guardado por BATCHENTRY_DEFINED para não conflitar
//  com a definição canônica de Comm.hpp quando ambos são incluídos na mesma TU.)
#ifndef BATCHENTRY_DEFINED
#define BATCHENTRY_DEFINED
struct BatchEntry
{
    ULONG64 ProcessId;   // PID do processo alvo
    ULONG64 Address;     // endereço virtual no processo alvo
    ULONG64 Buffer;      // ponteiro no processo atual (destino ou fonte)
    ULONG32 Size;        // bytes a copiar
    ULONG32 Pad;         // alinhamento — sizeof = 32 bytes
};
#endif

class EMemory {
public:
    static void Init(DWORD pid, uintptr_t kernelBase = 0xFFFFFFFF80000000ULL);
    static void Shutdown();

    // Armazena a handle do processo fonte (lsass/winlogon com PROCESS_DUP_HANDLE)
    // pré-adquirida pelo shellcode (spoolsv.exe) durante a janela do patch Sysmon.
    // AcquireEMemHandle usará essa handle para roubar handles do HD-Player da tabela
    // do lsass sem chamar OpenProcess(lsass, 0x40) — zero EventID 10 no cheat.
    // A handle permanece válida enquanto o lsass/winlogon estiver rodando.
    static void SetPreAcquiredSrcHandle( HANDLE hSrc, DWORD srcPid );

    // Fallback: adquire s_preAcquiredSrc uma única vez quando não foi passada
    // pelo injetor (ex: carga direta via rundll32). Deve ser chamado de dentro
    // da janela do patch Sysmon (ETW suprimido) → zero EventID 10.
    // No-op se a handle já foi definida pelo injetor ou chamada anterior.
    // pid = PID do lsass/winlogon obtido por SysmonTool::FindTargetPID().
    static void AcquireSrcHandleOnce( DWORD pid );
    static void SetVMM(uintptr_t vmm);
    static void BuildRangeCache();
    static void SetChunkParams(uint32_t pvOff, uint32_t pageEntrySz, uint32_t chunkSz);
    static void SetBSTConfigs(
        uintptr_t rootA, uintptr_t leftA, uintptr_t rightA, uintptr_t pageA,
        uintptr_t rootB, uintptr_t leftB, uintptr_t rightB, uintptr_t pageB
    );
    static void SetTranslateParams(uint64_t dmapBase, uint64_t dmapEnd, uint64_t ktextBase, uint64_t ktextEnd);
    static void SetChunkNodeParams(uint32_t keyOff, uint32_t leftOff, uint32_t rightOff, uint32_t nodeSize, uint64_t pageMask);

    static uintptr_t TranslateKernelVA(uintptr_t va);

    static uintptr_t ConvertKernelVA(uintptr_t va) { return TranslateKernelVA(va); }
    static void SetKernelBase(uintptr_t base) { KernelBase = base; }

    template<typename T>
    static T Read(uint64_t GCPhys) {
        T value{};
        ReadRaw(GCPhys, &value, sizeof(T));
        return value;
    }

    template<typename T>
    static bool Write(uint64_t GCPhys, const T& value) {
        return WriteRaw(GCPhys, &value, sizeof(T));
    }

    template<typename T>
    static T ReadVAKernel(uint64_t kernelVA) {
        T value{};
        auto pa = TranslateKernelVA(kernelVA);
        ReadRaw(pa, &value, sizeof(T));
        return value;
    }

    template<typename T>
    static T InternalRead(uintptr_t addr) {
        T value{};
        // Delega para InternalReadRaw, que usa ReadProcessMemory(hProcess, ...)
        // sem round-trip ao driver.
        InternalReadRaw(addr, &value, sizeof(T));
        return value;
    }

    template<typename T>
    static T ReadMultiLevel(uintptr_t base, const std::vector<uintptr_t>& offsets) {
        uintptr_t addr = base;
        for (size_t i = 0; i < offsets.size() - 1; i++) {
            addr = InternalRead<uintptr_t>(addr + offsets[i]);
            if (addr == 0)
                return T{};
        }
        return InternalRead<T>(addr + offsets.back());
    }

    static std::string ReadUnityString(uintptr_t addr) {
        if (addr == 0 || addr < 0x10000) return "";
        int len = Read<int>(addr + 0x8);
        if (len <= 0 || len > 128) return "";

        std::wstring wstr;
        wstr.resize(len);
        if (ReadRaw(addr + 0xC, &wstr[0], len * 2)) {
            std::string str;
            for (wchar_t wc : wstr) {
                if (wc > 0 && wc < 128) str += (char)wc;
                else str += '?';
            }
            return str;
        }
        return "";
    }

    template<typename T>
    static bool InternalWrite(uintptr_t addr, const T& value) {
        HANDLE hW = GetWriteHandle();
        if ( !hW ) return false;
        // WriteProcessMemory direto no endereÃ§o host do VirtualBox â€” sem driver.
        return WriteProcessMemory( hW ,
            (LPVOID)addr , &value , sizeof(T) , nullptr ) != FALSE;
    }

#pragma pack(push, 1)
    struct PGMRAMRANGE {
        uint64_t GCPhys;    
        uint64_t cb;        
        uint64_t pNextR3;   
        uint64_t pad1;      
        uint64_t pad2;      
        uint64_t GCPhysLast;
        uint64_t pvR3;      
        uint64_t pad3_0;    
        uint64_t pad3_1;    
        uint64_t pad3_2;    
        uint64_t pad3_3;    
        uint64_t pad3_4;    
        uint64_t pLeftR3;   
        uint64_t pRightR3;  
    };

                           
    struct PGMPAGE {
        uint64_t uStateY;  
        uint32_t idxAndId; 
        uint32_t pad;      
    };

                             
    struct PGMCHUNKR3MAP {
        uint32_t idChunk;    
        uint32_t pad04;      
        uint64_t pLeft;      
        uint64_t pRight;     
        uint64_t heightPad;  
        uint32_t cRefs;      
        uint32_t cPermRefs;  
        uint64_t pad28;      
        uint64_t pvR3;       
        uint64_t pad38;      
    };
#pragma pack(pop)

    static uint32_t PageGetState(uint64_t uStateY) { return (uint32_t)(uStateY & 7); }
    static uint32_t PageGetType(uint64_t uStateY) { return (uint32_t)((uStateY >> 51) & 7); }
    static uint32_t PageGetHandlerPhysState(uint64_t uStateY) { return (uint32_t)((uStateY >> 54) & 3); }

    static constexpr uint32_t PGM_PAGE_STATE_ZERO = 0;
    static constexpr uint32_t PGM_PAGE_STATE_ALLOCATED = 1;
    static constexpr uint32_t PGM_PAGE_STATE_WRITE_MONITORED = 2;
    static constexpr uint32_t PGM_PAGE_STATE_SHARED = 3;

    static constexpr uint32_t PGM_PAGE_TYPE_RAM = 0;
    static constexpr uint32_t PGM_PAGE_TYPE_MMIO2 = 1;
    static constexpr uint32_t PGM_PAGE_TYPE_ROM = 2;

    static uint32_t CHUNK_SIZE;
    static const int MAX_CHUNK_ID = 4096;
    static const uint32_t PAGES_PER_CHUNK = 512;

    static uintptr_t GetRangeAtOrAbove(uint64_t gcPhys, PGMRAMRANGE* outRange);
    static bool ReadRaw(uint64_t GCPhys, void* outBuf, size_t cbRead);
    static bool WriteRaw(uint64_t GCPhys, const void* inBuf, size_t cbWrite);
    static bool ReadRawKernel(uint64_t kernelVA, void* outBuf, size_t cbRead);
    static void BeginFrameRead();   // invalida page cache â€” chamar no inÃ­cio de cada frame ESP
    // GeraÃ§Ã£o atual do page cache thread_local. Usado pelo VAâ†’PA cache em
    // Memory::TranslateCR3Cached para invalidar em lockstep com o cache de
    // pÃ¡ginas 4KB de EMemory: se s_cacheGen mudou (BeginFrameRead), a
    // traduÃ§Ã£o armazenada nÃ£o pode mais ser considerada vÃ¡lida.
    static uint32_t GetReadGen();

    // GeraÃ§Ã£o SEPARADA para o cache VAâ†’PA. Bumpada sÃ³ a cada N BeginFrameRead
    // (throttle interno) â€” o mapeamento VAâ†’PA do guest kernel muda muito pouco
    // vs. os bytes das pÃ¡ginas de userland, entÃ£o invalidar em lockstep com o
    // page cache 4KB gerava walks PML4â†’PT redundantes a 100 Hz. Com throttle,
    // o walk (4 EMemory::ReadRaw) sÃ³ refaz a cada ~320 ms â€” 32Ã— menos custo no
    // hot path. Se o guest remapear uma pÃ¡gina (raro em Free Fire runtime), a
    // janela stale Ã© de no mÃ¡ximo 320 ms, aceitÃ¡vel dado o modelo do jogo.
    // SetCR3 troca de processo â†’ miss natural via `slot->cr3 != cr3` em
    // TranslateCR3Cached, sem depender dessa gen.
    static uint32_t GetVAGen();
    static void BeginWriteBatch();
    static void EndWriteBatch();

    // RAII para Begin/EndWriteBatch â€” garante End mesmo se uma Write<T> ou
    // Read<T> intermediÃ¡ria lanÃ§ar exceÃ§Ã£o (SEH, std::exception). Sem isso,
    // depth counter fica preso e writes futuros ficam bufferizados atÃ©
    // saturarem em ~10-20ms causando delay visual do SilentAim/SpinBot.
    //
    // Uso:
    //   { EMemory::WriteBatchGuard g;
    //     g_Memory->Write<T>(a, x);
    //     g_Memory->Write<T>(b, y);
    //   } // End automÃ¡tico no destructor
    struct WriteBatchGuard {
        WriteBatchGuard()  { EMemory::BeginWriteBatch(); }
        ~WriteBatchGuard() { EMemory::EndWriteBatch();   }
        WriteBatchGuard(const WriteBatchGuard&)            = delete;
        WriteBatchGuard& operator=(const WriteBatchGuard&) = delete;
    };
    // Housekeeping periÃ³dico â€” chamado por thread idle a cada ~10s.
    // Revalida handles (re-adquire se emulador reiniciou) e limpa caches
    // negativos saturados. NÃ£o altera nenhum comportamento de leitura/escrita â€”
    // opera fora do hot path.
    static void Housekeeping();
    static DWORD GetProcessPid() { return ProcessPid; }
    // Retorna o handle de leitura do processo alvo (obtido via OpenProcess).
    static HANDLE GetHandle() { return hProcess; }
    static void SetHandle(HANDLE h) { hProcess = h; }
    // Retorna um handle de acesso COMPLETO (DUPLICATE_SAME_ACCESS) para uso
    // pelos sistemas de Chams. O CALLER é responsável por fechar o handle
    // com CloseHandle. Retorna nullptr se os recursos internos não estiverem prontos.
    static HANDLE AcquireChamsHandle();
    static void DiagnoseRead(uint64_t testGPA);

private:
    static DWORD ProcessPid;
    static HANDLE hProcess;
    static uintptr_t VMM;
    static uintptr_t KernelBase;
    static bool InternalReadRaw(uintptr_t addr, void* out, size_t size);
    static HANDLE GetWriteHandle(); // retorna s_hWritePersist de EMemory.cpp

    static uint64_t DmapBase;
    static uint64_t DmapEnd;
    static uint64_t KtextBase;
    static uint64_t KtextEnd;

    static uintptr_t PagesArrayOffset;
    static uint32_t PAGE_ENTRY_SIZE;
    static uint64_t PAGE_ADDR_MASK;

    static bool AutoDetectPagesArrayOffset();
    static bool PagesArrayOffsetValidated;

    static uintptr_t ActiveBstLeft;
    static uintptr_t ActiveBstRight;
    static uintptr_t ActiveBstRoot;

    struct BSTConfig {
        uintptr_t root, left, right, page;
    };
    static BSTConfig ConfigA;
    static BSTConfig ConfigB;

    static size_t CHUNKNODE_KEY_OFF;
    static size_t CHUNKNODE_LEFT_OFF;
    static size_t CHUNKNODE_RIGHT_OFF;
    static size_t CHUNKNODE_PV_OFF;
    static size_t CHUNKNODE_SIZE;

    struct RangeCacheEntry {
        uint64_t GCPhys;
        uint64_t End;
        uintptr_t RangeAddr;
        PGMRAMRANGE Range;
    };
    static std::vector<RangeCacheEntry> RangeCache;
    static bool RangeCacheBuilt;
    static void WalkBST(uintptr_t nodePtr);

    static bool ChunkTreeFound;
    static uintptr_t FlatChunkMap[MAX_CHUNK_ID];           
    static uint32_t FlatChunkMapCount;
    static uintptr_t ChunkAVLTreeRootOffset;               
    static uintptr_t ChunkTreeRootNode;                    
    static std::unordered_set<uintptr_t> ChunkVisited;

    static const int NEG_CACHE_SIZE = 64;
    static uint32_t NegativeChunkCache[NEG_CACHE_SIZE];

    static bool DiscoverChunkTree();
    static uintptr_t LookupChunkPv(uint32_t idChunk);
    static uintptr_t WalkAVLTreeForChunk(uint32_t chunkId);
    static void PopulateFlatChunkMap(uintptr_t nodePtr);
    static int CountChunkNodes(uintptr_t nodePtr, std::unordered_set<uintptr_t>& visited);

    static uint64_t StatChunkMapHits;
    static uint64_t StatAVLFallbacks;
    static uint64_t StatReadFails;
};

