#include "Draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Windows.h>
#include "../Cheat.hpp"
#include "../Math/Unity/Transform/Transform.hpp"
#include "../Memory/Memory.hpp"
#include "../Math/Vectors/Vector3.hpp"
#include "../Options.hpp"
#include "../RayCast/RayCast.hpp"
#include "../AimModules/AimModules.hpp"
#include "../SharedFrame.hpp"
#include "Weapon/NameGun.hpp"
#include "Fonts/EspFont.hpp"
#include "../../FrameWork/Dependencies/ImGui/imgui.h"
#include <string>

// Wrap ImGui::CalcTextSize with the ESP font at a specific size so measurements
// match what the draw list actually renders (avoids off-by-a-few-pixel text
// alignment when the user has customized per-element sizes).
static ImVec2 EspCalcTextSize(float sizePx, const char* text) {
    ImFont* f = EspFont::Verdana ? EspFont::Verdana : ImGui::GetFont();
    return f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, text);
}

// AddText with the ESP font at a specific pixel size. Falls back to default if
// the atlas isn't ready yet.
static void EspDrawText(ImDrawList* DL, float sizePx, ImVec2 pos, ImU32 col, const char* text) {
    ImFont* f = EspFont::Verdana ? EspFont::Verdana : ImGui::GetFont();
    DL->AddText(f, sizePx, pos, col, text);
}

// -----------------------------------------------------------------------------
// Text-measurement caches (perf).
// EspCalcTextSize walks the glyph table for every character on every call, and
// the ESP loop was invoking it up to 3x per entity per frame (~150 walks in a
// full lobby) on strings that almost never change: integer meter distance,
// weapon label bound to (weaponId, style), and per-entity name. These caches
// memoize the ImVec2 sizes and are invalidated automatically when the ESP
// font pointer or pixel size changes (e.g. atlas rebuild or a size slider
// drag), so measurement stays pixel-identical to the uncached path.
// -----------------------------------------------------------------------------
namespace {
    inline ImFont* EspFontPtr() {
        return EspFont::Verdana ? EspFont::Verdana : ImGui::GetFont();
    }

    // (A) Distance: string is always "%dM" -- cache one width per integer meter
    //     for the currently-active DistanceSize.
    struct DistCache {
        ImFont* font;
        float   sizePx;
        float   height;
        float   width[1024]; // 0 == "not yet measured"
    };
    static DistCache g_DistCache = {};

    static ImVec2 CalcDistSize(float sizePx, int meters, const char* fallback) {
        ImFont* f = EspFontPtr();
        if (f != g_DistCache.font || sizePx != g_DistCache.sizePx) {
            g_DistCache.font   = f;
            g_DistCache.sizePx = sizePx;
            memset(g_DistCache.width, 0, sizeof(g_DistCache.width));
            ImVec2 h = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, "0M");
            g_DistCache.height = h.y;
        }
        if ((unsigned)meters >= 1024u) {
            return f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, fallback);
        }
        if (g_DistCache.width[meters] == 0.0f) {
            ImVec2 s = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, fallback);
            g_DistCache.width[meters] = s.x;
            if (s.y > g_DistCache.height) g_DistCache.height = s.y;
        }
        return ImVec2(g_DistCache.width[meters], g_DistCache.height);
    }

    // (B) Weapon: keyed by (weaponId<<8)|style. Open-addressed, linear probe.
    struct WeaponSlot { uint32_t key; ImVec2 sz; bool used; };
    static constexpr int kWeaponSlots = 512;
    struct WeaponCache {
        ImFont*    font;
        float      sizePx;
        WeaponSlot slots[kWeaponSlots];
    };
    static WeaponCache g_WeaponCache = {};

    static ImVec2 CalcWeaponSize(float sizePx, uint16_t weaponId, uint8_t style, const char* label) {
        ImFont* f = EspFontPtr();
        if (f != g_WeaponCache.font || sizePx != g_WeaponCache.sizePx) {
            g_WeaponCache.font   = f;
            g_WeaponCache.sizePx = sizePx;
            for (int i = 0; i < kWeaponSlots; ++i) g_WeaponCache.slots[i].used = false;
        }
        const uint32_t key = ((uint32_t)weaponId << 8) | (uint32_t)style;
        uint32_t idx = (key * 2654435761u) & (kWeaponSlots - 1);
        for (int probe = 0; probe < 8; ++probe) {
            WeaponSlot& s = g_WeaponCache.slots[(idx + probe) & (kWeaponSlots - 1)];
            if (!s.used) {
                s.key  = key;
                s.sz   = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, label);
                s.used = true;
                return s.sz;
            }
            if (s.key == key) return s.sz;
        }
        return f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, label);
    }

    // (C) Name: keyed by entity id; per-slot name hash catches HP-suffix and
    //     BOT-prefix mutations because those are already baked into nameBuf
    //     before this call.
    struct NameSlot { uint32_t entity; uint32_t hash; ImVec2 sz; bool used; };
    static constexpr int kNameSlots = 256;
    struct NameCache {
        ImFont*  font;
        float    sizePx;
        NameSlot slots[kNameSlots];
    };
    static NameCache g_NameCache = {};

    static uint32_t Fnv1a(const char* data, int len) {
        uint32_t h = 2166136261u;
        for (int i = 0; i < len; ++i) {
            h ^= (uint8_t)data[i];
            h *= 16777619u;
        }
        return h;
    }

    static ImVec2 CalcNameSize(float sizePx, uint32_t entity, const char* name, int nameLen) {
        ImFont* f = EspFontPtr();
        if (f != g_NameCache.font || sizePx != g_NameCache.sizePx) {
            g_NameCache.font   = f;
            g_NameCache.sizePx = sizePx;
            for (int i = 0; i < kNameSlots; ++i) g_NameCache.slots[i].used = false;
        }
        const uint32_t h   = Fnv1a(name, nameLen);
        uint32_t idx = (entity * 2654435761u) & (kNameSlots - 1);
        for (int probe = 0; probe < 4; ++probe) {
            NameSlot& s = g_NameCache.slots[(idx + probe) & (kNameSlots - 1)];
            if (!s.used) {
                s.entity = entity;
                s.hash   = h;
                s.sz     = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, name);
                s.used   = true;
                return s.sz;
            }
            if (s.entity == entity) {
                if (s.hash != h) {
                    s.hash = h;
                    s.sz   = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, name);
                }
                return s.sz;
            }
        }
        return f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, name);
    }
}

