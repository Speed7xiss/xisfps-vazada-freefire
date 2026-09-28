// DriverMapperInit.cpp
// Integração do kdmapper com o fluxo de inicialização do cheat.
//
// DESIGN:
//   HollowMode exclusivo — NUNCA AllocatePool.
//   HollowMode escreve nossos bytes sobre uma imagem de driver do sistema
//   já mapeada, sem criar nova alocação NonPagedPool (detectável por scanners).
//
//   A função é dividida em fases para que a janela de patch do Sysmon
//   seja mantida em <6.5 s (orçamento idêntico ao da auth):
//
//     Fase 1 — MapKernelDriverFast():
//       intel_driver::Load() + kdmapper::MapDriver() + intel_driver::Unload()
//       Rápida (~2-4 s), sem esperar handshake.
//       Deve rodar dentro da janela de patch do Sysmon.
//
//     Fase 2 — WaitForHandshake(timeoutMs):
//       Aguarda ENTRY_DRIVER_MAGIC. Pode demorar até 10 s.
//       Rodar FORA da janela de patch (Sysmon habilitado — sem eventos sensíveis aqui).
//
//   Removidos (bugs ou causam trava):
//     SetDriverInfo  — GetCommandPayloadSize retorna 0 para COMMAND_SET_DRIVER_INFO,
//                      então o driver nunca lê Base/Size do cliente; no-op.
//     SetPreviousMode — muda PreviousMode da thread atual → D3D/DXGI falha silenciosa.
//     SetPPL (daqui) — FindProtectionOffset faz ZwQuerySystemInformation pesado;
//                      se crashar o worker thread, WaitForResponseBlocking() trava para
//                      sempre. SetPPL é chamado em thread separada dentro de
//                      IniciarInterface(), APÓS CreateDeviceD3D() + ShowWindow().

#define DISABLE_OUTPUT   // Silencia kdmLog — loader não tem console visível

#include "include/DriverMapperInit.hpp"
#include <Windows.h>

#include "include/intel_driver.hpp"
#include "include/kdmapper.hpp"
#include "include/nt.hpp"
#include "include/xorstr.hpp"

// Payload: bytes do driver a ser mapeado no kernel.
#include "bytes/bytes.h"

// Canal de comunicação compartilhado e cliente usermode.
#include "../DriverCom/CommClient.hpp"

// Debug panel (ImGui) — diagnóstico step-by-step do mapper
#include "../Imgui/menu/helpers/debug_log.h"

// ─────────────────────────────────────────────────────────────────────────────
// Estado interno do módulo
// ─────────────────────────────────────────────────────────────────────────────

