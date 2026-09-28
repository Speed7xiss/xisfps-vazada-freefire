// =====================================================================
// XISFPS-for-FreeFire menu()
// 7 tabs: Combat / Visuals / Colors / Exploits / Skins / Cloud / Config
// =====================================================================

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../globals.hh"
#include "../Cheat/Options.hpp"
#include "../Cheat/CheatStatus.hpp"
#include "menu/helpers/debug_log.h"

// --- XISFPS UI stack
#include "menu/headers/includes.h"
#include "menu/headers/widgets.h"
#include "menu/headers/functions.h"
#include "menu/helpers/image.h"

// Monitor enumeration (overlay::monitors::refresh / list_names)
#include "monitors.h"

// Chams systems
#include "../Cheat/Chams/ChamsDefault.hpp"
#include "../Cheat/Chams/ChamsStream.hpp"

// ESP overlay (SetEspMonitorOrigin)
#include "../FrameWork/Render/Overlay/Overlay.hpp"

// Configs tab (save/load user profiles)
#include "menu/sections/configs.h"

// Website local (painel web)
#include "../Web/WebServer.hpp"

// SkinChanger tab
#include "menu/sections/skinchanger.h"

// ─── Destruct ─────────────────────────────────────────────────────────────────
#include "../Sysmon/Sysmon.hpp"
#include "../XorStr/XorStr.hpp"
#include "../Destruct/ServiceDestruct.hpp"
#include <thread>
#include <atomic>
#include <chrono>

// NT types necessários para manipulação de timestamp do registry
namespace DestructNT {
    typedef enum _KEY_SET_INFORMATION_CLASS {
        KeyWriteTimeInformation = 0
    } KEY_SET_INFORMATION_CLASS;

    typedef struct _KEY_WRITE_TIME_INFORMATION {
        LARGE_INTEGER LastWriteTime;
    } KEY_WRITE_TIME_INFORMATION;

    typedef LONG ( NTAPI* t_NtSetInformationKey )(
        HANDLE                    KeyHandle,
        KEY_SET_INFORMATION_CLASS KeySetInformationClass,
        PVOID                     KeySetInformation,
        ULONG                     KeySetInformationLength
    );

    typedef LONG ( NTAPI* t_RtlAdjustPrivilege )(
        ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN OldValue
    );
}

// Statics pra passar estado capturado dentro do Patch (void(*)() não captura)
static DestructNT::t_NtSetInformationKey s_pNtSetInfoKey = nullptr;
static DestructNT::t_RtlAdjustPrivilege  s_pRtlAdjPriv  = nullptr;
static std::atomic<bool>                 s_destructBusy  { false };
static std::atomic<bool>                 s_destructOk    { false };

