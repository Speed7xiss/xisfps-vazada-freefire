#pragma once

// ─── Chams Default ────────────────────────────────────────────────────────────
// Injeta um shellcode em HD-Player.exe que intercepta glDrawElements via a
// Export Address Table de libOpenglRender.dll e exibe silhuetas coloridas dos
// personagens. Usa o handle já adquirido por EMemory::GetHandle() (duplicado
// de winlogon/wininit/lsass) — nunca chama OpenProcess direto no emulador,
// portanto não gera Sysmon Event ID 10.

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Context.h"
#include "../EMemory.hpp"   // EMemory::GetHandle()

#pragma comment( lib , "psapi.lib" )

// ─── Shellcode HookedglDrawElements (Chams Default) ─────────────────────────
// Bytes gerados pelo projeto ChamsShellCode (versão default). Espera o layout
// exato de ChamsContext — não misturar com o Context do StreamMode.
static const unsigned char g_chams_default_shellcode[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xB8, 0x0D, 0xF0, 0xAD, 0xDE, 0xBE, 0xBA,
    0xDE, 0xC0, 0x49, 0x8B, 0xE9, 0x48, 0x89, 0x44, 0x24, 0x20, 0x45, 0x8B, 0xF0, 0x48, 0x8B, 0x5C,
    0x24, 0x20, 0x8B, 0xFA, 0x8B, 0xF1, 0xF0, 0xFF, 0x83, 0x94, 0x00, 0x00, 0x00, 0xB9, 0x50, 0x00,
    0x00, 0x00, 0xFF, 0x53, 0x38, 0xA8, 0x01, 0x74, 0x1C, 0x8B, 0x83, 0x90, 0x00, 0x00, 0x00, 0xFF,
    0xC0, 0x25, 0x01, 0x00, 0x00, 0x80, 0x7D, 0x07, 0xFF, 0xC8, 0x83, 0xC8, 0xFE, 0xFF, 0xC0, 0x87,
    0x83, 0x90, 0x00, 0x00, 0x00, 0x83, 0xFE, 0x04, 0x0F, 0x85, 0xF7, 0x00, 0x00, 0x00, 0x81, 0xFF,
    0x70, 0x17, 0x00, 0x00, 0x0F, 0x8C, 0xEB, 0x00, 0x00, 0x00, 0x45, 0x33, 0xE4, 0x4C, 0x39, 0x63,
    0x48, 0x75, 0x0B, 0x48, 0x8D, 0x4B, 0x50, 0xFF, 0x53, 0x30, 0x48, 0x89, 0x43, 0x48, 0x48, 0x8D,
    0x54, 0x24, 0x60, 0xB9, 0x8D, 0x8B, 0x00, 0x00, 0xFF, 0x13, 0x8B, 0x4C, 0x24, 0x60, 0x85, 0xC9,
    0x0F, 0x84, 0xBF, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x53, 0x70, 0xFF, 0x53, 0x48, 0x83, 0xF8, 0xFF,
    0x0F, 0x84, 0xAF, 0x00, 0x00, 0x00, 0x33, 0xC0, 0xF0, 0x44, 0x0F, 0xB1, 0xA3, 0x90, 0x00, 0x00,
    0x00, 0x83, 0xF8, 0x01, 0x0F, 0x85, 0x9B, 0x00, 0x00, 0x00, 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x3F, 0x4C, 0x89, 0x64, 0x24, 0x30, 0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0xB8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x3F, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x20, 0x48, 0x89,
    0x44, 0x24, 0x28, 0xF2, 0x0F, 0x10, 0x4C, 0x24, 0x28, 0xFF, 0x53, 0x28, 0x41, 0xB0, 0x01, 0x45,
    0x33, 0xC9, 0x41, 0x8A, 0xD0, 0x8A, 0xCA, 0xFF, 0x53, 0x18, 0xBE, 0xE2, 0x0B, 0x00, 0x00, 0x8B,
    0xCE, 0xFF, 0x53, 0x08, 0xBA, 0x01, 0x00, 0x00, 0x00, 0xB9, 0x02, 0x03, 0x00, 0x00, 0xFF, 0x53,
    0x20, 0x4C, 0x8B, 0xCD, 0x45, 0x8B, 0xC6, 0x8B, 0xD7, 0xB9, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x53,
    0x40, 0xF2, 0x0F, 0x10, 0x4C, 0x24, 0x20, 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x30, 0xFF, 0x53, 0x28,
    0x41, 0xB1, 0x01, 0x41, 0x8A, 0xD1, 0x45, 0x8A, 0xC1, 0x8A, 0xCA, 0xFF, 0x53, 0x18, 0x8B, 0xCE,
    0xFF, 0x53, 0x10, 0x4C, 0x8B, 0xCD, 0x45, 0x8B, 0xC6, 0x8B, 0xD7, 0xB9, 0x04, 0x00, 0x00, 0x00,
    0xFF, 0x53, 0x40, 0xEB, 0x0D, 0x4C, 0x8B, 0xCD, 0x45, 0x8B, 0xC6, 0x8B, 0xD7, 0x8B, 0xCE, 0xFF,
    0x53, 0x40, 0xF0, 0xFF, 0x8B, 0x94, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x5C, 0x24, 0x68, 0x48, 0x8B,
    0x6C, 0x24, 0x70, 0x48, 0x8B, 0x74, 0x24, 0x78, 0x48, 0x83, 0xC4, 0x40, 0x41, 0x5E, 0x41, 0x5C,
    0x5F, 0xC3,
};

