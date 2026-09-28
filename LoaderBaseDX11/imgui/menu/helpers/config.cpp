#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "../headers/config.h"
#include <utility>
#include <imgui_internal.h>
#include "../settings/colors.h"
// Precisamos de GetAsyncKeyState + VK_* para traduzir a ImGuiKey capturada
// no popup do gear (valores >= 512) para o Virtual-Key da Win32 que os
// threads do AimModules esperam (1..255).
#include <Windows.h>
// Monitor Render — projetor nao disponivel nesta build; apenas monitores.h.
#include "../../monitors.h"

//
// XISFPS-for-FreeFire config store.
//
// The XISFPS widget layer (widgets->checkbox, widgets->slider_float, etc.)
// looks up state in a config store keyed by string. The FreeFire backend,
// however, keeps its state in a bunch of raw globals (`g_Options.AimDMA.*`,
// `functions.aim.*`, …). To reuse the XISFPS widgets untouched we:
//
//   1. Register every FreeFire-visible widget as a cfg entry with its initial
//      value pulled from the FreeFire global (init_config).
//   2. On every frame, before the UI renders, we copy FreeFire globals →
//      cfg (so external code that flips a global is reflected immediately).
//   3. After the UI renders, we copy cfg → FreeFire globals (so widget
//      edits propagate to the backend).
//
// This lets the FreeFire hooks / DMA loops keep reading from their existing
// globals without knowing the UI even exists.
//

// ---- Include the FreeFire backend so we can read/write its globals. --------
#include "../../../globals.hh"
// Options.hpp lives under Esp/Cheat/. Guard the include so it can be pulled
// in from other build roots without breaking.
#include "../../../Cheat/Options.hpp"

static std::string strip_imgui_suffix(const std::string& s)
{
    auto p = s.find("##");
    return p == std::string::npos ? s : s.substr(0, p);
}