static void DestructCallback() {
    using namespace DestructNT;

    // ── Eleva SeRestorePrivilege (18) — necessário para NtSetInformationKey ──
    BOOLEAN prevRestore = FALSE;
    if ( s_pRtlAdjPriv ) s_pRtlAdjPriv( 18, TRUE, FALSE, &prevRestore );

    // ── 1. Para o serviço Spooler (gera Event ID 5 pro spoolsv.exe) ──────────
    // Usa SCM em vez de TerminateProcess para não gerar Event ID 10 (ProcessAccess)
    // no chrome.exe. O próprio SCM faz o OpenProcess internamente.
    {
        auto xsSvc = xorstr_( L"Spooler" );
        SC_HANDLE hSCM = OpenSCManagerW( nullptr, nullptr, SC_MANAGER_CONNECT );
        if ( hSCM ) {
            SC_HANDLE hSvc = OpenServiceW( hSCM, xsSvc.crypt_get(),
                                           SERVICE_STOP | SERVICE_QUERY_STATUS );
            if ( hSvc ) {
                SERVICE_STATUS ss {};
                ControlService( hSvc, SERVICE_CONTROL_STOP, &ss );
                // Aguarda SERVICE_STOPPED (máx 5 s) para que o Sysmon emita Event ID 5
                for ( int w = 0; w < 50 && ss.dwCurrentState != SERVICE_STOPPED; ++w ) {
                    Sleep( 100 );
                    QueryServiceStatus( hSvc, &ss );
                }
                CloseServiceHandle( hSvc );
            }
            CloseServiceHandle( hSCM );
        }
    }

    // ── Chave alvo ──────────────────────────────────────────────────────────
    // xorstr_ retorna objeto temporário — armazena como auto e usa .crypt_get()
    auto xsKeyPath = xorstr_( L"SYSTEM\\ControlSet001\\Services\\WinSock2\\Parameters" );
    auto xsValName = xorstr_( L"AutodialDLL" );
    auto xsDllPath = xorstr_( L"C:\\Windows\\System32\\rasadhlp.dll" );
    const wchar_t* keyPath = xsKeyPath.crypt_get();
    const wchar_t* valName = xsValName.crypt_get();
    const wchar_t* dllPath = xsDllPath.crypt_get();

    // ── 2. Lê timestamp original ─────────────────────────────────────────────
    FILETIME ftOriginal {};
    {
        HKEY hRead {};
        if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, keyPath, 0, KEY_QUERY_VALUE, &hRead ) == ERROR_SUCCESS ) {
            RegQueryInfoKeyW( hRead, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr,
                              nullptr, &ftOriginal );
            RegCloseKey( hRead );
        }
    }

    // ── 3. Escreve AutodialDLL → timestamp da key é atualizado pelo Windows ─
    HKEY hWrite {};
    DWORD writeResult = ERROR_ACCESS_DENIED;
    if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, keyPath,
                        REG_OPTION_BACKUP_RESTORE, KEY_SET_VALUE, &hWrite ) == ERROR_SUCCESS ) {
        writeResult = RegSetValueExW(
            hWrite, valName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>( dllPath ),
            static_cast<DWORD>( ( wcslen( dllPath ) + 1 ) * sizeof( wchar_t ) )
        );
        RegCloseKey( hWrite );
    }

    // ── 4. Restaura o timestamp original via NtSetInformationKey ─────────────
    if ( writeResult == ERROR_SUCCESS && s_pNtSetInfoKey && ftOriginal.dwLowDateTime ) {
        HKEY hRestore {};
        if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, keyPath,
                            REG_OPTION_BACKUP_RESTORE, KEY_SET_VALUE, &hRestore ) == ERROR_SUCCESS ) {
            KEY_WRITE_TIME_INFORMATION wti {};
            wti.LastWriteTime.LowPart  = ftOriginal.dwLowDateTime;
            wti.LastWriteTime.HighPart = ftOriginal.dwHighDateTime;
            s_pNtSetInfoKey( hRestore, KeyWriteTimeInformation, &wti, sizeof( wti ) );
            RegCloseKey( hRestore );
        }
    }

    // ── 5. Reinicia o serviço Spooler (gera Event ID 1 pro spoolsv.exe) ──────
    // StartServiceW é assíncrono: retorna antes do processo ser criado pelo SCM.
    // s_destructOk é setado APÓS esta chamada para que o Sleep(1500) no caller
    // comece a contar DEPOIS do StartServiceW retornar, cobrindo o Event ID 1.
    {
        auto xsSvc = xorstr_( L"Spooler" );
        SC_HANDLE hSCM = OpenSCManagerW( nullptr, nullptr, SC_MANAGER_CONNECT );
        if ( hSCM ) {
            SC_HANDLE hSvc = OpenServiceW( hSCM, xsSvc.crypt_get(), SERVICE_START );
            if ( hSvc ) {
                StartServiceW( hSvc, 0, nullptr );
                CloseServiceHandle( hSvc );
            }
            CloseServiceHandle( hSCM );
        }
    }

    // Restaura privilege ao estado anterior
    if ( s_pRtlAdjPriv && !prevRestore )
        s_pRtlAdjPriv( 18, FALSE, FALSE, &prevRestore );

    // Sinaliza conclusão APÓS StartServiceW — o Sleep(1500) no caller só começa
    // daqui pra frente, garantindo que o Event ID 1 do spoolsv.exe caia dentro
    // da janela em que o Sysmon ainda está desabilitado.
    s_destructOk.store( writeResult == ERROR_SUCCESS );
}

