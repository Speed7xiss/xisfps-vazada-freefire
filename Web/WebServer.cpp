#include "WebServer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <cstdio>

#include "../Imgui/menu/headers/config.h"
#include "../Cheat/CheatStatus.hpp"
#include "../Cheat/SkinChanger/SkinChanger.hpp"
#include "../Cheat/SkinChanger/ClothesDB.hpp"
#include "../Cheat/SharedFrame.hpp"
#include "../Cheat/Cheat.hpp"
#include "../Cheat/AimModules/AimModules.hpp"

namespace WebSite
{
    static std::atomic<bool>     s_running{ false };
    static std::atomic<int>      s_port{ 0 };
    static std::atomic<unsigned> s_hits{ 0 };
    static std::atomic<unsigned long long> s_startedTick{ 0 };
    static std::string           s_root;
    static std::string           s_error;
    static std::mutex            s_mtx;
    static SOCKET                s_listen = INVALID_SOCKET;
    static std::thread           s_thread;
    static bool                  s_wsa = false;

    std::string LastError() { std::lock_guard<std::mutex> l(s_mtx); return s_error; }
    bool IsRunning() { return s_running.load(); }
    int RunningPort() { return s_port.load(); }

    static void setErr(const std::string& e) { std::lock_guard<std::mutex> l(s_mtx); s_error = e; }