// -------- Feature list --------
//
// One block per tab, one line per widget. Keys must match the ones the new
// gui.cpp passes to `widgets->…`. Defaults are pulled from the FreeFire
// globals so the initial UI state reflects the actual backend.
//
void c_config::init_config()
{
    static bool init = false;
    if (std::exchange(init, true)) return;

    auto& O = g_Options;
    auto& A = O.AimDMA;
    auto& P = O.Visuals.ESP.Players;

    // ===== COMBAT =====
    // Aim Assistance (left column of the Combat tab)
    add_option<checkbox_t>    ("Enable Aim",              functions.aim.enabled, true);
    add_option<checkbox_t>    ("Left Shoulder",           A.LegitAimLeftShoulder);
    add_option<checkbox_t>    ("Right Shoulder",          A.LegitAimRightShoulder);
    add_option<slider_int_t>  ("Aim Delay",               A.LegitAimDelay, 0, 360, "%d");
    // Aimbot 2022/Left/Right/Trick removidos — escreviam em functions.aim.*
    // que só têm consumers no menu legado (menu_legacy_unused) que nunca é
    // invocado. Left/Right Shoulder acima já cobrem shoulder-aims via
    // A.LegitAim*Shoulder.
    add_option<checkbox_t>    ("Precision",               A.Precision,               true);

    // Advanced (right column)
    add_option<checkbox_t>    ("Aimbot Memory",           A.AimBotMemory,            true);
    add_option<checkbox_t>    ("Show FOV##mem",           A.AimBotMemoryDrawFov);
    add_option<slider_float_t>("Memory FOV",              A.AimBotMemoryFov,   1.f,   800.f, "%.0f");
    add_option<checkbox_t>    ("Silent Aim",              A.SilentAim,               true);
    add_option<checkbox_t>    ("Visible Check##silent",   A.SilentVisibleCheck);
    add_option<checkbox_t>    ("Show FOV##silent",        A.SilentDrawFov);
    add_option<dropdown_t>    ("Silent Target",           A.SilentAimTarget, string_t{ "Head", "Chest" });
    add_option<slider_float_t>("Silent FOV",              A.SilentFov,         1.f,   800.f, "%.0f");
    add_option<checkbox_t>    ("SpinBot",                 A.SpinBot,                 true);
    add_option<slider_int_t>  ("Spin Speed",              A.SpinBotSpeed,      1,     100,   "%d");
    add_option<checkbox_t>    ("AimLock",                 A.Aimlock,                 true);
    add_option<checkbox_t>    ("GhostMode",               A.GhostMode,               true);

    // ===== VISUALS =====
    add_option<checkbox_t>    ("Enable Box",              P.ESPBox);
    add_option<checkbox_t>    ("Enable Skeleton",         P.ESPSkeleton);
    add_option<checkbox_t>    ("Enable Line",             P.ESPLine);
    add_option<checkbox_t>    ("Enable Health",           P.ESPHealth);
    add_option<checkbox_t>    ("Enable Distance",         P.ESPDistance);
    add_option<checkbox_t>    ("Enable Name",             P.ESPName);
    add_option<checkbox_t>    ("Enable Weapon",           P.ESPWeapon);
    add_option<checkbox_t>    ("Visible Check",           P.UseVisibilityColor);
    add_option<checkbox_t>    ("TeamCheck",               P.TeamCheck);
    add_option<checkbox_t>    ("Stream Mode",             O.General.StreamMode);

    add_option<dropdown_t>    ("Box Style",               P.ESPBoxStyle,    string_t{ "Normal", "Filled", "Corners" });
    add_option<dropdown_t>    ("Health Position",         P.ESPHealthPos,   string_t{ "Left", "Right", "Top", "Bottom" });
    add_option<dropdown_t>    ("Distance Position",       P.ESPDistancePos, string_t{ "Left", "Right", "Top", "Bottom" });
    add_option<dropdown_t>    ("Line Position",           P.ESPLinePos,     string_t{ "Top", "Bottom" });
    add_option<dropdown_t>    ("Name Position",           P.ESPNamePos,     string_t{ "Left", "Right", "Top", "Bottom" });
    add_option<dropdown_t>    ("Name Style",              P.ESPNameStyle,   string_t{ "Nick only", "Nick + HP", "BOT tag" });
    add_option<dropdown_t>    ("Weapon Position",         P.ESPWeaponPos,   string_t{ "Left", "Right", "Top", "Bottom" });
    add_option<dropdown_t>    ("Weapon Style",            P.ESPWeaponStyle, string_t{ "Name only", "Icon + Name", "Only Icon" });
    add_option<slider_int_t>  ("Esp Render",              P.EspRenderDistance, 1, 1000, "%dm");

    // ===== COLORS =====
    add_option<color_edit_t>  ("Box Color",       col_t{ P.BoxColor[0],      P.BoxColor[1],      P.BoxColor[2],      P.BoxColor[3]      }, true);
    add_option<color_edit_t>  ("Line Color",      col_t{ P.LineColor[0],     P.LineColor[1],     P.LineColor[2],     P.LineColor[3]     }, true);
    add_option<color_edit_t>  ("Skeleton Color",  col_t{ P.SkeletonColor[0], P.SkeletonColor[1], P.SkeletonColor[2], P.SkeletonColor[3] }, true);
    add_option<color_edit_t>  ("Distance Color",  col_t{ P.DistanceColor[0], P.DistanceColor[1], P.DistanceColor[2], P.DistanceColor[3] }, true);
    add_option<color_edit_t>  ("Name Color",      col_t{ P.NameColor[0],     P.NameColor[1],     P.NameColor[2],     P.NameColor[3]     }, true);
    add_option<color_edit_t>  ("Weapon Color",    col_t{ P.WeaponColor[0],   P.WeaponColor[1],   P.WeaponColor[2],   P.WeaponColor[3]   }, true);
    // VisibleCheck Color — cor aplicada quando Visible Check está ON e o
    // inimigo está atrás de parede. Antes era IM_COL32(255,30,30,a) hardcoded
    // no pickCol. Backend continua chamando o campo de HiddenColor.
    add_option<color_edit_t>  ("VisibleCheck Color", col_t{ P.HiddenColor[0], P.HiddenColor[1], P.HiddenColor[2], P.HiddenColor[3] }, true);
    add_option<slider_float_t>("Name Size",             P.NameSize,          8.f,  40.f, "%.0f");
    add_option<slider_float_t>("Distance Size",         P.DistanceSize,      8.f,  40.f, "%.0f");
    add_option<slider_float_t>("Weapon Size",           P.WeaponSize,        8.f,  60.f, "%.0f");
    add_option<slider_float_t>("Box Thickness",         P.BoxThickness,      0.5f,  5.f, "%.1f");
    // Antes hardcoded 1.f dentro do Draw.cpp — agora expostos.
    add_option<slider_float_t>("Line Thickness",        P.LineThickness,     0.5f,  5.f, "%.1f");
    add_option<slider_float_t>("Skeleton Thickness",    P.SkeletonThickness, 0.5f,  5.f, "%.1f");

    // ===== EXPLOITS =====
    add_option<checkbox_t>    ("Alok Buff",               A.AlokBuff);
    add_option<checkbox_t>    ("Infinite Buff",           A.AlokBuffInfinite);
    // Alok Level — 3 itens. Alok 1 é o piso (antes era "Natural" / 1.00×),
    // agora sempre aplica pelo menos 1.15×. Idx 0..2 → AimModules soma +1
    // pra indexar kMultScales.
    add_option<dropdown_t>    ("Alok Level",              A.AlokBuffLevel,       string_t{ "Alok 1", "Alok 2", "Alok 3" });
    add_option<dropdown_t>    ("Atributo de Arma",        A.AtributarArmaSpeed,  string_t{ "Padrao", "Nivel 1", "Nivel 2", "Nivel 3", "Nivel 4" });
    add_option<checkbox_t>    ("Pixel Bug",               A.BugarPixel);
    add_option<checkbox_t>    ("Fast Medkit",             A.FastMedkit);
    add_option<checkbox_t>    ("More Damage",             A.MoreDamage);
    add_option<checkbox_t>    ("Fire Delay",              A.FireDelay);
    add_option<checkbox_t>    ("Soco Longe",              A.SocoLonge);
    add_option<checkbox_t>    ("No Reload",               A.NoReload);
    add_option<checkbox_t>    ("Infinite Ammo",           A.InfiniteAmmo);
    add_option<checkbox_t>    ("No Recoil",               A.NoRecoil);
    // Speed Hack
    add_option<checkbox_t>    ("Speed Hack",              A.SpeedHack);
    add_option<slider_float_t>("Speed Scale",             A.SpeedScale,        1.0f, 10.0f);
    add_option<checkbox_t>    ("Maxim Skill",             A.MaximSkill);
    add_option<checkbox_t>    ("Aimbot Scope",            A.AimbotScope);

    // ===== CONFIG =====
    add_option<dropdown_t>    ("Overlay Type",            O.General.OverlayBackend, string_t{ "Normal", "Nvidia", "Discord" });
    add_option<checkbox_t>    ("StreamMode##cfg",         functions.exploits.streamMode, true);
    // Website local — porta do painel web. cfg-only (sem campo no backend),
    // mesmo padrao dos dropdowns de monitor: a GUI le via fill direto.
    add_option<slider_int_t>  ("Website Port",            1010, 1, 65535, "%d");

    // Accent color — same slot the XISFPS main uses; defaults to the red 95,0,0.
    add_option<color_edit_t>  ("Menu Accent Color##color", col_t{ 95.f/255.f, 0.f, 0.f, 1.f }, false);

    // ===== MONITOR RENDER =====
    // Um dropdown por projector. Items são preenchidos com "Monitor 0" como
    // placeholder no init — a GUI repopula via monitors::list_names() toda vez
    // que a tab Config é renderizada, então o dropdown sempre reflete os
    // monitores realmente conectados no momento.
    // Índice 0 = auto-follow (padrão). Índice > 0 = monitor fixo escolhido.
    add_option<dropdown_t>    ("Cheat Monitor",           0, string_t{ "Monitor 0" });
    add_option<dropdown_t>    ("Esp Monitor",             0, string_t{ "Monitor 0" });
}

