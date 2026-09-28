#pragma once
#include <atomic>
#include <Windows.h>
#include <cstdint>
#include <vector>
#include "../Math/Vectors/Vector3.hpp"

namespace Off {
    // Registro runtime dos offsets (aba Offsets do website). Definido em AimModules.cpp.
    struct OffEntry { const char* name; uint32_t* ptr; };
    const std::vector<OffEntry>& OffTable();
}

namespace AimModules {
    extern std::atomic<bool>     bRunning;
    extern std::atomic<uint32_t> g_bsEntity;    // entidade sob BoneSwap principal (0 = nenhum)
    extern std::atomic<uint32_t> g_bsSavedHip;  // valor original em entity+0x4B0 (ROOTNODE = Hips)
    extern std::atomic<uint32_t> g_bsSavedBlood;// valor original em entity+0x49C (HEADNODE = Head)

    // GhostMode state — set from TickGhostMode (see below), read from
    // Draw.cpp so the overlay can render the '+' marker on the frozen
    // hitbox and the "Ghost Damage: ON/FAKE" HUD.
    //
    // g_GhostPos is written and read on the same thread now (Draw), but
    // the mutex-protected accessors stay for defense-in-depth: a plain
    // Vector3 assignment is NOT atomic (12 bytes), and if any future
    // background thread ever writes here a racing read could observe a
    // mixed X/Y/Z (e.g. new X with old Y/Z) and produce a phantom
    // distance in the label.
    extern std::atomic<bool> g_GhostActive;

    // Full ghost snapshot: the anchor position plus the 18 bones of the
    // local player captured at activation time. The bones are in world
    // coords — Draw just needs to W2S them each frame with the current
    // view matrix and draw them like the ESP does for enemies.
    struct GhostSnapshot {
        Vector3 pos;
        Vector3 bones[18];
        bool    skelValid;
    };

    Vector3       GetGhostPos();
    void          SetGhostPos(const Vector3& p);
    GhostSnapshot GetGhostSnapshot();

    // Ghost toggle driver — runs on the CALLER thread (Draw). Called every
    // render frame from Draw.cpp so key transitions are observed at render
    // rate (~60Hz+) instead of the old 50ms background-poll cycle. That
    // 50ms cadence was the actual reason the anchor / '+' marker looked
    // "stuck at the first place I used ghost": the background loop could
    // sit up to 50ms behind a press/release, and if the local Transform
    // was still being reeled back to the previous anchor during that
    // window (server hadn't released it yet), the fresh capture would
    // just re-read the same old position. Driving it from Draw removes
    // the lag and mirrors the ZmInternal reference layout exactly.
    void TickGhostMode(uint32_t localPlayer);

    // -------------------------------------------------------------------------
    // ExploitDebugState — preenchida pelos 5 loops novos a cada ciclo.
    // Lida pelo painel de debug na GUI sem cruzar camadas.
    // -------------------------------------------------------------------------
    struct ExploitDebugState {
        // Ponteiros
        std::atomic<uint32_t> localPlayer  {0};
        std::atomic<uint32_t> playerAttribs{0};

        // Speed
        float speedScaleRead  = 0.f;
        float dashScaleRead   = 0.f;
        float speedScaleWrite = 0.f;

        // Damage (display local via BuffWeaponDamageScale 0xC4 — servidor reseta via buff sync)
        float dmgScaleRead  = 0.f;
        float dmgScaleWrite = 0.f;

        // Moco/Laura
        bool  showMapRead   = false;
        bool  showStepRead  = false;
        int   stepMaxRead   = 0;
        int   stepMinRead   = 0;

    };
    extern ExploitDebugState g_ExploitDbg;

    void StartAll(uint32_t il2cpp);

    // StopAll é fire-and-forget: apenas seta bRunning=false. Chamado por
    // frame no render loop quando todas as opções de aim ficam off, por
    // isso NÃO pode bloquear (bloquearia a renderização). Antes de liberar
    // g_Memory / EMemory (Cheat::Unload), chame WaitAllStopped() para
    // sincronizar com o exit real das threads.
    void StopAll();

    // Espera bounded até todas as threads detached observarem bRunning=false
    // e saírem do while(bRunning). Retorna true se todas saíram dentro do
    // timeout; false se o timeout foi atingido. Uso obrigatório antes de
    // desmontar as estruturas compartilhadas com os workers (g_Memory,
    // EMemory) — se sair antes das threads, elas dereferenciam g_Memory
    // após o delete e o processo crasha. Timeout padrão largo (5s) porque
    // no worst case um Write DMA em progresso pode segurar por >1s sob
    // carga; o custo de esperar é irrelevante frente ao risco de crash.
    bool WaitAllStopped(unsigned int timeoutMs = 5000);
}