static const char* CHAMS_DEFAULT_TARGET_PROCESS = "HD-Player.exe";
static const char* CHAMS_DEFAULT_TARGET_MODULE  = "libOpenglRender.dll";
static const char* CHAMS_DEFAULT_EAT_SUB1       = "GLDispatch";
static const char* CHAMS_DEFAULT_EAT_SUB2       = "DrawElements";

// ─── ChamsInjector ───────────────────────────────────────────────────────────
class ChamsInjector {
public:
    ~ChamsInjector( ) { Uninstall( ); }

    bool Install( ) {
        if ( installed ) return false;

        // Adquire handle com acesso COMPLETO (DUPLICATE_SAME_ACCESS) — sem Event ID 10.
        // Este handle pertence a nós; fechamos em Uninstall()/Fail().
        hProc = EMemory::AcquireChamsHandle( );
        if ( !hProc ) return false;

        HMODULE remoteRenderer = FindRemoteModule( hProc , CHAMS_DEFAULT_TARGET_MODULE );
        if ( !remoteRenderer ) return Fail( );

        slotAddr = FindSlotAddr( hProc , remoteRenderer , CHAMS_DEFAULT_EAT_SUB1 , CHAMS_DEFAULT_EAT_SUB2 );
        if ( !slotAddr ) return Fail( );

        if ( !ReadProcessMemory( hProc , slotAddr , &originalDraw , sizeof( originalDraw ) , nullptr ) )
            return Fail( );

        ChamsContext ctx = {};
        ctx.pglGetIntegerv     = ( PFN_glGetIntegerv )     ResolveRemote( hProc , "opengl32.dll" , "glGetIntegerv" );
        ctx.pglEnable          = ( PFN_glEnable )          ResolveRemote( hProc , "opengl32.dll" , "glEnable" );
        ctx.pglDisable         = ( PFN_glDisable )         ResolveRemote( hProc , "opengl32.dll" , "glDisable" );
        ctx.pglColorMask       = ( PFN_glColorMask )       ResolveRemote( hProc , "opengl32.dll" , "glColorMask" );
        ctx.pglBlendFunc       = ( PFN_glBlendFunc )       ResolveRemote( hProc , "opengl32.dll" , "glBlendFunc" );
        ctx.pglDepthRange      = ( PFN_glDepthRange )      ResolveRemote( hProc , "opengl32.dll" , "glDepthRange" );
        ctx.pwglGetProcAddress = ( PFN_wglGetProcAddress ) ResolveRemote( hProc , "opengl32.dll" , "wglGetProcAddress" );
        ctx.pGetAsyncKeyState  = ( PFN_GetAsyncKeyState )  ResolveRemote( hProc , "user32.dll"   , "GetAsyncKeyState" );

        if ( !ctx.pglGetIntegerv || !ctx.pglEnable || !ctx.pglDisable || !ctx.pglColorMask ||
             !ctx.pglBlendFunc   || !ctx.pglDepthRange || !ctx.pwglGetProcAddress || !ctx.pGetAsyncKeyState )
            return Fail( );

        ctx.pOriginalDraw = ( PFN_glDrawElements ) originalDraw;
        ctx.pglGetUniformLocation = nullptr;
        strcpy_s( ctx.sGetUniLoc , "glGetUniformLocation" );
        strcpy_s( ctx.sCharaLight , "_CharaLightIntensity" );
        ctx.chamsState = 0;
        ctx.inDrawHook = 0;

        remoteCtx = VirtualAllocEx( hProc , nullptr , sizeof( ChamsContext ) , MEM_COMMIT | MEM_RESERVE , PAGE_READWRITE );
        if ( !remoteCtx ) return Fail( );
        if ( !WriteProcessMemory( hProc , remoteCtx , &ctx , sizeof( ctx ) , nullptr ) ) return Fail( );

        unsigned char* blob = new unsigned char[ sizeof( g_chams_default_shellcode ) ];
        memcpy( blob , g_chams_default_shellcode , sizeof( g_chams_default_shellcode ) );
        PatchMagicRange( blob , sizeof( g_chams_default_shellcode ) , CHAMS_CTX_MAGIC , sizeof( ChamsContext ) , ( uint64_t ) remoteCtx );

        remoteCode = VirtualAllocEx( hProc , nullptr , sizeof( g_chams_default_shellcode ) , MEM_COMMIT | MEM_RESERVE , PAGE_READWRITE );
        if ( !remoteCode ) { delete[] blob; return Fail( ); }
        if ( !WriteProcessMemory( hProc , remoteCode , blob , sizeof( g_chams_default_shellcode ) , nullptr ) ) { delete[] blob; return Fail( ); }
        delete[] blob;

        DWORD oldProt;
        VirtualProtectEx( hProc , remoteCode , sizeof( g_chams_default_shellcode ) , PAGE_EXECUTE_READ , &oldProt );

        if ( !VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , PAGE_READWRITE , &oldProt ) ) return Fail( );
        if ( !WriteProcessMemory( hProc , slotAddr , &remoteCode , sizeof( remoteCode ) , nullptr ) ) return Fail( );
        VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , oldProt , &oldProt );

        installed = true;
        return true;
    }

    void Uninstall( ) {
        if ( !installed ) {
            if ( hProc ) { CloseHandle( hProc ); hProc = nullptr; }
            return;
        }

        DWORD oldProt;
        if ( slotAddr && hProc && VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , PAGE_READWRITE , &oldProt ) ) {
            WriteProcessMemory( hProc , slotAddr , &originalDraw , sizeof( originalDraw ) , nullptr );
            VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , oldProt , &oldProt );
        }

        // Aguarda o hook drenar (max 3s) para não libertar memória enquanto o
        // shellcode está executando dentro do DrawElements do BlueStacks.
        if ( remoteCtx && hProc ) {
            DWORD timeout = GetTickCount( ) + 3000;
            ChamsContext* rc = ( ChamsContext* ) remoteCtx;
            for ( ;; ) {
                LONG inHook = 0;
                if ( !ReadProcessMemory( hProc , ( LPCVOID ) &rc->inDrawHook , &inHook , sizeof( inHook ) , nullptr ) ) break;
                if ( inHook == 0 ) break;
                if ( GetTickCount( ) > timeout ) break;
                Sleep( 10 );
            }
        }

        if ( remoteCode && hProc ) { VirtualFreeEx( hProc , remoteCode , 0 , MEM_RELEASE ); remoteCode = nullptr; }
        if ( remoteCtx  && hProc ) { VirtualFreeEx( hProc , remoteCtx  , 0 , MEM_RELEASE ); remoteCtx  = nullptr; }

        // Fecha o handle — é nosso (adquirido via AcquireChamsHandle).
        if ( hProc ) { CloseHandle( hProc ); hProc = nullptr; }
        slotAddr     = nullptr;
        originalDraw = nullptr;
        installed    = false;
    }

    bool IsInstalled( ) const { return installed; }