// ---------------------------------------------------------------------------
// Sync layer — called by gui.cpp once per frame, before and after render.
// ---------------------------------------------------------------------------
static bool s_cfg_from_globals_needed = true;

// Forward decls — o corpo das duas funções vive lá embaixo (junto com
// process_checkbox_keybind), mas o set_bind/get_bind aqui em cima precisa
// enxergar elas para converter VK <-> ImGuiKey.
static int imgui_key_to_vk(int k);
static int vk_to_imgui_key(int vk);

namespace xisfps_ff_sync
{
    // ------------------------------------------------------------------
    // Pointer cache — resolved once, right after init_config() registers
    // every option in the map. Rationale: the mirror layer used to do
    // ~65 cfg->fill<T>(const std::string&) lookups per frame. Each call
    // built a temporary std::string from a literal (heap alloc past MSVC
    // SSO=15 chars) and walked std::map's red-black tree by string
    // compare. std::map guarantees pointer stability across further
    // inserts, options are never erased, and init_config() self-guards
    // against double-init — so caching T* here is safe. After resolve
    // runs the mirror step is just field copies.
    // ------------------------------------------------------------------
    struct cfg_ptr_cache
    {
        // Checkboxes
        checkbox_t* enable_aim               = nullptr;
        checkbox_t* left_shoulder            = nullptr;
        checkbox_t* right_shoulder           = nullptr;
        checkbox_t* precision                = nullptr;
        checkbox_t* aimbot_memory            = nullptr;
        checkbox_t* show_fov_mem             = nullptr;
        checkbox_t* silent_aim               = nullptr;
        checkbox_t* visible_check_silent     = nullptr;
        checkbox_t* show_fov_silent          = nullptr;
        checkbox_t* spinbot                  = nullptr;
        checkbox_t* aimlock                  = nullptr;
        checkbox_t* ghostmode                = nullptr;
        checkbox_t* enable_box               = nullptr;
        checkbox_t* enable_skeleton          = nullptr;
        checkbox_t* enable_line              = nullptr;
        checkbox_t* enable_health            = nullptr;
        checkbox_t* enable_distance          = nullptr;
        checkbox_t* enable_name              = nullptr;
        checkbox_t* enable_weapon            = nullptr;
        checkbox_t* visible_check            = nullptr;
        checkbox_t* team_check               = nullptr;
        checkbox_t* stream_mode              = nullptr;
        checkbox_t* alok_buff                = nullptr;
        checkbox_t* infinite_buff            = nullptr;
        checkbox_t* pixel_bug                = nullptr;
        checkbox_t* fast_medkit              = nullptr;
        checkbox_t* more_damage              = nullptr;
        checkbox_t* fire_delay               = nullptr;
        checkbox_t* soco_longe               = nullptr;
        checkbox_t* no_reload                = nullptr;
        checkbox_t* infinite_ammo            = nullptr;
        checkbox_t* no_recoil                = nullptr;
        checkbox_t* speed_hack               = nullptr;
        checkbox_t*     maxim_skill          = nullptr;
        checkbox_t*     aimbot_scope         = nullptr;
        checkbox_t* stream_mode_cfg          = nullptr;

        // Slider int
        slider_int_t* aim_delay              = nullptr;
        slider_int_t* spin_speed             = nullptr;
        slider_int_t* esp_render             = nullptr;

        // Slider float
        slider_float_t* memory_fov           = nullptr;
        slider_float_t* silent_fov           = nullptr;
        slider_float_t* speed_scale          = nullptr;
        slider_float_t* name_size            = nullptr;
        slider_float_t* distance_size        = nullptr;
        slider_float_t* weapon_size          = nullptr;
        slider_float_t* box_thickness        = nullptr;
        slider_float_t* line_thickness       = nullptr;
        slider_float_t* skeleton_thickness   = nullptr;

        // Dropdown
        dropdown_t* silent_target            = nullptr;
        dropdown_t* box_style                = nullptr;
        dropdown_t* health_position          = nullptr;
        dropdown_t* distance_position        = nullptr;
        dropdown_t* line_position            = nullptr;
        dropdown_t* name_position            = nullptr;
        dropdown_t* name_style               = nullptr;
        dropdown_t* weapon_position          = nullptr;
        dropdown_t* weapon_style             = nullptr;
        dropdown_t* alok_level               = nullptr;
        dropdown_t* atributo_arma            = nullptr;
        dropdown_t* overlay_type             = nullptr;
        dropdown_t* cheat_monitor            = nullptr;
        dropdown_t* esp_monitor              = nullptr;

        // Color
        color_edit_t* box_color              = nullptr;
        color_edit_t* line_color             = nullptr;
        color_edit_t* skeleton_color         = nullptr;
        color_edit_t* distance_color         = nullptr;
        color_edit_t* name_color             = nullptr;
        color_edit_t* weapon_color           = nullptr;
        color_edit_t* visible_check_color    = nullptr;
    };

    static cfg_ptr_cache C;

