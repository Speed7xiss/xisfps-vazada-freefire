#pragma once
#include <string>

// Servidor HTTP local do painel web (ex: http://192.168.0.17:1010).
// Roda numa thread propria; Start/Stop sao thread-safe.
namespace WebSite
{
    // root: pasta servida (ex: <exe>\\web). Sem index.html la, "/" cai na pagina interna.
    bool Start(int port, const std::string& root);
    void Stop();
    bool IsRunning();
    int RunningPort();
    std::string LastError();
}

// Ponte implementada na GUI (tem o contexto do sections/configs.h).
namespace WebBridge
{
    std::string ConfigsList(); // [{"name","slot","fav","created","modified"}]
    std::string ConfigAction(const std::string& action, const std::string& name); // load|save|delete
}
