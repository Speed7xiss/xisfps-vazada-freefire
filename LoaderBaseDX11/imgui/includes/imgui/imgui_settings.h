#include "imgui.h"

namespace c {

	namespace notify {
		inline ImVec4 background = ImColor(15, 14, 16, 255);
	}

	namespace input_text {
		inline ImVec4 background = ImColor(11, 10, 11, 255);
		inline ImVec4 border = ImColor(20, 19, 20);
		inline float rounding = 5.f;
	}

	namespace button {
		inline ImVec4 background = ImColor(80, 80, 80, 255);
		inline ImVec4 background_hovered = ImColor(80, 80, 80, 255);
		inline ImVec4 border = ImColor(80, 80, 80);
		inline float rounding = 5.f;
	}

	namespace label {
		inline ImVec4 text = ImColor(255, 255, 255, 100);
		inline ImVec4 text_hovered = ImColor(255, 0, 0);
	}


}