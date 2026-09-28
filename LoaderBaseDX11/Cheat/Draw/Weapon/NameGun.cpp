#include "NameGun.hpp"

// Icons live in the private-use range U+E000..U+E204 and are provided by the
// merged WeaponIcon font (see EspFont.cpp). Encoded here as raw UTF-8 bytes
// for maximum portability — no /utf-8 compiler flag required.
//   U+E066 -> "\xEE\x81\xA6"     U+E080 -> "\xEE\x82\x80"
//   U+E070 -> "\xEE\x81\xB0"     U+E087 -> "\xEE\x82\x87"
//   U+E074 -> "\xEE\x81\xB4"     U+E088 -> "\xEE\x82\x88"
// General rule for U+EXXX in this range:
//   byte0 = 0xEE
//   byte1 = 0x80 | ((XXX >> 6) & 0x3F)
//   byte2 = 0x80 | (XXX & 0x3F)

std::vector<Namegun::GunInfo> Namegun::GunData;

void Namegun::Init() {
    if (!GunData.empty()) return;
    GunData.resize(25000);

    // Especiais
    GunData[6016]  = { "AIRDROP",                    "",                 true };
    GunData[10006] = { "DROP CAMINHAO",              "",                 true };
    GunData[-15524 + 25000] = { "Mini Drone de cura",      "",           true };
    GunData[-15521 + 25000] = { "Mini Drone de cura (1USO)","",          true };
    GunData[21001] = { "Pistoleta de cura",          "\xEE\x81\xA6",     true };
    GunData[21002] = { "M590",                       "\xEE\x82\x84",     true };

    // Fuzil
    GunData[2]   = { "M4A1",       "\xEE\x81\xB5" };
    GunData[80]  = { "M4A1-I",     "\xEE\x81\xB5" };
    GunData[81]  = { "M4A1-II",    "\xEE\x81\xB5" };
    GunData[82]  = { "M4A1-III",   "\xEE\x81\xB5" };
    GunData[0]   = { "AK47",       "\xEE\x81\xB4" };
    GunData[6]   = { "AK47",       "\xEE\x81\xB4" };
    GunData[11]  = { "M14",        "\xEE\x81\xB9" };
    GunData[63]  = { "M14-I",      "\xEE\x81\xB9" };
    GunData[126] = { "M14-II",     "\xEE\x81\xB9" };
    GunData[127] = { "M14-III",    "\xEE\x81\xB9" };
    GunData[12]  = { "SCAR",       "\xEE\x81\xB8" };
    GunData[178] = { "SCAR-I",     "\xEE\x81\xB8" };
    GunData[179] = { "SCAR-II",    "\xEE\x81\xB8" };
    GunData[180] = { "SCAR-III",   "\xEE\x81\xB8" };
    GunData[14]  = { "GROZA",      "\xEE\x81\xBB" };
    GunData[70]  = { "GROZA-X",    "\xEE\x81\xBB" };
    GunData[24]  = { "FAMAS",      "\xEE\x81\xBA" };
    GunData[67]  = { "FAMAS-I",    "\xEE\x81\xBA" };
    GunData[130] = { "FAMAS-II",   "\xEE\x81\xBA" };
    GunData[131] = { "FAMAS-III",  "\xEE\x81\xBA" };
    GunData[28]  = { "XM8",        "\xEE\x81\xB7" };
    GunData[33]  = { "AN94",       "\xEE\x81\xBD" };
    GunData[39]  = { "Plasma",     "\xEE\x81\xBC" };
    GunData[46]  = { "AUG",        "\xEE\x81\xB6" };
    GunData[193] = { "AUG-I",      "\xEE\x81\xB6" };
    GunData[194] = { "AUG-II",     "\xEE\x81\xB6" };
    GunData[195] = { "AUG-III",    "\xEE\x81\xB6" };
    GunData[47]  = { "Parafal",    "\xEE\x81\xBF" };
    GunData[57]  = { "Atiradeira", "\xEE\x81\xBE" };
    GunData[73]  = { "G36-ASSALTO","\xEE\x82\x87" };
    GunData[74]  = { "G36-ALCANCE","\xEE\x82\x87" };

    // Fuzil De Atirador
    GunData[18] = { "SKS",      "\xEE\x81\xB1" };
    GunData[26] = { "SVD",      "\xEE\x81\xB0" };
    GunData[72] = { "SVD-Y",    "\xEE\x81\xB0" };
    GunData[48] = { "Carapina", "\xEE\x81\xB3" };
    GunData[89] = { "AC80",     "\xEE\x81\xB2" };

    // Metralhadora
    GunData[19]  = { "M249",    "\xEE\x81\xAF" };
    GunData[71]  = { "M249-X",  "\xEE\x81\xAF" };
    GunData[30]  = { "M60",     "\xEE\x81\xAE" };
    GunData[61]  = { "M60-I",   "\xEE\x81\xAE" };
    GunData[122] = { "M60-II",  "\xEE\x81\xAE" };
    GunData[123] = { "M60-III", "\xEE\x81\xAE" };
    GunData[54]  = { "Kord",    "" };

    // SMG
    GunData[7]   = { "UMP",          "\xEE\x81\xA3" };
    GunData[8]   = { "MP5",          "\xEE\x81\xA1" };
    GunData[60]  = { "MP5-I",        "\xEE\x81\xA1" };
    GunData[120] = { "MP5-II",       "\xEE\x81\xA1" };
    GunData[121] = { "MP5-III",      "\xEE\x81\xA1" };
    GunData[13]  = { "VSS",          "\xEE\x81\xA5" };
    GunData[62]  = { "VSS-I",        "\xEE\x81\xA5" };
    GunData[124] = { "VSS-II",       "\xEE\x81\xA5" };
    GunData[125] = { "VSS-III",      "\xEE\x81\xA5" };
    GunData[15]  = { "MP40",         "\xEE\x81\xA4" };
    GunData[32]  = { "P90",          "\xEE\x81\xA9" };
    GunData[35]  = { "CG15",         "\xEE\x81\xA7" };
    GunData[43]  = { "Thompson",     "\xEE\x81\xAB" };
    GunData[49]  = { "Vector",       "\xEE\x81\xAA" };
    GunData[69]  = { "Double-Vector","\xEE\x82\x88" };
    GunData[88]  = { "MAC10",        "\xEE\x81\xAD" };
    GunData[228] = { "MAC10-I",      "\xEE\x81\xAD" };
    GunData[229] = { "MAC10-II",     "\xEE\x81\xAD" };
    GunData[230] = { "MAC10-III",    "\xEE\x81\xAD" };
    GunData[150] = { "Bisao",        "\xEE\x81\xAC" };

    // Espingarda
    GunData[5]   = { "M1014",           "\xEE\x81\x99" };
    GunData[184] = { "M1014-I",         "\xEE\x81\x99" };
    GunData[185] = { "M1014-II",        "\xEE\x81\x99" };
    GunData[186] = { "M1014-III",       "\xEE\x81\x99" };
    GunData[29]  = { "SPAS12",          "\xEE\x81\x9A" };
    GunData[41]  = { "M1887",           "\xEE\x81\x9D" };
    GunData[119] = { "M1887-X",         "\xEE\x81\x9D" };
    GunData[50]  = { "MAG-7",           "\xEE\x81\x9B" };
    GunData[86]  = { "Carga Extra",     "\xEE\x81\x9F" };
    GunData[181] = { "Trogon-Espingarda","\xEE\x81\x9E" };
    GunData[182] = { "Trogon-Granada",   "\xEE\x81\x9E" };

    // Fuzil De Precisao
    GunData[4]   = { "AWM",        "\xEE\x81\x8F" };
    GunData[65]  = { "AWM-Y",      "\xEE\x81\x8F" };
    GunData[21]  = { "Kar98K",     "\xEE\x81\x90" };
    GunData[64]  = { "Kar98K-I",   "\xEE\x81\x90" };
    GunData[128] = { "Kar98K-II",  "\xEE\x81\x90" };
    GunData[129] = { "Kar98K-III", "\xEE\x81\x90" };
    GunData[45]  = { "M82B",       "\xEE\x81\x93" };
    GunData[75]  = { "M24",        "\xEE\x81\x94" };
    GunData[78]  = { "FP DE CURA", "" };
    GunData[197] = { "VSK94",      "\xEE\x81\x96" };

    // Pistola
    GunData[3]  = { "USP",              "\xEE\x81\x8E" };
    GunData[56] = { "USP-2",            "\xEE\x82\x80" };
    GunData[9]  = { "Aguia do Deserto", "\xEE\x81\x91" };
    GunData[10] = { "G18",              "\xEE\x81\x92" };
    GunData[20] = { "M1873",            "\xEE\x81\x97" };
    GunData[25] = { "M500",             "\xEE\x81\x98" };
    GunData[55] = { "M1917",            "\xEE\x81\x9C" };
    GunData[58] = { "Mini Uzi",         "\xEE\x81\xA2" };
    GunData[93] = { "Pistola de Cura",  "\xEE\x81\xA6" };

    // Armas de Contato
    GunData[16] = { "Panela",  "\xEE\x81\x8A" };
    GunData[17] = { "Machete", "\xEE\x81\x8B" };
    GunData[27] = { "Bastao",  "\xEE\x81\x8C" };
    GunData[34] = { "Katana",  "" };
    GunData[51] = { "Foice",   "\xEE\x81\x8D" };
    GunData[53] = { "Faca FF", "\xEE\x82\x85" };

    // Punho
    GunData[1] = { "Punho", "\xEE\x80\x85" };

    // Granada
    GunData[601]  = { "Granada",           "\xEE\x82\x80" };
    GunData[603]  = { "Granada de Fumaca", "\xEE\x82\x86" };
    GunData[1201] = { "Parede de Gel",     "\xEE\x82\x81" };
    GunData[1204] = { "Parede de Gel",     "\xEE\x82\x81" };
    GunData[602]  = { "Granada de Luz",    "\xEE\x82\x82" };
    GunData[608]  = { "Bomba Congelante",  "\xEE\x82\x83" };

    // Outros
    GunData[23]  = { "M79",                "" };
    GunData[36]  = { "RGS-50",             "" };
    GunData[196] = { "FGL-24",             "" };
    GunData[100] = { "Lanca-Chamas",       "\xEE\x81\xA8" };
    GunData[99]  = { "Arma de Escudo",     "" };
    GunData[617] = { "Quebra-Gel",         "" };
    GunData[1401]= { "Minas Terrestres",   "" };
    GunData[1006]= { "Mini Drone",         "" };
    GunData[1015]= { "Caixote de Suprimentos","" };
}

const char* Namegun::GetGunName(short gunId) {
    int adjustedId = gunId < 0 ? gunId + 25000 : gunId;
    if (adjustedId >= 0 && adjustedId < (int)GunData.size() &&
        (!GunData[adjustedId].name.empty() || GunData[adjustedId].isSpecial)) {
        return GunData[adjustedId].name.c_str();
    }
    return "";
}

const char* Namegun::GetGunIcon(short gunId) {
    int adjustedId = gunId < 0 ? gunId + 25000 : gunId;
    if (adjustedId >= 0 && adjustedId < (int)GunData.size()) {
        return GunData[adjustedId].icon.c_str();
    }
    return "";
}

bool Namegun::HasIcon(short gunId) {
    int adjustedId = gunId < 0 ? gunId + 25000 : gunId;
    return (adjustedId >= 0 && adjustedId < (int)GunData.size() &&
            !GunData[adjustedId].icon.empty());
}
