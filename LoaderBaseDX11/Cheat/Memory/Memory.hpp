#pragma once
#include <Windows.h>
#include <cstdint>
#include "../EMemory.hpp"

class Memory
{
public:
    template<typename T>
    T Read(uintptr_t address) const {
        T result{};
        uintptr_t physAddress;
        if (ConvertCR3(address, physAddress)) {
            EMemory::ReadRaw(physAddress, &result, sizeof(T));
        }
        return result;
    }

    // ── Ptr validity check (Fix A do flickering ESP) ────────────────────────
    // EMemory::ReadRaw usa memset(out, 0xFF) em falha de range/mapping e
    // memset(out, 0x00) em chunk lookup miss. Um ptr que retorna 0 pode ser
    // "endereco válido zerado" ou "falha silenciosa"; mas 0xFFFFFFFF é
    // GARANTIDAMENTE falha silenciosa — nenhum ptr válido de userland do
    // Free Fire ARM32 emulado chega nesse valor. A faixa 0x00100000..
    // 0xEF000000 cobre folgado o heap do BlueStacks (Android userland
    // fica <2GB por processo em ARM32), e o gate inferior (>=1MB) descarta
    // ponteiros pequenos que são sempre falha.
    //
    // Usado em SharedFrame.cpp e Transform.cpp em conjunto com ReadPtr abaixo
    // para reduzir cache poisoning e RPM failure rate.
    static bool IsValidPtr(uint32_t p) {
        // FIX FLICKER: teto 0xEF000000 rejeitava ponteiros userland legítimos
        // em regiões altas do ARM32 do BlueStacks (ASLR pode alocar arenas
        // acima disso). Só filtra 0 e faixa baixa <1MB (kernel/small-int).
        // Failure sentinel 0xFFFFFFFF do ReadRaw agora é filtrado pelo call-
        // site via checagem explícita se necessário.
        return p >= 0x00100000u && p != 0xFFFFFFFFu;
    }

    // ── Ptr read com retry (Fix B do flickering ESP) ────────────────────────
    // Chain de leituras (entity→AvatarMgr→Avatar→AvatarData→...) tem chance
    // acumulada de falha alta (probabilidade de sucesso = (1-ε)^N). Um retry
    // simples reduz ε por-RPM de 1-2% pra ~0% em ~95% dos casos onde a página
    // acabou de ser tocada (2ª tentativa entra no page cache warm).
    //
    // Sucesso = passa em IsValidPtr. Custo em caso comum (sucesso first-try):
    // ZERO — retorna imediatamente. Custo em falha: até 2 RPMs extras (~µs).
    // Retorna 0 se falhar todas as tentativas — call site pode gate com
    // IsValidPtr no retorno pra descartar a entity uniformemente.
    uint32_t ReadPtr(uintptr_t address, int retries = 3) const {
        for (int i = 0; i < retries; ++i) {
            uint32_t v = Read<uint32_t>(address);
            if (IsValidPtr(v)) return v;
        }
        return 0;
    }

    bool ReadBytes(uintptr_t address, void* buffer, size_t size) const {
        auto* dst = static_cast<uint8_t*>(buffer);
        size_t offset = 0;
        while (offset < size) {
            uintptr_t physAddr;
            if (!ConvertCR3(address + offset, physAddr))
                return false;
            size_t pageRem = 0x1000 - (physAddr & 0xFFFull);
            size_t toRead  = (size - offset < pageRem) ? (size - offset) : pageRem;
            if (!EMemory::ReadRaw(physAddr, dst + offset, toRead))
                return false;
            offset += toRead;
        }
        return true;
    }

    template<typename T>
    bool Write(uintptr_t address, const T& value) const {
        uintptr_t physAddress;
        if (!ConvertCR3(address, physAddress))
            return false;
        return EMemory::WriteRaw(physAddress, &value, sizeof(T));
    }

    static void SetCR3(uintptr_t cr3) {
        GuestCR3 = cr3;
    }

    static bool ValidatePhys(uintptr_t guestVA) {
        return !guestVA;
    }

    static bool ConvertCR3(uintptr_t guestVA, uintptr_t& guestPA) {
        guestPA = 0;
        if (guestVA <= 0x1000)
            return false;
        // Delega ao cache VA→PA thread_local. Em hit (caso comum após o primeiro
        // Read<T> da página no frame), pula os 4 EMemory::ReadRaw do walk PML4→PT.
        // Em miss, cai no TranslateCR3 tradicional — comportamento byte-idêntico.
        return TranslateCR3Cached(guestVA, GuestCR3, guestPA);
    }

    // Miss backing implementado em Memory.cpp; mantém TranslateCR3 (walk completo)
    // intocado como backend para o caminho lento.
    static bool TranslateCR3Cached(uintptr_t guestVA, uintptr_t cr3, uintptr_t& guestPA);

    static bool TranslateCR3(uintptr_t guestVA, uintptr_t cr3, uintptr_t& guestPA) {
        guestPA = 0;

        if (static_cast<int64_t>(guestVA) != ((static_cast<int64_t>(guestVA) << 16) >> 16))
            return false;

        const uintptr_t pml4_base = cr3 & 0xFFFFFFFFFFFFF000ULL;
        const uint64_t PML4_index = (guestVA >> 39) & 0x1FF;
        const uint64_t PDPT_index = (guestVA >> 30) & 0x1FF;
        const uint64_t PD_index = (guestVA >> 21) & 0x1FF;
        const uint64_t PT_index = (guestVA >> 12) & 0x1FF;
        const uint64_t page_off = guestVA & 0xFFF;

        uint64_t entry = 0;
        uintptr_t table = 0;

        if (!EMemory::ReadRaw(pml4_base + PML4_index * 8, &entry, sizeof(entry))) return false;
        if (!(entry & 1)) return false;
        table = static_cast<uintptr_t>(entry & 0xFFFFFFFFF000ULL);

        if (!EMemory::ReadRaw(table + PDPT_index * 8, &entry, sizeof(entry))) return false;
        if (!(entry & 1)) return false;
        if (entry & (1ULL << 7)) {
            guestPA = static_cast<uintptr_t>(entry & 0xFFFFFC0000000ULL) + (guestVA & 0x3FFFFFFFULL);
            return guestPA != 0;
        }
        table = static_cast<uintptr_t>(entry & 0xFFFFFFFFF000ULL);

        if (!EMemory::ReadRaw(table + PD_index * 8, &entry, sizeof(entry))) return false;
        if (!(entry & 1)) return false;
        if (entry & (1ULL << 7)) {
            guestPA = static_cast<uintptr_t>(entry & 0xFFFFFFE00000ULL) + (guestVA & 0x1FFFFFULL);
            return guestPA != 0;
        }
        table = static_cast<uintptr_t>(entry & 0xFFFFFFFFF000ULL);

        if (!EMemory::ReadRaw(table + PT_index * 8, &entry, sizeof(entry))) return false;
        if (!(entry & 1)) return false;

        guestPA = static_cast<uintptr_t>(entry & 0xFFFFFFFFF000ULL) + page_off;
        return guestPA != 0;
    }


private:
    static uintptr_t GuestCR3;
};

extern Memory* g_Memory;