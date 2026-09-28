#pragma once

//
// XISFPS custom icons — v3 (thin line-art).
//
// v1 → too many small strokes ("wiry").
// v2 → filled silhouettes, way too bold.
// v3 → outline-only shapes, thin strokes, Feather/Lucide feel. The shapes
//      stay the same size as v2 (big, not inset) but every stroke is a
//      hairline. Fill is used ONLY for the tiny accent dot on the map pin.
//
// Signature is unchanged:
//   void icon_xxx(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th);
//     c  = center point (screen)
//     s  = target box size
//     col= color
//     th = stroke thickness — the widget layer passes ~1.0 px baseline
//

#include "imgui.h"
#include "imgui_internal.h"

namespace xisfps::icons
{
	// ─── Combat — pistol (outline) ───────────────────────────────────────
	//   One continuous polyline traces the whole silhouette. No fill —
	//   reads as line-art like the reference.
	inline void combat(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const float r = s * 0.5f;
		const ImVec2 p[] = {
			{ c.x - r * 0.85f, c.y - r * 0.45f },   // slide top-left
			{ c.x + r * 0.55f, c.y - r * 0.45f },   // slide top-right
			{ c.x + r * 0.55f, c.y - r * 0.25f },   // barrel top-left
			{ c.x + r * 0.95f, c.y - r * 0.25f },   // barrel top-right
			{ c.x + r * 0.95f, c.y - r * 0.05f },   // barrel bottom-right
			{ c.x + r * 0.30f, c.y - r * 0.05f },   // slide bottom-right
			{ c.x + r * 0.05f, c.y + r * 0.75f },   // grip bottom-right
			{ c.x - r * 0.55f, c.y + r * 0.75f },   // grip bottom-left
			{ c.x - r * 0.85f, c.y - r * 0.05f },   // grip top-left (back to start row)
		};
		dl->PathClear();
		for (auto& v : p) dl->PathLineTo(v);
		dl->PathStroke(col, ImDrawFlags_Closed, th);
	}

	// ─── Visuals — binoculars (outline) ─────────────────────────────────
	//   Two thin rings connected by a slim bridge.
	inline void visuals(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const float r_lens = s * 0.24f;
		const float dx     = s * 0.24f;
		const ImVec2 L{ c.x - dx, c.y + s * 0.02f };
		const ImVec2 R{ c.x + dx, c.y + s * 0.02f };

		dl->AddCircle(L, r_lens, col, 28, th);
		dl->AddCircle(R, r_lens, col, 28, th);
		// Bridge — single thin line rather than a filled rect
		dl->AddLine({ L.x + r_lens, c.y + s * 0.02f },
		            { R.x - r_lens, c.y + s * 0.02f }, col, th);
	}

	// ─── Lists — sheet with ruled lines (outline) ───────────────────────
	//   Rounded rectangle body + two short ruled lines inside.
	inline void lists(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const float w = s * 0.58f;
		const float h = s * 0.76f;
		const ImVec2 tl{ c.x - w * 0.5f, c.y - h * 0.5f };
		const ImVec2 br{ c.x + w * 0.5f, c.y + h * 0.5f };

		dl->AddRect(tl, br, col, s * 0.09f, 0, th);

		const float x0 = tl.x + w * 0.20f;
		const float x1 = br.x - w * 0.20f;
		const float y1 = c.y - h * 0.15f;
		const float y2 = c.y + h * 0.12f;
		dl->AddLine({ x0, y1 }, { x1, y1 }, col, th);
		dl->AddLine({ x0, y2 }, { c.x + w * 0.05f, y2 }, col, th);
	}

