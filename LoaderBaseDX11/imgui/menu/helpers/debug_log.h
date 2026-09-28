#pragma once
//
// XISFPS debug log — pequeno buffer circular thread-safe de mensagens
// visíveis ao usuário dentro da imgui. Serve como "console de status"
// para operações assíncronas (Destruct, Overlay restart, futura Cloud/Config,
// etc). Mensagens devem ser CURTAS e sem detalhes internos do cheat: o
// cliente vê apenas se algo foi bem-sucedido ou não.
//
// Uso:
//   xdbg->push_success("Destruct: OK");
//   xdbg->push_error("Destruct: falhou");
//   xdbg->render(child_size);
//
#include "imgui.h"
#include <string>
#include <deque>
#include <mutex>
#include <memory>
#include <ctime>

enum class debug_severity
{
    info,
    success,
    warning,
    error,
};

struct debug_entry
{
    debug_severity sev;
    std::string    time;   // "HH:MM:SS"
    std::string    text;
};

class c_debug_log
{
public:
    // Limite duro do buffer — LIFO drop quando estoura.
    static constexpr size_t k_capacity = 64;

    void push(debug_severity sev, const std::string& text);
    void push_info   (const std::string& t) { push(debug_severity::info,    t); }
    void push_success(const std::string& t) { push(debug_severity::success, t); }
    void push_warning(const std::string& t) { push(debug_severity::warning, t); }
    void push_error  (const std::string& t) { push(debug_severity::error,   t); }

    void clear();

    // Renderiza dentro de uma região com o tamanho passado. Deve ser
    // chamado dentro do escopo de um child já aberto pelo caller — assim
    // reaproveitamos o styling do card XISFPS. `size` só define a altura
    // da área rolável interna; width usa ContentRegionAvail.
    void render(float height);

private:
    std::mutex              mtx_;
    std::deque<debug_entry> entries_;
    bool                    scroll_pending_ = false;
};

inline std::unique_ptr<c_debug_log> xdbg = std::make_unique<c_debug_log>();
