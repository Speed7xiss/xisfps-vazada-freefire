#pragma once

// ── UNICODE_STRING — definição própria ───────────────────────────────────────
// O projeto define _WINTERNL_ como macro de pré-processamento no vcxproj.
// Isso faz com que a seção de winnt.h que declara UNICODE_STRING (guardada
// por #ifndef _WINTERNL_) seja ignorada, e que winternl.h também seja
// completamente ignorado (o arquivo inteiro é #ifndef _WINTERNL_).
//
// Solução: definimos UNICODE_STRING ANTES de incluir qualquer header do SDK,
// usando apenas tipos built-in do C++ para não depender de nada externo.
// O guard _UNICODE_STRING_DEFINED impede colisão com qualquer typedef que
// o SDK eventualmente tente definir no mesmo Translation Unit.

#ifndef _UNICODE_STRING_DEFINED
#define _UNICODE_STRING_DEFINED
typedef struct _UNICODE_STRING {
    unsigned short  Length;
    unsigned short  MaximumLength;
    wchar_t*        Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING* PCUNICODE_STRING;
#endif

// ─────────────────────────────────────────────────────────────────────────────
#include <Windows.h>
#pragma comment(lib, "ntdll.lib")

#pragma warning(push)
#pragma warning(disable: 4005) // macro redefinition (ntstatus x SDK)
#include <ntstatus.h>
#pragma warning(pop)

#ifndef NT_SUCCESS
#  define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// ── Declarações de funções NT no escopo global ────────────────────────────────
// winternl.h está desativado pelo projeto; declaramos os exports de ntdll
// que precisamos. Escopo global (extern "C") para que service.cpp possa
// chamar RtlInitUnicodeString sem qualificar com nt::.

extern "C" {
    NTSTATUS NTAPI NtLoadDriver  (PUNICODE_STRING DriverServiceName);
    NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);
    NTSTATUS NTAPI RtlAdjustPrivilege(
        ULONG Privilege, BOOLEAN Enable, BOOLEAN Client, BOOLEAN* WasEnabled);
    VOID     NTAPI RtlInitUnicodeString(
        PUNICODE_STRING DestinationString, const wchar_t* SourceString);
}

namespace nt
{
    // Re-exporta as funções NT para o namespace nt:: de forma que os call
    // sites existentes (nt::NtLoadDriver, nt::RtlAdjustPrivilege, …) compilem
    // sem alteração.  extern "C" dentro de um namespace em C++ mantém linkage
    // C (sem mangling) mas torna o nome visível como nt::Função.
    extern "C" NTSTATUS NtLoadDriver  (PUNICODE_STRING DriverServiceName);
    extern "C" NTSTATUS NtUnloadDriver(PUNICODE_STRING DriverServiceName);
    extern "C" NTSTATUS RtlAdjustPrivilege(
        ULONG Privilege, BOOLEAN Enable, BOOLEAN Client, BOOLEAN* WasEnabled);

    constexpr auto SystemModuleInformation        = 11;
    constexpr auto SystemExtendedHandleInformation = 64;

    typedef struct _SYSTEM_HANDLE
    {
        PVOID  Object;
        HANDLE UniqueProcessId;
        HANDLE HandleValue;
        ULONG  GrantedAccess;
        USHORT CreatorBackTraceIndex;
        USHORT ObjectTypeIndex;
        ULONG  HandleAttributes;
        ULONG  Reserved;
    } SYSTEM_HANDLE, *PSYSTEM_HANDLE;

    typedef struct _SYSTEM_HANDLE_INFORMATION_EX
    {
        ULONG_PTR HandleCount;
        ULONG_PTR Reserved;
        SYSTEM_HANDLE Handles[1];
    } SYSTEM_HANDLE_INFORMATION_EX, *PSYSTEM_HANDLE_INFORMATION_EX;