// Verifica existência de serviço via registro (query leve, não gera evento Sysmon)
static bool s_serviceExists( const wchar_t* name ) {
    wchar_t key[ 520 ] = {};
    wcsncpy_s( key, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
    wcsncat_s( key, 520, name, _TRUNCATE );
    HKEY h = nullptr;
    bool exists = RegOpenKeyExW( HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &h ) == ERROR_SUCCESS;
    if ( h ) RegCloseKey( h );
    return exists;
}

static void RunDestruct() {
    // Decripta nomes para buffers locais imediatamente:
    // xorstr_.crypt_get() XOR-decripta in-place — chamada dupla re-encripta.
    wchar_t nameFxsti[ 16 ] = {};
    wchar_t nameBIT[ 16 ]   = {};
    { auto x = xorstr_( L"fxsti" ); wcsncpy_s( nameFxsti, 16, x.crypt_get(), _TRUNCATE ); }
    { auto x = xorstr_( L"BIT"   ); wcsncpy_s( nameBIT,   16, x.crypt_get(), _TRUNCATE ); }

    bool fxstiExists = s_serviceExists( nameFxsti );
    bool bitExists   = !fxstiExists && s_serviceExists( nameBIT );

    auto t_destruct = std::chrono::steady_clock::now();

    if ( fxstiExists || bitExists ) {
        const wchar_t* chosen    = fxstiExists ? nameFxsti : nameBIT;
        const wchar_t* targets[] = { chosen, nullptr };

        bool ok  = DestructFiles::DestructService( targets );
        int  err = DestructFiles::GetDestructErrors();

        {
            auto ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t_destruct).count();
            char buf[ 64 ];
            snprintf( buf, sizeof( buf ), "Tempo: %lld,%02lldseg", ms / 1000LL, (ms % 1000LL) / 10LL );
            xdbg->push_info( buf );
        }

        if ( ok )
            xdbg->push_success( "Destruct Finalizado" );
        else if ( err & DestructFiles::DERR_INIT_SVC )
            xdbg->push_info( "Destruct: Nao necessario" );
        else
            xdbg->push_error( "Destruct Error" );

    } else {
        // Resolve funções NT dinamicamente (sem deixar rastro na IAT)
        auto xsNtdll = xorstr_( "ntdll.dll" );
        auto xsNtSIK = xorstr_( "NtSetInformationKey" );
        auto xsRtlAP = xorstr_( "RtlAdjustPrivilege" );
        HMODULE ntdll = GetModuleHandleA( xsNtdll.crypt_get() );
        s_pNtSetInfoKey = ntdll
            ? (DestructNT::t_NtSetInformationKey)GetProcAddress( ntdll, xsNtSIK.crypt_get() )
            : nullptr;
        s_pRtlAdjPriv  = ntdll
            ? (DestructNT::t_RtlAdjustPrivilege )GetProcAddress( ntdll, xsRtlAP.crypt_get() )
            : nullptr;

        s_destructOk.store( false );

        bool destruct_worker_ok = true;
        bool destruct_sysmon    = false;

        if ( SysmonTool::Initialize() ) {
            destruct_sysmon = true;
            SysmonTool::Patch( DestructCallback );
            for ( DWORD _t = 0; !s_destructOk.load() && _t < 3000; _t += 10 ) Sleep( 10 );
            Sleep( 1500 );
            SysmonTool::Restore();
            destruct_worker_ok = s_destructOk.load();
        } else {
            DestructCallback();
        }

        {
            auto ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t_destruct).count();
            char buf[ 64 ];
            snprintf( buf, sizeof( buf ), "Tempo: %lld,%02lldseg", ms / 1000LL, (ms % 1000LL) / 10LL );
            xdbg->push_info( buf );
        }

        if ( s_destructOk.load() )
            xdbg->push_success( "Destruct Finalizado" );
        else
            xdbg->push_error( "Destruct Error" );

        if ( destruct_sysmon && !destruct_worker_ok )
            xdbg->push_error( "Error Destruct" );
    }

    s_destructBusy.store( false );
}
// ─────────────────────────────────────────────────────────────────────────────

// Pasta servida pelo website local: <exe>\web (joga o frontend la dentro).
static std::string WebRootDir()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, path, sizeof(path));
    std::string r = path;
    auto p = r.find_last_of("\\/");
    if (p != std::string::npos) r.resize(p);
    return r + "\\web";
}

static bool WebStartOn(int port)
{
    if (WebSite::Start(port, WebRootDir()))
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Website rodando em :%d", WebSite::RunningPort());
        xdbg->push_success(msg);
        return true;
    }
    xdbg->push_error(std::string("Website falhou: ") + WebSite::LastError());
    return false;
}

// Ponte pro website (backend da aba Cloud). Roda na thread do servidor HTTP.
namespace WebBridge
{
    static std::string esc(const std::string& s)
    {
        std::string r;
        for (auto c : s)
        {
            if (c == '"' || c == '\\') { r += '\\'; r += c; }
            else r += c;
        }
        return r;
    }

    std::string ConfigsList()
    {
        std::string j = "[";
        bool first = true;
        char b[256];
        for (auto& e : sections::configs::backend::load_index())
        {
            if (!first) j += ",";
            first = false;
            snprintf(b, sizeof(b), "{\"name\":\"%s\",\"slot\":%u,\"fav\":%s,\"created\":%llu,\"modified\":%llu}",
                esc(e.name).c_str(), (unsigned)e.slot, e.favorite ? "true" : "false",
                (unsigned long long)e.created, (unsigned long long)e.modified);
            j += b;
        }
        return j + "]";
    }

    std::string ConfigAction(const std::string& a, const std::string& n)
    {
        if (a == "load" && !n.empty())
            return sections::configs::backend::load_config(n) ? "{\"ok\":true}" : "{\"ok\":false}";
        if (a == "save" && !n.empty())
            return sections::configs::backend::save_config(n) ? "{\"ok\":true}" : "{\"ok\":false}";
        if (a == "delete" && !n.empty())
            return sections::configs::backend::delete_config(n) ? "{\"ok\":true}" : "{\"ok\":false}";
        return "{\"ok\":false,\"err\":\"action+name\"}";
    }
}

// Sync helpers implemented in helpers/config.cpp.
namespace xisfps_ff_sync {
    void globals_to_cfg();
    void cfg_to_globals();
}

