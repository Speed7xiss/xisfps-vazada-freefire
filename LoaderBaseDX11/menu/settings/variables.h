#pragma once
#include <string>
#include <vector>
#include "imgui.h"
#include "../headers/flags.h"
#include <map>

// XISFPS-for-FreeFire: 7 tabs — Combat, Visuals, Colors, Exploits, Skins, Cloud, Config.
enum class section_id
{
	combat      = 1,   // Aim / Silent / Spinbot / Aimlock / Ghost
	visuals     = 2,   // ESP toggles + style dropdowns
	colors      = 3,   // ESP color pickers + sizes
	exploits    = 4,   // Buffs / Atributar / No Recoil / etc
	skinchanger = 5,   // Cloth swap (SkinChanger)
	configs     = 6,   // Save / Load / Rename / Delete / Import / Export configs
	config      = 7,   // Overlay backend + StreamMode + Destruct + Exit
};

struct sub_section
{
	std::string name;
	int index;
	bool is_group;
	std::vector<sub_section> items;
	bool is_external;
};

struct section
{
	std::string icon;
	std::string name;
	bool separator;
	std::vector<sub_section> subsections;
};

class c_variables
{
public:
	struct
	{
		std::string name{ "oblivion" };
		window_flags flags{ window_flags_no_saved_settings | window_flags_no_decoration | window_flags_no_scroll_with_mouse | window_flags_no_scrollbar | window_flags_no_background | window_flags_no_nav };
		ImVec2 size{ 720, 460 };
		ImVec2 padding{ 0, 0 };
		ImVec2 spacing{ 0, 0 };
		float rounding{ 2 };
		float shadow{ 0 };
		float border{ 0 };
		float scrollbar{ 6 };
		float scrollbar_content{ 0 };
		ImVec2 scrollbar_border{ 4, 4 };
		float scrollbar_offset{ 10 };
	} window;

	struct
	{
		float bar_width{ 140 };
		float sidebar_width{ 0 };
		float header_height{ 40 };

		std::pair<section_id, std::vector<section>> sections_data
		{
			section_id::combat,
			{
				{ "", "", true, {} },   // index 0 — padding, never rendered
				{
					"", "Combat", true,
					{
						{"Assistance", 0, true, {
							{"Aim", 0, false, {}},
						}},
					}
				},
				{
					"", "Visuals", false,
					{
						{"ESP", 0, true, {
							{"Players", 0, false, {}},
						}},
					}
				},
				{
					"", "Colors", false,
					{
						{"ESP", 0, true, {
							{"Colors", 0, false, {}},
						}},
					}
				},
				{
					"", "Exploits", true,
					{
						{"Combat Mods", 0, true, {
							{"Modifiers", 0, false, {}},
						}},
					}
				},
				{
					"", "Skins", false,
					{
						{"Wardrobe", 0, false, {}},
					}
				},
				{
					"", "Cloud", false,
					{
						{"Profiles", 0, false, {}},
					}
				},
				{
					"", "Config", false,
					{
						{"System", 0, false, {}},
					}
				},
			}
		};
		section_id active_section{ section_id::combat };
		std::vector<int> count_subsections{ 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		std::vector<int> active_subsections{ 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		float section_alpha{ 1 };
		float sub_section_alpha{ 1 };

		float dpi{ 1 };
		int stored_dpi{ 100 };
		bool dpi_changed{ true };
		bool dpi_fix{ false };
		float anim_speed{ 1 };

		ImRect color_edit_rect;
		ImVec2 sub_pos{};
		ImDrawList* drawlist;

		std::string name{ "XISFPS" };
		std::string date{ "01.01.2025 15:30" };
		ImTextureID texture;
		ImTextureID brand_logo{ nullptr };
		ImTextureID tab_tex_combat         { nullptr };
		ImTextureID tab_tex_visuals        { nullptr };
		ImTextureID tab_tex_lists          { nullptr };
		ImTextureID tab_tex_exploits       { nullptr };
		ImTextureID tab_tex_location       { nullptr };
		ImTextureID tab_tex_configs        { nullptr };
		ImTextureID tab_tex_config         { nullptr };
		ImTextureID tab_tex_cloud          { nullptr };
		ImTextureID tab_tex_shirt          { nullptr };
		// Legado — mantidos para compatibilidade com helpers.cpp e section.cpp.
		ImTextureID tab_tex_trigger_custom { nullptr };
		ImTextureID tab_tex_recoil_custom  { nullptr };
		ImTextureID tab_tex_spoof          { nullptr };
		ImTextureID tab_tex_serial         { nullptr };

		float section_pos{ 0 };
	} gui;

	gui_style style;

};

inline std::unique_ptr<c_variables> var = std::make_unique<c_variables>();
