#pragma once

#include <imgui.h>

namespace fonts_shared
{
	inline ImFont* gender_font        = nullptr;
	inline ImFont* mdl2_icon_font     = nullptr;
	inline ImFont* mdl2_icon_font_lg  = nullptr;
	inline ImFont* game_icons_font    = nullptr;
	inline ImFont* material_icons_font = nullptr;
	inline ImFont* weapon_font_a      = nullptr;
	inline ImFont* weapon_font_b      = nullptr;

	#define MICON_PLACE              "\xEE\x95\x9F"
	#define MICON_PERSON_ADD         "\xEE\x9F\xBE"
	#define MICON_PERSON_REMOVE      "\xEE\xBD\xA6"
	#define MICON_CHECKROOM          "\xEF\x86\x9E"
	#define MICON_SWITCH_ACCOUNT     "\xEE\xA7\xAD"
	#define MICON_PEOPLE             "\xEE\x9F\xBB"
	#define MICON_VISIBILITY         "\xEE\xA3\xB4"
	#define MICON_SPORTS_KABADDI     "\xEE\xA8\xB4"

	#define MICON_MOVE_TO_INBOX      "\xEE\x85\xA8"
	#define MICON_BUILD              "\xEE\xA1\xA9"
	#define MICON_DONUT_LARGE        "\xEE\xA4\x97"
	#define MICON_LOCK_OPEN          "\xEE\xA2\x98"
	#define MICON_LOCK               "\xEE\xA2\x97"
	#define MICON_CHECK              "\xEE\x97\x8A"
	#define MICON_STAR               "\xEE\xA0\x88"

	// XISFPS: sidebar tab icons. Material Icons codepoints picked to be
	// obvious at a glance — the user can freely swap the bytes below to
	// change what shows up next to each tab label.
	#define MICON_TRACK_CHANGES      "\xEE\x87\xB7"   // U+E1F7 — crosshair, Legit
	#define MICON_LIST               "\xEE\xA2\x96"   // U+E896 — list, Lists
	#define MICON_FOLDER             "\xEE\x8B\x87"   // U+E2C7 — folder, Configs
	#define MICON_SETTINGS           "\xEE\xA2\xB8"   // U+E8B8 — gear, Config
}