    static void resolve_ptrs()
    {
        static bool s_resolved = false;
        if (std::exchange(s_resolved, true)) return;

        // CRÍTICO: init_config precisa rodar ANTES de qualquer cfg->fill<T> não-
        // checkbox. Motivo: cfg->fill<T>(name) faz options[name] que CRIA entrada
        // default (std::variant default constrói o PRIMEIRO tipo — checkbox_t).
        // Sem init_config populando o map primeiro, get_if<slider_int_t>,
        // get_if<dropdown_t>, get_if<color_edit_t>, get_if<slider_float_t>
        // retornam NULLPTR porque a variant segura checkbox_t. O cache abaixo
        // fica cheio de nullptrs pra tudo que não é checkbox, e como o cache é
        // resolvido UMA VEZ (s_resolved guard), toda edição de dropdown/slider/
        // color_edit é silenciosamente descartada em cfg_to_globals — os widgets
        // gravam corretamente em cfg (usam cfg->fill fresh a cada frame), mas
        // nunca chegam ao g_Options. Manifestação: Atributo de Arma, cores do
        // ESP, tamanhos, posições, silent FOV, aim delay, etc — TUDO que não
        // é checkbox parava de funcionar. init_config é idempotente (guard
        // interno `static bool init`), então chamá-lo aqui + de novo em
        // c_gui::initialize() é seguro.
        cfg->init_config();

        // Checkboxes
        C.enable_aim               = cfg->fill<checkbox_t>("Enable Aim");
        C.left_shoulder            = cfg->fill<checkbox_t>("Left Shoulder");
        C.right_shoulder           = cfg->fill<checkbox_t>("Right Shoulder");
        C.precision                = cfg->fill<checkbox_t>("Precision");
        C.aimbot_memory            = cfg->fill<checkbox_t>("Aimbot Memory");
        C.show_fov_mem             = cfg->fill<checkbox_t>("Show FOV##mem");
        C.silent_aim               = cfg->fill<checkbox_t>("Silent Aim");
        C.visible_check_silent     = cfg->fill<checkbox_t>("Visible Check##silent");
        C.show_fov_silent          = cfg->fill<checkbox_t>("Show FOV##silent");
        C.spinbot                  = cfg->fill<checkbox_t>("SpinBot");
        C.aimlock                  = cfg->fill<checkbox_t>("AimLock");
        C.ghostmode                = cfg->fill<checkbox_t>("GhostMode");
        C.enable_box               = cfg->fill<checkbox_t>("Enable Box");
        C.enable_skeleton          = cfg->fill<checkbox_t>("Enable Skeleton");
        C.enable_line              = cfg->fill<checkbox_t>("Enable Line");
        C.enable_health            = cfg->fill<checkbox_t>("Enable Health");
        C.enable_distance          = cfg->fill<checkbox_t>("Enable Distance");
        C.enable_name              = cfg->fill<checkbox_t>("Enable Name");
        C.enable_weapon            = cfg->fill<checkbox_t>("Enable Weapon");
        C.visible_check            = cfg->fill<checkbox_t>("Visible Check");
        C.team_check               = cfg->fill<checkbox_t>("TeamCheck");
        C.stream_mode              = cfg->fill<checkbox_t>("Stream Mode");
        C.alok_buff                = cfg->fill<checkbox_t>("Alok Buff");
        C.infinite_buff            = cfg->fill<checkbox_t>("Infinite Buff");
        C.pixel_bug                = cfg->fill<checkbox_t>("Pixel Bug");
        C.fast_medkit              = cfg->fill<checkbox_t>("Fast Medkit");
        C.more_damage              = cfg->fill<checkbox_t>("More Damage");
        C.fire_delay               = cfg->fill<checkbox_t>("Fire Delay");
        C.soco_longe               = cfg->fill<checkbox_t>("Soco Longe");
        C.no_reload                = cfg->fill<checkbox_t>("No Reload");
        C.infinite_ammo            = cfg->fill<checkbox_t>("Infinite Ammo");
        C.no_recoil                = cfg->fill<checkbox_t>("No Recoil");
        C.speed_hack               = cfg->fill<checkbox_t>("Speed Hack");
        C.maxim_skill              = cfg->fill<checkbox_t>("Maxim Skill");
        C.aimbot_scope             = cfg->fill<checkbox_t>("Aimbot Scope");
        C.stream_mode_cfg          = cfg->fill<checkbox_t>("StreamMode##cfg");

        // Slider int
        C.aim_delay                = cfg->fill<slider_int_t>("Aim Delay");
        C.spin_speed               = cfg->fill<slider_int_t>("Spin Speed");
        C.esp_render               = cfg->fill<slider_int_t>("Esp Render");

        // Slider float
        C.memory_fov               = cfg->fill<slider_float_t>("Memory FOV");
        C.silent_fov               = cfg->fill<slider_float_t>("Silent FOV");
        C.speed_scale              = cfg->fill<slider_float_t>("Speed Scale");
        C.name_size                = cfg->fill<slider_float_t>("Name Size");
        C.distance_size            = cfg->fill<slider_float_t>("Distance Size");
        C.weapon_size              = cfg->fill<slider_float_t>("Weapon Size");
        C.box_thickness            = cfg->fill<slider_float_t>("Box Thickness");
        C.line_thickness           = cfg->fill<slider_float_t>("Line Thickness");
        C.skeleton_thickness       = cfg->fill<slider_float_t>("Skeleton Thickness");

        // Dropdown
        C.silent_target            = cfg->fill<dropdown_t>("Silent Target");
        C.box_style                = cfg->fill<dropdown_t>("Box Style");
        C.health_position          = cfg->fill<dropdown_t>("Health Position");
        C.distance_position        = cfg->fill<dropdown_t>("Distance Position");
        C.line_position            = cfg->fill<dropdown_t>("Line Position");
        C.name_position            = cfg->fill<dropdown_t>("Name Position");
        C.name_style               = cfg->fill<dropdown_t>("Name Style");
        C.weapon_position          = cfg->fill<dropdown_t>("Weapon Position");
        C.weapon_style             = cfg->fill<dropdown_t>("Weapon Style");
        C.alok_level               = cfg->fill<dropdown_t>("Alok Level");
        C.atributo_arma            = cfg->fill<dropdown_t>("Atributo de Arma");
        C.overlay_type             = cfg->fill<dropdown_t>("Overlay Type");
        C.cheat_monitor            = cfg->fill<dropdown_t>("Cheat Monitor");
        C.esp_monitor              = cfg->fill<dropdown_t>("Esp Monitor");

        // Color
        C.box_color                = cfg->fill<color_edit_t>("Box Color");
        C.line_color               = cfg->fill<color_edit_t>("Line Color");
        C.skeleton_color           = cfg->fill<color_edit_t>("Skeleton Color");
        C.distance_color           = cfg->fill<color_edit_t>("Distance Color");
        C.name_color               = cfg->fill<color_edit_t>("Name Color");
        C.weapon_color             = cfg->fill<color_edit_t>("Weapon Color");
        C.visible_check_color      = cfg->fill<color_edit_t>("VisibleCheck Color");
    }

