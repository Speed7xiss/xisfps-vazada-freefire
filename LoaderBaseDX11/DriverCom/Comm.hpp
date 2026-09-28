#pragma once

#ifdef _KERNEL_MODE
#include <ntifs.h>
#else
#include <Windows.h>
#endif

#define ENTRY_USER_MAGIC   0x1337
#define ENTRY_DRIVER_MAGIC 0x1338

// ─── Comandos legados (compatíveis com HideConsole/Comm.hpp) ──────────────────
#define COMMAND_EXIT              0x1339
#define COMMAND_HEARTBEAT         0x1340
#define COMMAND_SET_NEW_PROCESS   0x1341
#define COMMAND_GET_PROCESS_BASE  0x1342
#define COMMAND_READ_MEMORY       0x1343
#define COMMAND_WRITE_MEMORY      0x1344
#define COMMAND_STREAM_MODE       0x1345
#define COMMAND_JOURNAL           0x1346
#define COMMAND_DELETE_REG_KEY    0x1347
#define COMMAND_DELETE_REG_VALUE  0x1348
#define COMMAND_GET_MODULE_BASE   0x1349
#define COMMAND_READ_PHYS_MEMORY  0x1350

// 0x1351 e 0x1352 reservados para compatibilidade com HideConsole:
//   0x1351 = COMMAND_QUERY_VIRTUAL_MEMORY (HideConsole usa isso)
//   0x1352 = COMMAND_BATCH_READ_MEMORY    (formato inline, 32 entradas)
#define COMMAND_QUERY_VIRTUAL_MEMORY  0x1351
#define COMMAND_BATCH_READ_MEMORY     0x1352

// ─── Novos comandos do driver (0x1360+, sem conflito com HideConsole) ─────────
#define COMMAND_SET_PPL               0x1361   // Define PPL-WindowsTcb no processo alvo
#define COMMAND_SET_PREV_MODE         0x1362   // Define PreviousMode=KernelMode na thread
#define COMMAND_BATCH_READ            0x1363   // Lê N endereços (formato ponteiro, max 512)
#define COMMAND_BATCH_WRITE           0x1364   // Escreve N endereços (formato ponteiro)
#define COMMAND_CLEAN_CSRSS           0x1365   // Zera strings alvo de todos os csrss.exe
#define COMMAND_SET_DRIVER_INFO       0x1367   // Passa base+size do pool para self-erase
#define COMMAND_SET_HOLLOW_TARGET     0x1368   // Em hollow mode: path do DriverObject alvo

// ─── Máximo de entradas por batch (novo formato pointer-based) ─────────────────
#define BATCH_MAX_ENTRIES  512
// Máximo de entradas por COMMAND_BATCH_READ_MEMORY (legacy inline) ────────────
#define BATCH_LEGACY_MAX_ENTRIES 32

// ─── Estruturas do novo formato de batch (pointer-based) ──────────────────────
#ifndef BATCHENTRY_DEFINED
#define BATCHENTRY_DEFINED
struct BatchEntry
{
    ULONG64 ProcessId;
    ULONG64 Address;
    ULONG64 Buffer;
    ULONG32 Size;
    ULONG32 Pad;
};
#endif

struct BatchReadCommand
{
    ULONG64 EntriesAddress;
    ULONG32 Count;
    ULONG32 Pad;
};

struct BatchWriteCommand
{
    ULONG64 EntriesAddress;
    ULONG32 Count;
    ULONG32 Pad;
};

// ─── Estruturas do formato legado de batch (inline, para HideConsole) ─────────
struct LegacyBatchEntry
{
    ULONG64 Address;
    ULONG64 Buffer;
    ULONG32 Size;
    ULONG32 Status;
};

struct LegacyBatchReadCommand
{
    ULONG64 ProcessId;
    ULONG32 Count;
    ULONG32 SuccessCount;
    LegacyBatchEntry Entries[BATCH_LEGACY_MAX_ENTRIES];
};

// ─── Estrutura de QueryVirtualMemory (para HideConsole) ──────────────────────
struct QueryVirtualMemoryCommand
{
    ULONG64 ProcessId;
    ULONG64 BaseAddress;
    // Saída
    ULONG64 OutBase;
    ULONG64 OutRegionSize;
    ULONG64 OutState;
    ULONG64 OutProtect;
    ULONG64 OutType;
    ULONG64 Status;
};

// ─── Estruturas de outros comandos ────────────────────────────────────────────
struct SetPPLCommand
{
    ULONG64  ProcessId;
    NTSTATUS Status;
};

struct SetPrevModeCommand
{
    ULONG64  ThreadId;
    NTSTATUS Status;
};

struct GetModuleBaseCommand
{
    ULONG64 ProcessId;
    WCHAR ModuleName[260];
    ULONG64 BaseAddress;
};

struct HeartBeatCommand
{
    ULONG64 Value;
    ULONG64 NextValue;
};

struct SetNewProcessCommand
{
    ULONG64 ProcessId;
    ULONG64 Address;
};

struct GetProcessBaseCommand
{
    ULONG64 ProcessId;
    ULONG64 BaseAddress;
};

struct MemoryCommand
{
    ULONG64 ProcessId;
    ULONG64 Address;
    ULONG64 Buffer;
    ULONG64 Size;
    ULONG64 ResultSize;
    ULONG64 Status;
};

struct StreamModeCommand
{
    ULONG64 Handle;
    ULONG64 Affinity;
};

struct DriverInfoCommand
{
    ULONG64 Base;
    ULONG64 Size;
};

struct HollowTargetCommand
{
    WCHAR DriverPath[64];
};

struct DeleteRegistryKeyCommand
{
    WCHAR KeyPath[512];
    NTSTATUS Status;
};

struct DeleteRegistryValueCommand
{
    WCHAR KeyPath[512];
    WCHAR ValueName[260];
    NTSTATUS Status;
};

// ─── Cabeçalho de polling (apenas os primeiros 4 ULONG64) ─────────────────────
struct CommHeader
{
    ULONG64 Magic;
    ULONG64 CommandMagic;
    ULONG64 Command;
    ULONG64 CommandResultMagic;
};

// ─── Estrutura principal de comunicação ───────────────────────────────────────
struct Comm
{
    ULONG64 Magic;
    ULONG64 CommandMagic;
    ULONG64 Command;
    ULONG64 CommandResultMagic;

    union
    {
        HeartBeatCommand         HeartBeat;
        SetNewProcessCommand     SetNewProcess;
        GetProcessBaseCommand    GetProcessBase;
        MemoryCommand            Memory;
        StreamModeCommand        Stream;
        DeleteRegistryKeyCommand    DeleteRegistryKey;
        DeleteRegistryValueCommand  DeleteRegistryValue;
        GetModuleBaseCommand        GetModuleBase;
        QueryVirtualMemoryCommand   QueryVirtualMemory;   // COMMAND_QUERY_VIRTUAL_MEMORY
        LegacyBatchReadCommand      LegacyBatchRead;      // COMMAND_BATCH_READ_MEMORY
        SetPPLCommand               SetPPL;
        SetPrevModeCommand          SetPrevMode;
        BatchReadCommand            BatchRead;
        BatchWriteCommand           BatchWrite;
        DriverInfoCommand           DriverInfo;
        HollowTargetCommand         HollowTarget;
    } Data;
};

static_assert(sizeof(CommHeader) == sizeof(ULONG64) * 4,
    "CommHeader must remain a compact 32-byte wire header");

#ifndef _KERNEL_MODE
extern "C" __declspec(dllexport) Comm g_Comm;
#endif
