#include "EspFont.hpp"
#include "VerdanaFont.hpp"
#include "WeaponIconFont.hpp"

namespace EspFont {
    ImFont* Verdana = nullptr;

    void Initialize() {
        if (Verdana != nullptr) return;

        ImGuiIO& io = ImGui::GetIO();

        // Glyph ranges expandidos pra cobrir nicks comuns no Free Fire:
        // - Latin-1 + Extended (pt-BR/ES/FR: ã, ç, é, ñ...)
        // - IPA Extensions / Spacing Modifiers (símbolos usados em nicks)
        // - Greek (jogadores gregos + símbolos matemáticos)
        // - Cyrillic (RU/UA/etc)
        // - General Punctuation (setinhas, hífens fantasia, ★, etc)
        // - Superscripts/Subscripts + Currency + Letterlike (▲★✰⚡)
        // - Arrows + Math Ops + Miscellaneous Symbols
        // - Vietnamese Extended (VN players usam Latin+combining marks)
        //
        // NÃO inclui: CJK (chinês/japonês/coreano) — atlas ficaria 5+ MB
        // extra. Nicks CJK renderizam via FallbackChar (?) sem quebrar layout.
        static const ImWchar textRanges[] = {
            0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement
            0x0100, 0x024F,   // Latin Extended-A + B
            0x0250, 0x02AF,   // IPA Extensions
            0x02B0, 0x02FF,   // Spacing Modifier Letters
            0x0370, 0x03FF,   // Greek + Coptic
            0x0400, 0x04FF,   // Cyrillic
            0x0500, 0x052F,   // Cyrillic Supplement
            0x1E00, 0x1EFF,   // Latin Extended Additional (Vietnamese)
            0x2000, 0x206F,   // General Punctuation (★, hífens fantasia)
            0x2070, 0x209F,   // Superscripts and Subscripts
            0x20A0, 0x20CF,   // Currency Symbols
            0x2100, 0x214F,   // Letterlike Symbols (™, ℠, №)
            0x2190, 0x21FF,   // Arrows
            0x2200, 0x22FF,   // Mathematical Operators
            0x2500, 0x257F,   // Box Drawing (moldura em nicks)
            0x2580, 0x259F,   // Block Elements
            0x25A0, 0x25FF,   // Geometric Shapes (◆●■▲)
            0x2600, 0x26FF,   // Miscellaneous Symbols (☆★☠♠♥)
            0x2700, 0x27BF,   // Dingbats (✓✗✦✧)
            0
        };

        ImFontConfig cfg{};
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH  = true;

        Verdana = io.Fonts->AddFontFromMemoryCompressedTTF(
            verdana_compressed_data, verdana_compressed_size,
            16.0f, &cfg, textRanges);

        // Fallback char explícito — qualquer glyph fora dos ranges acima
        // (CJK, emoji, símbolos raros) renderiza como '?' em vez de espaço
        // vazio ou glyph corrompido. Também blindar contra ImGui version
        // que talvez não seteie fallback por default.
        if ( Verdana ) {
            Verdana->FallbackChar = (ImWchar)'?';
        }

        // Merge weapon icons (U+E000..U+E204) INTO the Verdana atlas so a single
        // AddText call renders both regular text and weapon icons transparently.
        // BAKED SIZE = 48px so that when the user cranks Weapon Size up to 60px
        // the glyphs downscale/upscale cleanly instead of turning into a
        // mush. At bake=16 the icon was pixelated any time it rendered above
        // ~24px because ImGui had to upscale by ≥1.5x.
        static const ImWchar iconRanges[] = { 0xE000, 0xE204, 0 };
        ImFontConfig iconCfg{};
        iconCfg.MergeMode   = true;
        iconCfg.PixelSnapH  = true;
        iconCfg.OversampleH = 2;
        iconCfg.OversampleV = 1;
        iconCfg.GlyphMinAdvanceX = 14.0f;
        iconCfg.GlyphOffset      = ImVec2(0.0f, 2.0f);

        io.Fonts->AddFontFromMemoryCompressedTTF(
            weapon_compressed_data, weapon_compressed_size,
            48.0f, &iconCfg, iconRanges);
    }

    void Reset() {
        // Verdana points into an ImGui atlas that will be destroyed with the
        // context on shutdown. Clearing it forces Initialize() to rebuild the
        // atlas fonts against the fresh context on the next emulator cycle.
        Verdana = nullptr;
    }
}