    // Local helpers — same semantics as the old set_/get_ lambdas but
    // driven off a cached pointer instead of a per-frame map probe.
    static inline void set_cb(checkbox_t* c, bool v)          { if (c) c->callback = v; }
    // Backend guarda VK puro; a UI espera ImGuiKey. Converte na entrada
    // pra que o get_key_name (indexa em ImGuiKey_NamedKey_BEGIN) renderize
    // "F5", "Mouse 1", etc — antes mostrava "..." porque VK=1 (LBUTTON)
    // fica fora do range da lookup table de ImGuiKey.
    static inline void set_bind(checkbox_t* c, int v)         { if (c) c->key_data.key = vk_to_imgui_key(v); }
    static inline void set_si(slider_int_t* c, int v)         { if (c) c->callback = v; }
    static inline void set_sf(slider_float_t* c, float v)     { if (c) c->callback = v; }
    static inline void set_dd(dropdown_t* c, int v)           { if (c) c->callback = v; }
    static inline void set_cl(color_edit_t* c, const float col[4]) {
        if (c) { c->color[0]=col[0]; c->color[1]=col[1]; c->color[2]=col[2]; c->color[3]=col[3]; }
    }

    static inline void get_cb(const checkbox_t* c, bool& dst)       { if (c) dst = c->callback; }
    // ImGuiKey -> VK na saída. Sem isso, A.*Bind = 512+ e GetAsyncKeyState
    // retorna 0 pra tudo — o bug raiz do "meu bind não funciona".
    //
    // MODO Toggle (mode==0): backend NÃO deve exigir tecla segurada. Como
    // o backend (AimModules.cpp) sempre faz `if (bind != 0 && !holding) skip`,
    // se deixarmos o VK ir pra lá, mesmo com a checkbox toggle=ON o feature
    // não dispararia (usuário não está segurando). Solução: pra Toggle,
    // empurra bind=0 pro backend (skipa a hold-check), e o process_keybinds
    // aqui na UI já cuidou de flipar callback→A.SilentAim etc. A tecla
    // real fica preservada no cfg pro widget mostrar/gravar corretamente.
    //
    // MODO Hold (mode==1): empurra o VK real. Backend gates em hold como sempre.
    static inline void get_bind(const checkbox_t* c, int& dst) {
        if (c) dst = (c->key_data.mode == 0) ? 0 : imgui_key_to_vk(c->key_data.key);
    }
    static inline void get_si(const slider_int_t* c, int& dst)      { if (c) dst = c->callback; }
    static inline void get_sf(const slider_float_t* c, float& dst)  { if (c) dst = c->callback; }
    static inline void get_dd(const dropdown_t* c, int& dst)        { if (c) dst = c->callback; }
    static inline void get_cl(const color_edit_t* c, float dst[4])  {
        if (c) { dst[0]=c->color[0]; dst[1]=c->color[1]; dst[2]=c->color[2]; dst[3]=c->color[3]; }
    }