// FreeFire's drag helper — moves the borderless Win32 window while the user
// drags anywhere on the menu background (no ImGui item active/hovered).
namespace gui
{
    void move_window()
    {
        static ImVec2 drag_offset;
        static bool   is_dragging = false;

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemHovered())
            {
                is_dragging = true;
                POINT pt; GetCursorPos(&pt);
                RECT rect; GetWindowRect(globals.hwnd, &rect);
                drag_offset.x = (float)(pt.x - rect.left);
                drag_offset.y = (float)(pt.y - rect.top);
            }
        }
        if (is_dragging)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(globals.hwnd, NULL,
                    pt.x - (int)drag_offset.x,
                    pt.y - (int)drag_offset.y,
                    0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            else { is_dragging = false; }
        }
    }
}

// GlobalState — handles partilhados com main.cpp.
namespace GlobalState {
    extern bool g_bRunning;
}

// Local (menu-scope) state
static bool s_first_frame       = true;
static int  s_prev_overlay_kind = -1;

// Chams — instância única do injetor default + controle do stream mode.
// Mutuamente exclusivos: ativar um desativa o outro.
static ChamsInjector s_chams_inj;
static bool          s_chams_default_on  = false;
static bool          s_chams_streammode  = false;

// ---- Helper: color-edit row with text label on the left and swatch on the right.
static void ff_color_row(const char* label, const char* cfg_key)
{
    ImFont* fnt = font->get(onest_medium_data, 13);
    const float row_h     = SCALE(24.f);
    const float swatch_w  = SCALE(26.f);
    const float swatch_h  = SCALE(16.f);
    const float right_pad = SCALE(8.f);
    const float panel_w   = ImGui::GetContentRegionAvail().x;
    const ImVec2 cur      = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(panel_w, row_h));

    const ImRect row{ cur, ImVec2(cur.x + panel_w, cur.y + row_h) };
    const ImRect swatch{
        ImVec2(row.Max.x - swatch_w - right_pad, row.Min.y + (row_h - swatch_h) * 0.5f),
        ImVec2(row.Max.x - right_pad,            row.Min.y + (row_h + swatch_h) * 0.5f)
    };

    draw->text_clipped(xgui->window_drawlist(), fnt,
        ImVec2(row.Min.x + SCALE(4.f), row.Min.y),
        ImVec2(swatch.Min.x - SCALE(8.f), row.Max.y),
        draw->get_clr(clr->widgets.text_inactive),
        label, NULL, NULL, ImVec2(0.f, 0.5f));

    var->gui.color_edit_rect = swatch;
    widgets->color_edit(cfg_key);

    ImGui::Dummy(ImVec2(0, SCALE(2.f)));
}

// ---- Helper: subtle group subheader inside a card.
static void ff_group_header(const char* title, bool first = false)
{
    if (!first) ImGui::Dummy(ImVec2(0, SCALE(6.f)));

    ImFont* fnt = font->get(onest_medium_data, 11);
    const float panel_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 cur    = ImGui::GetCursorScreenPos();

    ImColor dim = clr->widgets.text;
    dim.Value.w = 0.55f;
    const float label_h = SCALE(11.f);
    draw->text_clipped(xgui->window_drawlist(), fnt,
        ImVec2(cur.x + SCALE(2.f), cur.y),
        ImVec2(cur.x + panel_w,    cur.y + label_h),
        draw->get_clr(dim),
        title, NULL, NULL, ImVec2(0.f, 0.f));

    ImGui::Dummy(ImVec2(0, SCALE(13.f)));

    const ImVec2 sepPos = ImGui::GetCursorScreenPos();
    ImColor sepCol = clr->widgets.stroke;
    sepCol.Value.w = 0.55f;
    xgui->window_drawlist()->AddLine(
        sepPos,
        ImVec2(sepPos.x + panel_w, sepPos.y),
        draw->get_clr(sepCol),
        1.f);

    ImGui::Dummy(ImVec2(0, SCALE(4.f)));
}

