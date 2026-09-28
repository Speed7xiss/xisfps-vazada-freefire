// imgui-notify base patrickcjk [Modified by fael]
// https://github.com/patrickcjk/imgui-notify

#ifndef IMGUI_NOTIFY
#define IMGUI_NOTIFY

#pragma once
#include <vector>

#include <includes/imgui/imgui_internal.h>
#include <map>
#include <iostream>

#define NOTIFY_MAX_MSG_LENGTH		255			// Max message content length
#define NOTIFY_PADDING_Y			15.f		// Bottom-left Y padding
#define NOTIFY_FADE_IN_OUT_TIME		150			// Find in and out duration
#define NOTIFY_DEFAULT_DISMISS		3000		// Auto dismiss after X ms
#define NOTIFY_OPACITY				1.0f		// 0-1 Toast opacity

enum class toast_phase
{
	toast_phase_fade_in,
	toast_phase_wait,
	toast_phase_fade_out,
	toast_phase_expired,
	toast_phase_COUNT
};

class toast
{
public:
	char			content[NOTIFY_MAX_MSG_LENGTH];
	int				dismiss_time = NOTIFY_DEFAULT_DISMISS;
	uint64_t		creation_time;

	float positionY;

	struct button12Anims {
		float closing_anim;
		float closing_alpha;
		float label_alpha;
	};

	toast(int dismiss_time = NOTIFY_DEFAULT_DISMISS)
	{
		this->dismiss_time = dismiss_time;
		this->creation_time = GetTickCount64();
	}

	toast(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		vsnprintf(this->content, sizeof(this->content), format, args);
		va_end(args);
	}

	toast(int dismiss_time, const char* format, ...) : toast(dismiss_time)
	{
		va_list args;
		va_start(args, format);
		vsnprintf(this->content, sizeof(this->content), format, args);
		va_end(args);
	}

	auto get_elapsed_time()
	{
		return GetTickCount64() - this->creation_time;
	}

	auto get_phase() -> toast_phase
	{
		const auto elapsed = get_elapsed_time();

		if (elapsed > NOTIFY_FADE_IN_OUT_TIME + this->dismiss_time + NOTIFY_FADE_IN_OUT_TIME)
		{
			return toast_phase::toast_phase_expired;
		}
		else if (elapsed > NOTIFY_FADE_IN_OUT_TIME + this->dismiss_time)
		{
			return toast_phase::toast_phase_fade_out;
		}
		else if (elapsed > NOTIFY_FADE_IN_OUT_TIME)
		{
			return toast_phase::toast_phase_wait;
		}
		else
		{
			return toast_phase::toast_phase_fade_in;
		}
	}

	auto get_fade_percent() -> float
	{
		auto phase = get_phase();

		const auto elapsed = get_elapsed_time();

		if (phase == toast_phase::toast_phase_fade_in)
		{
			return (float)elapsed / (float)NOTIFY_FADE_IN_OUT_TIME;
		}
		else if (phase == toast_phase::toast_phase_fade_out)
		{
			return 1.f - (((float)elapsed - (float)NOTIFY_FADE_IN_OUT_TIME - (float)this->dismiss_time) / (float)NOTIFY_FADE_IN_OUT_TIME);
		}

		return 1.f;
	}
};

namespace notify
{
	inline std::vector<toast> toast_list;

	/// <summary>
	/// Insert a new toast in the list
	/// </summary>
	inline void insert(const toast& msg, const float& position = 20.f)
	{
		toast newToast = msg;
		newToast.positionY = position;
		toast_list.push_back(newToast);
	}

	/// <summary>
	/// Remove a toast from the list by its index
	/// </summary>
	/// <param name="index">index of the toast to remove</param>
	inline void remove(int index)
	{
		toast_list.erase(toast_list.begin() + index);
	}

	/// <summary>
	/// Render toasts, call at the end of your rendering!
	/// </summary>
	/// 

	inline void render()
	{
		const auto vp_size = ImGui::GetMainViewport()->Size;
		float height = 0.f;

		for (auto i = 0; i < toast_list.size(); i++)
		{
			auto* current_toast = &toast_list[i];
			ImGui::SetCursorPosY(current_toast->positionY - height);

			// Remove toast if expired
			if (current_toast->get_phase() == toast_phase::toast_phase_expired)
			{
				remove(i);
				continue;
			}

			// Get opacity based on the current phase
			auto opacity = NOTIFY_OPACITY * current_toast->get_fade_percent();

			// Save height for next toasts
			height += ImGui::GetTextLineHeightWithSpacing() + NOTIFY_PADDING_Y;

			// Get Colors
			ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			text_color.w = opacity;

			ImVec4 frame_color = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(c::notify::background));
			frame_color.w = opacity;

			ImVec2 textSize = ImGui::CalcTextSize(current_toast->content);

			// Calculate the animation factor based on time
			float animation_factor = std::fmodf(ImGui::GetTime(), 1.0f); // Range from 0.0 to 1.0
			float pulse_factor = std::abs(0.5f - animation_factor) * 2.0f; // Range from 0.0 to 1.0 with a pulse effect

			// Render the toast content
			ImGui::PushStyleColor(ImGuiCol_Text, text_color);

			auto rect_pos = ImGui::GetCursorScreenPos();
			ImVec2 rect_size = ImVec2(textSize.x + 2 * /*padding x*/ 10, textSize.y + 2 * /*padding y*/5);

			// Calculate the centered position for the rectangle
			ImVec2 window_pos = ImGui::GetWindowPos();
			float window_width = ImGui::GetWindowWidth();
			float rect_x = window_pos.x + (window_width - rect_size.x) * 0.5f;

			rect_pos.x = rect_x;

			const ImVec4 border_color = ImVec4(17.f / 255.f, 17.f / 255.f, 17.f / 255.f, opacity);
			float border_thickness = 1.0f;
			ImVec2 border_pos = ImVec2(rect_pos.x - border_thickness, rect_pos.y - border_thickness);
			ImVec2 border_size = ImVec2(rect_size.x + 2 * border_thickness, rect_size.y + 2 * border_thickness);

			ImGui::GetWindowDrawList()->AddRect(border_pos, ImVec2(border_pos.x + border_size.x, border_pos.y + border_size.y), ImGui::ColorConvertFloat4ToU32(border_color), 4, ImDrawFlags_RoundCornersAll, border_thickness);

			ImGui::GetWindowDrawList()->AddRectFilled(rect_pos, ImVec2(rect_pos.x + rect_size.x, rect_pos.y + rect_size.y), ImGui::ColorConvertFloat4ToU32(frame_color), 4);

			// Calculate the centered position for the text
			ImVec2 text_pos = ImVec2(rect_pos.x + (rect_size.x - textSize.x) * 0.5f, rect_pos.y + (rect_size.y - textSize.y) * 0.5f);

			ImGui::SetCursorScreenPos(text_pos);
			ImGui::Text(current_toast->content);
			ImGui::PopStyleColor();
		}
	}
}

#endif