    // Push FreeFire globals into the cfg store (usually called only on the
    // first frame — subsequent frames trust cfg as the source of truth).
    void globals_to_cfg()
    {
        // Populate the pointer cache once, before the seed step runs. Safe
        // to call every frame — self-guards with std::exchange.
        resolve_ptrs();

        auto& O = g_Options; auto& A = O.AimDMA; auto& P = O.Visuals.ESP.Players;

        // Enable Aim gates o thread do LegitAim em AimModules.cpp:944
        // (`if (!g_Options.AimDMA.LegitAimEnable) skip`). Antes estava
        // sync'do em functions.aim.enabled (legacy, não usado pelo AimModules
        // atual) — por isso a checkbox não ativava o aimbot. Espelhamos os
        // dois pra manter o legacy consistente.
        set_cb(C.enable_aim, A.LegitAimEnable);
        set_cb(C.left_shoulder, A.LegitAimLeftShoulder);
        set_cb(C.right_shoulder, A.LegitAimRightShoulder);
        set_si(C.aim_delay, A.LegitAimDelay);
        set_cb(C.precision, A.Precision);
        set_cb(C.aimbot_memory, A.AimBotMemory);
        set_cb(C.show_fov_mem, A.AimBotMemoryDrawFov);
        set_sf(C.memory_fov, A.AimBotMemoryFov);
        set_cb(C.silent_aim, A.SilentAim);
        set_cb(C.visible_check_silent, A.SilentVisibleCheck);
        set_cb(C.show_fov_silent, A.SilentDrawFov);
        set_dd(C.silent_target, A.SilentAimTarget);
        set_sf(C.silent_fov, A.SilentFov);
        set_cb(C.spinbot, A.SpinBot);
        set_si(C.spin_speed, A.SpinBotSpeed);
        set_cb(C.aimlock, A.Aimlock);
        set_cb(C.ghostmode, A.GhostMode);

        set_cb(C.enable_box, P.ESPBox);
        set_cb(C.enable_skeleton, P.ESPSkeleton);
        set_cb(C.enable_line, P.ESPLine);
        set_cb(C.enable_health, P.ESPHealth);
        set_cb(C.enable_distance, P.ESPDistance);
        set_cb(C.enable_name, P.ESPName);
        set_cb(C.enable_weapon, P.ESPWeapon);
        set_cb(C.visible_check, P.UseVisibilityColor);
        set_cb(C.team_check, P.TeamCheck);
        set_cb(C.stream_mode, O.General.StreamMode);
        set_dd(C.box_style, P.ESPBoxStyle);
        set_dd(C.health_position, P.ESPHealthPos);
        set_dd(C.distance_position, P.ESPDistancePos);
        set_dd(C.line_position, P.ESPLinePos);
        set_dd(C.name_position, P.ESPNamePos);
        set_dd(C.name_style, P.ESPNameStyle);
        set_dd(C.weapon_position, P.ESPWeaponPos);
        set_dd(C.weapon_style, P.ESPWeaponStyle);
        set_si(C.esp_render, P.EspRenderDistance);

        set_cl(C.box_color,      P.BoxColor);
        set_cl(C.line_color,     P.LineColor);
        set_cl(C.skeleton_color, P.SkeletonColor);
        set_cl(C.distance_color, P.DistanceColor);
        set_cl(C.name_color,     P.NameColor);
        set_cl(C.weapon_color,   P.WeaponColor);
        set_cl(C.visible_check_color, P.HiddenColor);
        set_sf(C.name_size,          P.NameSize);
        set_sf(C.distance_size,      P.DistanceSize);
        set_sf(C.weapon_size,        P.WeaponSize);
        set_sf(C.box_thickness,      P.BoxThickness);
        set_sf(C.line_thickness,     P.LineThickness);
        set_sf(C.skeleton_thickness, P.SkeletonThickness);

        set_cb(C.alok_buff,     A.AlokBuff);
        set_cb(C.infinite_buff, A.AlokBuffInfinite);
        set_dd(C.alok_level,    A.AlokBuffLevel);
        set_dd(C.atributo_arma, A.AtributarArmaSpeed);
        set_cb(C.pixel_bug,   A.BugarPixel);
        set_cb(C.fast_medkit, A.FastMedkit);
        set_cb(C.more_damage, A.MoreDamage);
        set_cb(C.fire_delay,  A.FireDelay);
        set_cb(C.soco_longe,  A.SocoLonge);
        set_cb(C.no_reload,   A.NoReload);
        set_cb(C.no_recoil,   A.NoRecoil);
        set_cb(C.speed_hack,  A.SpeedHack);
        set_sf(C.speed_scale, A.SpeedScale);
        set_cb(C.infinite_ammo,   A.InfiniteAmmo);
        set_cb(C.maxim_skill,     A.MaximSkill);
        set_cb(C.aimbot_scope,    A.AimbotScope);

        set_dd(C.overlay_type, O.General.OverlayBackend);
        set_cb(C.stream_mode_cfg, functions.exploits.streamMode);

        // XISFPS port: seed the checkbox key_data.key from each FreeFire bind
        // global. Once seeded, the gear popup on each checkbox shows the
        // current key and any edit propagates back via cfg_to_globals below.
        set_bind(C.enable_aim,    A.LegitAimKey);
        set_bind(C.aimbot_memory, A.AimBotMemoryBind);
        set_bind(C.silent_aim,    A.SilentAimBind);
        set_bind(C.spinbot,       A.SpinBotBind);
        set_bind(C.aimlock,       A.AimlockBind);
        set_bind(C.ghostmode,     A.GhostModeBind);
    }