	// ─── Exploits — mystic sparkle (outline) ────────────────────────────
	//   Four-pointed star drawn as an 8-point closed polyline.
	inline void exploits(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const float R = s * 0.40f;
		const float k = R * 0.22f;
		const ImVec2 pts[] = {
			{ c.x,     c.y - R },
			{ c.x + k, c.y - k },
			{ c.x + R, c.y     },
			{ c.x + k, c.y + k },
			{ c.x,     c.y + R },
			{ c.x - k, c.y + k },
			{ c.x - R, c.y     },
			{ c.x - k, c.y - k },
		};
		dl->PathClear();
		for (auto& v : pts) dl->PathLineTo(v);
		dl->PathStroke(col, ImDrawFlags_Closed, th);
	}

	// ─── Location — map pin (outline) ───────────────────────────────────
	//   Classic pin outline: arc + two edges meeting at the tip, plus a
	//   small hollow circle in the head.
	inline void location(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const float r = s * 0.28f;
		const ImVec2 head{ c.x, c.y - s * 0.08f };
		const ImVec2 tip { c.x, c.y + s * 0.50f };

		// Arc spans from ~30° right of top, all the way around the top, to
		// ~30° left of top. Then close with two straight lines to the tip.
		const float a0 = IM_PI * 0.20f;                 // angle where right side leaves the arc
		const float a1 = IM_PI - IM_PI * 0.20f + IM_PI; // wraps around the top back to left

		dl->PathClear();
		dl->PathArcTo(head, r, a0, a1, 26);
		dl->PathLineTo(tip);
		dl->PathStroke(col, ImDrawFlags_Closed, th);

		// Inner ring
		dl->AddCircle(head, r * 0.40f, col, 16, th);
	}

	// ─── Configs — 6-tooth gear (outline) ───────────────────────────────
	//   Every tooth traced as an outlined trapezoid. Body ring + hub as
	//   thin outlines. No filled parts.
	inline void configs(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const int   teeth      = 6;
		const float r_body     = s * 0.30f;
		const float r_tooth    = s * 0.42f;
		const float r_hole     = s * 0.09f;
		const float tooth_half = (IM_PI / teeth) * 0.34f;

		for (int i = 0; i < teeth; ++i)
		{
			const float a = (IM_PI * 2.f * i) / teeth - IM_PI * 0.5f;
			const ImVec2 p0{ c.x + cosf(a - tooth_half) * r_body,  c.y + sinf(a - tooth_half) * r_body };
			const ImVec2 p1{ c.x + cosf(a + tooth_half) * r_body,  c.y + sinf(a + tooth_half) * r_body };
			const ImVec2 p2{ c.x + cosf(a + tooth_half) * r_tooth, c.y + sinf(a + tooth_half) * r_tooth };
			const ImVec2 p3{ c.x + cosf(a - tooth_half) * r_tooth, c.y + sinf(a - tooth_half) * r_tooth };

			// Outline the tooth (3 outer edges — the inner edge is the body ring below)
			dl->AddLine(p0, p3, col, th);
			dl->AddLine(p3, p2, col, th);
			dl->AddLine(p2, p1, col, th);
		}

		dl->AddCircle(c, r_body, col, 32, th);
		dl->AddCircle(c, r_hole, col, 14, th);
	}

	// ─── Config — sliders / tune (outline) ──────────────────────────────
	//   Two tracks with two knobs. Knobs are hollow circles, not filled dots.
	inline void config(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th)
	{
		const float w      = s * 0.72f;
		const float x0     = c.x - w * 0.5f;
		const float x1     = c.x + w * 0.5f;
		const float knob_r = s * 0.09f;

		// Top row — knob toward the left
		const float y1 = c.y - s * 0.18f;
		dl->AddLine({ x0, y1 }, { x1, y1 }, col, th);
		dl->AddCircle({ x0 + w * 0.30f, y1 }, knob_r, col, 14, th);

		// Bottom row — knob toward the right
		const float y2 = c.y + s * 0.18f;
		dl->AddLine({ x0, y2 }, { x1, y2 }, col, th);
		dl->AddCircle({ x0 + w * 0.70f, y2 }, knob_r, col, 14, th);
	}
}
