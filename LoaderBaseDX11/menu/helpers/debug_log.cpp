// imgui_internal.h uses ImVec2 operator overloads gated by this macro;
// precisa vir ANTES de qualquer include indireto de imgui_internal.h.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "debug_log.h"
#include "../headers/includes.h"
#include "../headers/widgets.h"
#include "../headers/functions.h"
#include <chrono>
#include <cstdio>

// Fmtd "HH:MM:SS" para o header de cada linha do log.
static std::string now_hhmmss()
{
    const auto  now  = std::chrono::system_clock::now();
    const auto  time = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_s(&tmv, &time);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

void c_debug_log::push(debug_severity sev, const std::string& text)
{
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.push_back(debug_entry{ sev, now_hhmmss(), text });
    while (entries_.size() > k_capacity) entries_.pop_front();
    scroll_pending_ = true;
}

void c_debug_log::clear()
{
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.clear();
    scroll_pending_ = false;
}

// Traduz severidade -> cor de texto. Mantém greyscale + acento vermelho/verde
// pra não brigar com o resto da UI. Alpha alto pra ler sobre o card escuro.
static ImU32 color_for(debug_severity sev)
{
    switch (sev)
    {
        case debug_severity::success: return IM_COL32(120, 200, 120, 235);
        case debug_severity::warning: return IM_COL32(220, 180,  80, 235);
        case debug_severity::error:   return IM_COL32(220,  80,  80, 235);
        case debug_severity::info:
        default:                      return IM_COL32(200, 200, 205, 200);
    }
}

// Bolinha colorida no lugar de letras — visual limpo, agnóstico de fonte,
// mesma silhueta pra todos os tipos, distinção pela cor da severidade.
// dot_r é o raio; centro é (cx, cy). Usa o mesmo ImDrawList do console.
static void draw_status_dot(ImDrawList* dl, ImVec2 center, float radius, ImU32 color)
{
    // Halo bem tenue por trás pro dot destacar sem virar bolha.
    ImU32 halo = (color & 0x00FFFFFF) | ((ImU32)55 << 24);
    dl->AddCircleFilled(center, radius + 1.5f, halo, 16);
    dl->AddCircleFilled(center, radius,        color, 16);
}

void c_debug_log::render(float height)
{
    const float panel_w = ImGui::GetContentRegionAvail().x;
    if (panel_w <= 0.f || height <= 0.f) return;

    // Fundo do console (mais escuro que o card pra dar contraste tipográfico).
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = xgui->window_drawlist();
    dl->AddRectFilled(
        start,
        ImVec2(start.x + panel_w, start.y + height),
        draw->get_clr(clr->window.background),
        SCALE(4.f));
    dl->AddRect(
        start,
        ImVec2(start.x + panel_w, start.y + height),
        draw->get_clr(clr->widgets.stroke),
        SCALE(4.f), 0, SCALE(1.f));

    // Área rolável interna (BeginChild puro do ImGui — mais leve que abrir
    // outro card XISFPS aqui dentro, e evita interferir no ritmo do parent).
    // Padding interno generoso — antes texto colava na borda esquerda/direita
    // do console. 14x10 dá respiro em ambos os eixos, alinhado ao card style.
    ImGui::PushStyleColor(ImGuiCol_ChildBg,       0);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,   0);
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowPadding, ImVec2(SCALE(14.f), SCALE(10.f)));
    ImGui::PushStyleVar  (ImGuiStyleVar_ItemSpacing,   ImVec2(0, SCALE(3.f)));
    // AlwaysUseWindowPadding é OBRIGATÓRIO — sem ele o WindowPadding
    // empurrado acima não afeta o cursor inicial do child, e as linhas
    // saem coladas na borda esquerda/direita do console.
    ImGui::BeginChild("xdbg_scroll", ImVec2(panel_w, height), false,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysUseWindowPadding);
    {
        // Texto e timestamp no MESMO size — antes texto rodava em 14 e
        // timestamp em 13, o texto parecia grande demais em cima do
        // horário. Agora ambos em 13, com o timestamp ficando em cinza
        // dim pra hierarquia continuar clara sem diferença de escala.
        ImFont* fnt      = font->get(onest_medium_data, 13);
        ImFont* fnt_time = font->get(onest_medium_data, 13);

        // Largura ÚTIL dentro do BeginChild (já descontou WindowPadding).
        const float inner_w = ImGui::GetContentRegionAvail().x;

        // Layout de coluna:
        //   [HH:MM:SS]   ·   message
        //   |-timestamp-|dot|--gap--|---text----|
        const float col_time_w   = SCALE(64.f);  // largura da coluna do timestamp
        const float col_gap      = SCALE(8.f);   // espaço antes do dot
        const float dot_r        = SCALE(1.8f);  // dot bem discreto
        const float col_dot_w    = SCALE(10.f);  // slot ocupado pelo dot
        const float text_gap     = SCALE(10.f);  // respiro entre dot e texto
        const float col_text_x   = col_time_w + col_gap + col_dot_w + text_gap;

        std::lock_guard<std::mutex> lk(mtx_);
        // Sem placeholder — console fica vazio até chegar a primeira mensagem.
        if (!entries_.empty())
        {
            for (const auto& e : entries_)
            {
                const ImVec2 cur   = ImGui::GetCursorScreenPos();
                const float  row_h = SCALE(18.f);
                ImGui::Dummy(ImVec2(inner_w, row_h));

                // "[12:34:56]" — cinza dim, esquerda.
                std::string tstamp = "[" + e.time + "]";
                ImColor dim = clr->widgets.text_inactive;
                draw->text_clipped(dl, fnt_time,
                    cur, ImVec2(cur.x + col_time_w, cur.y + row_h),
                    draw->get_clr(dim),
                    tstamp.c_str(), NULL, NULL, ImVec2(0.f, 0.5f));

                // Bolinha da severidade — centrada verticalmente no meio da linha.
                const ImU32 col = color_for(e.sev);
                draw_status_dot(dl,
                    ImVec2(cur.x + col_time_w + col_gap + col_dot_w * 0.5f,
                           cur.y + row_h * 0.5f),
                    dot_r, col);

                // Mensagem — cor da severidade, alinhamento vertical central.
                draw->text_clipped(dl, fnt,
                    ImVec2(cur.x + col_text_x, cur.y),
                    ImVec2(cur.x + inner_w,    cur.y + row_h),
                    col, e.text.c_str(), NULL, NULL, ImVec2(0.f, 0.5f));
            }
        }

        if (scroll_pending_)
        {
            ImGui::SetScrollHereY(1.f);
            scroll_pending_ = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
