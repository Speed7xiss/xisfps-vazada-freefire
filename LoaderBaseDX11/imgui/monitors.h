#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef small
#undef small
#endif
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

namespace overlay
{
	namespace monitors
	{
		struct info_t
		{
			std::string name;

			long        x = 0, y = 0, w = 0, h = 0;
			bool        primary = false;
		};

		inline std::mutex          list_mtx;
		inline std::vector<info_t> list_cache;

		inline BOOL CALLBACK enum_cb(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
		{
			auto* out = reinterpret_cast<std::vector<info_t>*>(lp);

			MONITORINFOEXA mi{};
			mi.cbSize = sizeof(mi);
			if (!GetMonitorInfoA(hMon, (MONITORINFO*)&mi))
				return TRUE;

			info_t e;
			e.x = mi.rcMonitor.left;
			e.y = mi.rcMonitor.top;
			e.w = mi.rcMonitor.right  - mi.rcMonitor.left;
			e.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
			e.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

			char buf[128];
			std::snprintf(buf, sizeof buf, "%ldx%ld%s", e.w, e.h, e.primary ? "  (Primary)" : "");
			e.name = buf;
			out->push_back(std::move(e));
			return TRUE;
		}

		inline auto refresh() -> void
		{
			std::vector<info_t> fresh;
			EnumDisplayMonitors(nullptr, nullptr, &enum_cb, (LPARAM)&fresh);

			std::sort(fresh.begin(), fresh.end(), [](const info_t& a, const info_t& b)
			{
				if (a.primary != b.primary) return a.primary;
				return a.x < b.x;
			});

			std::lock_guard g(list_mtx);
			list_cache = std::move(fresh);
		}

		inline auto list() -> std::vector<info_t>
		{

			{
				std::lock_guard g(list_mtx);
				if (!list_cache.empty()) return list_cache;
			}
			refresh();
			std::lock_guard g(list_mtx);
			return list_cache;
		}

		struct rect_t { long x, y, w, h; };

		inline auto target_rect(int idx) -> rect_t
		{
			auto l = list();
			if (l.empty())
			{

				return { 0, 0, 1920, 1080 };
			}
			auto pick = [](const info_t& m) -> rect_t { return { m.x, m.y, m.w, m.h }; };
			if (idx < 0 || idx >= (int)l.size())
			{
				for (const auto& m : l) if (m.primary) return pick(m);
				return pick(l.front());
			}
			return pick(l[idx]);
		}

		inline auto list_names() -> std::vector<std::string>
		{
			std::vector<std::string> out;
			for (const auto& m : list()) out.push_back(m.name);
			return out;
		}
	}
}
