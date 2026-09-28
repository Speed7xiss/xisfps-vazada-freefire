#pragma once
// Redirect: mapeia xorstr.hpp para a biblioteca XorStr do projeto ativo.
// DriverMapper/.cpp incluem <xorstr.hpp> (angle-bracket) via diretório local.
// Macros disponíveis (de XorStr/XorStr.hpp):
//   _("literal")   → xorstr_("literal").crypt_get()
//   _w("literal")  → equivalente para wide strings
//   xorstr("...")  → alias de _()
#include "../../XorStr/XorStr.hpp"