    // Push cfg values back into FreeFire globals — called every frame AFTER
    // the widgets ran, so any user edit lands in the backend before the next
    // hook loop reads.
    void cfg_to_globals()
    {
        // Defensive — globals_to_cfg() normally seeds this on frame 0, but
        // if a caller ever reverses the order the cache still populates.
        resolve_ptrs();

        auto& O = g_Options; auto& A = O.AimDMA; auto& P = O.Visuals.ESP.Players;

        // Enable Aim: escreve tanto pro A.LegitAimEnable (o que backend lê)
        // quanto pro functions.aim.enabled (legacy, mantido em sync).
        get_cb(C.enable_aim, A.LegitAimEnable);
        if (C.enable_aim) functions.aim.enabled = C.enable_aim->callback;
        get_cb(C.left_shoulder, A.LegitAimLeftShoulder);
        get_cb(C.right_shoulder, A.LegitAimRightShoulder);
        get_si(C.aim_delay, A.LegitAimDelay);
        get_cb(C.precision, A.Precision);
        get_cb(C.aimbot_memory, A.AimBotMemory);
        get_cb(C.show_fov_mem, A.AimBotMemoryDrawFov);
        get_sf(C.memory_fov, A.AimBotMemoryFov);
        get_cb(C.silent_aim, A.SilentAim);
        get_cb(C.visible_check_silent, A.SilentVisibleCheck);
        get_cb(C.show_fov_silent, A.SilentDrawFov);
        get_dd(C.silent_target, A.SilentAimTarget);
        get_sf(C.silent_fov, A.SilentFov);
        get_cb(C.spinbot, A.SpinBot);
        get_si(C.spin_speed, A.SpinBotSpeed);
        get_cb(C.aimlock, A.Aimlock);
        get_cb(C.ghostmode, A.GhostMode);

        get_cb(C.enable_box, P.ESPBox);
        get_cb(C.enable_skeleton, P.ESPSkeleton);
        get_cb(C.enable_line, P.ESPLine);
        get_cb(C.enable_health, P.ESPHealth);
        get_cb(C.enable_distance, P.ESPDistance);
        get_cb(C.enable_name, P.ESPName);
        get_cb(C.enable_weapon, P.ESPWeapon);
        get_cb(C.visible_check, P.UseVisibilityColor);
        get_cb(C.team_check, P.TeamCheck);
        get_cb(C.stream_mode, O.General.StreamMode);
        get_dd(C.box_style, P.ESPBoxStyle);
        get_dd(C.health_position, P.ESPHealthPos);
        get_dd(C.distance_position, P.ESPDistancePos);
        get_dd(C.line_position, P.ESPLinePos);
        get_dd(C.name_position, P.ESPNamePos);
        get_dd(C.name_style, P.ESPNameStyle);
        get_dd(C.weapon_position, P.ESPWeaponPos);
        get_dd(C.weapon_style, P.ESPWeaponStyle);
        get_si(C.esp_render, P.EspRenderDistance);

        get_cl(C.box_color,      P.BoxColor);
        get_cl(C.line_color,     P.LineColor);
        get_cl(C.skeleton_color, P.SkeletonColor);
        get_cl(C.distance_color, P.DistanceColor);
        get_cl(C.name_color,     P.NameColor);
        get_cl(C.weapon_color,   P.WeaponColor);
        get_cl(C.visible_check_color, P.HiddenColor);
        get_sf(C.name_size,          P.NameSize);
        get_sf(C.distance_size,      P.DistanceSize);
        get_sf(C.weapon_size,        P.WeaponSize);
        get_sf(C.box_thickness,      P.BoxThickness);
        get_sf(C.line_thickness,     P.LineThickness);
        get_sf(C.skeleton_thickness, P.SkeletonThickness);

        get_cb(C.alok_buff,     A.AlokBuff);
        get_cb(C.infinite_buff, A.AlokBuffInfinite);
        get_dd(C.alok_level,    A.AlokBuffLevel);
        get_dd(C.atributo_arma, A.AtributarArmaSpeed);
        get_cb(C.pixel_bug,   A.BugarPixel);
        get_cb(C.fast_medkit, A.FastMedkit);
        get_cb(C.more_damage, A.MoreDamage);
        get_cb(C.fire_delay,  A.FireDelay);
        get_cb(C.soco_longe,  A.SocoLonge);
        get_cb(C.no_reload,   A.NoReload);
        get_cb(C.no_recoil,   A.NoRecoil);
        get_cb(C.speed_hack,  A.SpeedHack);
        get_sf(C.speed_scale, A.SpeedScale);
        get_cb(C.infinite_ammo,    A.InfiniteAmmo);
        get_cb(C.maxim_skill,      A.MaximSkill);
        get_cb(C.aimbot_scope,     A.AimbotScope);
        get_dd(C.overlay_type, O.General.OverlayBackend);
        get_cb(C.stream_mode_cfg, functions.exploits.streamMode);

        // Aplica StreamMode na janela do cheat (globals.hwnd).
        // Mesmo padrão do ESP — só chama SetWindowDisplayAffinity quando muda.
        {
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
            static bool s_last_cheat_stream = !functions.exploits.streamMode;
            if ( functions.exploits.streamMode != s_last_cheat_stream ) {
                if ( globals.hwnd )
                    SetWindowDisplayAffinity( globals.hwnd ,
                        functions.exploits.streamMode ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE );
                s_last_cheat_stream = functions.exploits.streamMode;
            }
        }

        // Bind sync — push edits from every checkbox gear popup back into
        // the FreeFire *Bind field. Backend threads read those fields
        // directly for hotkey activation.
        get_bind(C.enable_aim,    A.LegitAimKey);
        get_bind(C.aimbot_memory, A.AimBotMemoryBind);
        get_bind(C.silent_aim,    A.SilentAimBind);
        get_bind(C.spinbot,       A.SpinBotBind);
        get_bind(C.aimlock,       A.AimlockBind);
        get_bind(C.ghostmode,     A.GhostModeBind);

        // ===== MONITOR RENDER =====
        // Os valores ficam no cfg; a GUI aplica os efeitos inline (SetWindowPos
        // para o Cheat monitor, Overlay::SetEspMonitorOrigin para o ESP).
        // Aqui apenas lemos para manter o cache em sincronia — sem side-effects
        // adicionais no sync layer.
        {
            int cheat_mon = 0; get_dd(C.cheat_monitor, cheat_mon);
            int esp_mon   = 0; get_dd(C.esp_monitor,   esp_mon);
            (void)cheat_mon; (void)esp_mon;
        }
    }
}

// ---------------------------------------------------------------------------
// ImGuiKey <-> Win32 VK translation
//
// O popup de bind (widgets/keybind.cpp:67) grava a tecla capturada como
// ImGuiKey — enum que começa em ImGuiKey_NamedKey_BEGIN (512). O backend
// (AimModules.cpp, ~10 pontos) usa GetAsyncKeyState(bind), que espera um
// Virtual-Key da Win32 (1..255). Sem esta conversão o GetAsyncKeyState
// retorna 0 silenciosamente e nenhum bind funciona.
// ---------------------------------------------------------------------------
static int imgui_key_to_vk(int k)
{
    if (k <= 0) return 0;
    // Valor legado ou seed vinda direto de A.*Key/Bind (VK puro).
    if (k < ImGuiKey_NamedKey_BEGIN) return k;

    switch ((ImGuiKey)k)
    {
        case ImGuiKey_Tab:         return VK_TAB;
        case ImGuiKey_LeftArrow:   return VK_LEFT;
        case ImGuiKey_RightArrow:  return VK_RIGHT;
        case ImGuiKey_UpArrow:     return VK_UP;
        case ImGuiKey_DownArrow:   return VK_DOWN;
        case ImGuiKey_PageUp:      return VK_PRIOR;
        case ImGuiKey_PageDown:    return VK_NEXT;
        case ImGuiKey_Home:        return VK_HOME;
        case ImGuiKey_End:         return VK_END;
        case ImGuiKey_Insert:      return VK_INSERT;
        case ImGuiKey_Delete:      return VK_DELETE;
        case ImGuiKey_Backspace:   return VK_BACK;
        case ImGuiKey_Space:       return VK_SPACE;
        case ImGuiKey_Enter:       return VK_RETURN;
        case ImGuiKey_Escape:      return VK_ESCAPE;
        case ImGuiKey_LeftCtrl:  case ImGuiKey_RightCtrl:  return VK_CONTROL;
        case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return VK_SHIFT;
        case ImGuiKey_LeftAlt:   case ImGuiKey_RightAlt:   return VK_MENU;
        case ImGuiKey_LeftSuper:   return VK_LWIN;
        case ImGuiKey_RightSuper:  return VK_RWIN;
        case ImGuiKey_CapsLock:    return VK_CAPITAL;
        case ImGuiKey_ScrollLock:  return VK_SCROLL;
        case ImGuiKey_NumLock:     return VK_NUMLOCK;
        case ImGuiKey_PrintScreen: return VK_SNAPSHOT;
        case ImGuiKey_Pause:       return VK_PAUSE;
        case ImGuiKey_MouseLeft:   return VK_LBUTTON;
        case ImGuiKey_MouseRight:  return VK_RBUTTON;
        case ImGuiKey_MouseMiddle: return VK_MBUTTON;
        case ImGuiKey_MouseX1:     return VK_XBUTTON1;
        case ImGuiKey_MouseX2:     return VK_XBUTTON2;
        default: break;
    }
    if (k >= ImGuiKey_0       && k <= ImGuiKey_9)       return '0'         + (k - ImGuiKey_0);
    if (k >= ImGuiKey_A       && k <= ImGuiKey_Z)       return 'A'         + (k - ImGuiKey_A);
    if (k >= ImGuiKey_F1      && k <= ImGuiKey_F12)     return VK_F1       + (k - ImGuiKey_F1);
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return VK_NUMPAD0  + (k - ImGuiKey_Keypad0);
    return 0;
}

