#pragma once

// ─── AntiDbg::Checks ─────────────────────────────────────────────────────────
// Funções de detecção de debugger.
// Retornam TRUE se o debugger foi detectado.
// Todas resolvem APIs dinamicamente (GetProcAddress) para resistir a IAT hooks.

namespace AntiDbg::Checks {

    // 1. PEB.BeingDebugged — bit clássico setado pelo Windows ao anexar debugger.
    bool PEB();

    // 2. NtQueryInformationProcess com três classes:
    //    ProcessDebugPort (0x07), ProcessDebugObjectHandle (0x1E), ProcessDebugFlags (0x1F).
    bool NtQueryDebug();

    // 3. CheckRemoteDebuggerPresent — API oficial que consulta o kernel.
    bool RemoteDebugger();

    // 4. Debug registers Dr0–Dr7 via GetThreadContext.
    //    Hardware breakpoints deixam rastro nesses registradores.
    bool HardwareBreakpoints();

    // 5. PEB.NtGlobalFlag — debugger seta 0x70 (heap checking flags).
    bool HeapFlags();

    // 6. RDTSC timing — single-step / trap flag infla o delta entre dois RDTSC.
    bool TimingRDTSC();

    // 7. Parent process check — PPID via NtQueryInformationProcess(ProcessBasicInformation),
    //    nome comparado contra lista de debuggers conhecidos.
    bool ParentIsDebugger();

} // namespace AntiDbg::Checks