// Convert an RGBA [0..1] color array into ImGui's packed ImU32 (ABGR).
static ImU32 EspColU32(const float c[4]) {
    return IM_COL32(
        (int)(c[0] * 255.0f + 0.5f),
        (int)(c[1] * 255.0f + 0.5f),
        (int)(c[2] * 255.0f + 0.5f),
        (int)(c[3] * 255.0f + 0.5f));
}

// UTF-16 (Il2CppString data) -> UTF-8 directly into a caller-provided char
// buffer. Handles ASCII directly and encodes non-ASCII code points as 2/3-byte
// UTF-8 sequences so ImGui renders them without falling through the string.
// Skips control chars, DEL, and surrogate halves (0xD800..0xDFFF) because
// encoding those as 3-byte UTF-8 produces sequences that ImGui rejects as
// invalid — safer to drop. Truncates at a codepoint boundary (never emits a
// partial sequence) if the buffer is too small. Returns bytes written
// excluding the trailing NUL. Zero std::string allocations on the hot path.
static int Utf16ToUtf8Buf(const uint16_t* src, int len, char* out, int outSize) {
    if (outSize <= 0) return 0;
    if (len <= 0) { out[0] = '\0'; return 0; }
    int w = 0;
    const int cap = outSize - 1; // leave room for NUL
    for (int i = 0; i < len; ++i) {
        uint16_t ch = src[i];
        if (ch < 0x80) {
            if (ch >= 0x20 && ch != 0x7F) { // printable ASCII only
                if (w + 1 > cap) break;
                out[w++] = (char)ch;
            }
        } else if (ch >= 0xD800 && ch <= 0xDFFF) {
            // Surrogate half — skip. Emoji / astral chars in nicknames are
            // rare in FF; dropping the surrogate avoids invalid UTF-8.
            continue;
        } else if (ch < 0x800) {
            if (w + 2 > cap) break;
            out[w++] = (char)(0xC0 | (ch >> 6));
            out[w++] = (char)(0x80 | (ch & 0x3F));
        } else {
            if (w + 3 > cap) break;
            out[w++] = (char)(0xE0 | (ch >> 12));
            out[w++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            out[w++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    out[w] = '\0';
    return w;
}

// Reads an Il2CppString: length at ptr+0x8, UTF-16 data at ptr+0xC (32-bit game).
// Writes UTF-8 directly into the caller-supplied buffer. Returns bytes written
// (0 on any failure or absurd length). Buffer is always NUL-terminated when
// outSize > 0.
static int ReadIl2CppStringBuf(uint32_t stringPtr, char* out, int outSize) {
    if (outSize <= 0) return 0;
    out[0] = '\0';
    if (stringPtr < 0x10000u || stringPtr == 0xFFFFFFFFu) return 0;
    int len = g_Memory->Read<int>(stringPtr + 0x8);
    if (len <= 0 || len > 64) return 0;
    uint16_t buf[64] = {};
    if (!g_Memory->ReadBytes(stringPtr + 0xC, buf, (size_t)len * 2)) return 0;
    return Utf16ToUtf8Buf(buf, len, out, outSize);
}

// Try Entity.EntityName first (direct on Player), fall back to
// PlayerNetwork.m_Profile -> BaseProfileInfo.NickName. Writes directly into
// outBuf; returns bytes written (0 on failure / empty). Zero heap allocs.
static int ReadPlayerNameBuf(uint32_t entity, char* outBuf, int outSize) {
    if (outSize <= 0) return 0;
    uint32_t namePtr = g_Memory->Read<uint32_t>(entity + Cheat::g_DrawOff.ENTITYNAME);
    int n = ReadIl2CppStringBuf(namePtr, outBuf, outSize);
    if (n > 0) return n;

    uint32_t profile = g_Memory->Read<uint32_t>(entity + Cheat::g_DrawOff.PROFILEPTR);
    if (profile >= 0x10000u && profile != 0xFFFFFFFFu) {
        uint32_t nick = g_Memory->Read<uint32_t>(profile + Cheat::g_DrawOff.NICKNAME);
        return ReadIl2CppStringBuf(nick, outBuf, outSize);
    }
    outBuf[0] = '\0';
    return 0;
}

// SEH wrapper — when ESP Name reads chase a stale/reallocated entity, the
// chain can occasionally dereference a page the emulator has just unmapped.
// Swallowing the AV here keeps the ESP frame going instead of crashing.
// Function body has NO local C++ objects — only raw types — because /EHsc
// forbids __try in functions that need object unwinding. (The whole callee
// chain is now char-buffer based, so this invariant holds trivially.)
static int SafeReadPlayerName(uint32_t entity, char* outBuf, int outSize) {
    __try {
        return ReadPlayerNameBuf(entity, outBuf, outSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (outSize > 0) outBuf[0] = '\0';
        return 0;
    }
}

// Weapon ID agora vem pré-resolvido no EntitySnapshot (Producer faz o read
// WEAPONREP+REPDATA na thread dele — elimina 1 RPM por entidade por frame no
// render thread quando ESPWeapon está on).

// ---------------------------------------------------------------------------
// Skeleton cache — only stable Transform-component pointers get cached.
// ---------------------------------------------------------------------------
namespace {
    // Skeleton chain cache — cache only Transform-component pointers (stable);
    // re-walk H1→H2→H3 every frame because that inner chain lives inside the
    // matrix pool which Unity reallocates.
    struct SkelChainCache {
        uint32_t entity;
        bool     female;
        uint32_t location[18];
        DWORD    lastMs;
        bool     valid;
    };
    static constexpr int  SKEL_MAX = 128;
    static constexpr DWORD SKEL_TTL = 3000;

    static SkelChainCache g_skelCache[SKEL_MAX] = {};
    static int g_skelCount = 0;
    static int g_skelNext  = 0;

    // Bone offsets sorted (0x10..0x54) — 18 consecutive uint32_t slots. We batch-read
    // this block, then remap per-gender to the semantic bone ordering below.
    // Male:   H=0x38 Chest=0x14 Stomach=0x10 Hip=0x48 LShoulder=0x18 LArm=0x1C LForearm=0x20 LHand=0x24
    //         RShoulder=0x28 RArm=0x2C RForearm=0x30 RHand=0x34 LThigh=0x3C LShin=0x40 LFoot=0x44
    //         RThigh=0x4C RShin=0x50 RFoot=0x54
    // Female: H=0x3C Chest=0x18 Stomach=0x14 Hip=0x10 LShoulder=0x1C LArm=0x20 LForearm=0x24 LHand=0x28
    //         RShoulder=0x2C RArm=0x30 RForearm=0x34 RHand=0x38 LThigh=0x40 LShin=0x44 LFoot=0x48
    //         RThigh=0x4C RShin=0x50 RFoot=0x54
    enum { BONE_HEAD=0, BONE_CHEST, BONE_STOMACH, BONE_HIP,
           BONE_LSHOULDER, BONE_LARM, BONE_LFOREARM, BONE_LHAND,
           BONE_RSHOULDER, BONE_RARM, BONE_RFOREARM, BONE_RHAND,
           BONE_LTHIGH, BONE_LSHIN, BONE_LFOOT,
           BONE_RTHIGH, BONE_RSHIN, BONE_RFOOT };

    // (slot in the 0x10..0x54 block for each bone semantic, offset-0x10 / 4)
    static const int kMaleSlot[18] = {
        (0x38-0x10)/4, (0x14-0x10)/4, (0x10-0x10)/4, (0x48-0x10)/4,
        (0x18-0x10)/4, (0x1C-0x10)/4, (0x20-0x10)/4, (0x24-0x10)/4,
        (0x28-0x10)/4, (0x2C-0x10)/4, (0x30-0x10)/4, (0x34-0x10)/4,
        (0x3C-0x10)/4, (0x40-0x10)/4, (0x44-0x10)/4,
        (0x4C-0x10)/4, (0x50-0x10)/4, (0x54-0x10)/4,
    };
    static const int kFemaleSlot[18] = {
        (0x3C-0x10)/4, (0x18-0x10)/4, (0x14-0x10)/4, (0x10-0x10)/4,
        (0x1C-0x10)/4, (0x20-0x10)/4, (0x24-0x10)/4, (0x28-0x10)/4,
        (0x2C-0x10)/4, (0x30-0x10)/4, (0x34-0x10)/4, (0x38-0x10)/4,
        (0x40-0x10)/4, (0x44-0x10)/4, (0x48-0x10)/4,
        (0x4C-0x10)/4, (0x50-0x10)/4, (0x54-0x10)/4,
    };

    static SkelChainCache* FindOrAllocSkel(uint32_t entity) {
        for (int i = 0; i < g_skelCount; ++i)
            if (g_skelCache[i].entity == entity) return &g_skelCache[i];
        int slot = g_skelNext++ % SKEL_MAX;
        if (g_skelCount < SKEL_MAX) ++g_skelCount;
        g_skelCache[slot] = {};
        return &g_skelCache[slot];
    }

    // Ensure Location pointers are cached for `entity`/`female`. Only cached tier is
    // the 18 Transform-component pointers (stable). The H1/H2/H3 chain lives
    // inside Unity's matrix pool and must be re-walked every frame.
    static bool LoadSkelChain(uint32_t entity, bool female, uint32_t (&outLoc)[18]) {
        DWORD now = GetTickCount();
        SkelChainCache* sc = FindOrAllocSkel(entity);

        if (sc->valid && sc->entity == entity && sc->female == female && now - sc->lastMs < SKEL_TTL) {
            for (int i = 0; i < 18; ++i) outLoc[i] = sc->location[i];
            sc->lastMs = now;
            return true;
        }

        sc->entity = entity;
        sc->female = female;
        sc->valid  = false;
        sc->lastMs = now;
        for (int i = 0; i < 18; ++i) sc->location[i] = 0;

        uint32_t capsule = g_Memory->Read<uint32_t>(entity + 0x7B4);  // atualizado 2026-09-16 (era 0x760) - List<CapsuleCollider> @ Player+0x7B4
        if (!capsule) return false;
        uint32_t transformPtr = g_Memory->Read<uint32_t>(capsule + 0x8);
        if (!transformPtr) return false;

        // Batch-read the 18 Location pointers (contiguous at transformPtr + 0x10..0x54)
        uint32_t locBlock[18] = {};
        if (!g_Memory->ReadBytes(transformPtr + 0x10, locBlock, sizeof(locBlock))) return false;

        const int* slots = female ? kFemaleSlot : kMaleSlot;
        for (int b = 0; b < 18; ++b) sc->location[b] = locBlock[slots[b]];

        sc->valid = true;
        for (int i = 0; i < 18; ++i) outLoc[i] = sc->location[i];
        return true;
    }

    // Clears the per-entity skeleton cache. Called from Data::Reset
    // on emulator shutdown so cycle-N entity pointers can't collide with
    // cycle-N+1 entity IDs by coincidence.
    static void ResetCaches() {
        for (int i = 0; i < SKEL_MAX; ++i) g_skelCache[i] = {};
        g_skelCount = 0;
        g_skelNext  = 0;
    }
} // namespace

// ---------------------------------------------------------------------------
// Match-chain pointer cache — rebuilt only when stale or invalidated
// ---------------------------------------------------------------------------
static uint32_t s_staticGF     = 0;
static uint32_t s_MatchGame    = 0;
static uint32_t s_Dictionary   = 0;
static uint32_t s_entitiesBase = 0;
static DWORD    s_gfMs         = 0;
static DWORD    s_matchMs      = 0;

static constexpr DWORD GF_TTL    = 5000; // staticGF chain — very stable
static constexpr DWORD MATCH_TTL =  800; // match sub-chain

// ---------------------------------------------------------------------------
void Data::Draw(HWND hWindow, uint32_t il2cpp, int width, int height) {
    ScreenWidth  = width;
    ScreenHeight = height;

    // RayCast é usado tanto pelo ESP visibility color quanto pelo Silent Visible
    // Check — basta uma das duas features estar ligada pra manter o pruner
    // resolvido/atualizado.
    if (g_Options.Visuals.ESP.Players.UseVisibilityColor || g_Options.AimDMA.SilentVisibleCheck)
        RayCast::Initialize();

    if (g_Options.AimDMA.SilentDrawFov) {
        ImGui::GetBackgroundDrawList()->AddCircle(
            ImVec2(ScreenWidth * 0.5f, ScreenHeight * 0.5f),
            g_Options.AimDMA.SilentFov,
            IM_COL32(255, 255, 255, 200), 64, 1.5f
        );
    }
    if (g_Options.AimDMA.AimBotMemoryDrawFov) {
        ImGui::GetBackgroundDrawList()->AddCircle(
            ImVec2(ScreenWidth * 0.5f, ScreenHeight * 0.5f),
            g_Options.AimDMA.AimBotMemoryFov,
            IM_COL32(255, 255, 255, 200), 64, 1.5f
        );
    }

    // ── Snapshot compartilhado ───────────────────────────────────────────────
    // Producer thread (Cheat::Shared::Producer) já resolveu toda a cadeia de
    // pointers + LP + Transform + ViewMatrix + Dict + scan das entidades.
    // O Draw só CONSOME esse snapshot — zero RPMs pra montar o contexto do
    // frame. Antes, cada frame do ESP fazia ~5000 RPMs (chain + 512 entidades
    // × ~10 leituras cada), sozinho o maior consumidor de CPU do cheat quando
    // combinado com as 5 threads de aim que também refaziam o mesmo scan.
    const Cheat::Shared::Snapshot* snap = Cheat::Shared::Producer::Get( ).Latest( );
    if ( !snap || !snap->valid || !snap->hasView ) return;

    // Cache stubs mantidos por compatibilidade com Reset() — atualizados
    // pra refletir o snapshot corrente. Reset zera esses, o Producer
    // reconstrói via cache TTL interno.
    s_staticGF     = snap->staticGF;
    s_MatchGame    = snap->matchGame;
    s_Dictionary   = snap->dictionary;
    s_entitiesBase = snap->entitiesBase;

    uint32_t   localPlayer    = snap->localPlayer;
    if ( !localPlayer ) return;

    // Drive the Ghost toggle from the render thread (mirrors ZmInternal's
    // layout). Runs BEFORE we consume any g_GhostActive / g_GhostPos state
    // this frame so the overlay reflects the current key edge, not the
    // previous frame's snapshot.
    AimModules::TickGhostMode( localPlayer );

    // Reproject com viewMatrix FRESH por render frame — elimina o delay
    // perceptual do box em pans rápidos de câmera. O box e o skeleton
    // acompanham a rotação do jogo em tempo real em vez de arrastar
    // 2-3 frames atrás usando a matriz stale do Producer.
    //
    // ANTI-PISCA CRÍTICO: a tentativa anterior alternava entre `fresh` e
    // `snap->viewMatrix` quando fresh falhava — as duas matrizes eram de
    // "eras" diferentes (fresh = agora, snap = último tick do Producer),
    // então cada frame que fazia fallback jogava o box pra uma posição
    // diferente → tremor visível.
    //
    // Fix: cache thread_local de "last-good fresh". Se fresh OK → usa e
    // cacheia. Se fresh falha → usa o CACHED-FRESH (mesma "era" da anterior,
    // não a snap-era). NUNCA volta pra snap->viewMatrix (exceto no primeiro
    // frame quando o cache está vazio). Assim ambos caminhos ficam na
    // mesma "family" de matrizes — nenhuma alternação entre eras.
    // Removido thread_local (static TLS falha em DLL mapeada manualmente).
    // Render é single-thread, então static simples é equivalente e seguro.
    static Matrix4x4 s_lastGoodFreshVM = { };
    static bool      s_hasCachedVM     = false;
    Matrix4x4 ViewMatrix;
    {
        Matrix4x4 fresh{ };
        bool ok = false;
        if ( snap->postRender && Memory::IsValidPtr( snap->postRender ) ) {
            fresh = g_Memory->Read<Matrix4x4>( snap->postRender + Cheat::g_DrawOff.VIEWMATRIX );
            const float m11 = fresh._11, m22 = fresh._22, m33 = fresh._33, m44 = fresh._44;
            const bool finite  = ( m11 == m11 ) && ( m22 == m22 ) && ( m33 == m33 ) && ( m44 == m44 );
            const bool nonzero = ( m11 != 0.f || m22 != 0.f || m33 != 0.f );
            ok = finite && nonzero;
        }
        if ( ok ) {
            s_lastGoodFreshVM = fresh;
            s_hasCachedVM     = true;
            ViewMatrix        = fresh;
        } else if ( s_hasCachedVM ) {
            ViewMatrix        = s_lastGoodFreshVM;   // mesma "era" — sem alternação
        } else {
            ViewMatrix        = snap->viewMatrix;    // primeiro frame apenas
        }
    }
    const Vector3&   MainPos       = snap->localPos;
    const Vector3&   CameraWorldPos = snap->cameraWorldPos;   // já resolvido com fallbacks

    // ── Loop de entidades ────────────────────────────────────────────────────
    // Itera as EntitySnapshots já pré-filtradas e pré-calculadas pelo Producer.
    // As leituras que antes eram feitas per-entity per-frame (AvatarMgr,
    // Avatar, AvatarData, TransformType, IsVisible, IsTeam, ipri, mDatas,
    // healthReplicationData, weaponReplicationData, HP, GetHeadPosition,
    // GetRootPosition, 3× W2S) agora vêm do snapshot — zero RPMs no hot path.
    // Único RPM opcional aqui é o RayCast quando "Visible Check" está ligado.
    for ( const auto& e : snap->entities )
    {
        // Producer já pré-filtrou (alive == passa em todos os testes básicos +
        // headPos válido). screenValid confirma que W2S retornou coords com Z>0.
        if ( !e.alive || !e.screenValid ) continue;

        uint32_t Entity                  = e.entity;
        uint32_t weaponReplicationData   = e.weaponRepData;
        int      ReplicationData         = e.hp;
        Vector3  PosHeadEntity           = e.headPos;
        float    Distancia               = e.distanceToLp;

        if ( Distancia > (float)g_Options.Visuals.ESP.Players.EspRenderDistance ) continue;

        // Early-out: se NENHUMA feature de desenho está ligada, pula o restante
        // (raycast, pickCol × 6, cálculo de box, cull) — antes o loop percorria
        // essas 40+ entidades por frame mesmo com o ESP todo desligado, gastando
        // CPU no render thread à toa. Se qualquer feature está on, cai no path
        // normal (raycast só é chamado se UseVisibilityColor também está on,
        // então o custo do IsVisible só vira relevante quando faz sentido).
        {
            const auto& P0 = g_Options.Visuals.ESP.Players;
            if ( !P0.ESPLine && !P0.ESPBox && !P0.ESPHealth && !P0.ESPDistance
              && !P0.ESPSkeleton && !P0.ESPName && !P0.ESPWeapon )
                continue;
        }

        bool RaycastVisible = true;
        if ( g_Options.Visuals.ESP.Players.UseVisibilityColor )
            RaycastVisible = RayCast::IsVisible( CameraWorldPos , PosHeadEntity );

        // Per-element colors — user picks in the Colors tab. When Visible Check
        // is ON and the enemy is currently hidden, we override every draw to
        // red so wall-status is instantly readable (alpha preserved from the
        // user's own choice so their opacity preference still applies).
        auto pickCol = [&](const float userColor[4], int redAlpha) -> ImU32 {
            if (RaycastVisible) return EspColU32(userColor);
            // Hidden state: usa HiddenColor (configurável na aba Colors).
            // Alpha vem do próprio color do elemento (redAlpha) para preservar
            // a opacidade que o usuário escolheu por elemento — o HiddenColor
            // controla APENAS o RGB da sobreposição.
            const float* h = g_Options.Visuals.ESP.Players.HiddenColor;
            return IM_COL32((int)(h[0]*255), (int)(h[1]*255), (int)(h[2]*255), redAlpha);
        };
        const auto& P = g_Options.Visuals.ESP.Players;
        const ImU32 colBox      = pickCol(P.BoxColor,      (int)(P.BoxColor[3]      * 255));
        const ImU32 colLine     = pickCol(P.LineColor,     (int)(P.LineColor[3]     * 255));
        const ImU32 colSkel     = pickCol(P.SkeletonColor, (int)(P.SkeletonColor[3] * 255));
        const ImU32 colDistance = pickCol(P.DistanceColor, (int)(P.DistanceColor[3] * 255));
        const ImU32 colName     = pickCol(P.NameColor,     (int)(P.NameColor[3]     * 255));
        const ImU32 colWeapon   = pickCol(P.WeaponColor,   (int)(P.WeaponColor[3]   * 255));

        // Reproject aqui com a ViewMatrix FRESH deste render frame (ver bloco
        // ~linha 436). World coords vêm do Producer (0-10ms stale — imperceptível
        // pra posição), projeção usa a matriz que o DX11 vai renderizar AGORA —
        // box gruda no modelo durante flicks em vez de arrastar 3-4 frames atrás.
        // Offsets Up*0.20/Down*0.10 são idênticos aos usados pelo Producer em
        // SharedFrame.cpp:575-576, então o comportamento visual é 1:1 com o que
        // o pipeline anterior desenhava — só sem o delay de 10ms. e.screenValid
        // (gate anterior linha ~451) continua garantindo que headPos/rootPos são
        // sanos; a checagem Z>0 abaixo pega o caso raro em que a matriz fresca
        // move o alvo pra trás da câmera entre snapshot e render frame.
        // Reproject com ViewMatrix fresh (ou last-good-fresh) — mesma matriz
        // que o skeleton e ghost overlay usam nesta frame → tudo coerente.
        // World coords vêm do Producer (0-2.7ms stale a 360Hz), projeção usa
        // a matriz do render frame atual → box gruda no modelo mesmo em pans
        // rápidos. Offsets Up*0.20/Down*0.10 idênticos ao Producer pra manter
        // comportamento visual 1:1 com o legado, só sem a defasagem.
        Vector3 HeadPos      = W2S::World2Screen( ViewMatrix , e.headPos + ( Vector3::Up( )   * 0.20f ) );
        Vector3 EntityScreen = W2S::World2Screen( ViewMatrix , e.rootPos + ( Vector3::Down( ) * 0.10f ) );
        if ( HeadPos.Z < 0.0f || EntityScreen.Z < 0.0f ) continue;

        // Cheap off-screen cull: only skip if the ENTIRE bounding box is off-screen
        {
            const float swf   = (float)ScreenWidth;
            const float shf   = (float)ScreenHeight;
            const float halfW = fabsf(EntityScreen.Y - HeadPos.Y) * 0.5f;
            const float boxL  = fminf(HeadPos.X, EntityScreen.X) - halfW;
            const float boxR  = fmaxf(HeadPos.X, EntityScreen.X) + halfW;
            const float boxT  = fminf(HeadPos.Y, EntityScreen.Y);
            const float boxB  = fmaxf(HeadPos.Y, EntityScreen.Y);
            if (boxR < 0.f || boxL > swf || boxB < 0.f || boxT > shf) continue;
        }

        float Height = fabsf(EntityScreen.Y - HeadPos.Y);
        // Prone/crouch clamp: quando o inimigo está deitado, o bone da
        // cabeça e o do quadril colapsam no eixo Y do mundo (~0.3m de
        // separação) e W2S projeta ~5-10px de altura a 30m — o box vira
        // efetivamente invisível justo no cenário em que o alvo é mais
        // difícil de spotar (camping em mato, sniper deitado). Enforce
        // um tamanho mínimo legível pra que o box permaneça útil sem
        // alterar comportamento pra players em pé/correndo (Height >> 20px
        // e Width >> 10px em qualquer distância normal de engajamento).
        // Custo runtime: 2 comparações por entity por frame — nulo.
        if (Height < 20.0f) Height = 20.0f;
        float Width  = Height / 2.0f;
        if (Width  < 10.0f) Width  = 10.0f;

        float boxLeft   = HeadPos.X - (Width / 2.0f);
        float boxRight  = HeadPos.X + (Width / 2.0f);
        float boxTop    = HeadPos.Y;
        // BUG FIX: boxBottom antes usava EntityScreen.Y raw — quando Height
        // era clamped pra 20px mas EntityScreen.Y - HeadPos.Y era menor
        // (ex: 8px pra player prone perto), o box virava uma "listra"
        // horizontal (8px de altura) enquanto a health bar renderizava
        // com a altura CLAMPED (20px), ficando maior que o box. Corrige
        // usando Height clamped pra consistência total entre box + health +
        // outros elementos que derivam de Height.
        float boxBottom = boxTop + Height;

        // Line
        if (g_Options.Visuals.ESP.Players.ESPLine) {
            bool fromTop = (g_Options.Visuals.ESP.Players.ESPLinePos == 0);
            ImVec2 origin = fromTop
                ? ImVec2(ScreenWidth * 0.5f, 0.f)
                : ImVec2(ScreenWidth * 0.5f, (float)ScreenHeight);
            ImVec2 target = fromTop
                ? ImVec2(HeadPos.X, HeadPos.Y)
                : ImVec2(EntityScreen.X, EntityScreen.Y);
            ImGui::GetBackgroundDrawList()->AddLine(origin, target, colLine, P.LineThickness);
        }

        // Box — style controlled by ESPBoxStyle (0=Normal, 1=Filled, 2=Corners)
        if (g_Options.Visuals.ESP.Players.ESPBox) {
            const float thick = P.BoxThickness;
            const int style = g_Options.Visuals.ESP.Players.ESPBoxStyle;
            auto* DL = ImGui::GetBackgroundDrawList();

            if (style == 0) {
                DL->AddRect(ImVec2(boxLeft, boxTop), ImVec2(boxRight, boxBottom), colBox, 0.0f, 0, thick);
            }
            else if (style == 1) {
                ImU32 fillCol = (colBox & 0x00FFFFFF) | (60u << 24);
                DL->AddRectFilled(ImVec2(boxLeft, boxTop), ImVec2(boxRight, boxBottom), fillCol);
                DL->AddRect(ImVec2(boxLeft, boxTop), ImVec2(boxRight, boxBottom), colBox, 0.0f, 0, thick);
            }
            else {
                float lineW = Width  / 3.0f;
                float lineH = Height / 3.0f;
                DL->AddRectFilled(
                    ImVec2(boxLeft, boxTop), ImVec2(boxRight, boxBottom),
                    IM_COL32(0, 0, 0, 40)
                );
                DL->AddLine(ImVec2(boxLeft,  boxTop),    ImVec2(boxLeft  + lineW, boxTop),    colBox, thick);
                DL->AddLine(ImVec2(boxLeft,  boxTop),    ImVec2(boxLeft,  boxTop  + lineH),   colBox, thick);
                DL->AddLine(ImVec2(boxRight, boxTop),    ImVec2(boxRight - lineW, boxTop),    colBox, thick);
                DL->AddLine(ImVec2(boxRight, boxTop),    ImVec2(boxRight, boxTop  + lineH),   colBox, thick);
                DL->AddLine(ImVec2(boxLeft,  boxBottom), ImVec2(boxLeft  + lineW, boxBottom), colBox, thick);
                DL->AddLine(ImVec2(boxLeft,  boxBottom), ImVec2(boxLeft,  boxBottom - lineH), colBox, thick);
                DL->AddLine(ImVec2(boxRight, boxBottom), ImVec2(boxRight - lineW, boxBottom), colBox, thick);
                DL->AddLine(ImVec2(boxRight, boxBottom), ImVec2(boxRight, boxBottom - lineH), colBox, thick);
            }
        }

        // Health bar
        if (g_Options.Visuals.ESP.Players.ESPHealth) {
            constexpr int MaxHealth = 200;
            float hp = std::clamp(static_cast<float>(ReplicationData) / MaxHealth, 0.0f, 1.0f);

            ImU32 hpColor;
            if (hp > 0.5f) {
                float t = (hp - 0.5f) * 2.0f;
                hpColor = IM_COL32((int)((1.0f - t) * 255), 255, 0, 255);
            } else {
                hpColor = IM_COL32(255, (int)(hp * 2.0f * 255), 0, 255);
            }

            int healthPos = g_Options.Visuals.ESP.Players.ESPHealthPos;
            if (healthPos == 0) {
                float barX = boxLeft - 5.0f;
                float barW = 2.5f;
                float filledH = Height * hp;
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(barX, boxTop), ImVec2(barX + barW, boxBottom), IM_COL32(0, 0, 0, 128));
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(barX, boxBottom - filledH), ImVec2(barX + barW, boxBottom), hpColor);
            } else if (healthPos == 1) {
                float barX = boxRight + 3.0f;
                float barW = 2.5f;
                float filledH = Height * hp;
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(barX, boxTop), ImVec2(barX + barW, boxBottom), IM_COL32(0, 0, 0, 128));
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(barX, boxBottom - filledH), ImVec2(barX + barW, boxBottom), hpColor);
            } else if (healthPos == 2) {
                float barY = boxTop - 6.0f;
                float barH = 2.5f;
                float filledW = Width * hp;
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(boxLeft, barY), ImVec2(boxRight, barY + barH), IM_COL32(0, 0, 0, 128));
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(boxLeft, barY), ImVec2(boxLeft + filledW, barY + barH), hpColor);
            } else {
                float barY = boxBottom + 3.0f;
                float barH = 2.5f;
                float filledW = Width * hp;
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(boxLeft, barY), ImVec2(boxRight, barY + barH), IM_COL32(0, 0, 0, 128));
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(boxLeft, barY), ImVec2(boxLeft + filledW, barY + barH), hpColor);
            }
        }

        // Distance
        if (g_Options.Visuals.ESP.Players.ESPDistance) {
            char distBuf[32];
            sprintf_s(distBuf, "%dM", (int)Distancia);
            const float sz_px = P.DistanceSize;
            ImVec2 sz = CalcDistSize(sz_px, (int)Distancia, distBuf);

            ImVec2 pos;
            int distPos = g_Options.Visuals.ESP.Players.ESPDistancePos;
            if (distPos == 0)      pos = ImVec2(boxLeft - sz.x - 5.0f,   boxTop + (Height - sz.y) * 0.5f);
            else if (distPos == 1) pos = ImVec2(boxRight + 5.0f,         boxTop + (Height - sz.y) * 0.5f);
            else if (distPos == 2) pos = ImVec2(HeadPos.X - sz.x * 0.5f, boxTop - sz.y - 3.0f);
            else                   pos = ImVec2(HeadPos.X - sz.x * 0.5f, boxBottom + 3.0f);

            auto* DL = ImGui::GetBackgroundDrawList();
            if (P.ESPTextShadow)
                EspDrawText(DL, sz_px, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 180), distBuf);
            EspDrawText(DL, sz_px, pos, colDistance, distBuf);
        }

        // Name
        if (g_Options.Visuals.ESP.Players.ESPName) {
            char nameBuf[256];
            int nameLen = 0;
            // Snapshot-first: Producer resolveu o nickname a 100 Hz —
            // memcpy local, zero RPMs no render thread. Se estiver vazio
            // (producer ainda não pegou, ou a cadeia falhou nesse tick),
            // cai no SafeReadPlayerName original: comportamento idêntico
            // ao pior caso de antes da migração.
            if (e.nameLen > 0) {
                nameLen = e.nameLen;
                if (nameLen >= (int)sizeof(nameBuf)) nameLen = (int)sizeof(nameBuf) - 1;
                memcpy(nameBuf, e.name, nameLen);
                nameBuf[nameLen] = '\0';
            } else {
                nameLen = SafeReadPlayerName(Entity, nameBuf, sizeof(nameBuf));
            }
            if (nameLen > 0) {
                switch (g_Options.Visuals.ESP.Players.ESPNameStyle) {
                    case 1: {
                        char hpBuf[32];
                        int hpLen = sprintf_s(hpBuf, " [%d]", ReplicationData);
                        if (hpLen > 0 && nameLen + hpLen < (int)sizeof(nameBuf)) {
                            memcpy(nameBuf + nameLen, hpBuf, hpLen);
                            nameLen += hpLen;
                            nameBuf[nameLen] = '\0';
                        }
                        break;
                    }
                    case 2: {
                        // e.isClientBot pré-cacheado pelo Producer — elimina
                        // 1 RPM por entidade por frame com ESPName+BOT tag.
                        if ( e.isClientBot ) {
                            constexpr char kBotTag[] = "[BOT] ";
                            constexpr int  kBotLen  = sizeof(kBotTag) - 1;
                            if (nameLen + kBotLen < (int)sizeof(nameBuf)) {
                                memmove(nameBuf + kBotLen, nameBuf, nameLen + 1);
                                memcpy(nameBuf, kBotTag, kBotLen);
                                nameLen += kBotLen;
                            }
                        }
                        break;
                    }
                    default: break;
                }

                const float sz_px = P.NameSize;
                ImVec2 sz = CalcNameSize(sz_px, Entity, nameBuf, nameLen);
                ImVec2 pos;
                int p = g_Options.Visuals.ESP.Players.ESPNamePos;
                if      (p == 0) pos = ImVec2(boxLeft - sz.x - 5.0f,   boxTop + (Height - sz.y) * 0.5f);
                else if (p == 1) pos = ImVec2(boxRight + 5.0f,         boxTop + (Height - sz.y) * 0.5f);
                else if (p == 2) pos = ImVec2(HeadPos.X - sz.x * 0.5f, boxTop - sz.y - 3.0f);
                else             pos = ImVec2(HeadPos.X - sz.x * 0.5f, boxBottom + 3.0f);

                auto* DL = ImGui::GetBackgroundDrawList();
                if (P.ESPTextShadow)
                    EspDrawText(DL, sz_px, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 180), nameBuf);
                EspDrawText(DL, sz_px, pos, colName, nameBuf);
            }
        }

        // Weapon
        if (g_Options.Visuals.ESP.Players.ESPWeapon) {
            // weaponId veio pré-lido pelo Producer (SharedFrame.cpp) — zero RPM aqui.
            // Sentinel -1 (nenhum weaponRepData) cai fora do range e é descartado, igual
            // ao comportamento antigo do ReadWeaponId retornando -1.
            int weaponId = e.weaponId;
            if (weaponId >= -25000 && weaponId <= 25000) {
                // GetGunName/GetGunIcon agora retornam const char* apontando pro
                // GunData estático — zero alocação de heap por chamada.
                const char* weaponName = Namegun::GetGunName((short)weaponId);
                if (weaponName[0] != '\0') {
                    const int style    = g_Options.Visuals.ESP.Players.ESPWeaponStyle;
                    const bool hasIcon = Namegun::HasIcon((short)weaponId);
                    // Stack buffer pra compor "icon name" quando style==1 —
                    // substitui as 2 std::string temporárias da concat antiga.
                    char weaponLabel[96];
                    const char* labelPtr;
                    if (style == 2 && hasIcon) {
                        labelPtr = Namegun::GetGunIcon((short)weaponId);
                    } else if (style == 1 && hasIcon) {
                        snprintf(weaponLabel, sizeof(weaponLabel), "%s %s",
                                 Namegun::GetGunIcon((short)weaponId), weaponName);
                        labelPtr = weaponLabel;
                    } else {
                        labelPtr = weaponName;
                    }

                    const float sz_px = P.WeaponSize;
                    ImVec2 sz = CalcWeaponSize(sz_px, (uint16_t)weaponId, (uint8_t)style, labelPtr);
                    ImVec2 pos;
                    int p = g_Options.Visuals.ESP.Players.ESPWeaponPos;
                    if      (p == 0) pos = ImVec2(boxLeft - sz.x - 5.0f,   boxTop + (Height - sz.y) * 0.5f);
                    else if (p == 1) pos = ImVec2(boxRight + 5.0f,         boxTop + (Height - sz.y) * 0.5f);
                    else if (p == 2) pos = ImVec2(HeadPos.X - sz.x * 0.5f, boxTop - sz.y - 3.0f);
                    else             pos = ImVec2(HeadPos.X - sz.x * 0.5f, boxBottom + 3.0f);

                    auto* DL = ImGui::GetBackgroundDrawList();
                    if (P.ESPTextShadow)
                        EspDrawText(DL, sz_px, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 180), labelPtr);
                    EspDrawText(DL, sz_px, pos, colWeapon, labelPtr);
                }
            }
        }

        // Skeleton — bones em world já vieram pré-resolvidos pelo Producer
        // (via a mesma cadeia Entity+0x760→+0x8→+slot→H1→H2→H3→+0x60 do
        // LoadSkelChain original). Aqui só fazemos W2S local e desenha —
        // zero RPMs. Elimina 72 RPMs por entidade por frame do render thread
        // (o maior consumidor remanescente pós-migração inicial).
        if ( g_Options.Visuals.ESP.Players.ESPSkeleton && e.skelValid ) {
            Vector3 bonesScreen[ 18 ];
            for ( int b = 0; b < 18; ++b ) {
                const Vector3& wp = e.skelWorld[ b ];
                if ( wp.X == 0.f && wp.Y == 0.f && wp.Z == 0.f ) { bonesScreen[ b ] = Vector3::Zero( ); continue; }
                Vector3 sp = W2S::World2Screen( ViewMatrix , wp );
                bonesScreen[ b ] = ( sp.Z < 0.0f ) ? Vector3::Zero( ) : sp;
            }

            auto* DL = ImGui::GetBackgroundDrawList( );
            auto SafeLine = [&]( int a , int b ) {
                const Vector3& A = bonesScreen[ a ];
                const Vector3& B = bonesScreen[ b ];
                if ( ( A.X == 0.0f && A.Y == 0.0f ) || ( B.X == 0.0f && B.Y == 0.0f ) ) return;
                DL->AddLine( ImVec2( A.X , A.Y ) , ImVec2( B.X , B.Y ) , colSkel , g_Options.Visuals.ESP.Players.SkeletonThickness );
            };

                SafeLine(BONE_HEAD, BONE_CHEST);
                SafeLine(BONE_CHEST, BONE_STOMACH);
                SafeLine(BONE_STOMACH, BONE_HIP);

                SafeLine(BONE_CHEST, BONE_LSHOULDER);
                SafeLine(BONE_LSHOULDER, BONE_LARM);
                SafeLine(BONE_LARM, BONE_LFOREARM);
                SafeLine(BONE_LFOREARM, BONE_LHAND);

                SafeLine(BONE_CHEST, BONE_RSHOULDER);
                SafeLine(BONE_RSHOULDER, BONE_RARM);
                SafeLine(BONE_RARM, BONE_RFOREARM);
                SafeLine(BONE_RFOREARM, BONE_RHAND);

                SafeLine(BONE_HIP, BONE_LTHIGH);
                SafeLine(BONE_LTHIGH, BONE_LSHIN);
                SafeLine(BONE_LSHIN, BONE_LFOOT);

                SafeLine(BONE_HIP, BONE_RTHIGH);
                SafeLine(BONE_RTHIGH, BONE_RSHIN);
                SafeLine(BONE_RSHIN, BONE_RFOOT);
        }
    }

    // ---------- GhostMode overlay --------------------------------------------
    if (AimModules::g_GhostActive.load()) {
        auto* DL = ImGui::GetBackgroundDrawList();
        // Snapshot the ghost anchor (pos + bones) through the thread-safe
        // accessor. TickGhostMode() runs on this same thread now, so the
        // race is gone in practice, but the accessor stays for
        // defense-in-depth in case another thread ever writes to it.
        AimModules::GhostSnapshot ghostSnap = AimModules::GetGhostSnapshot();
        Vector3 ghostWorld = ghostSnap.pos;

        Vector3 ghostScreen = W2S::World2Screen(ViewMatrix, ghostWorld);
        if (ghostScreen.Z > 0.0f) {
            const float sz = 10.0f;
            const ImU32 col = IM_COL32(0, 255, 255, 255);
            DL->AddLine(ImVec2(ghostScreen.X, ghostScreen.Y - sz),
                        ImVec2(ghostScreen.X, ghostScreen.Y + sz), col, 2.0f);
            DL->AddLine(ImVec2(ghostScreen.X - sz, ghostScreen.Y),
                        ImVec2(ghostScreen.X + sz, ghostScreen.Y), col, 2.0f);
        }

        // Ghost skeleton — reuses the exact SafeLine bone connectivity of
        // the enemy ESP skeleton. Bones were captured in world coords at
        // Ghost activation and are anchored there forever (until next
        // press) — we just W2S them every frame with the current view
        // matrix. Color/thickness mirror the ESP skeleton settings so it
        // looks like "yourself painted in cyan at the anchor".
        if (ghostSnap.skelValid) {
            Vector3 bonesScreen[18];
            for (int b = 0; b < 18; ++b) {
                const Vector3& wp = ghostSnap.bones[b];
                if (wp.X == 0.f && wp.Y == 0.f && wp.Z == 0.f) { bonesScreen[b] = Vector3::Zero(); continue; }
                Vector3 sp = W2S::World2Screen(ViewMatrix, wp);
                bonesScreen[b] = (sp.Z < 0.0f) ? Vector3::Zero() : sp;
            }

            const ImU32 ghostSkelCol = IM_COL32(0, 255, 255, 255);
            const float ghostSkelThick = g_Options.Visuals.ESP.Players.SkeletonThickness > 0.0f
                                        ? g_Options.Visuals.ESP.Players.SkeletonThickness
                                        : 1.5f;

            auto SafeLine = [&](int a, int b) {
                const Vector3& A = bonesScreen[a];
                const Vector3& B = bonesScreen[b];
                if ((A.X == 0.0f && A.Y == 0.0f) || (B.X == 0.0f && B.Y == 0.0f)) return;
                DL->AddLine(ImVec2(A.X, A.Y), ImVec2(B.X, B.Y), ghostSkelCol, ghostSkelThick);
            };

            SafeLine(BONE_HEAD, BONE_CHEST);
            SafeLine(BONE_CHEST, BONE_STOMACH);
            SafeLine(BONE_STOMACH, BONE_HIP);

            SafeLine(BONE_CHEST, BONE_LSHOULDER);
            SafeLine(BONE_LSHOULDER, BONE_LARM);
            SafeLine(BONE_LARM, BONE_LFOREARM);
            SafeLine(BONE_LFOREARM, BONE_LHAND);

            SafeLine(BONE_CHEST, BONE_RSHOULDER);
            SafeLine(BONE_RSHOULDER, BONE_RARM);
            SafeLine(BONE_RARM, BONE_RFOREARM);
            SafeLine(BONE_RFOREARM, BONE_RHAND);

            SafeLine(BONE_HIP, BONE_LTHIGH);
            SafeLine(BONE_LTHIGH, BONE_LSHIN);
            SafeLine(BONE_LSHIN, BONE_LFOOT);

            SafeLine(BONE_HIP, BONE_RTHIGH);
            SafeLine(BONE_RTHIGH, BONE_RSHIN);
            SafeLine(BONE_RSHIN, BONE_RFOOT);
        }

        // Read the local player's CURRENT position fresh from the
        // Transform this frame (mirrors ZmInternal) instead of using
        // snap->localPos. The snapshot lags by up to a producer cycle,
        // and — more importantly — the snapshot's localTransform pointer
        // could be resolved before the game finished releasing the
        // previous ghost anchor, making the label distance measure
        // "old client pos vs. old anchor" ≈ 0 → false ON, or vice versa.
        // Reading right here, right now, guarantees the label reflects
        // where the player actually is at draw time.
        Vector3 currentPos = MainPos;
        {
            uint32_t curTf = g_Memory->Read<uint32_t>(localPlayer + Cheat::g_DrawOff.TRANSFORM);
            if (curTf) currentPos = Transform::get_position_Injected(curTf);
        }

        // Horizontal-only distance (XZ plane): Ghost's damage-registration
        // tolerance is a *horizontal* radius around the server anchor.
        // Including Y in the check caused a jump / stair / small terrain
        // bump to blow past the 4.6m threshold on its own and flip the
        // label to "FAKE" even while the player was standing near the
        // anchor.
        const float dx = currentPos.X - ghostWorld.X;
        const float dz = currentPos.Z - ghostWorld.Z;
        const float ghostDistance = std::sqrt(dx*dx + dz*dz);
        const bool  inRange = ghostDistance <= 4.60f;
        const char* txt = inRange ? "Ghost Damage: ON" : "Ghost Damage: FAKE";
        const ImU32 txtCol = inRange ? IM_COL32(30, 255, 30, 255) : IM_COL32(255, 30, 30, 255);

        const float fontSize = 18.0f;
        ImVec2 tsz = EspCalcTextSize(fontSize, txt);
        ImVec2 tpos(((float)ScreenWidth - tsz.x) * 0.5f, 90.0f);
        EspDrawText(DL, fontSize, ImVec2(tpos.x + 1, tpos.y + 1), IM_COL32(0, 0, 0, 200), txt);
        EspDrawText(DL, fontSize, tpos, txtCol, txt);
    }
}

void Data::Reset() {
    // Match-chain cache — belongs to the previous emulator's il2cpp layout.
    s_staticGF     = 0;
    s_MatchGame    = 0;
    s_Dictionary   = 0;
    s_entitiesBase = 0;
    s_gfMs         = 0;
    s_matchMs      = 0;

    // Per-entity skeleton and filter caches live inside the anonymous
    // namespace above; ResetCaches() is the accessor (skeleton cache only —
    // per-entity filter cache foi removido para bater com o loop da source
    // ff-semanal/AimlockInternal, que resolve a cadeia per-frame).
    ResetCaches();
}
