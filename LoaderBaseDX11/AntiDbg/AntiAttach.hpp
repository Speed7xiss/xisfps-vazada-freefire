#pragma once

// ─── AntiDbg::Attach ─────────────────────────────────────────────────────────
// Previne attach de debugger ao processo.
//
// Técnica primária: patchar ntdll!DbgBreakPoint e ntdll!DbgUiRemoteBreakin
// com RET imediato. Quando um debugger tenta se anexar ao processo:
//   1. Injeta uma thread no target que executa DbgUiRemoteBreakin
//   2. DbgBreakPoint é chamado para sinalizar o attach
// Com ambas patchadas → attach falha silenciosamente.
//
// Técnica secundária: NtSetInformationThread(ThreadHideFromDebugger=0x11)
// remove a thread da lista de threads visíveis ao debugger — não recebe
// eventos de debug (breakpoints, exceptions) para essa thread.
//
// WatchLoop re-patcha a cada ATTACH_WATCH_INTERVAL_MS para restaurar
// caso alguém reverta as patches.

#include <Windows.h>

namespace AntiDbg::Attach {

    // Esconde hThread do debugger via NtSetInformationThread(0x11).
    void HideThread( HANDLE hThread );

    // Patcha DbgBreakPoint + DbgUiRemoteBreakin → RET imediato.
    // Chame uma vez no Init() e depois deixe o WatchLoop manter.
    void PatchBreakpointFunctions();

    // Loop de vigilância: re-patcha a cada 5s. Não retorna — rode em thread.
    void WatchLoop();

} // namespace AntiDbg::Attach