// Path do DriverObject do candidato hollowed com sucesso.
// Preenchido por MapKernelDriverFast(). Lido por GetHollowedObj().
static const wchar_t* g_MappedObj = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// MapKernelDriverFast — Fase 1
// intel load + kdmapper::MapDriver() + intel unload
// NÃO aguarda ENTRY_DRIVER_MAGIC.
// Projetada para concluir em <6.5 s (janela de patch do Sysmon).
// Alvo único: cdrom.sys — HollowMode exclusivo, sem fallback.
// ─────────────────────────────────────────────────────────────────────────────
bool DriverMapperInit::MapKernelDriverFast()
{
    // Guard: só mapeia uma vez por lifetime do processo.
    if ( g_CommClient ) return true;

    // ── 1. Reverte impersonação antes de NtLoadDriver ────────────────────────────
    // Quando chamado dentro do Patch do Sysmon, o thread está impersonando um
    // token duplicado de lsass/wininit/winlogon via ImpersonateLoggedOnUser().
    // NtLoadDriver (dentro de intel_driver::Load) verifica a identidade efetiva
    // e retorna 0xC0001056 (vendor-defined, não existe em ntstatus.h) ao detectar
    // o token de impersonação de processo protegido (PPL).
    // Revertemos antes do Load; kdmapper::MapDriver() opera apenas via IOCTLs
    // ao Intel driver já carregado e aberto — não precisa de token SYSTEM.
    RevertToSelf();

    // ── 2. Garante que o Intel driver não está carregado de rodada anterior ────
    if ( intel_driver::IsRunning() ) {
        xdbg->push_info( "Intel: Unload anterior" );
        intel_driver::Unload();
    }

    // ── 3. Carrega o Intel NAL driver vulnerável ──────────────────────────────
    NTSTATUS loadStatus = intel_driver::Load();

    // Se o primeiro Load() falhou: loga o código exato, força Unload() e retry.
    if ( !NT_SUCCESS(loadStatus) ) {
        char buf1[48];
        snprintf( buf1, sizeof(buf1), "Intel Fail1: %08X", (unsigned)loadStatus );
        xdbg->push_info( buf1 );
        xdbg->push_info( "Intel: forçando Unload" );
        intel_driver::Unload();
        Sleep(500);
        loadStatus = intel_driver::Load();  // segunda tentativa
    }

    if ( !NT_SUCCESS(loadStatus) ) {
        char buf[48];
        snprintf( buf, sizeof(buf), "Intel Load Fail: %08X", (unsigned)loadStatus );
        xdbg->push_error( buf );
        return false;
    }
    xdbg->push_info( "Intel Load OK" );

    // ── 3. Inicializa canal de comunicação e CommClient ───────────────────────
    // CommClient criado ANTES do mapping: Initialize() seta ENTRY_USER_MAGIC
    // ANTES de chamar kdmapper::MapDriver(). O driver lê o magic no DriverEntry
    // e sabe que g_Comm é um ponteiro válido para o canal de comunicação.
    ZeroMemory( &g_Comm, sizeof(g_Comm) );
    g_CommClient = std::make_unique<CommClient>( &g_Comm );
    g_CommClient->Initialize();   // seta g_Comm.Magic = ENTRY_USER_MAGIC

    // ── 4. HollowMode — alvo único: cdrom.sys ─────────────────────────────────
    // hollow_mode=true: bytes escritos sobre cdrom.sys já mapeado no kernel.
    // Nunca usa AllocatePool — AllocationMode irrelevante em hollow_mode.
    NTSTATUS driverExitCode = STATUS_UNSUCCESSFUL;
    ULONG64 mappedBase = kdmapper::MapDriver(
        const_cast<BYTE*>( reinterpret_cast<const BYTE*>(driver_bytes) ),
        (ULONG64)GetCurrentProcessId(),               // param1 → CurrentCommPID
        (ULONG64)&g_Comm,                             // param2 → CurrentCommAddress
        false,                                        // free = false
        true,                                         // destroyHeader = true
        kdmapper::AllocationMode::AllocateIndependentPages,
        false,                                        // PassAllocationAddressAsFirstParam
        nullptr,                                      // callback
        &driverExitCode,
        true,           // hollow_mode = TRUE
        _("cdrom.sys")  // único alvo — sem fallback
    );

    // ── 5. Descarrega o Intel driver independente do resultado ────────────────
    intel_driver::Unload();

    if ( !mappedBase || !NT_SUCCESS(driverExitCode) )
    {
        char buf[64];
        if ( !mappedBase )
            snprintf( buf, sizeof(buf), "MapDriver: base nula" );
        else
            snprintf( buf, sizeof(buf), "Driver Exit: %08X", (unsigned)driverExitCode );
        xdbg->push_error( buf );
        g_CommClient.reset();
        g_MappedObj = nullptr;
        return false;
    }
    xdbg->push_info( "MapDriver OK" );

    // ── 6. Armazena o path do DriverObject para uso posterior ────────────────
    g_MappedObj = L"\\Driver\\cdrom";

    // Fase 1 concluída com sucesso.
    // g_CommClient está inicializado, g_Comm.Magic = ENTRY_USER_MAGIC.
    // O driver está em execução — use WaitForHandshake() para aguardar o magic.
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// WaitForHandshake — Fase 2
// Aguarda ENTRY_DRIVER_MAGIC. Deve rodar FORA da janela de patch do Sysmon.
// Retorna false se o driver não respondeu dentro de timeoutMs.
// ─────────────────────────────────────────────────────────────────────────────
bool DriverMapperInit::WaitForHandshake( DWORD timeoutMs )
{
    if ( !g_CommClient ) return false;

    for ( DWORD t = 0; t < timeoutMs && g_Comm.Magic != ENTRY_DRIVER_MAGIC; t += 10 )
        Sleep(10);

    if ( g_Comm.Magic != ENTRY_DRIVER_MAGIC )
    {
        // Driver não respondeu dentro do timeout — driver provavelmente crashou
        // no DriverEntry (pool insuficiente, candidato hollow corrompido, etc.).
        g_CommClient.reset();
        g_MappedObj = nullptr;
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetHollowedObj
// ─────────────────────────────────────────────────────────────────────────────
const wchar_t* DriverMapperInit::GetHollowedObj()
{
    return g_MappedObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// MapKernelDriver — wrapper de compatibilidade (Fase 1 + Fase 2 em sequência).
// Não aplica patch de Sysmon. Usar diretamente apenas quando o patch não for
// necessário (ex: testes, DllMain inline onde o sysmon já foi patchado).
// Para o fluxo de produção, use MapKernelDriverFast() + WaitForHandshake()
// separadamente dentro da janela de patch do Sysmon.
// ─────────────────────────────────────────────────────────────────────────────
bool DriverMapperInit::MapKernelDriver()
{
    if ( !MapKernelDriverFast() ) return false;
    return WaitForHandshake( 10000 );
}

// ─────────────────────────────────────────────────────────────────────────────
// UnloadKernelDriver — teardown completo, zero rastro no kernel.
//
// 1. Envia COMMAND_EXIT via g_CommClient. O driver executa HollowCleanup():
//    - Restaura os bytes originais do driver hollowed (desfaz o overwrite)
//    - Nulifica DriverObject->DriverUnload
//    - Instala SafeIrpDispatch em todos os MajorFunction entries
//    - Encerra o worker thread
// 2. Nulifica g_CommClient — call-sites com `if(g_CommClient)` viram no-op.
//
// Deve ser chamado UMA VEZ, logo antes do processo sair (ou antes de ExitProcess).
// ─────────────────────────────────────────────────────────────────────────────
void DriverMapperInit::UnloadKernelDriver()
{
    if ( !g_CommClient )
        return;

    // COMMAND_EXIT bloqueia até o driver confirmar teardown completo.
    (void)g_CommClient->Exit();

    // Nulifica handle usermode.
    g_CommClient.reset();
    g_MappedObj = nullptr;
}
