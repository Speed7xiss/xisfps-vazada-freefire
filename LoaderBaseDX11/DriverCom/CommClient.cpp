#include "CommClient.hpp"

std::unique_ptr<CommClient> g_CommClient;

// Canal de comunicação compartilhado usermode↔driver.
// Declarado __declspec(dllexport) para que o kdmapper possa passar &g_Comm
// como param2 ao DriverEntry do driver mapeado.
extern "C" __declspec(dllexport) Comm g_Comm = {};