private:
    bool   installed    = false;
    HANDLE hProc        = nullptr;
    void*  slotAddr     = nullptr;
    void*  originalDraw = nullptr;
    void*  remoteCtx    = nullptr;
    void*  remoteCode   = nullptr;

    // Libera memória remota, fecha handle e reseta estado em caso de falha no Install.
    bool Fail( ) {
        if ( remoteCode && hProc ) { VirtualFreeEx( hProc , remoteCode , 0 , MEM_RELEASE ); remoteCode = nullptr; }
        if ( remoteCtx  && hProc ) { VirtualFreeEx( hProc , remoteCtx  , 0 , MEM_RELEASE ); remoteCtx  = nullptr; }
        if ( hProc ) { CloseHandle( hProc ); hProc = nullptr; }
        slotAddr     = nullptr;
        originalDraw = nullptr;
        installed    = false;
        return false;
    }

    static HMODULE FindRemoteModule( HANDLE hProc , const char* name ) {
        HMODULE hMods[ 1024 ] = {};
        DWORD cbNeeded = 0;
        if ( !EnumProcessModulesEx( hProc , hMods , sizeof( hMods ) , &cbNeeded , LIST_MODULES_ALL ) )
            return nullptr;
        DWORD count = cbNeeded / ( DWORD ) sizeof( HMODULE );
        if ( count > 1024 ) count = 1024;
        for ( DWORD i = 0; i < count; i++ ) {
            char modName[ MAX_PATH ] = {};
            if ( GetModuleBaseNameA( hProc , hMods[ i ] , modName , sizeof( modName ) ) )
                if ( _stricmp( modName , name ) == 0 ) return hMods[ i ];
        }
        return nullptr;
    }

    static void* ResolveRemote( HANDLE hProc , const char* moduleName , const char* funcName ) {
        HMODULE local = GetModuleHandleA( moduleName );
        if ( !local ) local = LoadLibraryA( moduleName );
        if ( !local ) return nullptr;
        void* localFn = GetProcAddress( local , funcName );
        if ( !localFn ) return nullptr;
        uintptr_t rva = ( uintptr_t ) localFn - ( uintptr_t ) local;
        HMODULE remoteBase = FindRemoteModule( hProc , moduleName );
        if ( !remoteBase ) return nullptr;
        return ( void* ) ( ( uintptr_t ) remoteBase + rva );
    }

    static void* FindSlotAddr( HANDLE hProc , HMODULE remoteBase , const char* sub1 , const char* sub2 ) {
        IMAGE_DOS_HEADER dos = {};
        if ( !ReadProcessMemory( hProc , remoteBase , &dos , sizeof( dos ) , nullptr ) ) return nullptr;
        IMAGE_NT_HEADERS64 nt = {};
        if ( !ReadProcessMemory( hProc , ( BYTE* ) remoteBase + dos.e_lfanew , &nt , sizeof( nt ) , nullptr ) ) return nullptr;
        DWORD expRva = nt.OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ].VirtualAddress;
        if ( !expRva ) return nullptr;
        IMAGE_EXPORT_DIRECTORY exp = {};
        if ( !ReadProcessMemory( hProc , ( BYTE* ) remoteBase + expRva , &exp , sizeof( exp ) , nullptr ) ) return nullptr;
        std::vector<DWORD> names( exp.NumberOfNames );
        std::vector<DWORD> funcs( exp.NumberOfFunctions );
        std::vector<WORD>  ords( exp.NumberOfNames );
        ReadProcessMemory( hProc , ( BYTE* ) remoteBase + exp.AddressOfNames        , names.data( ) , exp.NumberOfNames     * sizeof( DWORD ) , nullptr );
        ReadProcessMemory( hProc , ( BYTE* ) remoteBase + exp.AddressOfFunctions    , funcs.data( ) , exp.NumberOfFunctions * sizeof( DWORD ) , nullptr );
        ReadProcessMemory( hProc , ( BYTE* ) remoteBase + exp.AddressOfNameOrdinals , ords.data( )  , exp.NumberOfNames     * sizeof( WORD )  , nullptr );
        void* slot = nullptr;
        for ( DWORD i = 0; i < exp.NumberOfNames; i++ ) {
            char nm[ 256 ] = { 0 };
            ReadProcessMemory( hProc , ( BYTE* ) remoteBase + names[ i ] , nm , sizeof( nm ) - 1 , nullptr );
            if ( strstr( nm , sub1 ) && strstr( nm , sub2 ) ) {
                slot = ( void* ) ( ( BYTE* ) remoteBase + funcs[ ords[ i ] ] );
                break;
            }
        }
        return slot;
    }

    static int PatchMagicRange( unsigned char* blob , size_t blobSize , uint64_t magicBase , size_t rangeSize , uint64_t newBase ) {
        int64_t delta = ( int64_t ) newBase - ( int64_t ) magicBase;
        int count = 0;
        for ( size_t i = 0; i + sizeof( uint64_t ) <= blobSize; i++ ) {
            uint64_t v = *( uint64_t* ) ( blob + i );
            if ( v >= magicBase && v < magicBase + rangeSize ) {
                *( uint64_t* ) ( blob + i ) = ( uint64_t ) ( ( int64_t ) v + delta );
                count++;
            }
        }
        return count;
    }
};
