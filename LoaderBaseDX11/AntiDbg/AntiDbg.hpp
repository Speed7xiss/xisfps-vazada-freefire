#pragma once

// ─── AntiDbg ──────────────────────────────────────────────────────────────────
// Sistema completo de Anti-Debug, Anti-Dump e Anti-Attach para a DLL do cheat.
//
// Camadas ativas após Init():
//
//   1. AntiDump     — apaga PE header, corrompe LDR e section headers na carga
//   2. CheckLoop    — thread 500ms: 7 técnicas de detecção de debugger
//   3. ProcessGuard — thread 3s: varre processos e janelas de análise → BSOD
//   4. AntiAttach   — thread 5s: re-patcha DbgBreakPoint + DbgUiRemoteBreakin
//
// Uso em DllThread (main.cpp):
//
//     AntiDbg::Init( g_hDll );     // uma vez, após o Sleep(2000) inicial
//     AntiDbg::HideThread();       // esconde DllThread
//
//     // No início de cada callback de thread criado pelo cheat:
//     AntiDbg::HideThread();
//
//     // No shutdown:
//     AntiDbg::Shutdown();

#include <Windows.h>

namespace AntiDbg {

    // Inicia todas as camadas de proteção.
    // hSelf = handle do próprio módulo (g_hDll do DllMain).
    void Init( HMODULE hSelf );

    // Esconde a thread atual do debugger via NtSetInformationThread.
    // Chame no topo de CADA thread do cheat.
    void HideThread();

    // Sinaliza shutdown — para as threads de vigilância.
    void Shutdown();

    // BSOD imediato via NtRaiseHardError + SeShutdownPrivilege.
    // Chamado internamente mas exposto para emergência.
    [[noreturn]] void TriggerBSOD();

} // namespace AntiDbg