    // https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_pool_type
    typedef enum class _POOL_TYPE {
        NonPagedPool,
        NonPagedPoolExecute              = NonPagedPool,
        PagedPool,
        NonPagedPoolMustSucceed          = NonPagedPool + 2,
        DontUseThisType,
        NonPagedPoolCacheAligned         = NonPagedPool + 4,
        PagedPoolCacheAligned,
        NonPagedPoolCacheAlignedMustS    = NonPagedPool + 6,
        MaxPoolType,
        NonPagedPoolBase                 = 0,
        NonPagedPoolBaseMustSucceed      = NonPagedPoolBase + 2,
        NonPagedPoolBaseCacheAligned     = NonPagedPoolBase + 4,
        NonPagedPoolBaseCacheAlignedMustS= NonPagedPoolBase + 6,
        NonPagedPoolSession              = 32,
        PagedPoolSession                 = NonPagedPoolSession + 1,
        NonPagedPoolMustSucceedSession   = PagedPoolSession + 1,
        DontUseThisTypeSession           = NonPagedPoolMustSucceedSession + 1,
        NonPagedPoolCacheAlignedSession  = DontUseThisTypeSession + 1,
        PagedPoolCacheAlignedSession     = NonPagedPoolCacheAlignedSession + 1,
        NonPagedPoolCacheAlignedMustSSession = PagedPoolCacheAlignedSession + 1,
        NonPagedPoolNx                   = 512,
        NonPagedPoolNxCacheAligned       = NonPagedPoolNx + 4,
        NonPagedPoolSessionNx            = NonPagedPoolNx + 32,
    } POOL_TYPE;

    typedef struct _RTL_PROCESS_MODULE_INFORMATION
    {
        HANDLE Section;
        PVOID  MappedBase;
        PVOID  ImageBase;
        ULONG  ImageSize;
        ULONG  Flags;
        USHORT LoadOrderIndex;
        USHORT InitOrderIndex;
        USHORT LoadCount;
        USHORT OffsetToFileName;
        UCHAR  FullPathName[256];
    } RTL_PROCESS_MODULE_INFORMATION, *PRTL_PROCESS_MODULE_INFORMATION;

    typedef struct _RTL_PROCESS_MODULES
    {
        ULONG NumberOfModules;
        RTL_PROCESS_MODULE_INFORMATION Modules[1];
    } RTL_PROCESS_MODULES, *PRTL_PROCESS_MODULES;

    typedef struct _HashBucketEntry
    {
        struct _HashBucketEntry* Next;
        UNICODE_STRING DriverName;   // UNICODE_STRING agora visível do escopo global
        ULONG CertHash[5];
    } HashBucketEntry, *PHashBucketEntry;

    typedef struct _RTL_BALANCED_LINKS {
        struct _RTL_BALANCED_LINKS* Parent;
        struct _RTL_BALANCED_LINKS* LeftChild;
        struct _RTL_BALANCED_LINKS* RightChild;
        CHAR  Balance;
        UCHAR Reserved[3];
    } RTL_BALANCED_LINKS;
    typedef RTL_BALANCED_LINKS* PRTL_BALANCED_LINKS;

    typedef struct _RTL_AVL_TABLE {
        RTL_BALANCED_LINKS BalancedRoot;
        PVOID OrderedPointer;
        ULONG WhichOrderedElement;
        ULONG NumberGenericTableElements;
        ULONG DepthOfTree;
        PVOID RestartKey;
        ULONG DeleteCount;
        PVOID CompareRoutine;
        PVOID AllocateRoutine;
        PVOID FreeRoutine;
        PVOID TableContext;
    } RTL_AVL_TABLE;
    typedef RTL_AVL_TABLE* PRTL_AVL_TABLE;

    typedef struct _PiDDBCacheEntry
    {
        LIST_ENTRY     List;
        UNICODE_STRING DriverName;   // UNICODE_STRING visível do escopo global
        ULONG          TimeDateStamp;
        NTSTATUS       LoadStatus;
        char           _0x0028[16];
    } PiDDBCacheEntry, *NPiDDBCacheEntry;

    typedef enum _KEY_INFORMATION_CLASS {
        KeyBasicInformation,
        KeyNodeInformation,
        KeyFullInformation,
        KeyNameInformation,
        KeyCachedInformation,
        KeyFlagsInformation,
        KeyVirtualizationInformation,
        KeyHandleTagsInformation,
        KeyTrustInformation,
        KeyLayerInformation,
        KeyWriteTimeInformation,
        MaxKeyInfoClass
    } KEY_INFORMATION_CLASS;

    typedef struct _KEY_WRITE_TIME_INFORMATION {
        LARGE_INTEGER LastWriteTime;
    } KEY_WRITE_TIME_INFORMATION, *PKEY_WRITE_TIME_INFORMATION;

    typedef NTSTATUS(WINAPI* t_NtSetInformationKey)(
        HANDLE              KeyHandle,
        KEY_INFORMATION_CLASS KeyInformationClass,
        PVOID               KeyInformation,
        ULONG               KeyInformationLength
    );
}
