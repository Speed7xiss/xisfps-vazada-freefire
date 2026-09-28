#include "EspStream.hpp"
#include <Windows.h>

void EspStreamApply( HWND hwnd, bool enable )
{
    if ( !hwnd ) hwnd = GetActiveWindow( );
    if ( !hwnd ) return;
    // Usermode streammode via SetWindowDisplayAffinity.
    // WDA_EXCLUDEFROMCAPTURE exclui a janela de capturas de tela.
    SetWindowDisplayAffinity( hwnd, enable ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE );
}