    // ---------- utils ----------
    static std::string urlDecode(const std::string& s)
    {
        std::string r;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '%' && i + 2 < s.size())
            {
                char h[3] = { s[i + 1], s[i + 2], 0 };
                r += (char)strtol(h, nullptr, 16);
                i += 2;
            }
            else if (s[i] == '+')
            {
                r += ' ';
            }
            else
            {
                r += s[i];
            }
        }
        return r;
    }

    static std::string jsonEsc(const std::string& s)
    {
        std::string r;
        for (auto c : s)
        {
            if (c == '"' || c == '\\') { r += '\\'; r += c; }
            else if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else r += c;
        }
        return r;
    }

    static std::map<std::string, std::string> parseQuery(const std::string& q)
    {
        std::map<std::string, std::string> m;
        size_t i = 0;
        while (i < q.size())
        {
            size_t e = q.find('&', i);
            std::string kv = q.substr(i, e == std::string::npos ? e : e - i);
            size_t eq = kv.find('=');
            if (eq != std::string::npos)
                m[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
            else if (!kv.empty())
                m[urlDecode(kv)] = "";
            if (e == std::string::npos) break;
            i = e + 1;
        }
        return m;
    }

    static const char* mimeFor(const std::string& path)
    {
        auto dot = path.rfind('.');
        std::string ext = dot == std::string::npos ? "" : path.substr(dot);
        for (auto& c : ext) c = (char)tolower((unsigned char)c);
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".css")  return "text/css; charset=utf-8";
        if (ext == ".js")   return "application/javascript; charset=utf-8";
        if (ext == ".json") return "application/json";
        if (ext == ".png")  return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif")  return "image/gif";
        if (ext == ".svg")  return "image/svg+xml";
        if (ext == ".ico")  return "image/x-icon";
        if (ext == ".woff2") return "font/woff2";
        if (ext == ".woff")  return "font/woff";
        if (ext == ".ttf")   return "font/ttf";
        return "application/octet-stream";
    }

    static bool readFile(const std::string& full, std::vector<char>& out)
    {
        HANDLE h = CreateFileA(full.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD sz = GetFileSize(h, NULL);
        if (sz == INVALID_FILE_SIZE || sz > 64 * 1024 * 1024) { CloseHandle(h); return false; }
        out.resize(sz);
        DWORD got = 0;
        BOOL ok = sz == 0 || (ReadFile(h, out.data(), sz, &got, NULL) && got == sz);
        CloseHandle(h);
        return ok != FALSE;
    }

    // ---------- /api/state + /api/set sobre o cfg (mesmo sync da GUI leva pro backend) ----------
    static std::string apiState()
    {
        std::lock_guard<std::mutex> l(s_mtx);
        std::string j = "{";
        bool first = true;
        for (auto& [name, var] : cfg->all_options())
        {
            // esconde entradas de infra da UI web
            if (name == "Website Port" || name == "Cheat Monitor" || name == "Esp Monitor" ||
                name == "Menu Accent Color##color") continue;
            std::string v;
            std::visit([&v](auto&& w) {
                using T = std::decay_t<decltype(w)>;
                char b[128];
                if constexpr (std::is_same_v<T, checkbox_t>)       v = w.callback ? "true" : "false";
                else if constexpr (std::is_same_v<T, slider_int_t>) { snprintf(b, sizeof(b), "%d", w.callback); v = b; }
                else if constexpr (std::is_same_v<T, slider_float_t>) { snprintf(b, sizeof(b), "%.2f", w.callback); v = b; }
                else if constexpr (std::is_same_v<T, dropdown_t>)  { snprintf(b, sizeof(b), "%d", w.callback); v = b; }
                else if constexpr (std::is_same_v<T, color_edit_t>) {
                    snprintf(b, sizeof(b), "[%.2f,%.2f,%.2f,%.2f]", w.color[0], w.color[1], w.color[2], w.color[3]); v = b;
                }
                else v = "\"?\"";
            }, var);
            if (v == "\"?\"") continue;
            if (!first) j += ",";
            first = false;
            j += "\"" + jsonEsc(name) + "\":" + v;
        }
        j += "}";
        return j;
    }

    static std::string apiSet(const std::map<std::string, std::string>& q)
    {
        auto ik = q.find("key");
        auto iv = q.find("value");
        if (ik == q.end() || iv == q.end()) return "{\"ok\":false,\"err\":\"key+value\"}";
        std::lock_guard<std::mutex> l(s_mtx);
        auto& opts = cfg->all_options();
        auto it = opts.find(ik->second);
        if (it == opts.end()) return "{\"ok\":false,\"err\":\"unknown key\"}";
        bool changed = false;
        std::visit([&](auto&& w) {
            using T = std::decay_t<decltype(w)>;
            if constexpr (std::is_same_v<T, checkbox_t>)
            {
                w.callback = (iv->second == "1" || iv->second == "true");
                changed = true;
            }
            else if constexpr (std::is_same_v<T, slider_int_t>)
            {
                int v = atoi(iv->second.c_str());
                w.callback = std::max(w.min, std::min(w.max, v));
                changed = true;
            }
            else if constexpr (std::is_same_v<T, slider_float_t>)
            {
                float v = (float)atof(iv->second.c_str());
                w.callback = std::max(w.min, std::min(w.max, v));
                changed = true;
            }
            else if constexpr (std::is_same_v<T, dropdown_t>)
            {
                int v = atoi(iv->second.c_str());
                if (!w.items.empty()) v = std::max(0, std::min((int)w.items.size() - 1, v));
                w.callback = v;
                changed = true;
            }
            else if constexpr (std::is_same_v<T, color_edit_t>)
            {
                // "r,g,b,a" floats 0..1
                float c[4] = { w.color[0], w.color[1], w.color[2], w.color[3] };
                const char* p = iv->second.c_str();
                for (int k = 0; k < 4 && p && *p; k++)
                {
                    c[k] = std::max(0.f, std::min(1.f, (float)atof(p)));
                    p = strchr(p, ',');
                    if (p) p++;
                }
                for (int k = 0; k < 4; k++) w.color[k] = c[k];
                changed = true;
            }
        }, it->second);
        return changed ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"readonly\"}";
    }

    // ---------- /api/meta: tipo + limites de cada controle ----------
    static bool metaVisible(const std::string& name)
    {
        return name != "Website Port" && name != "Cheat Monitor" && name != "Esp Monitor" &&
               name != "Menu Accent Color##color";
    }

    static std::string apiMeta()
    {
        std::lock_guard<std::mutex> l(s_mtx);
        std::string j = "{";
        bool first = true;
        for (auto& [name, var] : cfg->all_options())
        {
            if (!metaVisible(name)) continue;
            std::string v;
            std::visit([&v](auto&& w) {
                using T = std::decay_t<decltype(w)>;
                char b[64];
                if constexpr (std::is_same_v<T, checkbox_t>) v = "{\"t\":\"b\"}";
                else if constexpr (std::is_same_v<T, slider_int_t>)
                    { snprintf(b, sizeof(b), "{\"t\":\"i\",\"min\":%d,\"max\":%d}", w.min, w.max); v = b; }
                else if constexpr (std::is_same_v<T, slider_float_t>)
                    { snprintf(b, sizeof(b), "{\"t\":\"f\",\"min\":%.2f,\"max\":%.2f}", w.min, w.max); v = b; }
                else if constexpr (std::is_same_v<T, dropdown_t>)
                {
                    v = "{\"t\":\"d\",\"items\":[";
                    for (size_t k = 0; k < w.items.size(); k++)
                    {
                        if (k) v += ",";
                        v += "\"" + jsonEsc(w.items[k]) + "\"";
                    }
                    v += "]}";
                }
                else if constexpr (std::is_same_v<T, color_edit_t>) v = "{\"t\":\"c\"}";
                else v = "";
            }, var);
            if (v.empty()) continue;
            if (!first) j += ",";
            first = false;
            j += "\"" + jsonEsc(name) + "\":" + v;
        }
        j += "}";
        return j;
    }

    // ---------- /api/skins + /api/skin (ClothesDB, mesma fonte da aba Skins) ----------
    static std::string apiSkins(int cat)
    {
        std::string j = "[";
        bool first = true;
        char b[64];
        for (int i = 0; i < SkinChanger::kClothesDBCount; i++)
        {
            const auto& e = SkinChanger::kClothesDB[i];
            int c = (int)e.category;
            if (cat >= 0 && c != cat) continue;
            if (!first) j += ",";
            first = false;
            snprintf(b, sizeof(b), "{\"id\":%u,\"r\":%d,\"c\":%d,\"n\":\"",
                e.id, (int)e.rarity, c);
            j += b;
            j += jsonEsc(e.description ? e.description : "?");
            j += "\"}";
        }
        j += "]";
        return j;
    }

    static std::string apiSkin(const std::map<std::string, std::string>& q)
    {
        auto ia = q.find("action");
        std::string a = ia == q.end() ? "state" : ia->second;
        if (a == "state")
        {
            char b[64];
            std::string j = "{";
            bool first = true;
            for (auto& w : SkinChanger::Wildcards())
            {
                if (!first) j += ",";
                first = false;
                snprintf(b, sizeof(b), "\"%d\":%u", (int)w.category, w.targetId);
                j += b;
            }
            j += "}";
            return j;
        }
        auto ic = q.find("cat");
        int cat = ic == q.end() ? -1 : atoi(ic->second.c_str());
        if (cat < 1 || cat > 6) return "{\"ok\":false,\"err\":\"cat 1..6\"}";
        auto c = (SkinChanger::Category)cat;
        if (a == "clear") { SkinChanger::ClearWildcard(c); return "{\"ok\":true}"; }
        if (a == "equip")
        {
            auto ii = q.find("id");
            if (ii == q.end()) return "{\"ok\":false,\"err\":\"id\"}";
            SkinChanger::SetWildcard(c, (uint32_t)strtoul(ii->second.c_str(), nullptr, 10));
            return "{\"ok\":true}";
        }
        return "{\"ok\":false,\"err\":\"action\"}";
    }

    // ---------- /api/configs + /api/config (via ponte na GUI) ----------
    static std::string apiConfigs()
    {
        return WebBridge::ConfigsList();
    }

    static std::string apiConfig(const std::map<std::string, std::string>& q)
    {
        auto ia = q.find("action");
        auto in = q.find("name");
        std::string a = ia == q.end() ? "" : ia->second;
        std::string n = in == q.end() ? "" : in->second;
        return WebBridge::ConfigAction(a, n);
    }

    // ---------- /api/radar: snapshot top-down (X/Z relativo ao local player) ----------
    static std::string apiRadar()
    {
        const auto* s = Cheat::Shared::Producer::Get().Latest();
        if (!s || !s->valid) return "{\"ok\":false}";
        char b[160];
        std::string j;
        snprintf(b, sizeof(b), "{\"ok\":true,\"n\":%u,\"lp\":[%.1f,%.1f],\"ents\":[",
            s->entityCount, s->localPos.X, s->localPos.Z);
        j += b;
        bool first = true;
        int n = 0;
        for (auto& e : s->entities)
        {
            if (!e.alive) continue;
            if (++n > 128) break;
            if (!first) j += ",";
            first = false;
            snprintf(b, sizeof(b), "[%.1f,%.1f,%d,%d,%d,\"",
                e.rootPos.X - s->localPos.X, e.rootPos.Z - s->localPos.Z,
                e.hp, e.isTeam ? 1 : 0, e.isVisible ? 1 : 0);
            j += b;
            j += jsonEsc(e.nameLen > 0 ? e.name : "?");
            j += "\"]";
        }
        j += "]}";
        return j;
    }

    // ---------- /api/offsets + /api/offset (teste e edicao live) ----------
    static uint32_t* findOffset(const std::string& grp, const std::string& name)
    {
        if (grp == "draw" || grp.empty())
            for (auto& e : Cheat::DrawOffTable())
                if (name == e.first) return e.second;
        if (grp == "aim" || grp.empty())
            for (auto& e : Off::OffTable())
                if (name == e.name) return e.ptr;
        return nullptr;
    }

    static std::string apiOffsets()
    {
        char b[96];
        std::string j = "[";
        bool first = true;
        auto put = [&](const char* grp, const char* name, uint32_t v) {
            if (!first) j += ",";
            first = false;
            snprintf(b, sizeof(b), "{\"g\":\"%s\",\"n\":\"%s\",\"v\":\"0x%X\",\"ok\":%s}",
                grp, name, v, v ? "true" : "false");
            j += b;
        };
        for (auto& e : Cheat::DrawOffTable()) put("draw", e.first, *e.second);
        for (auto& e : Off::OffTable()) put("aim", e.name, *e.ptr);
        return j + "]";
    }

    static std::string apiOffset(const std::map<std::string, std::string>& q)
    {
        auto ia = q.find("action");
        std::string a = ia == q.end() ? "" : ia->second;
        if (a == "test")
        {
            // Teste ponta-a-ponta da cadeia: snapshot valido, localplayer,
            // entidades e viewmatrix preenchidos = offsets do Draw batendo.
            const auto* s = Cheat::Shared::Producer::Get().Latest();
            bool valid = s && s->valid;
            bool local = valid && s->localPlayer != 0;
            unsigned ents = valid ? s->entityCount : 0;
            bool vm = false;
            if (valid)
                for (int i = 0; i < 4 && !vm; i++)
                    for (int k = 0; k < 4 && !vm; k++)
                        if (s->viewMatrix.m[i][k] != 0.f) vm = true;
            int phase = (int)CheatStatus::GetPhase();
            bool ok = (phase == 3) && valid && local && ents > 0 && vm;
            char b[256];
            snprintf(b, sizeof(b),
                "{\"ok\":%s,\"phase\":%d,\"valid\":%s,\"local\":%s,\"entities\":%u,\"viewmatrix\":%s}",
                ok ? "true" : "false", phase, valid ? "true" : "false",
                local ? "true" : "false", ents, vm ? "true" : "false");
            return b;
        }
        if (a == "set")
        {
            auto ig = q.find("g");
            auto in = q.find("name");
            auto iv = q.find("value");
            if (in == q.end() || iv == q.end()) return "{\"ok\":false,\"err\":\"g+name+value\"}";
            uint32_t* p = findOffset(ig == q.end() ? "" : ig->second, in->second);
            if (!p) return "{\"ok\":false,\"err\":\"unknown\"}";
            *p = (uint32_t)strtoul(iv->second.c_str(), nullptr, 0);
            char b[64];
            snprintf(b, sizeof(b), "{\"ok\":true,\"readback\":\"0x%X\"}", *p);
            return b;
        }
        return "{\"ok\":false,\"err\":\"action\"}";
    }

    // ---------- pagina interna (quando <root> nao tem index.html) ----------
    static const char* kFallback =
        "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>XISFPS Web</title><style>"
        ":root{--bg:#08080a;--panel:#101014;--panel2:#141419;--line:#1f1f26;--line2:#2b2b33;--txt:#e8e8ea;--mut:#8a8a93;--dim:#55555e;--red:#e11;--red2:#a00;--green:#3dff73}"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{background:var(--bg);color:var(--txt);font-family:-apple-system,'Segoe UI',Inter,Roboto,Arial,sans-serif;font-size:14px;display:flex;min-height:100vh;letter-spacing:.2px}"
        "#side{width:200px;background:#0a0a0d;border-right:1px solid var(--line);padding:18px 12px;position:sticky;top:0;height:100vh;flex-shrink:0;display:flex;flex-direction:column}"
        ".logo{font-size:24px;font-weight:800;color:var(--red);letter-spacing:3px;padding:0 10px;text-shadow:0 0 22px rgba(238,17,17,.4)}"
        ".logo span{display:block;font-size:10px;letter-spacing:3px;color:var(--dim);font-weight:400;margin:2px 0 18px}"
        "#tabs{display:flex;flex-direction:column;gap:2px;flex:1}"
        "#tabs button{display:flex;align-items:center;gap:10px;background:none;border:0;color:var(--mut);padding:11px 12px;border-radius:9px;font-size:14px;cursor:pointer;transition:all .15s;border-left:2px solid transparent;text-align:left;width:100%}"
        "#tabs button:hover{background:#15151b;color:#fff}"
        "#tabs button.act{background:#1a0d0d;color:#fff;border-left-color:var(--red);box-shadow:inset 0 0 20px rgba(238,17,17,.07)}"
        "#tabs button .dot{width:7px;height:7px;border-radius:50%;background:#333;flex-shrink:0}"
        "#tabs button.act .dot{background:var(--red);box-shadow:0 0 8px var(--red)}"
        "#conn{display:flex;align-items:center;gap:8px;padding:10px;font-size:12px;color:var(--mut);border-top:1px solid var(--line)}"
        "#conn i{width:8px;height:8px;border-radius:50%;background:#ff2b2b;flex-shrink:0}"
        "#conn.on i{background:var(--green);box-shadow:0 0 8px rgba(61,255,115,.7)}"
        "main{flex:1;padding:0 26px 50px;min-width:0;max-width:1100px}"
        "#top{display:flex;align-items:center;justify-content:space-between;padding:18px 0;margin-bottom:18px;border-bottom:1px solid var(--line);position:sticky;top:0;background:rgba(8,8,10,.92);backdrop-filter:blur(8px);z-index:5}"
        "#crumb{font-size:18px;font-weight:700}"
        "#crumb small{display:block;font-size:11px;color:var(--dim);font-weight:400;margin-top:2px}"
        "#pill{font-size:12px;color:var(--mut);background:var(--panel);border:1px solid var(--line);border-radius:999px;padding:7px 16px;white-space:nowrap}"
        ".page{display:none}"
        ".page.act{display:block;animation:fadeUp .25s ease}"
        "@keyframes fadeUp{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}"
        ".grp{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:16px 18px;margin-bottom:16px;transition:border-color .2s}"
        ".grp:hover{border-color:#2a2a33}"
        ".grp h3{font-size:11px;letter-spacing:2px;color:var(--red);text-transform:uppercase;margin-bottom:8px;font-weight:700}"
        ".row{display:flex;align-items:center;gap:12px;padding:9px 0;border-bottom:1px solid #16161b;font-size:14px}"
        ".row:last-child{border:0}"
        ".row label{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#cfcfd4}"
        ".sw{position:relative;width:52px;height:28px;flex-shrink:0;cursor:pointer}"
        ".sw input{display:none}"
        ".sw .tr{position:absolute;inset:0;background:#1c1c22;border:1px solid #2e2e37;border-radius:14px;transition:all .2s}"
        ".sw .kn{position:absolute;top:2px;left:2px;width:22px;height:22px;border-radius:50%;background:linear-gradient(180deg,#5a5a63,#333338);transition:left .22s cubic-bezier(.3,1.4,.5,1),background .2s,box-shadow .2s}"
        ".sw:hover .tr{border-color:var(--red2)}"
        ".sw input:checked+.tr{background:linear-gradient(180deg,#d00,#900);border-color:#f22;box-shadow:0 0 12px rgba(238,17,17,.5)}"
        ".sw input:checked+.tr .kn{left:26px;background:linear-gradient(180deg,#fff,#cfcfcf);box-shadow:0 2px 6px rgba(0,0,0,.5)}"
        ".swst{font-size:11px;font-weight:800;min-width:30px;color:#55555e;letter-spacing:1px}"
        ".swst.on{color:#ff4d4d;text-shadow:0 0 8px rgba(255,43,43,.6)}"
        ".row input[type=range]{flex:1;-webkit-appearance:none;appearance:none;background:transparent;cursor:pointer;height:22px;--p:50%}"
        ".row input[type=range]::-webkit-slider-runnable-track{height:4px;border-radius:2px;background:linear-gradient(90deg,var(--red) var(--p),#2b2b33 var(--p))}"
        ".row input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:20px;border-radius:4px;background:linear-gradient(180deg,#f22,#a00);margin-top:-8px;box-shadow:0 0 8px rgba(238,17,17,.55);transition:transform .12s}"
        ".row input[type=range]::-webkit-slider-thumb:hover{transform:scale(1.15)}"
        ".row input[type=range]::-moz-range-track{height:4px;border-radius:2px;background:#2b2b33}"
        ".row input[type=range]::-moz-range-progress{height:4px;border-radius:2px;background:var(--red)}"
        ".row input[type=range]::-moz-range-thumb{width:14px;height:20px;border:0;border-radius:4px;background:linear-gradient(180deg,#f22,#a00)}"
        ".val{min-width:58px;text-align:right;color:#fff;font-size:13px;font-variant-numeric:tabular-nums}"
        ".row select{background:#000;color:#fff;border:1px solid var(--line2);border-radius:8px;padding:8px 10px;max-width:230px;cursor:pointer}"
        ".row select:focus,.txtin:focus{border-color:var(--red);outline:none}"
        ".row input[type=color]{width:46px;height:32px;border:1px solid var(--line2);background:#000;padding:2px;border-radius:8px;cursor:pointer}"
        ".row input[type=number],.txtin{background:#000;color:#fff;border:1px solid var(--line2);border-radius:8px;padding:8px 10px}"
        ".abtn{background:linear-gradient(180deg,#c00,#900);color:#fff;border:0;border-radius:8px;padding:9px 18px;cursor:pointer;font-weight:700;transition:transform .12s,box-shadow .12s}"
        ".abtn:hover{transform:translateY(-1px);box-shadow:0 4px 16px rgba(238,17,17,.4)}"
        ".abtn:active{transform:none}"
        ".gbtn{background:#17171c;color:#ccc;border:1px solid var(--line2);border-radius:8px;padding:9px 18px;cursor:pointer;transition:all .15s}"
        ".gbtn:hover{border-color:var(--red);color:#fff}"
        "#cats{display:flex;gap:8px;margin-bottom:14px;flex-wrap:wrap}"
        "#cats button{background:var(--panel);color:var(--mut);border:1px solid var(--line);border-radius:9px;padding:8px 16px;cursor:pointer;transition:all .15s}"
        "#cats button:hover{color:#fff;border-color:var(--red)}"
        "#cats button.act{background:linear-gradient(180deg,#c00,#900);color:#fff;border-color:transparent;box-shadow:0 0 14px rgba(238,17,17,.4)}"
        "#tools{display:flex;gap:10px;margin-bottom:14px}"
        "#tools input,#tools select{flex:1;background:#000;color:#fff;border:1px solid var(--line2);border-radius:9px;padding:10px 12px}"
        "#tools input:focus,#tools select:focus{border-color:var(--red);outline:none}"
        "#tools select{flex:0 0 140px}"
        "#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:12px}"
        ".card{background:var(--panel);border:1px solid var(--line);border-top:3px solid #555;border-radius:10px;padding:12px;font-size:13px;transition:transform .15s,box-shadow .15s,border-color .15s}"
        ".card:hover{transform:translateY(-2px);box-shadow:0 10px 24px rgba(0,0,0,.55);border-color:#3a3a44}"
        ".card .av{width:46px;height:46px;border-radius:50%;background:linear-gradient(145deg,#232329,#121215);border:2px solid #555;display:flex;align-items:center;justify-content:center;font-size:20px;font-weight:800;color:#fff;margin-bottom:10px}"
        ".card .nm{font-weight:700;margin-bottom:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        ".card .id{color:var(--dim);font-size:11px;margin-bottom:10px;font-variant-numeric:tabular-nums}"
        ".card button{width:100%;border:1px solid var(--line2);border-radius:8px;padding:8px;cursor:pointer;background:#1a1a20;color:#999;transition:all .15s}"
        ".card button:hover{border-color:var(--red);color:#fff}"
        ".card.eq button{background:linear-gradient(180deg,#c00,#900);color:#fff;border-color:transparent;font-weight:700}"
        ".orow{display:flex;align-items:center;gap:12px;padding:7px 0;border-bottom:1px solid #16161b;font-size:13px}"
        ".orow:last-child{border:0}"
        ".odot{width:9px;height:9px;border-radius:50%;flex-shrink:0}"
        ".odot.ok{background:var(--green);box-shadow:0 0 6px rgba(61,255,115,.6)}"
        ".odot.bad{background:#ff2b2b;box-shadow:0 0 6px rgba(255,43,43,.6)}"
        ".onm{flex:1;font-family:Consolas,Menlo,monospace;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#cfcfd4}"
        ".ovl{font-family:Consolas,Menlo,monospace;color:#fff;min-width:90px;text-align:right}"
        ".cfg{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin-bottom:10px;display:flex;align-items:center;gap:12px}"
        ".cfg b{flex:1}.cfg small{color:var(--dim);display:block;font-weight:400;font-size:11px;margin-top:2px}"
        "#radar{width:100%;max-width:540px;background:#0b0b0e;border:1px solid var(--line);border-radius:12px}"
        ".hint{color:var(--dim);font-size:12px;margin-top:10px}"
        "#toasts{position:fixed;right:18px;bottom:18px;display:flex;flex-direction:column;gap:8px;z-index:99}"
        ".toast{background:#141419;border:1px solid var(--line2);border-left:3px solid var(--red);border-radius:10px;padding:11px 16px;font-size:13px;animation:tin .25s ease;box-shadow:0 8px 24px rgba(0,0,0,.5);max-width:320px}"
        ".toast.ok{border-left-color:var(--green)}"
        "@keyframes tin{from{opacity:0;transform:translateX(20px)}to{opacity:1;transform:none}}"
        ".toast.out{opacity:0;transform:translateX(20px);transition:all .3s}"
        ".skel{border-radius:8px;background:linear-gradient(90deg,#141419 25%,#1c1c22 50%,#141419 75%);background-size:200% 100%;animation:sh 1.2s infinite}"
        "@keyframes sh{from{background-position:200% 0}to{background-position:-200% 0}}"
        "::-webkit-scrollbar{width:10px;height:10px}"
        "::-webkit-scrollbar-track{background:var(--bg)}"
        "::-webkit-scrollbar-thumb{background:#26262e;border-radius:5px}"
        "::-webkit-scrollbar-thumb:hover{background:var(--red2)}"
        "::selection{background:var(--red2);color:#fff}"
        "@media(max-width:780px){body{flex-direction:column}#side{width:100%;height:auto;position:static;border-right:0;border-bottom:1px solid var(--line)}#tabs{flex-direction:row;overflow-x:auto}#tabs button{white-space:nowrap}main{padding:0 14px 40px}}"
        "</style></head><body>"
        "<aside id=side><div class=logo>XISFPS<span>web panel</span></div><nav id=tabs></nav><div id=conn><i></i><b>connecting…</b></div></aside>"
        "<main><header id=top><div id=crumb>Combat<small>aim settings</small></div><div id=pill>…</div></header><div id=pages></div></main>"
        "<div id=toasts></div>"
        "<script>"
        "'use strict';"
        "async function api(path,ms){"
        "  const c=new AbortController();const t=setTimeout(()=>c.abort(),ms||8000);"
        "  try{const r=await fetch(path,{signal:c.signal});clearTimeout(t);if(!r.ok)return null;return await r.json()}"
        "  catch(e){clearTimeout(t);return null}"
        "}"
        "function toast(msg,ok){"
        "  const box=document.getElementById('toasts');if(!box)return;"
        "  const d=document.createElement('div');d.className='toast'+(ok?' ok':'');d.textContent=msg;"
        "  box.appendChild(d);while(box.children.length>4)box.removeChild(box.firstChild);"
        "  setTimeout(()=>{d.classList.add('out');setTimeout(()=>d.remove(),350)},3200);"
        "}"
        "function setConn(on,txt){"
        "  const c=document.getElementById('conn');if(!c)return;"
        "  c.classList.toggle('on',!!on);const b=c.querySelector('b');if(b)b.textContent=txt||(on?'connected':'offline');"
        "}"
        "const TABS=[['combat','Combat','aim settings'],['visuals','Visuals','esp settings'],['colors','Colors','esp style'],['exploits','Exploits','mods'],['radar','Radar','live map'],['offsets','Offsets','memory'],['skins','Skins','changer'],['cloud','Cloud','profiles'],['config','Config','system']];"
        "const GROUPS={"
        "combat:[['Aim Assistance',['Enable Aim','Aimbot Scope','Left Shoulder','Right Shoulder','Aim Delay','Precision']],['Silent Aim',['Aimbot Memory','Show FOV##mem','Memory FOV','Silent Aim','Visible Check##silent','Show FOV##silent','Silent Target','Silent FOV','AimLock']]],"
        "visuals:[['ESP',['Enable Box','Enable Skeleton','Enable Line','Enable Health','Enable Distance','Enable Name','Enable Weapon','Visible Check','TeamCheck','Stream Mode']],['Style',['Box Style','Health Position','Distance Position','Line Position','Name Position','Name Style','Weapon Position','Weapon Style','Esp Render']]],"
        "colors:[['ESP Colors',['Box Color','Line Color','Skeleton Color','Distance Color','Name Color','Weapon Color','VisibleCheck Color']],['Sizes',['Name Size','Distance Size','Weapon Size','Box Thickness','Line Thickness','Skeleton Thickness']]],"
        "exploits:[['Buffs',['Alok Buff','Infinite Buff','Alok Level','Atributo de Arma','GhostMode','SpinBot','Spin Speed','Maxim Skill']],['Combat Mods',['Pixel Bug','Fast Medkit','More Damage','Fire Delay','Soco Longe','No Reload','Infinite Ammo','No Recoil','Speed Hack','Speed Scale']]],"
        "config:[['Backend',['Overlay Type','StreamMode##cfg']]]};"
        "const RCOL=['#999','#70dc60','#52aef0','#b278f0','#ffa038','#ff7b1c','#ff2b2b'];"
        "const RNAMES=['Common','Uncommon','Rare','Epic','Legendary','Mythic','Special'];"
        "const CATS=[[4,'Hat'],[5,'Facepaint'],[6,'Mask'],[1,'Jacket'],[2,'Pants'],[3,'Shoes']];"
        "let META={},STATE={},CUR='combat',SKINS=null,EQ={},SCAT=4,RRANGE=150;"
        "const disp=k=>String(k).split('##')[0];"
        "function el(tag,cls,txt){const e=document.createElement(tag);if(cls)e.className=cls;if(txt!==undefined)e.textContent=txt;return el_fix(e)}"
        "function el_fix(e){return e}"
        "async function setVal(key,val){"
        "  const r=await api('/api/set?key='+encodeURIComponent(key)+'&value='+encodeURIComponent(val));"
        "  if(!r||!r.ok&&r!==null&&r.ok!==undefined){/*noop*/}"
        "  if(r===null){toast('sem resposta do cheat');return}"
        "  if(r.ok===false){toast('falhou: '+(r.err||'?'));return}"
        "  STATE=await api('/api/state')||STATE;paintState();"
        "}"
        "function ctl(key){"
        "  const m=META[key];const host=document.createElement('div');host.className='row';host.dataset.k=key;"
        "  if(!m||STATE[key]===undefined){host.style.display='none';return host}"
        "  const v=STATE[key];"
        "  host.appendChild(el('label','',disp(key)));"
        "  if(m.t==='b'){"
        "    const l=el('label','sw');const o=document.createElement('input');o.type='checkbox';o.checked=!!v;"
        "    o.onchange=()=>setVal(key,o.checked?1:0);"
        "    const t=el('span','tr');t.appendChild(el('span','kn'));l.appendChild(o);l.appendChild(t);host.appendChild(l);"
        "    host.appendChild(el('span','swst'+(v?' on':''),v?'ON':'OFF'));"
        "  }else if(m.t==='i'||m.t==='f'){"
        "    const r=document.createElement('input');r.type='range';r.step=m.t==='i'?1:0.1;r.min=m.min;r.max=m.max;r.value=v;"
        "    const pf=()=>r.style.setProperty('--p',((r.value-r.min)/((r.max-r.min)||1)*100)+'%');pf();"
        "    const s=el('span','val',String(v));"
        "    r.oninput=()=>{s.textContent=r.value;pf()};r.onchange=()=>setVal(key,r.value);"
        "    host.appendChild(r);host.appendChild(s);"
        "  }else if(m.t==='d'){"
        "    const s=document.createElement('select');"
        "    (m.items||[]).forEach((t,i)=>{const o=document.createElement('option');o.value=i;o.textContent=t;if(i===v)o.selected=true;s.appendChild(o)});"
        "    s.onchange=()=>setVal(key,s.value);host.appendChild(s);"
        "  }else if(m.t==='c'&&Array.isArray(v)){"
        "    const c=document.createElement('input');c.type='color';"
        "    const hx=[0,1,2].map(i=>Math.round(Math.min(1,Math.max(0(+v[i]||0)))*255).toString(16).padStart(2,'0')).join('');"
        "    c.value='#'+hx;"
        "    const a=document.createElement('input');a.type='number';a.min=0;a.max=1;a.step=0.01;a.value=(+v[3]||0).toFixed(2);a.style.width='64px';"
        "    const go=()=>{const n=parseInt(c.value.slice(1),16);setVal(key,((n>>16)&255)/255+','+((n>>8)&255)/255+','+(n&255)/255+','+a.value)};"
        "    c.onchange=go;a.onchange=go;host.appendChild(c);host.appendChild(a);"
        "  }"
        "  return host;"
        "}"
        "function buildTabs(){"
        "  const t=document.getElementById('tabs');t.innerHTML='';"
        "  TABS.forEach(x=>{"
        "    const b=document.createElement('button');b.dataset.t=x[0];"
        "    const d=document.createElement('span');d.className='dot';b.appendChild(d);"
        "    b.appendChild(document.createTextNode(x[1]));b.onclick=()=>show(x[0]);t.appendChild(b);"
        "  });"
        "}"
        "function pageEl(id){"
        "  let p=document.getElementById('pg-'+id);"
        "  if(p)return p;"
        "  p=document.createElement('div');p.className='page';p.id='pg-'+id;"
        "  document.getElementById('pages').appendChild(p);return p;"
        "}"
        "function show(tab){"
        "  CUR=tab;"
        "  document.querySelectorAll('#tabs button').forEach(b=>b.classList.toggle('act',b.dataset.t===tab));"
        "  const meta=TABS.find(x=>x[0]===tab);"
        "  const cr=document.getElementById('crumb');"
        "  if(cr&&meta){cr.childNodes[0].textContent=meta[1];const sm=cr.querySelector('small');if(sm)sm.textContent=meta[2]}"
        "  document.querySelectorAll('#pages .page').forEach(p=>p.classList.remove('act'));"
        "  const pg=pageEl(tab);pg.classList.add('act');"
        "  if(!pg.dataset.built){pg.dataset.built='1';buildPage(tab,pg)}"
        "  else if(tab==='cloud')buildCloud(pg,true);"
        "}"
        "function buildPage(tab,pg){"
        "  if(tab==='skins')return buildSkins(pg);"
        "  if(tab==='cloud')return buildCloud(pg,false);"
        "  if(tab==='radar')return buildRadar(pg);"
        "  if(tab==='offsets')return buildOffsets(pg);"
        "  (GROUPS[tab]||[]).forEach(g=>{"
        "    const d=el('div','grp');d.appendChild(el('h3','',g[0]));"
        "    g[1].forEach(k=>d.appendChild(ctl(k)));pg.appendChild(d);"
        "  });"
        "}"
        "function paintState(){"
        "  document.querySelectorAll('#pages .page.act [data-k]').forEach(w=>{"
        "    const k=w.dataset.k,v=STATE[k];if(v===undefined)return;const m=META[k];if(!m)return;"
        "    if(m.t==='b'){const o=w.querySelector('input[type=checkbox]');if(o)o.checked=!!v;const t=w.querySelector('.swst');if(t){t.textContent=v?'ON':'OFF';t.classList.toggle('on',!!v)}}"
        "    else if(m.t==='i'||m.t==='f'){const r=w.querySelector('input[type=range]');const s=w.querySelector('.val');"
        "      if(r&&document.activeElement!==r){r.value=v;r.style.setProperty('--p',((v-m.min)/((m.max-m.min)||1)*100)+'%')}"
        "      if(s)s.textContent=v}"
        "    else if(m.t==='d'){const s=w.querySelector('select');if(s)s.value=v}"
        "  });"
        "}"
        "/* radar */"
        "function buildRadar(pg){"
        "  const bar=el('div');bar.id='cats';"
        "  [[50,'50m'],[100,'100m'],[150,'150m'],[300,'300m']].forEach(z=>{"
        "    const b=el('button','',z[1]);if(z[0]===RRANGE)b.classList.add('act');"
        "    b.onclick=()=>{RRANGE=z[0];bar.querySelectorAll('button').forEach(x=>x.classList.remove('act'));b.classList.add('act');paintRadar()};"
        "    bar.appendChild(b)});"
        "  pg.appendChild(bar);"
        "  const cv=document.createElement('canvas');cv.id='radar';cv.width=520;cv.height=520;"
        "  pg.appendChild(cv);"
        "  pg.appendChild(el('div','hint','vermelho = inimigo (claro = visivel) | verde = time | triangulo = voce (norte pra cima)'));"
        "  paintRadar();"
        "}"
        "async function paintRadar(){"
        "  const cv=document.getElementById('radar');if(!cv||CUR!=='radar')return;"
        "  const d=await api('/api/radar');if(!d)return;"
        "  const c=cv.getContext('2d'),W=cv.width,R=W/2;c.clearRect(0,0,W,W);"
        "  c.strokeStyle='#232329';c.lineWidth=1;"
        "  [0.33,0.66,1].forEach(f=>{c.beginPath();c.arc(R,R,R*f,0,7);c.stroke()});"
        "  c.beginPath();c.moveTo(R,0);c.lineTo(R,W);c.moveTo(0,R);c.lineTo(W,R);c.stroke();"
        "  c.fillStyle='#666';c.font='12px Arial';c.fillText('N',R-4,14);"
        "  if(!d.ok){c.fillStyle='#888';c.fillText('sem partida',R-34,R);return}"
        "  const k=R/RRANGE;"
        "  (d.ents||[]).forEach(e=>{"
        "    let x=e[0]*k,z=e[1]*k;const m=Math.sqrt(x*x+z*z);"
        "    if(m>R-6){x*=(R-6)/m;z*=(R-6)/m}"
        "    const team=e[3]===1;"
        "    if(!team&&e[4]===1){c.shadowColor='#ff2b2b';c.shadowBlur=9}else{c.shadowBlur=0}"
        "    c.fillStyle=team?'#3dff73':(e[4]===1?'#ff2b2b':'#7a1f1f');"
        "    c.beginPath();c.arc(R+x,R+z,5,0,7);c.fill();"
        "    if(e[5]&&Math.sqrt(e[0]*e[0]+e[1]*e[1])<60){c.shadowBlur=0;c.fillStyle='#bbb';c.font='11px Arial';c.fillText(e[5],R+x+8,R+z+4)}"
        "  });"
        "  c.shadowBlur=0;c.fillStyle='#fff';c.beginPath();c.moveTo(R,R-8);c.lineTo(R-6,R+6);c.lineTo(R+6,R+6);c.closePath();c.fill();"
        "  c.fillStyle='#888';c.font='12px Arial';c.fillText(d.n+' players',8,W-10);"
        "}"
        "/* skins */"
        "async function buildSkins(pg){"
        "  const bar=el('div');bar.id='cats';"
        "  CATS.forEach(c=>{"
        "    const b=el('button','',c[1]);if(c[0]===SCAT)b.classList.add('act');"
        "    b.onclick=()=>{SCAT=c[0];bar.querySelectorAll('button').forEach(x=>x.classList.remove('act'));b.classList.add('act');paintGrid()};"
        "    bar.appendChild(b)});"
        "  pg.appendChild(bar);"
        "  const tools=el('div');tools.id='tools';"
        "  const q=document.createElement('input');q.placeholder='Search skin by name or ID';q.id='sq';"
        "  const rs=document.createElement('select');rs.id='rs';"
        "  [['-1','All rarities'],['0','Common'],['1','Uncommon'],['2','Rare'],['3','Epic'],['4','Legendary'],['5','Mythic'],['6','Special']].forEach(o=>{const e=document.createElement('option');e.value=o[0];e.textContent=o[1];rs.appendChild(e)});"
        "  let deb=null;"
        "  q.oninput=()=>{clearTimeout(deb);deb=setTimeout(paintGrid,180)};rs.onchange=paintGrid;"
        "  tools.appendChild(q);tools.appendChild(rs);pg.appendChild(tools);"
        "  const gr=el('div');gr.id='grid';pg.appendChild(gr);"
        "  gr.innerHTML='<div class=\"skel\" style=\"height:120px\"></div><div class=\"skel\" style=\"height:120px\"></div><div class=\"skel\" style=\"height:120px\"></div><div class=\"skel\" style=\"height:120px\"></div>';"
        "  SKINS=await api('/api/skins');"
        "  if(!SKINS){gr.innerHTML='';gr.appendChild(el('div','hint','falha ao carregar skins'));return}"
        "  const eq=await api('/api/skin?action=state');EQ=eq||{};"
        "  paintGrid();"
        "}"
        "function paintGrid(){"
        "  const gr=document.getElementById('grid');if(!gr)return;gr.innerHTML='';"
        "  const qe=document.getElementById('sq'),re=document.getElementById('rs');"
        "  const q=qe?(qe.value||'').toLowerCase():'';const r=re?re.value:'-1';"
        "  const frag=document.createDocumentFragment();let n=0;"
        "  for(const s of SKINS){"
        "    if(s.c!==SCAT)continue;"
        "    if(r!=='-1'&&s.r!==+r)continue;"
        "    if(q&&String(s.n||'').toLowerCase().indexOf(q)<0&&String(s.id).indexOf(q)<0)continue;"
        "    if(++n>400)break;"
        "    const d=el('div','card');d.style.borderTopColor=RCOL[s.r]||'#555';"
        "    const eqd=EQ[String(SCAT)]===s.id;if(eqd)d.classList.add('eq');"
        "    const av=el('div','av',String(s.n||'?').charAt(0).toUpperCase());av.style.borderColor=RCOL[s.r]||'#555';"
        "    const nm=el('div','nm',s.n);nm.title=s.n;"
        "    const b=el('button','',eqd?'Equipped':'Equip Skin');"
        "    b.onclick=async()=>{"
        "      const rr=await api('/api/skin?action='+(eqd?'clear':'equip')+'&cat='+SCAT+'&id='+s.id);"
        "      if(!rr){toast('sem resposta');return}"
        "      if(rr.ok===false){toast('falhou');return}"
        "      const eq2=await api('/api/skin?action=state');EQ=eq2||{};"
        "      const on=EQ[String(SCAT)]===s.id;"
        "      d.classList.toggle('eq',on);b.textContent=on?'Equipped':'Equip Skin';"
        "      b.onclick=null;toast(on?'skin equipada':'skin removida',true);"
        "      paintGrid();"
        "    };"
        "    d.appendChild(av);d.appendChild(nm);d.appendChild(el('div','id','#'+s.id));d.appendChild(b);frag.appendChild(d);"
        "  }"
        "  if(!n)frag.appendChild(el('div','hint','nenhuma skin encontrada'));"
        "  gr.appendChild(frag);"
        "}"
        "/* cloud */"
        "async function buildCloud(pg,refresh){"
        "  const list=await api('/api/configs');"
        "  pg.innerHTML='';"
        "  const d=el('div','grp');d.appendChild(el('h3','','Profiles'));"
        "  if(!list){d.appendChild(el('div','hint','falha ao carregar profiles'));pg.appendChild(d);return}"
        "  const fmt=t=>{const dt=new Date(t*1000);return isNaN(dt)?'-':dt.toLocaleString()};"
        "  list.forEach(c=>{"
        "    const r=el('div','cfg');"
        "    const b=el('b','',c.name);"
        "    b.appendChild(el('small','',('modificado '+(c.modified?fmt(c.modified):'-'))));"
        "    const lo=el('button','abtn','Load');"
        "    lo.onclick=async()=>{const rr=await api('/api/config?action=load&name='+encodeURIComponent(c.name));"
        "      if(!rr){toast('sem resposta');return}"
        "      if(rr.ok===false){toast('falhou o load');return}"
        "      STATE=await api('/api/state')||STATE;paintState();toast('profile carregado',true)};"
        "    const de=el('button','gbtn','Delete');"
        "    de.onclick=async()=>{if(!confirm('apagar '+c.name+'?'))return;"
        "      const rr=await api('/api/config?action=delete&name='+encodeURIComponent(c.name));"
        "      if(rr&&rr.ok!==false){toast('apagado',true);buildCloud(pg,true)}else toast('falhou')};"
        "    r.appendChild(b);r.appendChild(lo);r.appendChild(de);d.appendChild(r);"
        "  });"
        "  const sv=el('div','cfg');"
        "  const inp=document.createElement('input');inp.className='txtin';inp.placeholder='New config name';inp.style.flex='1';"
        "  const sb=el('button','abtn','Save');"
        "  sb.onclick=async()=>{if(!inp.value)return;"
        "    const rr=await api('/api/config?action=save&name='+encodeURIComponent(inp.value));"
        "    if(rr&&rr.ok!==false){toast('salvo',true);buildCloud(pg,true)}else toast('falhou')};"
        "  sv.appendChild(inp);sv.appendChild(sb);d.appendChild(sv);"
        "  pg.appendChild(d);"
        "}"
        "/* offsets */"
        "async function buildOffsets(pg){"
        "  const bar=el('div');bar.id='cats';"
        "  const tst=el('button','','Testar cadeia');tst.onclick=runTest;"
        "  const rel=el('button','','Reload');"
        "  rel.onclick=()=>{pg.dataset.built='';pg.innerHTML='';buildOffsets(pg)};"
        "  bar.appendChild(tst);bar.appendChild(rel);pg.appendChild(bar);"
        "  const bn=el('div');bn.id='obanner';pg.appendChild(bn);"
        "  const list=await api('/api/offsets');"
        "  if(!list){bn.className='';bn.textContent='';pg.appendChild(el('div','hint','falha ao carregar offsets'));return}"
        "  [['draw','Draw — cadeia do ESP'],['aim','Aim — offsets de escrita']].forEach(g=>{"
        "    const d=el('div','grp');d.appendChild(el('h3','',g[1]));"
        "    list.filter(o=>o.g===g[0]).forEach(o=>{"
        "      const r=el('div','orow');"
        "      const dot=el('span','odot '+(o.ok?'ok':'bad'));"
        "      const nm=el('span','onm',o.n);nm.title=o.n;"
        "      const vl=el('span','ovl',o.v);"
        "      const eb=el('button','gbtn','EDIT');"
        "      eb.onclick=()=>{"
        "        r.innerHTML='';"
        "        const i=document.createElement('input');i.className='txtin';i.value=o.v;i.style.flex='1';"
        "        const ok=el('button','abtn','OK');"
        "        const go=async()=>{"
        "          const rr=await api('/api/offset?action=set&g='+o.g+'&name='+encodeURIComponent(o.n)+'&value='+encodeURIComponent(i.value));"
        "          if(!rr){toast('sem resposta');return}"
        "          if(rr.ok)toast(o.n+' = '+rr.readback+' aplicado',true);else toast('falhou: '+(rr.err||'?'));"
        "          pg.dataset.built='';pg.innerHTML='';buildOffsets(pg);"
        "        };"
        "        ok.onclick=go;i.onkeydown=e=>{if(e.key==='Enter')go()};"
        "        r.appendChild(i);r.appendChild(ok);i.focus();i.select();"
        "      };"
        "      r.appendChild(dot);r.appendChild(nm);r.appendChild(vl);r.appendChild(eb);d.appendChild(r);"
        "    });"
        "    pg.appendChild(d);"
        "  });"
        "}"
        "function obanner(cls,txt){const b=document.getElementById('obanner');if(!b)return;b.className=cls?('banner '+cls):'';b.textContent=txt||''}"
        "async function runTest(){"
        "  obanner('','testando cadeia...');"
        "  const t=await api('/api/offset?action=test');"
        "  if(!t){obanner('bad','sem resposta do cheat');return}"
        "  if(t.ok){obanner('ok','tudo certo — fase running, '+t.entities+' entidades, viewmatrix ok')}"
        "  else{"
        "    const p=[];"
        "    if(t.phase!==3)p.push('fase='+t.phase+' (fora de partida)');"
        "    if(!t.valid)p.push('snapshot invalido');if(!t.local)p.push('localplayer zerado');"
        "    if(!t.entities)p.push('0 entidades');if(!t.viewmatrix)p.push('viewmatrix zerada');"
        "    obanner('bad','falha: '+p.join(' | '));"
        "  }"
        "}"
        "/* status */"
        "async function tick(){"
        "  const s=await api('/api/status');"
        "  const pill=document.getElementById('pill');"
        "  if(s){"
        "    setConn(true,'connected');"
        "    if(pill)pill.textContent='porta '+s.port+' | up '+s.up+'s | reqs '+s.hits+' | fase '+s.phase;"
        "    STATE=await api('/api/state')||STATE;paintState();"
        "    if(CUR==='radar')paintRadar();"
        "  }else{"
        "    setConn(false,'offline — cheat fechado?');"
        "    if(pill)pill.textContent='offline';"
        "  }"
        "}"
        "async function init(){"
        "  buildTabs();"
        "  META=await api('/api/meta')||{};"
        "  STATE=await api('/api/state')||{};"
        "  if(!Object.keys(META).length){toast('cheat nao respondeu — api offline')}"
        "  setConn(!!Object.keys(META).length);"
        "  show('combat');tick();setInterval(tick,2500);"
        "}"
        "init();"
        "</script></body></html>";

    // ---------- http ----------
    static void sendAll(SOCKET s, const char* p, size_t n)
    {
        while (n > 0)
        {
            int w = send(s, p, (int)(n > 65536 ? 65536 : n), 0);
            if (w <= 0) break;
            p += w; n -= w;
        }
    }

    static void serve(SOCKET c)
    {
        char buf[8192];
        int got = recv(c, buf, sizeof(buf) - 1, 0);
        if (got <= 0) return;
        buf[got] = 0;
        std::string req(buf, (size_t)got);
        auto eol = req.find("\r\n");
        std::istringstream first(req.substr(0, eol == std::string::npos ? req.size() : eol));
        std::string method, target, ver;
        first >> method >> target >> ver;
        s_hits.fetch_add(1);

        int code = 200;
        std::string mime = "text/plain; charset=utf-8";
        std::string bodyStr;
        std::vector<char> bodyBin;
        bool isBin = false;

        if (method != "GET") { code = 405; bodyStr = "method not allowed"; }
        else
        {
            std::string path = target, query;
            auto q = target.find('?');
            if (q != std::string::npos) { path = target.substr(0, q); query = target.substr(q + 1); }
            path = urlDecode(path);

            if (path == "/api/status")
            {
                mime = "application/json";
                unsigned long long up = (GetTickCount64() - s_startedTick.load()) / 1000;
                char b[256];
                snprintf(b, sizeof(b), "{\"running\":true,\"port\":%d,\"up\":%llu,\"hits\":%u,\"phase\":%d}",
                    s_port.load(), up, s_hits.load(), (int)CheatStatus::GetPhase());
                bodyStr = b;
            }
            else if (path == "/api/state")
            {
                mime = "application/json";
                bodyStr = apiState();
            }
            else if (path == "/api/set")
            {
                mime = "application/json";
                bodyStr = apiSet(parseQuery(query));
            }
            else if (path == "/api/meta")
            {
                mime = "application/json";
                bodyStr = apiMeta();
            }
            else if (path == "/api/skins")
            {
                mime = "application/json";
                auto qq = parseQuery(query);
                auto ic = qq.find("cat");
                bodyStr = apiSkins(ic == qq.end() ? -1 : atoi(ic->second.c_str()));
            }
            else if (path == "/api/skin")
            {
                mime = "application/json";
                bodyStr = apiSkin(parseQuery(query));
            }
            else if (path == "/api/configs")
            {
                mime = "application/json";
                bodyStr = apiConfigs();
            }
            else if (path == "/api/config")
            {
                mime = "application/json";
                bodyStr = apiConfig(parseQuery(query));
            }
            else if (path == "/api/radar")
            {
                mime = "application/json";
                bodyStr = apiRadar();
            }
            else if (path == "/api/offsets")
            {
                mime = "application/json";
                bodyStr = apiOffsets();
            }
            else if (path == "/api/offset")
            {
                mime = "application/json";
                bodyStr = apiOffset(parseQuery(query));
            }
            else
            {
                if (path.empty() || path.back() == '/') path += "index.html";
                // anti traversal
                if (path.find("..") != std::string::npos) { code = 403; bodyStr = "forbidden"; }
                else
                {
                    std::string full = s_root + "\\" + path.substr(1);
                    for (auto& ch : full) if (ch == '/') ch = '\\';
                    if (readFile(full, bodyBin))
                    {
                        mime = mimeFor(full);
                        isBin = true;
                    }
                    else if (path == "/index.html")
                    {
                        mime = "text/html; charset=utf-8";
                        bodyStr = kFallback;
                    }
                    else { code = 404; bodyStr = "not found"; }
                }
            }
        }

        const char* reason = code == 200 ? "OK" : code == 404 ? "Not Found" :
            code == 403 ? "Forbidden" : code == 405 ? "Method Not Allowed" : "Error";
        size_t len = isBin ? bodyBin.size() : bodyStr.size();
        char head[512];
        int hn = snprintf(head, sizeof(head),
            "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
            code, reason, mime.c_str(), (unsigned long long)len);
        sendAll(c, head, (size_t)hn);
        if (isBin && !bodyBin.empty()) sendAll(c, bodyBin.data(), bodyBin.size());
        else if (!isBin && !bodyStr.empty()) sendAll(c, bodyStr.data(), bodyStr.size());
    }

    static void loop()
    {
        while (s_running.load())
        {
            fd_set f;
            FD_ZERO(&f);
            FD_SET(s_listen, &f);
            timeval tv{ 0, 100000 };
            int r = select(0, &f, nullptr, nullptr, &tv);
            if (!s_running.load()) break;
            if (r > 0 && FD_ISSET(s_listen, &f))
            {
                SOCKET c = accept(s_listen, nullptr, nullptr);
                if (c != INVALID_SOCKET)
                {
                    DWORD to = 8000;
                    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
                    serve(c);
                    closesocket(c);
                }
            }
        }
    }

    bool Start(int port, const std::string& root)
    {
        if (s_running.load()) { setErr("ja rodando"); return false; }
        if (port < 1 || port > 65535) { setErr("porta invalida"); return false; }

        if (!s_wsa)
        {
            WSADATA wd;
            if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { setErr("WSAStartup falhou"); return false; }
            s_wsa = true;
        }

        SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (l == INVALID_SOCKET) { setErr("socket falhou"); return false; }
        BOOL re = TRUE;
        setsockopt(l, SOL_SOCKET, SO_REUSEADDR, (const char*)&re, sizeof(re));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((u_short)port);
        if (bind(l, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR)
        {
            char b[128];
            snprintf(b, sizeof(b), "bind :%d falhou (%d) — porta em uso?", port, WSAGetLastError());
            setErr(b);
            closesocket(l);
            return false;
        }
        if (listen(l, 8) == SOCKET_ERROR)
        {
            setErr("listen falhou");
            closesocket(l);
            return false;
        }

        s_root = root;
        s_listen = l;
        s_port.store(port);
        s_hits.store(0);
        s_startedTick.store(GetTickCount64());
        s_running.store(true);
        setErr("");
        s_thread = std::thread(loop);
        return true;
    }

    void Stop()
    {
        if (!s_running.exchange(false)) return;
        if (s_listen != INVALID_SOCKET) { closesocket(s_listen); s_listen = INVALID_SOCKET; }
        if (s_thread.joinable()) s_thread.join();
        s_port.store(0);
    }
}