static int vk_to_imgui_key(int vk)
{
    if (vk <= 0) return 0;
    if (vk >= ImGuiKey_NamedKey_BEGIN) return vk; // já é ImGuiKey

    switch (vk)
    {
        case VK_TAB:      return ImGuiKey_Tab;
        case VK_LEFT:     return ImGuiKey_LeftArrow;
        case VK_RIGHT:    return ImGuiKey_RightArrow;
        case VK_UP:       return ImGuiKey_UpArrow;
        case VK_DOWN:     return ImGuiKey_DownArrow;
        case VK_PRIOR:    return ImGuiKey_PageUp;
        case VK_NEXT:     return ImGuiKey_PageDown;
        case VK_HOME:     return ImGuiKey_Home;
        case VK_END:      return ImGuiKey_End;
        case VK_INSERT:   return ImGuiKey_Insert;
        case VK_DELETE:   return ImGuiKey_Delete;
        case VK_BACK:     return ImGuiKey_Backspace;
        case VK_SPACE:    return ImGuiKey_Space;
        case VK_RETURN:   return ImGuiKey_Enter;
        case VK_ESCAPE:   return ImGuiKey_Escape;
        case VK_CONTROL:  return ImGuiKey_LeftCtrl;
        case VK_SHIFT:    return ImGuiKey_LeftShift;
        case VK_MENU:     return ImGuiKey_LeftAlt;
        case VK_LWIN:     return ImGuiKey_LeftSuper;
        case VK_RWIN:     return ImGuiKey_RightSuper;
        case VK_CAPITAL:  return ImGuiKey_CapsLock;
        case VK_SCROLL:   return ImGuiKey_ScrollLock;
        case VK_NUMLOCK:  return ImGuiKey_NumLock;
        case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
        case VK_PAUSE:    return ImGuiKey_Pause;
        case VK_LBUTTON:  return ImGuiKey_MouseLeft;
        case VK_RBUTTON:  return ImGuiKey_MouseRight;
        case VK_MBUTTON:  return ImGuiKey_MouseMiddle;
        case VK_XBUTTON1: return ImGuiKey_MouseX1;
        case VK_XBUTTON2: return ImGuiKey_MouseX2;
        default: break;
    }
    if (vk >= '0'         && vk <= '9')         return ImGuiKey_0       + (vk - '0');
    if (vk >= 'A'         && vk <= 'Z')         return ImGuiKey_A       + (vk - 'A');
    if (vk >= VK_F1       && vk <= VK_F12)      return ImGuiKey_F1      + (vk - VK_F1);
    if (vk >= VK_NUMPAD0  && vk <= VK_NUMPAD9)  return ImGuiKey_Keypad0 + (vk - VK_NUMPAD0);
    return 0;
}

// ---------------------------------------------------------------------------
// process_checkbox_keybind — chamado por c_config::process_keybinds a cada
// frame. Usa GetAsyncKeyState (global, funciona com foco fora do menu HWND)
// no lugar de ImGui::IsKeyPressed/IsKeyDown (que dependem de eventos WM_KEY*
// que só chegam com o menu focado).
//
// Toggle (mode==0): edge-triggered — só flip no down-transition. Usamos
// widget.is_active como debounce "was-down-last-frame" já que GetAsyncKeyState
// é level-triggered.
// Hold (mode==1): level-triggered — is_active espelha o hold.
// ---------------------------------------------------------------------------
void process_checkbox_keybind(checkbox_t& widget)
{
    if (!widget.keybind || widget.key_data.key == 0) return;

    const int vk = imgui_key_to_vk(widget.key_data.key);
    if (vk == 0) return;

    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool is_toggle_mode = widget.key_data.mode == 0;

    if (is_toggle_mode)
    {
        if (down && !widget.is_active)
        {
            widget.stored_value = widget.callback;
            widget.callback     = !widget.callback;
            widget.is_active    = true;
        }
        else if (!down && widget.is_active)
        {
            widget.is_active = false;
        }
    }
    else
    {
        widget.is_active = down;
    }
}

void c_config::process_keybinds()
{
    for (auto& [name, variant] : options)
    {
        std::visit([this](auto&& widget) {
            using T = std::decay_t<decltype(widget)>;
            if constexpr (has_keybind<T>::value) if (widget.keybind) process_checkbox_keybind(widget);
        }, variant);
    }
    for (const auto& a : action_keybinds)
    {
        if (!a.fire) continue;
        auto* c = cfg->fill<checkbox_t>(a.config_key);
        if (!c || c->key_data.key == 0) continue;
        if (ImGui::IsKeyPressed((ImGuiKey)c->key_data.key, false)) a.fire();
    }
}