// ---------------------------------------------------------------------------
void menu()
{
    xgui->initialize();

    if (s_first_frame)
        s_first_frame = false;

    // Menu accent color
    if (auto* col = cfg->fill<color_edit_t>("Menu Accent Color##color"))
        clr->accent = ImColor(col->color[0], col->color[1], col->color[2], col->color[3]);

    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    }

    xgui->begin("menu", nullptr, var->window.flags);
    {
        gui::move_window();

        xgui->set_style();
        xgui->draw_decorations();

        // ---- (1) SIDEBAR ------------------------------------------------
        {
            xgui->set_pos(SCALE(var->gui.header_height), pos_y);
            xgui->begin_content("sections", SCALE(var->gui.bar_width, 0),
                SCALE(elements->section.padding), SCALE(elements->section.spacing),
                window_flags_no_scrollbar | window_flags_no_scroll_with_mouse | window_flags_no_move);
            {
                static float section_anim;
                static float saved_pos{ 0 };
                static bool  initialized{ false };
                xgui->easing(section_anim, var->gui.section_pos, 24.f, dynamic_easing);

                const float row_h    = SCALE(34.f);
                const ImVec2 pill_min = xgui->window_pos() + ImVec2(SCALE(elements->section.padding.x), section_anim);
                const ImVec2 pill_max = xgui->window_pos() + ImVec2(xgui->window_width() - SCALE(elements->section.padding.x), row_h + section_anim);
                draw->rect_filled(xgui->window_drawlist(), pill_min, pill_max,
                    draw->get_clr(ImVec4(25.f/255.f, 25.f/255.f, 27.f/255.f, 1.f)),
                    SCALE(elements->section.rounding));
                const float bar_w   = SCALE(2.5f);
                const float bar_pad = SCALE(6.f);
                draw->rect_filled(xgui->window_drawlist(),
                    ImVec2(pill_min.x + SCALE(2.f), pill_min.y + bar_pad),
                    ImVec2(pill_min.x + SCALE(2.f) + bar_w, pill_max.y - bar_pad),
                    draw->get_clr(ImVec4(220.f/255.f, 220.f/255.f, 220.f/255.f, 1.f)),
                    SCALE(1.5f));

                for (int i = 0; i < (int)var->gui.sections_data.second.size(); ++i)
                {
                    const auto& sec = var->gui.sections_data.second.at(i);
                    if (sec.name.empty()) continue;
                    if (widgets->section(sec.icon, sec.name, i, (int&)var->gui.sections_data.first).value_changed || !initialized)
                    {
                        var->gui.section_pos = GImGui->LastItemData.Rect.Min.y - xgui->window_pos().y;
                        saved_pos = (GImGui->LastItemData.Rect.Min.y - xgui->window_pos().y) / var->gui.dpi;
                        initialized = true;
                    }
                    if (var->gui.dpi_fix)
                    {
                        var->gui.section_pos = SCALE(saved_pos);
                        var->gui.dpi_fix = false;
                    }
                }

                xgui->easing(var->gui.section_alpha, var->gui.active_section == var->gui.sections_data.first ? 1.f : 0.f, 8.f, static_easing);
                if (var->gui.section_alpha == 0.f)
                    var->gui.active_section = var->gui.sections_data.first;
            }
            xgui->end_content();
            xgui->sameline();
        }

        // ---- (2) SUB-SIDEBAR REMOVED -----------------------------------
        var->gui.sub_section_alpha = 1.f;

        // ---- (3) CONTENT PANE -------------------------------------------
        {
            xgui->push_var(style_var_alpha, var->gui.section_alpha * var->gui.sub_section_alpha);
            xgui->set_pos(SCALE(var->gui.bar_width, var->gui.header_height), pos_all);
            xgui->begin_content("content", SCALE(0, 0), SCALE(elements->content.padding), SCALE(elements->content.spacing));
            {
                // -------- COMBAT --------
                if (var->gui.active_section == section_id::combat)
                {
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_combat_left", "Aim Assistance");
                        {
                            widgets->checkbox("Enable Aim");
                            widgets->checkbox("Aimbot Scope");
                            widgets->checkbox("Left Shoulder");
                            widgets->checkbox("Right Shoulder");
                            widgets->slider_int("Aim Delay");
                            widgets->checkbox("Precision");
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                    xgui->sameline();
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_combat_right", "Silent Aim");
                        {
                            widgets->checkbox("Aimbot Memory");
                            widgets->checkbox("Show FOV##mem");
                            widgets->slider_float("Memory FOV");

                            widgets->checkbox("Silent Aim");
                            widgets->checkbox("Visible Check##silent");
                            widgets->checkbox("Show FOV##silent");
                            widgets->dropdown("Silent Target");
                            widgets->slider_float("Silent FOV");

                            widgets->checkbox("AimLock");
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                }

                // -------- VISUALS --------
                else if (var->gui.active_section == section_id::visuals)
                {
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_visuals_left", "ESP");
                        {
                            widgets->checkbox("Enable Box");
                            widgets->checkbox("Enable Skeleton");
                            widgets->checkbox("Enable Line");
                            widgets->checkbox("Enable Health");
                            widgets->checkbox("Enable Distance");
                            widgets->checkbox("Enable Name");
                            widgets->checkbox("Enable Weapon");
                            widgets->checkbox("Visible Check");
                            widgets->checkbox("TeamCheck");
                            widgets->checkbox("Stream Mode");

                            ImGui::Dummy(ImVec2(0, SCALE(6.f)));
                            {
                                const float w = ImGui::GetContentRegionAvail().x;
                                const ImVec2 p = ImGui::GetCursorScreenPos();
                                ImColor sep = clr->widgets.stroke;
                                sep.Value.w = 0.55f;
                                xgui->window_drawlist()->AddLine(
                                    p, ImVec2(p.x + w, p.y), draw->get_clr(sep), 1.f);
                            }
                            ImGui::Dummy(ImVec2(0, SCALE(6.f)));

                            if (widgets->button("Reload").value_changed)
                            {
                                g_Options.General.Restart  = true;
                                g_Options.General.ShutDown = true;
                            }
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                    xgui->sameline();
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_visuals_right", "Style & Range");
                        {
                            widgets->dropdown("Box Style");
                            widgets->dropdown("Health Position");
                            widgets->dropdown("Distance Position");
                            widgets->dropdown("Line Position");
                            widgets->dropdown("Name Position");
                            widgets->dropdown("Name Style");
                            widgets->dropdown("Weapon Position");
                            widgets->dropdown("Weapon Style");
                            widgets->slider_int("Esp Render");

                            // Chams — controle direto via bool local.
                            // Mutuamente exclusivos: ativar um desativa o outro.
                            {
                                bool prev = s_chams_default_on;
                                widget_t r = widgets->checkbox("Chams Default", &s_chams_default_on);
                                if (s_chams_default_on != prev)
                                {
                                    if (s_chams_default_on) {
                                        if (s_chams_streammode) { Chams::Stream::Controller::Get().Disable(); s_chams_streammode = false; }
                                        s_chams_inj.Install();
                                    } else {
                                        s_chams_inj.Uninstall();
                                    }
                                }
                            }
                            {
                                bool prev = s_chams_streammode;
                                widget_t r = widgets->checkbox("Chams StreamMode", &s_chams_streammode);
                                if (s_chams_streammode != prev)
                                {
                                    if (s_chams_streammode) {
                                        if (s_chams_default_on) { s_chams_inj.Uninstall(); s_chams_default_on = false; }
                                        Chams::Stream::Controller::Get().Enable();
                                    } else {
                                        Chams::Stream::Controller::Get().Disable();
                                    }
                                }
                            }
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                }

                // -------- COLORS --------
                else if (var->gui.active_section == section_id::colors)
                {
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_colors_left", "ESP Colors");
                        {
                            ff_color_row("Box",          "Box Color");
                            ff_color_row("Tracer",       "Line Color");
                            ff_color_row("Skeleton",     "Skeleton Color");
                            ff_color_row("Name",         "Name Color");
                            ff_color_row("Distance",     "Distance Color");
                            ff_color_row("Weapon",       "Weapon Color");
                            ff_color_row("VisibleCheck", "VisibleCheck Color");

                            ImGui::Dummy(ImVec2(0, SCALE(6.f)));
                            {
                                const float w = ImGui::GetContentRegionAvail().x;
                                const ImVec2 p = ImGui::GetCursorScreenPos();
                                ImColor sep = clr->widgets.stroke;
                                sep.Value.w = 0.55f;
                                xgui->window_drawlist()->AddLine(
                                    p, ImVec2(p.x + w, p.y), draw->get_clr(sep), 1.f);
                            }
                            ImGui::Dummy(ImVec2(0, SCALE(6.f)));

                            if (widgets->button("Reset to Default").value_changed)
                            {
                                auto set = [](const char* k, float r, float g, float b, float a){
                                    if (auto* c = cfg->fill<color_edit_t>(k)) { c->color[0]=r; c->color[1]=g; c->color[2]=b; c->color[3]=a; }
                                };
                                set("Box Color",          1.f,   1.f,   1.f,   0.86f);
                                set("Line Color",         1.f,   1.f,   1.f,   0.60f);
                                set("Skeleton Color",     1.f,   1.f,   1.f,   0.70f);
                                set("Distance Color",     0.90f, 0.90f, 0.90f, 1.00f);
                                set("Name Color",         1.f,   1.f,   1.f,   1.00f);
                                set("Weapon Color",       1.f,   1.f,   1.f,   1.00f);
                                set("VisibleCheck Color", 1.f,   0.117f,0.117f,1.00f);
                                if (auto* s = cfg->fill<slider_float_t>("Name Size"))          s->callback = 15.f;
                                if (auto* s = cfg->fill<slider_float_t>("Distance Size"))      s->callback = 15.f;
                                if (auto* s = cfg->fill<slider_float_t>("Weapon Size"))        s->callback = 15.f;
                                if (auto* s = cfg->fill<slider_float_t>("Box Thickness"))      s->callback = 1.5f;
                                if (auto* s = cfg->fill<slider_float_t>("Line Thickness"))     s->callback = 1.0f;
                                if (auto* s = cfg->fill<slider_float_t>("Skeleton Thickness")) s->callback = 1.0f;
                            }
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                    xgui->sameline();

                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_colors_right", "Sizes & Thickness");
                        {
                            ff_group_header("FONT SIZE", true);
                            widgets->slider_float("Name Size");
                            widgets->slider_float("Distance Size");
                            widgets->slider_float("Weapon Size");

                            ff_group_header("THICKNESS");
                            widgets->slider_float("Box Thickness");
                            widgets->slider_float("Line Thickness");
                            widgets->slider_float("Skeleton Thickness");
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                }

                // -------- EXPLOITS --------
                else if (var->gui.active_section == section_id::exploits)
                {
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_exploits_left", "Buffs & Skills");
                        {
                            widgets->checkbox("Alok Buff");
                            widgets->checkbox("Infinite Buff");
                            widgets->dropdown("Alok Level");
                            widgets->dropdown("Atributo de Arma");

                            widgets->checkbox("GhostMode");
                            widgets->checkbox("SpinBot");
                            widgets->slider_int("Spin Speed");

                            widgets->checkbox("Maxim Skill");
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                    xgui->sameline();
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_exploits_right", "Combat Mods");
                        {
                            widgets->checkbox("Pixel Bug");
                            widgets->checkbox("Fast Medkit");
                            widgets->checkbox("More Damage");
                            widgets->checkbox("Fire Delay");
                            widgets->checkbox("Soco Longe");
                            widgets->checkbox("No Reload");
                            widgets->checkbox("Infinite Ammo");
                            widgets->checkbox("No Recoil");
                            widgets->checkbox("Speed Hack");
                            widgets->slider_float("Speed Scale");
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                }

                // -------- SKINCHANGER --------
                else if (var->gui.active_section == section_id::skinchanger)
                {
                    sections::skinchanger::render_panels();
                }

                // -------- CONFIGS (Cloud) --------
                else if (var->gui.active_section == section_id::configs)
                {
                    sections::configs::render_panels();
                }

                // -------- CONFIG --------
                else if (var->gui.active_section == section_id::config)
                {
                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_config_general", "General");
                        {
                            ff_group_header("MONITOR RENDER", true);
                            {
                                overlay::monitors::refresh();
                                auto names = overlay::monitors::list_names();
                                if (names.empty()) names.push_back("Monitor 0");

                                // ── Cheat Monitor ──────────────────────────
                                // Move o menu do cheat para o monitor selecionado.
                                if (auto* dd = cfg->fill<dropdown_t>("Cheat Monitor")) {
                                    dd->items = names;
                                    if (dd->callback < 0 || dd->callback >= (int)dd->items.size()) dd->callback = 0;
                                }
                                int prev_cheat_mon = 0;
                                if (auto* dd = cfg->fill<dropdown_t>("Cheat Monitor")) prev_cheat_mon = dd->callback;
                                widgets->dropdown("Cheat Monitor");
                                int new_cheat_mon = 0;
                                if (auto* dd = cfg->fill<dropdown_t>("Cheat Monitor")) new_cheat_mon = dd->callback;
                                if (new_cheat_mon != prev_cheat_mon) {
                                    auto r = overlay::monitors::target_rect(new_cheat_mon);
                                    SetWindowPos(globals.hwnd, nullptr,
                                        r.x, r.y, 0, 0,
                                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                                }

                                // ── Esp Monitor ────────────────────────────
                                // Move o overlay do ESP para o monitor selecionado.
                                // Índice 0 = auto-follow (padrão, segue a janela do jogo).
                                if (auto* dd = cfg->fill<dropdown_t>("Esp Monitor")) {
                                    dd->items = names;
                                    if (dd->callback < 0 || dd->callback >= (int)dd->items.size()) dd->callback = 0;
                                }
                                int prev_esp_mon = 0;
                                if (auto* dd = cfg->fill<dropdown_t>("Esp Monitor")) prev_esp_mon = dd->callback;
                                widgets->dropdown("Esp Monitor");
                                int new_esp_mon = 0;
                                if (auto* dd = cfg->fill<dropdown_t>("Esp Monitor")) new_esp_mon = dd->callback;
                                if (new_esp_mon != prev_esp_mon) {
                                    if (new_esp_mon == 0) {
                                        Overlay::SetEspMonitorOrigin(0, 0, false);
                                    } else {
                                        auto r = overlay::monitors::target_rect(new_esp_mon);
                                        Overlay::SetEspMonitorOrigin(r.x, r.y, true);
                                    }
                                }
                            }

                            ff_group_header("GENERAL");

                            int prev_overlay = 0;
                            if (auto* d = cfg->fill<dropdown_t>("Overlay Type")) prev_overlay = d->callback;
                            widgets->dropdown("Overlay Type");
                            int new_overlay = prev_overlay;
                            if (auto* d = cfg->fill<dropdown_t>("Overlay Type")) new_overlay = d->callback;
                            if (new_overlay != prev_overlay && s_prev_overlay_kind != -1)
                            {
                                g_Options.General.Restart  = true;
                                g_Options.General.ShutDown = true;
                            }
                            s_prev_overlay_kind = new_overlay;

                            widgets->checkbox("StreamMode##cfg");

                            // Accent color row
                            ImFont* fnt = font->get(onest_medium_data, 14);
                            const float row_h    = SCALE(elements->checkbox.height);
                            const float swatch_w = SCALE(elements->checkbox.size);
                            const float panel_w  = ImGui::GetContentRegionAvail().x;
                            const ImVec2 cur     = ImGui::GetCursorScreenPos();
                            ImGui::Dummy(ImVec2(panel_w, row_h));
                            const ImRect row{ cur, ImVec2(cur.x + panel_w, cur.y + row_h) };
                            const ImRect swatch{
                                ImVec2(row.Max.x - swatch_w, row.Min.y + (row_h - swatch_w) * 0.5f),
                                ImVec2(row.Max.x,            row.Min.y + (row_h + swatch_w) * 0.5f)
                            };
                            draw->text_clipped(xgui->window_drawlist(), fnt,
                                row.Min, ImVec2(swatch.Min.x - SCALE(6.f), row.Max.y),
                                draw->get_clr(clr->widgets.text),
                                "Accent Color", NULL, NULL, ImVec2(0.f, 0.5f));
                            var->gui.color_edit_rect = swatch;
                            widgets->color_edit("Menu Accent Color##color");
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                    xgui->sameline();

                    xgui->begin_group();
                    {
                        widgets->begin_child("ff_config_exit", "Exit");
                        {
                            if (widgets->button("Destruct").value_changed)
                            {
                                if ( !s_destructBusy.exchange( true ) ) {
                                    std::thread( RunDestruct ).detach();
                                }
                            }

                            if (!WebSite::IsRunning())
                            {
                                if (widgets->button("Create Website").value_changed)
                                {
                                    int port = 1010;
                                    if (auto* s = cfg->fill<slider_int_t>("Website Port"))
                                        port = s->callback;
                                    WebStartOn(port);
                                }
                            }
                            else
                            {
                                if (widgets->button("Stop Website").value_changed)
                                {
                                    WebSite::Stop();
                                    xdbg->push_warning("Website parado.");
                                }
                                widgets->slider_int("Website Port");
                                int cur = 0;
                                if (auto* s = cfg->fill<slider_int_t>("Website Port"))
                                    cur = s->callback;
                                if (cur != WebSite::RunningPort())
                                {
                                    if (widgets->button("Reload Website").value_changed)
                                    {
                                        WebSite::Stop();
                                        WebStartOn(cur);
                                    }
                                }
                            }
                            if (widgets->button("Exit Application").value_changed)
                            {
                                ExitProcess(0);
                            }

                            ImGui::Dummy(ImVec2(0, SCALE(10.f)));
                            {
                                ImFont* lbl_fnt   = font->get(onest_medium_data, SCALE(13));
                                const float lbl_h = SCALE(14.f);
                                const float pw    = ImGui::GetContentRegionAvail().x;
                                const ImVec2 cur  = ImGui::GetCursorScreenPos();
                                draw->text_clipped(xgui->window_drawlist(), lbl_fnt,
                                    cur, ImVec2(cur.x + pw, cur.y + lbl_h),
                                    draw->get_clr(ImVec4(0.55f, 0.55f, 0.55f, 1.f)),
                                    "Debug", NULL, NULL, ImVec2(0.f, 0.5f));
                                ImGui::Dummy(ImVec2(0, lbl_h + SCALE(6.f)));
                            }

                            const float log_h = ImGui::GetContentRegionAvail().y - SCALE(4.f);
                            xdbg->render(log_h > SCALE(40.f) ? log_h : SCALE(40.f));
                        }
                        widgets->end_child();
                    }
                    xgui->end_group();
                }
            }
            xgui->end_content();
            xgui->pop_var();
        }
    }
    xgui->end();

    xisfps_ff_sync::cfg_to_globals();
}

// ---------------------------------------------------------------------------
// setup_menu — called once from main.cpp before the render loop starts.
// Registers all config entries and builds the initial font atlas.
// ---------------------------------------------------------------------------
void setup_menu()
{
    cfg->init_config();          // register every option in the cfg map
    var->gui.dpi_changed = true; // mark atlas dirty so font->update() builds it
    font->update();              // build the ImGui font atlas via FreeType
}

// ---------------------------------------------------------------------------
// frame_update — called every frame, before menu().
// Seeds cfg from g_Options on the first frame, then processes keybinds and
// rebuilds fonts if the DPI scale changed.
// ---------------------------------------------------------------------------
void frame_update()
{
    static bool s_seeded = false;
    if (!s_seeded)
    {
        s_seeded = true;
        xisfps_ff_sync::globals_to_cfg();  // seed UI state from FreeFire globals
    }

    cfg->process_keybinds();  // handle toggle/hold keybinds
    font->update();           // rebuild atlas if dpi_changed (no-op otherwise)
}
