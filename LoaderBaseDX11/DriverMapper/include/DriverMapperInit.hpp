#pragma once
#include <Windows.h>

// ─── DriverMapperInit ────────────────────────────────────────────────────────
// Integração do kdmapper com o fluxo de inicialização do loader.
//
// Fluxo de chamada correto (main.cpp / MapKernelDriverPatched):
//
//   ┌─ SysmonTool::Patch ─────────────────────────────────────┐
//   │  MapKernelDriverFast()  ← intel load + map + unload     │  < 6.5 s
//   └─────────────────────────────────────────────────────────┘
//   SysmonTool::Restore()
//   WaitForHandshake(10000)  ← fora do patch (pode demorar até 10 s)
//   thread{ SetHollowTarget + CleanJournal + CleanCsrss }
//   ... IniciarInterface() ...
//   thread{ SetPPL }   ← após CreateDeviceD3D() + ShowWindow()
//
// ─────────────────────────────────────────────────────────────────────────────

namespace DriverMapperInit
{
    // ── Fase 1: intel load + kdmapper::MapDriver() + intel unload ────────────
    // Projetada para concluir dentro da janela de patch do Sysmon (<6.5 s).
    // Não aguarda ENTRY_DRIVER_MAGIC — isso é responsabilidade de WaitForHandshake().
    // Seta g_CommClient se o mapeamento foi bem sucedido.
    // HollowMode exclusivo: NUNCA usa AllocatePool.
    // Alvo único: cdrom.sys — sem fallback para outros drivers.
    bool MapKernelDriverFast();

    // ── Fase 2: aguarda ENTRY_DRIVER_MAGIC do driver ─────────────────────────
    // Chamar APÓS SysmonTool::Restore() e APÓS MapKernelDriverFast() retornar true.
    // O driver escreve o magic quando o worker thread está ativo e pronto.
    // Retorna false (e reseta g_CommClient) se o driver não respondeu dentro de timeoutMs.
    bool WaitForHandshake( DWORD timeoutMs );

    // ── Wrapper de compatibilidade ────────────────────────────────────────────
    // Chama MapKernelDriverFast() + WaitForHandshake(10000) em sequência.
    // Não aplica patch de Sysmon — use MapKernelDriverFast() + WaitForHandshake()
    // separadamente quando o patch for necessário.
    bool MapKernelDriver();

    // Retorna o path do DriverObject do candidato hollowed com sucesso.
    // Válido após MapKernelDriverFast() retornar true. nullptr caso contrário.
    // Usar para chamar SetHollowTarget() pós-handshake.
    const wchar_t* GetHollowedObj();

    // ── Teardown ──────────────────────────────────────────────────────────────
    // Envia COMMAND_EXIT ao driver (HollowCleanup + self-erase) e nulifica
    // g_CommClient. Zero rastro no kernel após retorno.
    // Chamar UMA VEZ, logo antes do processo sair (ou antes de ExitProcess).
    void UnloadKernelDriver();
}
