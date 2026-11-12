/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/host/ui/menu_kit.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace odin {

namespace {

/* The cut of the corners of a group and of a control, the room around a
 * row and between its texts and its control, and the room in a cell of a
 * control and in a button, in points */
constexpr float GROUP_CUT = 5.0f;
constexpr float CONTROL_CUT = 3.0f;
constexpr float ROW_PAD = 8.0f;
constexpr float GAP = 14.0f;
constexpr float CELL_WIDE = 7.0f;
constexpr float CELL_HIGH = 3.0f;
constexpr float BUTTON_WIDE = 8.0f;
constexpr float BUTTON_HIGH = 4.0f;
/* The texts of a row have that room at the least beside its control, else
 * the control goes under them */
constexpr float TEXT_LEAST = 120.0f;
/* A tip goes on to the next line at that width, in heights of the font;
 * the mark of a row that has one, that far after its name in points */
constexpr float TIP_EMS = 26.0f;
/* The track of a slider, and the room between it and its value, in points */
constexpr float TRACK_W = 64.0f;
constexpr float TRACK_GAP = 8.0f;
constexpr char ASK[] = "(?)";
constexpr float ASK_GAP = 5.0f;
/* What is left of a colour on a row that cannot change; of the action
 * colour in the fill of a button on */
constexpr float OFF_ALPHA = 0.4f;
constexpr float CHOSEN_FILL = 0.15f;
/* The letters of a title in small capitals that far apart, in ems */
constexpr float TITLE_SPACING = 0.12f;

/* The most labels a choice has */
constexpr size_t MOST_LABELS = 8;

ImU32
faded(ImU32 colour, bool off)
{
	return off ? MenuKit::alpha(colour, OFF_ALPHA) : colour;
}

/* The fill of a cell of a control, its corners cut on the outer sides of
 * the control only */
void
fill_cell(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, bool first, bool last, ImU32 colour)
{
	std::array<ImVec2, 8> points;
	int n = 0;

	if (first) {
		points[n++] = { a.x, a.y + cut };
		points[n++] = { a.x + cut, a.y };
	} else {
		points[n++] = a;
	}
	if (last) {
		points[n++] = { b.x - cut, a.y };
		points[n++] = { b.x, a.y + cut };
		points[n++] = { b.x, b.y - cut };
		points[n++] = { b.x - cut, b.y };
	} else {
		points[n++] = { b.x, a.y };
		points[n++] = b;
	}
	if (first) {
		points[n++] = { a.x + cut, b.y };
		points[n++] = { a.x, b.y - cut };
	} else {
		points[n++] = { a.x, b.y };
	}
	list->AddConvexPolyFilled(points.data(), n, colour);
}

/* The character at p (UTF-8) and its length in bytes */
unsigned
decode(const char *p, int &length)
{
	const auto c = static_cast<unsigned char>(p[0]);

	length = 1;
	if (c < 0x80)
		return c;

	const int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	unsigned code = c & (0x7Fu >> n);

	for (; length < n && (static_cast<unsigned char>(p[length]) & 0xC0) == 0x80; length++)
		code = code << 6 | (static_cast<unsigned char>(p[length]) & 0x3F);
	return code;
}

/* The signs the font has not, and their widths in ems */
struct Sign {
	unsigned code;
	float em;
};

constexpr Sign SIGNS[] = {
	{ 0x25B2, 0.75f }, { 0x25BC, 0.75f }, { 0x25C0, 0.7f }, { 0x25B6, 0.7f }, { 0x25BE, 0.6f },
	{ 0x2190, 0.9f }, { 0x2192, 0.9f }, { 0x2191, 0.6f }, { 0x2193, 0.6f }, { 0x2194, 1.1f }, { 0x2195, 0.6f },
	{ 0x23CE, 0.9f }, { 0x2715, 0.85f }, { 0x25CB, 0.85f }, { 0x25A1, 0.85f }, { 0x25B3, 0.85f },
	{ 0x2039, 0.5f }, { 0x203A, 0.5f }, { 0x2212, 0.6f }, { 0x232B, 1.0f },
	{ 0x2460, 1.0f }, { 0x2461, 1.0f }, { 0x2462, 1.0f }, { 0x2463, 1.0f }, { 0x2464, 1.0f },
	{ 0x2465, 1.0f }, { 0x2466, 1.0f }, { 0x2467, 1.0f }, { 0x2468, 1.0f },
};

const Sign *
sign_of(unsigned code)
{
	for (const Sign &sign : SIGNS)
		if (sign.code == code)
			return &sign;
	return nullptr;
}

/* A sign in its box from x, a line of that size high from y */
void
draw_sign(ImDrawList *list, const Sign &sign, float x, float y, float size, ImU32 colour)
{
	const float e = size, w = std::max(1.0f, e * 0.09f), adv = sign.em * e, cx = x + adv / 2.0f, cy = y + e * 0.52f;
	const float h = e * 0.26f, l = x + e * 0.1f, r = x + adv - e * 0.1f, head = e * 0.2f;
	const auto line = [&](ImVec2 p, ImVec2 q) { list->AddLine(p, q, colour, w); };
	/* An arrow head at the tip, pointing along (dx, dy) */
	const auto tip = [&](ImVec2 t, float dx, float dy) {
		line(t, { t.x - dx * head + dy * head, t.y - dy * head - dx * head });
		line(t, { t.x - dx * head - dy * head, t.y - dy * head + dx * head });
	};

	/* ① to ⑨: its number in a circle */
	if (sign.code >= 0x2460 && sign.code <= 0x2468) {
		const char digit[2] = { static_cast<char>('1' + (sign.code - 0x2460)), 0 };
		const float small = e * 0.7f;
		const ImVec2 wide = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.0f, digit);

		list->AddCircle({ cx, cy }, e * 0.42f, colour, 0, w);
		list->AddText(ImGui::GetFont(), small, { cx - wide.x / 2.0f, cy - wide.y / 2.0f }, colour, digit);
		return;
	}
	switch (sign.code) {
	case 0x25B2:
		list->AddTriangleFilled({ cx, cy - h }, { cx + h * 1.15f, cy + h * 0.85f }, { cx - h * 1.15f, cy + h * 0.85f },
		                        colour);
		break;
	case 0x25BC:
		list->AddTriangleFilled({ cx - h * 1.15f, cy - h * 0.85f }, { cx + h * 1.15f, cy - h * 0.85f }, { cx, cy + h },
		                        colour);
		break;
	case 0x25B6:
		list->AddTriangleFilled({ cx - h * 0.85f, cy - h * 1.15f }, { cx + h, cy }, { cx - h * 0.85f, cy + h * 1.15f },
		                        colour);
		break;
	case 0x25C0:
		list->AddTriangleFilled({ cx + h * 0.85f, cy + h * 1.15f }, { cx - h, cy }, { cx + h * 0.85f, cy - h * 1.15f },
		                        colour);
		break;
	case 0x25BE: {
		const float s = e * 0.18f;

		list->AddTriangleFilled({ cx - s * 1.2f, cy - s * 0.6f }, { cx + s * 1.2f, cy - s * 0.6f }, { cx, cy + s * 0.9f },
		                        colour);
		break;
	}
	case 0x2190:
	case 0x2192:
	case 0x2194: {
		line({ l, cy }, { r, cy });
		if (sign.code != 0x2190)
			tip({ r, cy }, 1.0f, 0.0f);
		if (sign.code != 0x2192)
			tip({ l, cy }, -1.0f, 0.0f);
		break;
	}
	case 0x2191:
	case 0x2193:
	case 0x2195: {
		const float top = cy - e * 0.32f, bottom = cy + e * 0.32f;

		line({ cx, top }, { cx, bottom });
		if (sign.code != 0x2193)
			tip({ cx, top }, 0.0f, -1.0f);
		if (sign.code != 0x2191)
			tip({ cx, bottom }, 0.0f, 1.0f);
		break;
	}
	case 0x23CE: {
		const ImVec2 corner = { r - e * 0.05f, cy + e * 0.12f }, end = { l + e * 0.05f, cy + e * 0.12f };

		line({ corner.x, cy - e * 0.32f }, corner);
		line(corner, end);
		tip(end, -1.0f, 0.0f);
		break;
	}
	case 0x2715: {
		const float m = e * 0.27f;

		line({ cx - m, cy - m }, { cx + m, cy + m });
		line({ cx + m, cy - m }, { cx - m, cy + m });
		break;
	}
	case 0x25CB:
		list->AddCircle({ cx, cy }, e * 0.3f, colour, 0, w);
		break;
	case 0x25A1:
		list->AddRect({ cx - e * 0.27f, cy - e * 0.27f }, { cx + e * 0.27f, cy + e * 0.27f }, colour, 0.0f,
		              ImDrawFlags_None, w);
		break;
	case 0x25B3:
		list->AddTriangle({ cx, cy - e * 0.3f }, { cx + e * 0.32f, cy + e * 0.25f }, { cx - e * 0.32f, cy + e * 0.25f },
		                  colour, w);
		break;
	case 0x232B: {
		/* A key pointing left, a cross in it */
		const float t = cy - e * 0.26f, d = cy + e * 0.26f, n = l + e * 0.22f, m = (n + r) / 2.0f + e * 0.04f;
		const ImVec2 points[5] = { { l, cy }, { n, t }, { r, t }, { r, d }, { n, d } };

		list->AddPolyline(points, 5, colour, ImDrawFlags_Closed, w);
		line({ m - e * 0.1f, cy - e * 0.1f }, { m + e * 0.1f, cy + e * 0.1f });
		line({ m + e * 0.1f, cy - e * 0.1f }, { m - e * 0.1f, cy + e * 0.1f });
		break;
	}
	case 0x2039:
	case 0x203A: {
		const float d = sign.code == 0x2039 ? 1.0f : -1.0f;
		const ImVec2 points[3] = { { cx + d * e * 0.1f, cy - e * 0.22f }, { cx - d * e * 0.1f, cy },
			                   { cx + d * e * 0.1f, cy + e * 0.22f } };

		list->AddPolyline(points, 3, colour, ImDrawFlags_None, w);
		break;
	}
	default:
		line({ l, cy }, { r, cy });
		break;
	}
}

/* A text drawn, its signs too, when `list` is given: its width */
float
run(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text, float size, float spacing)
{
	ImFont *const font = ImGui::GetFont();
	float x = at.x;
	const char *p = text, *plain = text;

	if (size <= 0.0f)
		size = ImGui::GetFontSize();

	const auto flush = [&](const char *end) {
		if (end <= plain)
			return;
		if (list)
			list->AddText(font, size, { x, at.y }, colour, plain, end);
		x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, plain, end).x;
	};

	while (*p) {
		int n;
		const Sign *const sign = sign_of(decode(p, n));

		if (!sign && spacing == 0.0f) {
			p += n;
			continue;
		}
		flush(p);
		if (sign) {
			if (list)
				draw_sign(list, *sign, x, at.y, size, colour);
			x += sign->em * size;
		} else {
			if (list)
				list->AddText(font, size, { x, at.y }, colour, p, p + n);
			x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, p + n).x;
		}
		p += n;
		plain = p;
		if (*p)
			x += spacing;
	}
	flush(p);
	return x - at.x;
}

/* The lines of a list that has only names */
std::vector<MenuKit::Item>
plain(std::span<const std::string> labels)
{
	std::vector<MenuKit::Item> items;

	for (const std::string &label : labels)
		items.push_back({ label, {}, {}, MenuKit::Icon::NONE, false });
	return items;
}


/* The box from a to b, its corners cut by `cut`, clockwise from the top left */
std::array<ImVec2, 8>
octagon(ImVec2 a, ImVec2 b, float cut)
{
	return { { { a.x + cut, a.y }, { b.x - cut, a.y }, { b.x, a.y + cut }, { b.x, b.y - cut },
	           { b.x - cut, b.y }, { a.x + cut, b.y }, { a.x, b.y - cut }, { a.x, a.y + cut } } };
}
} /* namespace */

ImU32
MenuKit::alpha(ImU32 colour, float share)
{
	const ImU32 a = (colour >> IM_COL32_A_SHIFT) & 0xFF;

	return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(static_cast<float>(a) * share) << IM_COL32_A_SHIFT);
}

float
MenuKit::measure(const char *text, float size, float spacing)
{
	return run(nullptr, {}, 0, text, size, spacing);
}

void
MenuKit::write(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text, float size, float spacing)
{
	run(list, at, colour, text, size, spacing);
}

std::string
MenuKit::fitted(const std::string &text, float width, float size)
{
	if (measure(text.c_str(), size) <= width)
		return text;

	const float dots = measure("...", size);
	size_t n = text.size();

	while (n > 0 && measure(text.substr(0, n).c_str(), size) + dots > width) {
		n--;
		/* not in the middle of a character of UTF-8 */
		while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
			n--;
	}
	return text.substr(0, n) + "...";
}

void
MenuKit::frame(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 border, float strokes) const
{
	const std::array<ImVec2, 8> points = octagon(a, b, cut);

	if (fill)
		list->AddConvexPolyFilled(points.data(), 8, fill);
	if (border)
		list->AddPolyline(points.data(), 8, border, ImDrawFlags_Closed, stroke() * strokes);
}

void
MenuKit::fading_line(ImDrawList *list, float left, float right, float y, float thick, ImU32 colour, float fade)
{
	const ImU32 clear = alpha(colour, 0.0f);
	const float ends = (right - left) * fade;

	list->AddRectFilledMultiColor({ left, y }, { left + ends, y + thick }, clear, colour, colour, clear);
	list->AddRectFilled({ left + ends, y }, { right - ends, y + thick }, colour);
	list->AddRectFilledMultiColor({ right - ends, y }, { right, y + thick }, colour, clear, clear, colour);
}

void
MenuKit::dashes(ImDrawList *list, std::span<const ImVec2> points, bool closed, ImU32 colour, float width, float on,
                float off)
{
	const size_t n = points.size();
	float left = on;	/* of the dash or of the gap now */
	bool drawing = true;

	for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); i++) {
		ImVec2 p = points[i];
		const ImVec2 q = points[(i + 1) % n];
		float length = std::hypot(q.x - p.x, q.y - p.y);

		while (length > 0.0f) {
			const float step = std::min(left, length);
			const ImVec2 r = { p.x + (q.x - p.x) * step / length, p.y + (q.y - p.y) * step / length };

			if (drawing)
				list->AddLine(p, r, colour, width);
			p = r;
			length -= step;
			left -= step;
			if (left <= 0.0f) {
				drawing = !drawing;
				left = drawing ? on : off;
			}
		}
	}
}

void
MenuKit::dashed_frame(ImDrawList *list, ImVec2 a, ImVec2 b, ImU32 colour) const
{
	dashes(list, octagon(a, b, px(CONTROL_CUT)), true, colour, stroke(), px(3.0f), px(2.0f));
}

/* The paths of the icons, in a square of 24 */
void
MenuKit::icon(ImDrawList *list, Icon icon, ImVec2 at, float side, ImU32 colour)
{
	const float k = side / 24.0f, w = std::max(1.0f, 1.8f * k);
	const auto p = [&](float x, float y) { return ImVec2{ at.x + x * k, at.y + y * k }; };
	const auto arc = [&](float x, float y, float r, float from, float to) {
		list->PathArcTo(p(x, y), r * k, from * std::numbers::pi_v<float> / 180.0f, to * std::numbers::pi_v<float> / 180.0f);
	};
	const auto lines = [&](std::initializer_list<ImVec2> points, bool closed) {
		for (const ImVec2 q : points)
			list->PathLineTo(p(q.x, q.y));
		list->PathStroke(colour, closed ? ImDrawFlags_Closed : ImDrawFlags_None, w);
	};

	switch (icon) {
	case Icon::NONE:
		break;
	case Icon::PAD:
	case Icon::INPUT:
		list->PathLineTo(p(6, 9));
		list->PathLineTo(p(18, 9));
		arc(18, 13, 4, 270, 360);
		list->PathLineTo(p(22, 14));
		arc(19, 14, 3, 0, 143);
		list->PathLineTo(p(15, 14));
		list->PathLineTo(p(9, 14));
		list->PathLineTo(p(7.4f, 15.8f));
		arc(5, 14, 3, 37, 180);
		list->PathLineTo(p(2, 13));
		arc(6, 13, 4, 180, 270);
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		if (icon == Icon::INPUT) {
			list->AddLine(p(7, 12), p(7, 15), colour, w);
			list->AddLine(p(5.5f, 13.5f), p(8.5f, 13.5f), colour, w);
		}
		break;
	case Icon::WHEEL:
		list->AddCircle(p(12, 12), 9.0f * k, colour, 0, w);
		list->AddCircle(p(12, 12), 2.5f * k, colour, 0, w);
		list->AddLine(p(3, 12), p(9.5f, 12), colour, w);
		list->AddLine(p(14.5f, 12), p(21, 12), colour, w);
		list->AddLine(p(12, 14.5f), p(12, 21), colour, w);
		break;
	case Icon::DISPLAY:
		list->AddRect(p(3, 5), p(21, 17), colour, 2.0f * k, ImDrawFlags_None, w);
		list->AddLine(p(8, 21), p(16, 21), colour, w);
		break;
	case Icon::KEYBOARD:
		list->AddRect(p(2, 6), p(22, 18), colour, 2.0f * k, ImDrawFlags_None, w);
		for (const float x : { 6.0f, 10.0f, 14.0f, 18.0f })
			list->AddLine(p(x, 10), p(x + 1, 10), colour, w);
		list->AddLine(p(7, 14), p(17, 14), colour, w);
		break;
	case Icon::AUDIO:
	case Icon::MUTE:
		lines({ { 4, 9 }, { 4, 15 }, { 8, 15 }, { 13, 19 }, { 13, 5 }, { 8, 9 } }, true);
		if (icon == Icon::AUDIO) {
			arc(13.35f, 12, 4, -48.5f, 48.5f);
			list->PathStroke(colour, ImDrawFlags_None, w);
		} else {
			list->AddLine(p(16, 9), p(21, 15), colour, w);
			list->AddLine(p(21, 9), p(16, 15), colour, w);
		}
		break;
	case Icon::HOTKEYS:
		list->AddRect(p(3, 7), p(21, 17), colour, 2.0f * k, ImDrawFlags_None, w);
		for (const float x : { 7.0f, 11.0f, 15.0f })
			list->AddLine(p(x, 11), p(x + 2, 11), colour, w);
		list->AddLine(p(8, 14), p(16, 14), colour, w);
		break;
	case Icon::OUTPUTS:
		list->AddCircle(p(12, 12), 3.0f * k, colour, 0, w);
		list->AddLine(p(12, 3), p(12, 6), colour, w);
		list->AddLine(p(12, 18), p(12, 21), colour, w);
		list->AddLine(p(3, 12), p(6, 12), colour, w);
		list->AddLine(p(18, 12), p(21, 12), colour, w);
		break;
	case Icon::LINK:
		/* Four cabinets, each linked to the next round a ring */
		for (const ImVec2 c : { ImVec2{ 6, 6 }, ImVec2{ 18, 6 }, ImVec2{ 18, 18 }, ImVec2{ 6, 18 } })
			list->AddRect(p(c.x - 2.5f, c.y - 2.5f), p(c.x + 2.5f, c.y + 2.5f), colour, 0.0f, 0, w);
		list->AddLine(p(9.5f, 6), p(14.5f, 6), colour, w);
		list->AddLine(p(18, 9.5f), p(18, 14.5f), colour, w);
		list->AddLine(p(14.5f, 18), p(9.5f, 18), colour, w);
		list->AddLine(p(6, 14.5f), p(6, 9.5f), colour, w);
		break;
	case Icon::LAMP:
		/* The bulb over its top, from its left foot to its right one */
		arc(12, 9, 6, 131.6f, 408.4f);
		list->PathLineTo(p(14.5f, 17));
		list->PathLineTo(p(9.5f, 17));
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		list->AddLine(p(9, 18), p(15, 18), colour, w);
		list->AddLine(p(10, 21), p(14, 21), colour, w);
		break;
	case Icon::RUMBLE:
		list->AddRect(p(8, 4), p(16, 20), colour, 2.0f * k, ImDrawFlags_None, w);
		list->AddLine(p(4, 8), p(4, 16), colour, w);
		list->AddLine(p(20, 8), p(20, 16), colour, w);
		list->AddLine(p(2, 10), p(2, 14), colour, w);
		list->AddLine(p(22, 10), p(22, 14), colour, w);
		break;
	case Icon::COIN:
		/* A coin and its rim */
		list->AddCircle(p(12, 12), 8.0f * k, colour, 0, w);
		list->AddCircle(p(12, 12), 5.0f * k, colour, 0, w);
		break;
	case Icon::LOCK:
		list->AddRect(p(5, 11), p(19, 20), colour, 2.0f * k, ImDrawFlags_None, w);
		list->PathLineTo(p(8, 11));
		arc(12, 8, 4, 180, 360);
		list->PathLineTo(p(16, 11));
		list->PathStroke(colour, ImDrawFlags_None, w);
		break;
	case Icon::FOLDER:
		list->PathLineTo(p(3, 7));
		arc(5, 7, 2, 180, 270);
		list->PathLineTo(p(9, 5));
		list->PathLineTo(p(11, 7));
		arc(19, 9, 2, 270, 360);
		arc(19, 17, 2, 0, 90);
		arc(5, 17, 2, 90, 180);
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		break;
	case Icon::ABOUT:
		list->AddCircle(p(12, 12), 9.0f * k, colour, 0, w);
		list->AddLine(p(12, 11), p(12, 17), colour, w);
		list->AddLine(p(12, 7.5f), p(12, 8.0f), colour, w);
		break;
	case Icon::LIST:
		for (const float y : { 7.0f, 12.0f, 17.0f }) {
			list->AddLine(p(9, y), p(20, y), colour, w);
			list->AddCircleFilled(p(4.5f, y), w * 0.8f, colour);
		}
		break;
	case Icon::SWAP:
		lines({ { 4, 8 }, { 20, 8 } }, false);
		lines({ { 16, 4 }, { 20, 8 }, { 16, 12 } }, false);
		lines({ { 20, 16 }, { 4, 16 } }, false);
		lines({ { 8, 12 }, { 4, 16 }, { 8, 20 } }, false);
		break;
	case Icon::DEFAULTS:
		/* Round from the top, the way of the clock, back to the top */
		arc(12, 13, 7, -60, 200);
		list->PathStroke(colour, ImDrawFlags_None, w);
		lines({ { 11, 3 }, { 15.5f, 6.9f }, { 10.5f, 9.5f } }, false);
		break;
	case Icon::CLEAR:
		list->AddLine(p(6, 6), p(18, 18), colour, w);
		list->AddLine(p(18, 6), p(6, 18), colour, w);
		break;
	case Icon::SETTINGS:
		/* A wheel with eight teeth round a hole */
		list->AddCircle(p(12, 12), 2.5f * k, colour, 0, w);
		list->AddCircle(p(12, 12), 6.5f * k, colour, 0, w);
		for (int t = 0; t < 8; t++) {
			const float angle = static_cast<float>(t) * std::numbers::pi_v<float> / 4.0f;
			const float c = std::cos(angle), s = std::sin(angle);

			list->AddLine(p(12 + 6.5f * c, 12 + 6.5f * s), p(12 + 9.5f * c, 12 + 9.5f * s), colour, 1.6f * w);
		}
		break;
	case Icon::MAP:
		lines({ { 6, 6 }, { 14, 12 }, { 6, 18 } }, true);
		list->AddLine(p(18, 6), p(18, 18), colour, w);
		break;
	case Icon::UNDO:
		lines({ { 9, 14 }, { 4, 9 }, { 9, 4 } }, false);
		list->PathLineTo(p(4, 9));
		list->PathLineTo(p(14, 9));
		arc(14, 15, 6, -90, 90);
		list->PathLineTo(p(11, 21));
		list->PathStroke(colour, ImDrawFlags_None, w);
		break;
	case Icon::RUNE:
		for (const auto &[x, y] : RUNE_OUTLINE)
			list->PathLineTo(p(x, y));
		list->PathFillConcave(colour);
		break;
	}
}

bool
MenuKit::focus(ImVec2 a, ImVec2 b, float out) const
{
	if (!m_focus || m_focus->count++ != m_focus->at)
		return false;
	m_focus->tip = m_row_tip;
	frame(ImGui::GetWindowDrawList(), { a.x - out, a.y - out }, { b.x + out, b.y + out }, px(CONTROL_CUT), 0,
	      m_palette.accent);
	/* The page scrolls to show it all */
	if (m_focus->moved) {
		const float top = ImGui::GetWindowPos().y, bottom = top + ImGui::GetWindowHeight(), room = px(6.0f);

		if (a.y - room < top)
			ImGui::SetScrollY(ImGui::GetScrollY() - (top - a.y) - room);
		else if (b.y + room > bottom)
			ImGui::SetScrollY(ImGui::GetScrollY() + (b.y - bottom) + room);
	}
	return true;
}

bool
MenuKit::tabs(std::span<const char *const> names, std::span<const Icon> icons, bool narrow, int &chosen,
              std::span<const std::string> pills, bool focusable) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const float wide = px(8.0f), high = px(CELL_HIGH + 1.0f), gap = px(4.0f), line = ImGui::GetTextLineHeight();
	const float h = line + 2.0f * high, side = line, space = px(5.0f);
	const int was = chosen;
	const auto shows = [&](size_t i) { return !narrow || static_cast<int>(i) == chosen; };
	const auto tab_w = [&](size_t i) {
		return side + (shows(i) ? space + ImGui::CalcTextSize(names[i]).x : 0.0f) + 2.0f * wide;
	};
	const auto pill_w = [&](size_t i) { return i < pills.size() && !pills[i].empty() ? pill_size(pills[i].c_str()).x : 0.0f; };
	const float pills_room = px(12.0f);
	const auto whole = [&] {
		float total = -gap + pill_w(0) + pill_w(1) + (pill_w(0) > 0.0f ? pills_room : 0.0f) +
		              (pill_w(1) > 0.0f ? pills_room : 0.0f);

		for (size_t i = 0; i < names.size(); i++)
			total += tab_w(i) + gap;
		return total;
	};
	float total = whole();
	const int count = static_cast<int>(names.size());

	/* The gamepad: left or right to the tab beside, the row laid out again */
	if (focusable && focus({ at.x + std::max(0.0f, (width - total) / 2.0f), at.y },
	                       { at.x + std::max(0.0f, (width - total) / 2.0f) + total, at.y + h }, px(3.0f)) &&
	    this->side()) {
		chosen = ((chosen + this->side()) % count + count) % count;
		total = whole();
	}

	float x = at.x + std::max(0.0f, (width - total) / 2.0f);

	const auto draw_pill = [&](size_t i) {
		const ImVec2 size = pill_size(pills[i].c_str());

		pill(list, { x, at.y + (h - size.y) / 2.0f }, pills[i].c_str());
		x += size.x + pills_room;
	};

	if (pill_w(0) > 0.0f)
		draw_pill(0);
	for (size_t i = 0; i < names.size(); i++) {
		const float w = tab_w(i);
		const ImVec2 a = { x, at.y }, b = { x + w, at.y + h };

		ImGui::SetCursorScreenPos(a);
		if (ImGui::InvisibleButton(names[i], { w, h }))
			chosen = static_cast<int>(i);

		const bool on = chosen == static_cast<int>(i);
		const ImU32 colour = on ? m_palette.action : m_palette.dim;

		if (on)
			frame(list, a, b, px(CONTROL_CUT), 0, alpha(m_palette.action, ACTION_LINE));
		if (i < icons.size())
			icon(list, icons[i], { a.x + wide, a.y + high }, side, colour);
		if (shows(i))
			list->AddText({ a.x + wide + side + space, a.y + high }, colour, names[i]);
		x += w + gap;
	}
	if (pill_w(1) > 0.0f) {
		x += pills_room - gap;
		draw_pill(1);
	}
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	return chosen != was;
}

void
MenuKit::group_title(const char *title)
{
	ImGui::Dummy({ 0.0f, px(10.0f) });
	small_caps(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), m_palette.cabinet, title);
	ImGui::Dummy({ 0.0f, font(SMALL) + px(5.0f) });
}

void
MenuKit::begin_group(const char *title)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();

	group_title(title);
	m_group_top = ImGui::GetCursorScreenPos();
	m_first_row = true;
	m_row_tip.clear();
	/* The rows on top, the frame drawn under them once their height is known */
	list->ChannelsSplit(2);
	list->ChannelsSetCurrent(1);
}

void
MenuKit::end_group()
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 end = ImGui::GetCursorScreenPos();

	list->ChannelsSetCurrent(0);
	frame(list, m_group_top, { m_group_top.x + ImGui::GetContentRegionAvail().x, end.y }, px(GROUP_CUT), 0,
	      m_palette.line);
	list->ChannelsMerge();
	/* What comes after the group is not its last row */
	m_row_tip.clear();
}

ImVec2
MenuKit::begin_row(float &width)
{
	const ImVec2 at = ImGui::GetCursorScreenPos();

	m_row_tip = std::move(m_next_tip);
	m_next_tip.clear();
	width = ImGui::GetContentRegionAvail().x;
	if (!m_first_row)
		ImGui::GetWindowDrawList()->AddLine({ at.x + 1.0f, at.y }, { at.x + width - 1.0f, at.y }, m_palette.line,
		                                    stroke());
	m_first_row = false;
	return at;
}

float
MenuKit::ask_width() const
{
	return px(ASK_GAP) + ImGui::GetFont()->CalcTextSizeA(font(NOTE), FLT_MAX, 0.0f, ASK).x;
}

/* The name, what the row does and why it does not change, one under the
 * other in that width, the last two smaller and fainter; the name faint
 * too on a row that cannot change now (`off`): their height. A (?) after
 * the name when the row has a tip, shown while the mouse is on the name. */
float
MenuKit::texts(ImVec2 at, float width, const char *name, const char *about, const char *locked, bool off,
               const char *warning) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	ImFont *const face = ImGui::GetFont();
	const float space = px(2.0f), note = font(NOTE), size = ImGui::GetFontSize();
	float h = wrapped_h(name, size, width);

	list->AddText(face, size, at, off ? m_palette.faint : m_palette.ink, name, nullptr, width);
	if (!m_row_tip.empty()) {
		const float x = at.x + std::min(ImGui::CalcTextSize(name).x, width - ask_width()) + px(ASK_GAP);

		list->AddText(face, note, { x, at.y + (size - note) / 2.0f }, m_palette.faint, ASK);
		if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(at, { at.x + width, at.y + h }))
			tip(m_row_tip.c_str());
	}
	for (const char *text : { warning ? warning : about, locked }) {
		if (!text || !*text)
			continue;
		list->AddText(face, note, { at.x, at.y + h + space }, text == warning ? m_palette.danger : m_palette.faint, text,
		              nullptr, width);
		h += space + wrapped_h(text, note, width);
	}
	return h;
}

MenuKit::Placed
MenuKit::place(ImVec2 at, float width, float control, float control_h, const char *name, const char *about,
               const char *locked, bool off, const char *warning) const
{
	const float pad = px(ROW_PAD), beside = width - control - px(GAP) - 2.0f * pad;
	/* A name and its (?) that do not fit beside the control: the control
	 * under them */
	const bool under = beside < px(TEXT_LEAST) ||
	                   (!m_row_tip.empty() && beside < ImGui::CalcTextSize(name).x + ask_width());
	const float text_h = texts({ at.x + pad, at.y + pad }, under ? width - 2.0f * pad : beside, name, about, locked,
	                           off, warning);

	if (under)
		return { text_h + px(6.0f) + control_h + 2.0f * pad, at.y + pad + text_h + px(6.0f) };

	const float h = std::max(text_h, control_h) + 2.0f * pad;

	return { h, at.y + (h - control_h) / 2.0f };
}

bool
MenuKit::choice(const char *name, const char *about, std::span<const char *const> labels, int &chosen,
                const char *locked, bool off, const char *warning)
{
	ImGui::PushID(name);

	const float pad = px(ROW_PAD);
	const ImVec2 control = segments_size(labels);
	const int was = chosen, last = static_cast<int>(labels.size()) - 1;
	float width;
	const ImVec2 at = begin_row(width);
	const Placed placed = place(at, width, control.x, control.y, name, about, locked, off, warning);

	/* The gamepad: left or right to the next label, A to the next one round */
	if (focus(at, { at.x + width, at.y + placed.h }) && !locked && !off) {
		if (side())
			chosen = std::clamp(std::max(chosen, 0) + side(), 0, last);
		else if (pressed())
			chosen = chosen >= last ? 0 : chosen + 1;
	}

	const bool changed = segments({ at.x + width - pad - control.x, placed.top }, labels, chosen, locked || off) ||
	                     chosen != was;

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	ImGui::PopID();
	return changed;
}

bool
MenuKit::choice(const char *name, const char *about, std::span<const char *const> labels, bool &on,
                const char *locked, bool off, const char *warning)
{
	int chosen = on ? 1 : 0;

	if (!choice(name, about, labels, chosen, locked, off, warning))
		return false;
	on = chosen == 1;
	return true;
}

ImVec2
MenuKit::segments_size(std::span<const char *const> labels) const
{
	float w = 0.0f;

	for (const char *label : labels)
		w += measure(label, font(SMALL)) + 2.0f * px(CELL_WIDE);
	return { w, font(SMALL) + 2.0f * px(CELL_HIGH) };
}

bool
MenuKit::segments(ImVec2 a, std::span<const char *const> labels, int &chosen, bool still) const
{
	IM_ASSERT(!labels.empty() && labels.size() <= MOST_LABELS);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float wide = px(CELL_WIDE), high = px(CELL_HIGH), size = font(SMALL);
	const float control_h = size + 2.0f * high;
	const ImU32 outline = faded(alpha(m_palette.action, ACTION_LINE), still);
	std::array<float, MOST_LABELS> cells;
	float control = 0.0f;

	for (size_t i = 0; i < labels.size(); i++)
		control += cells[i] = measure(labels[i], size) + 2.0f * wide;

	const int was = chosen;
	float x = a.x;

	ImGui::BeginDisabled(still);
	for (size_t i = 0; i < labels.size(); i++) {
		const ImVec2 ca = { x, a.y }, cb = { x + cells[i], a.y + control_h };

		ImGui::SetCursorScreenPos(ca);
		if (ImGui::InvisibleButton(labels[i], { cells[i], control_h }))
			chosen = static_cast<int>(i);

		const bool on = chosen == static_cast<int>(i);

		if (on)
			fill_cell(list, ca, cb, px(CONTROL_CUT), i == 0, i + 1 == labels.size(), faded(m_palette.action, still));
		if (i > 0)
			list->AddLine(ca, { x, cb.y }, outline, stroke());
		write(list, { x + wide, a.y + high },
		      faded(on ? m_palette.on_action : m_palette.dim, still), labels[i], size);
		x += cells[i];
	}
	ImGui::EndDisabled();
	frame(list, a, { a.x + control, a.y + control_h }, px(CONTROL_CUT), 0, outline);
	return chosen != was;
}

bool
MenuKit::dropdown(const char *name, const char *about, std::span<const std::string> labels, int &chosen)
{
	return dropdown(name, about, plain(labels), chosen);
}

bool
MenuKit::dropdown(const char *name, const char *about, std::span<const Item> items, int &chosen)
{
	ImGui::PushID(name);

	const float pad = px(ROW_PAD), wide = px(CELL_WIDE + 1.0f), caret = measure("▾");
	const float control_h = picker_height();
	float longest = 0.0f, width;

	for (const Item &item : items)
		longest = std::max(longest, before_name(item, Icon::NONE) + measure(item.name.c_str()));

	const ImVec2 at = begin_row(width);
	/* A pixel more: the longest would be cut by the rounding of picker() */
	const float control = std::min(longest + 3.0f * wide + caret + px(1.0f), width / 2.0f);
	const Placed placed = place(at, width, control, control_h, name, about, nullptr);
	const bool changed =
		picker("box", { at.x + width - pad - control, placed.top }, control, items, chosen, Icon::NONE, nullptr);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	ImGui::PopID();
	return changed;
}

float
MenuKit::picker_height() const
{
	return ImGui::GetTextLineHeight() + 2.0f * px(CELL_HIGH);
}


bool
MenuKit::picker(const char *id, ImVec2 at, float width, std::span<const Item> items, int &chosen, Icon icon_shown,
                const char *foot) const
{
	ImGui::PushID(id);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &p = m_palette;
	const float wide = px(CELL_WIDE + 1.0f), high = px(CELL_HIGH), caret = measure("▾");
	const ImVec2 b = { at.x + width, at.y + picker_height() };
	const int was = chosen;

	const bool focused = focus(at, b, px(3.0f));

	ImGui::SetCursorScreenPos(at);
	if (ImGui::InvisibleButton("open", { width, b.y - at.y }) || (focused && pressed()))
		ImGui::OpenPopup("list");
	frame(list, at, b, px(GROUP_CUT), 0, alpha(p.action, ACTION_LINE));
	if (chosen >= 0 && static_cast<size_t>(chosen) < items.size()) {
		const Item &item = items[static_cast<size_t>(chosen)];
		const float side = ImGui::GetTextLineHeight(), before = before_name(item, icon_shown);

		icon(list, icon_shown, { at.x + wide, at.y + high }, side, p.ink);
		colours(list, { at.x + wide + before - colours_width(item) - px(6.0f), at.y + high }, item);
		list->PushClipRect(at, { b.x - wide - caret, b.y }, true);
		write(list, { at.x + wide + before, at.y + high }, p.ink,
		      fitted(item.name, b.x - 2.0f * wide - caret - at.x - wide - before).c_str());
		list->PopClipRect();
	}
	write(list, { b.x - wide - caret, at.y + high }, p.dim, "▾");

	list_popup({ at.x, b.y + px(2.0f) }, width, items, chosen, foot, at.y - px(2.0f));
	ImGui::PopID();
	return chosen != was;
}

bool
MenuKit::list_popup(ImVec2 at, float width, std::span<const Item> items, int &chosen, const char *foot,
                    float above) const
{
	const MenuPalette &p = m_palette;
	const int was = chosen;
	const float room = ImGui::GetIO().DisplaySize.x, margin = px(8.0f);
	const float line = ImGui::GetTextLineHeight(), note = font(NOTE), tiny = font(TINY), gap = px(12.0f);
	/* A word at the right of a line: in small capitals in a pill, or plain */
	const auto tag_w = [&](const Item &item) {
		if (item.tag.empty())
			return 0.0f;
		return item.strong ? measure(item.tag.c_str(), tiny, tiny * TITLE_SPACING) + 2.0f * px(5.0f) :
		                     measure(item.tag.c_str(), note);
	};
	const auto before = [&](const Item &item) { return before_name(item, item.icon); };
	float longest = width;

	/* As wide as its longest line, moved left where it would leave the
	 * window */
	for (const Item &item : items) {
		const float tag = item.tag.empty() ? 0.0f : gap + tag_w(item);
		const float text = std::max(measure(item.name.c_str()) + tag, measure(item.about.c_str(), note));

		longest = std::max(longest, before(item) + text + 2.0f * px(CELL_WIDE) + 2.0f * px(4.0f));
	}
	longest = std::min(longest, room - 2.0f * margin);

	const float pad = std::round(px(4.0f)), spacing = px(2.0f), high = px(CELL_HIGH + 1.0f), under = px(2.0f);
	float tall = 2.0f * pad;

	for (const Item &item : items)
		tall += line + 2.0f * high + (item.about.empty() ? 0.0f : under + note) + spacing;
	if (foot)
		tall += wrapped_h(foot, note, longest - 2.0f * pad - 2.0f * px(CELL_WIDE)) + px(6.0f);
	if (above >= 0.0f && at.y + tall > ImGui::GetIO().DisplaySize.y - margin)
		at.y = std::max(margin, above - tall);
	ImGui::SetNextWindowPos({ std::max(margin, std::min(at.x, room - margin - longest)), at.y });
	ImGui::SetNextWindowSizeConstraints({ longest, 0.0f }, { longest, ImGui::GetIO().DisplaySize.y });
	ImGui::PushStyleColor(ImGuiCol_PopupBg, p.screen);
	ImGui::PushStyleColor(ImGuiCol_Border, p.stroke);
	ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32_BLACK_TRANS);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, p.hover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, p.hover);
	ImGui::PushStyleColor(ImGuiCol_Text, p.ink);
	ImGui::PushStyleColor(ImGuiCol_NavCursor, p.accent);
	/* In whole pixels: the window is, and a fraction more would scroll */
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { pad, pad });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, spacing });
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.0f, 0.5f });
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, stroke());
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	if (ImGui::BeginPopup("list")) {
		ImDrawList *const list = ImGui::GetWindowDrawList();

		for (size_t i = 0; i < items.size(); i++) {
			const Item &item = items[i];
			const bool on = static_cast<int>(i) == chosen;
			const float h = line + 2.0f * high + (item.about.empty() ? 0.0f : under + note);
			const ImVec2 a = ImGui::GetCursorScreenPos();

			const ImU32 ink = item.off ? p.faint : p.ink;

			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable("##item", on, item.off ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None,
			                      { 0.0f, h }))
				chosen = static_cast<int>(i);
			ImGui::PopID();

			const ImVec2 b = ImGui::GetItemRectMax();
			const float x = a.x + px(CELL_WIDE) + before(item);

			if (on)
				frame(list, ImGui::GetItemRectMin(), b, px(CONTROL_CUT), 0, p.stroke);
			icon(list, item.icon, { a.x + px(CELL_WIDE), a.y + high }, line, ink);
			colours(list, { x - colours_width(item) - px(6.0f), a.y + high }, item);
			write(list, { x, a.y + high }, ink, item.name.c_str());
			if (item.strong && !item.tag.empty()) {
				const ImVec2 ta = { b.x - px(CELL_WIDE) - tag_w(item), a.y + high + (line - tiny) / 2.0f - px(2.0f) };
				const ImVec2 tb = { b.x - px(CELL_WIDE), ta.y + tiny + px(4.0f) };

				list->AddRect(ta, tb, p.outline, (tb.y - ta.y) / 2.0f, ImDrawFlags_None, stroke());
				write(list, { ta.x + px(5.0f), ta.y + px(2.0f) }, p.ink, item.tag.c_str(), tiny, tiny * TITLE_SPACING);
			} else if (!item.tag.empty()) {
				write(list, { b.x - px(CELL_WIDE) - tag_w(item), a.y + high + (line - note) / 2.0f }, p.faint,
				      item.tag.c_str(), note);
			}
			if (!item.about.empty())
				write(list, { x, a.y + high + line + under }, item.off ? p.faint : p.dim, item.about.c_str(), note);
		}
		if (foot) {
			const float w = ImGui::GetContentRegionAvail().x - 2.0f * px(CELL_WIDE);
			const ImVec2 f = ImGui::GetCursorScreenPos();

			list->AddText(ImGui::GetFont(), note, { f.x + px(CELL_WIDE), f.y + px(4.0f) }, p.faint, foot, nullptr, w);
			ImGui::Dummy({ w, wrapped_h(foot, note, w) + px(6.0f) });
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(7);
	return chosen != was;
}

/* Squares three quarters of a line, two points apart, outlined */
float
MenuKit::colours_width(const Item &item) const
{
	const float side = std::round(ImGui::GetTextLineHeight() * 0.75f), gap = px(2.0f);
	const auto shown = std::count_if(item.colours.begin(), item.colours.end(),
	                                 [](ImU32 colour) { return (colour & IM_COL32_A_MASK) != 0; });

	return shown ? static_cast<float>(shown) * (side + gap) - gap : 0.0f;
}

float
MenuKit::before_name(const Item &item, Icon icon_shown) const
{
	const float shown = colours_width(item), gap = px(6.0f);

	return (icon_shown != Icon::NONE ? ImGui::GetTextLineHeight() + gap : 0.0f) + (shown ? shown + gap : 0.0f);
}

void
MenuKit::colours(ImDrawList *list, ImVec2 at, const Item &item) const
{
	const float line = ImGui::GetTextLineHeight(), side = std::round(line * 0.75f), gap = px(2.0f);
	const float y = std::round(at.y + (line - side) / 2.0f);
	float x = std::round(at.x);

	for (const ImU32 colour : item.colours) {
		if (!(colour & IM_COL32_A_MASK))
			continue;
		list->AddRectFilled({ x, y }, { x + side, y + side }, colour);
		list->AddRect({ x, y }, { x + side, y + side }, m_palette.outline, 0.0f, ImDrawFlags_None, stroke());
		x += side + gap;
	}
}

ImVec2
MenuKit::button_size(const char *label, Icon icon_shown) const
{
	const float size = font(SMALL), side = icon_shown != Icon::NONE ? size + px(4.0f) : 0.0f;

	return { side + measure(label, size) + 2.0f * px(BUTTON_WIDE), size + 2.0f * px(BUTTON_HIGH) };
}

bool
MenuKit::button(const char *label, bool same_line, Icon icon_shown, Tone tone) const
{
	if (same_line)
		ImGui::SameLine(0.0f, px(10.0f));

	const ImVec2 at = ImGui::GetCursorScreenPos();
	const bool clicked = button(label, at, false, icon_shown, tone);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy(button_size(label, icon_shown));
	return clicked;
}

bool
MenuKit::button(const char *label, ImVec2 at, bool on, Icon icon_shown, Tone tone) const
{
	const ImVec2 box = button_size(label, icon_shown);
	const bool focused = focus(at, { at.x + box.x, at.y + box.y }, px(3.0f));

	return press_button(label, at, on, icon_shown, tone) || (focused && pressed());
}

bool
MenuKit::press_button(const char *label, ImVec2 at, bool on, Icon icon_shown, Tone tone) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float size = font(SMALL);
	const ImVec2 box = button_size(label, icon_shown), b = { at.x + box.x, at.y + box.y };

	ImGui::SetCursorScreenPos(at);

	const bool clicked = ImGui::InvisibleButton(label, box);
	const bool hovered = ImGui::IsItemHovered();
	const bool strong = tone == Tone::STRONG && !hovered;
	const ImU32 hot = tone == Tone::DANGER ? m_palette.danger : m_palette.action;
	const ImU32 border = strong ? m_palette.stroke : hovered || on ? hot : alpha(hot, ACTION_LINE);
	const ImU32 ink = strong ? m_palette.ink : hot;
	float x = at.x + px(BUTTON_WIDE);

	frame(list, at, b, px(CONTROL_CUT), on ? alpha(m_palette.action, CHOSEN_FILL) : 0, border);
	if (icon_shown != Icon::NONE) {
		icon(list, icon_shown, { x, at.y + px(BUTTON_HIGH) }, size, ink);
		x += size + px(4.0f);
	}
	write(list, { x, at.y + px(BUTTON_HIGH) }, ink, label, size);
	return clicked;
}

ImVec2
MenuKit::arrow_size() const
{
	return { px(22.0f), font(SMALL) + 2.0f * px(BUTTON_HIGH) };
}

bool
MenuKit::arrow(const char *id, ImVec2 at, bool right) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 size = arrow_size(), b = { at.x + size.x, at.y + size.y };
	const char *const sign = right ? "›" : "‹";
	const float big = font(1.15f);

	ImGui::SetCursorScreenPos(at);

	const bool clicked = ImGui::InvisibleButton(id, size);

	list->AddRect(at, b, alpha(m_palette.action, ACTION_LINE), size.y / 2.0f, ImDrawFlags_None, stroke());
	write(list, { (at.x + b.x - measure(sign, big)) / 2.0f, (at.y + b.y - big) / 2.0f }, m_palette.action, sign, big);
	return clicked;
}

void
MenuKit::small_caps(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text)
{
	const float size = font(SMALL);

	write(list, at, colour, text, size, size * TITLE_SPACING);
}

float
MenuKit::small_caps_width(const char *text)
{
	const float size = font(SMALL);

	return measure(text, size, size * TITLE_SPACING);
}

void
MenuKit::tip(const char *text) const
{
	const MenuPalette &p = m_palette;

	ImGui::PushStyleColor(ImGuiCol_PopupBg, p.screen);
	ImGui::PushStyleColor(ImGuiCol_Border, p.stroke);
	ImGui::PushStyleColor(ImGuiCol_Text, p.ink);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { std::round(px(6.0f)), std::round(px(3.0f)) });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, stroke());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	if (ImGui::BeginTooltip()) {
		/* A sentence goes on to the next line rather than off the window */
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * TIP_EMS);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(3);
}

void
MenuKit::text(const char *name, const char *about, const char *value)
{
	this->value(name, about, value, nullptr);
}

bool
MenuKit::value(const char *name, const char *about, const char *value, const char *action, const char *word,
               const char *locked)
{
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float pad = px(ROW_PAD), space = px(8.0f), size = font(SMALL), small = font(TINY);
	const char *const shown_button = locked ? nullptr : action;
	const char *const shown_word = shown_button ? nullptr : word;
	ImFont *const face = ImGui::GetFont();
	const ImVec2 button_box = shown_button ? button_size(shown_button) : ImVec2{};
	const float word_w = shown_word ? measure(shown_word, small, small * TITLE_SPACING) : 0.0f;
	const float after = shown_button ? button_box.x + space : shown_word ? word_w + space : 0.0f;
	float width;
	const ImVec2 at = begin_row(width);
	/* A long value takes half the row at most, on more lines; the whole
	 * row under the texts, where they have not room enough (a pixel more:
	 * a text as wide as its room may go to the line) */
	const float wide_w = std::ceil(face->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x) + 1.0f;
	const bool under = width - std::min(wide_w, width / 2.0f - after) - after - px(GAP) - 2.0f * pad < px(TEXT_LEAST);
	const float value_w = std::max(px(20.0f), std::min(wide_w, (under ? width - 2.0f * pad : width / 2.0f) - after));
	const ImVec2 value_size = face->CalcTextSizeA(size, FLT_MAX, value_w, value);
	const float control_h = std::max(value_size.y, button_box.y);
	const Placed placed = place(at, width, value_w + after, control_h, name, about, locked);
	const float h = placed.h, mid = placed.top + control_h / 2.0f;
	const float right = at.x + width - pad;
	/* The gamepad presses its button */
	bool clicked = shown_button && focus(at, { at.x + width, at.y + h }) && pressed();

	list->AddText(face, size, { right - after - value_w, mid - value_size.y / 2.0f }, m_palette.dim, value, nullptr,
	              value_w);
	if (shown_button) {
		m_button_a = { right - button_box.x, mid - button_box.y / 2.0f };
		m_button_b = { m_button_a.x + button_box.x, m_button_a.y + button_box.y };
		clicked = press_button(shown_button, m_button_a, false, Icon::NONE, Tone::PLAIN) || clicked;
	} else if (shown_word) {
		write(list, { right - word_w, mid - small / 2.0f }, m_palette.faint, shown_word, small, small * TITLE_SPACING);
	}
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return clicked;
}

bool
MenuKit::edit(const char *name, const char *about, std::string &text, const char *hint, size_t most,
              const char *locked)
{
	ImGui::PushID(name);

	const float pad = px(ROW_PAD), field_h = ImGui::GetFrameHeight();
	float width;
	const ImVec2 at = begin_row(width);
	const float field_w = std::min(px(230.0f), width / 2.0f);
	const Placed placed = place(at, width, field_w, field_h, name, about, locked);
	const ImVec2 a = { at.x + width - pad - field_w, placed.top }, b = { a.x + field_w, a.y + field_h };
	std::vector<char> buffer(most + 1, 0);
	bool done = false;

	/* The gamepad: A puts the keyboard in the field */
	if (focus(at, { at.x + width, at.y + placed.h }) && pressed() && !locked)
		ImGui::SetKeyboardFocusHere();
	text.copy(buffer.data(), std::min(text.size(), most));
	ImGui::SetCursorScreenPos({ a.x + px(6.0f), a.y });
	ImGui::SetNextItemWidth(field_w - px(12.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
	ImGui::PushStyleColor(ImGuiCol_Text, locked ? m_palette.faint : m_palette.ink);
	ImGui::PushStyleColor(ImGuiCol_TextDisabled, m_palette.faint);
	ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, alpha(m_palette.action, CHOSEN_FILL));
	if (ImGui::InputTextWithHint("##field", hint, buffer.data(), buffer.size(),
	                             locked ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_CharsNoBlank))
		text = buffer.data();
	done = ImGui::IsItemDeactivatedAfterEdit();

	const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();

	ImGui::PopStyleColor(6);
	frame(ImGui::GetWindowDrawList(), a, b, px(CONTROL_CUT), 0,
	      active ? m_palette.action : hovered && !locked ? alpha(m_palette.action, ACTION_LINE) : m_palette.outline);
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	ImGui::PopID();
	return done;
}

bool
MenuKit::value_list(const char *name, const char *about, const char *value, const char *action,
                    std::span<const std::string> labels, int &chosen, const char *locked)
{
	const int was = chosen;

	ImGui::PushID(name);
	if (this->value(name, about, value, action, nullptr, locked))
		ImGui::OpenPopup("list");
	list_popup({ m_button_a.x, m_button_b.y + px(2.0f) }, m_button_b.x - m_button_a.x, plain(labels), chosen, nullptr,
	           m_button_a.y - px(2.0f));
	ImGui::PopID();
	return chosen != was;
}

bool
MenuKit::slider(const char *name, const char *about, float &value, float low, float high, float step, const char *shown,
                const char *locked, bool off)
{
	ImGui::PushID(name);

	const bool still = locked || off;
	const float pad = px(ROW_PAD), control = track_size(shown, "-60 dB").x, line = ImGui::GetTextLineHeight();
	float width;
	const ImVec2 at = begin_row(width);
	const Placed placed = place(at, width, control, line, name, about, locked, off);
	const bool focused = focus(at, { at.x + width, at.y + placed.h });
	const bool moved = track({ at.x + width - pad - control, placed.top }, value, low, high, step, shown, focused, still);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	ImGui::PopID();
	return moved;
}

ImVec2
MenuKit::custom_row(const char *name, const char *about, float control, float control_h)
{
	const float pad = px(ROW_PAD);
	float width;
	const ImVec2 at = begin_row(width);
	const Placed placed = place(at, width, control, control_h, name, about, nullptr);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	return { at.x + width - pad - control, placed.top };
}

ImVec2
MenuKit::track_size(const char *shown, const char *widest) const
{
	const float size = font(SMALL);

	return { px(TRACK_W) + px(TRACK_GAP) + std::max(measure(shown, size), measure(widest, size)), ImGui::GetTextLineHeight() };
}

bool
MenuKit::track(ImVec2 at, float &value, float low, float high, float step, const char *shown, bool focused,
               bool still) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float track_w = px(TRACK_W), track_h = px(6.0f), space = px(TRACK_GAP), size = font(SMALL);
	const float line = ImGui::GetTextLineHeight(), mid = at.y + line / 2.0f;
	const ImVec2 a = { at.x, mid - track_h / 2.0f }, b = { a.x + track_w, a.y + track_h };
	const float was = value;

	/* The gamepad: left or right, a twentieth of the way */
	if (focused && !still && side()) {
		const float jump = std::max(step, std::round((high - low) / 20.0f / step) * step);

		value = std::clamp(value + static_cast<float>(side()) * jump, low, high);
	}

	/* The track takes the mouse a little above and below it */
	ImGui::SetCursorScreenPos(at);
	ImGui::BeginDisabled(still);
	ImGui::InvisibleButton("track", { track_w, line });

	const bool hovered = !still && ImGui::IsItemHovered();

	if (!still && ImGui::IsItemActive()) {
		const float t = std::clamp((ImGui::GetIO().MousePos.x - a.x) / track_w, 0.0f, 1.0f);

		value = low + std::round(t * (high - low) / step) * step;
	} else if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
		value = std::clamp(value + (ImGui::GetIO().MouseWheel > 0.0f ? step : -step), low, high);
	}
	ImGui::EndDisabled();

	const float fill = (std::clamp(value, low, high) - low) / (high - low);
	const float r = track_h / 2.0f;

	list->AddRectFilled(a, { a.x + track_w * fill, b.y }, faded(m_palette.action, still), r);
	list->AddRect(a, b, faded(alpha(m_palette.action, ACTION_LINE), still), r, ImDrawFlags_None, stroke());
	write(list, { b.x + space, mid - size / 2.0f }, faded(m_palette.ink, still), shown, size);
	return value != was;
}

ImVec2
MenuKit::key_size(const char *name) const
{
	const float h = font(SMALL) + 2.0f * px(BUTTON_HIGH);

	return { std::max(h, measure(name, font(NOTE)) + 2.0f * px(4.0f)), h };
}

void
MenuKit::key(ImDrawList *list, ImVec2 at, const char *name, bool used, bool lit) const
{
	const ImVec2 size = key_size(name), b = { at.x + size.x, at.y + size.y };
	const float note = font(NOTE), w = measure(name, note);

	list->AddRectFilled(at, b, lit ? m_palette.accent : m_palette.part, px(5.0f));
	list->AddRect(at, b, lit ? m_palette.accent : used ? m_palette.stroke : m_palette.outline, px(5.0f), ImDrawFlags_None,
	              stroke());
	write(list, { at.x + (size.x - w) / 2.0f, at.y + (size.y - note) / 2.0f },
	      lit ? m_palette.on_accent : used ? m_palette.ink : m_palette.faint, name, note);
}

ImVec2
MenuKit::pill_size(const char *name) const
{
	const float h = font(SMALL) + 2.0f * px(BUTTON_HIGH);

	return { std::max(h, measure(name, font(SMALL)) + 2.0f * px(6.0f)), h };
}

void
MenuKit::pill(ImDrawList *list, ImVec2 at, const char *name) const
{
	const ImVec2 size = pill_size(name);
	const float small = font(SMALL), w = measure(name, small);

	list->AddRect(at, { at.x + size.x, at.y + size.y }, m_palette.outline, size.y / 2.0f, ImDrawFlags_None, stroke());
	write(list, { at.x + (size.x - w) / 2.0f, at.y + (size.y - small) / 2.0f }, m_palette.ink, name, small);
}

void
MenuKit::badge(ImDrawList *list, ImVec2 at, const char *label) const
{
	const ImVec2 box = button_size(label);

	frame(list, at, { at.x + box.x, at.y + box.y }, px(CONTROL_CUT), m_palette.accent, 0);
	write(list, { at.x + px(BUTTON_WIDE), at.y + px(BUTTON_HIGH) }, m_palette.on_accent, label, font(SMALL));
}

int
MenuKit::shortcut(const char *name, std::span<const std::vector<std::string>> keys, std::span<const std::string> pad,
                  int catching)
{
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const char press[] = "press...", none[] = "-";
	const float small = font(SMALL), note = font(NOTE);
	const float pad_room = px(ROW_PAD), gap = px(3.0f), apart = px(8.0f), plus = measure("+", small);
	const float cap_h = key_size("x").y;
	const float press_key = measure(press, note) + 2.0f * px(5.0f), press_pad = measure(press, small) + 2.0f * px(7.0f);
	/* A combination: its caps with + between them */
	const auto combination_w = [&](size_t i) {
		if (static_cast<int>(i) == catching)
			return press_key;

		float w = 0.0f;

		for (size_t k = 0; k < keys[i].size(); k++)
			w += key_size(keys[i][k].c_str()).x + (k ? 2.0f * gap + plus : 0.0f);
		return w;
	};
	float keys_w = 0.0f, pad_w = 0.0f;

	for (size_t i = 0; i < keys.size(); i++)
		keys_w += combination_w(i) + (i ? apart : 0.0f);
	/* The buttons as wide as "Guide + Back" at the least */
	if (catching == 2)
		pad_w = press_pad;
	else if (pad.empty())
		pad_w = measure(none, small);
	else
		for (size_t i = 0; i < pad.size(); i++)
			pad_w += pill_size(pad[i].c_str()).x + (i ? 2.0f * gap + plus : 0.0f);
	pad_w = std::max(pad_w, pill_size("Guide").x + pill_size("Back").x + 2.0f * gap + plus);

	float width;
	const ImVec2 at = begin_row(width);
	const float control = keys_w + px(GAP) + pad_w;
	/* Where the name has not room enough beside them, they go under it */
	const Placed placed = place(at, width, control, cap_h, name, nullptr, nullptr);
	const float h = placed.h, y = placed.top;
	float x = at.x + width - pad_room - control;
	int clicked = -1;
	/* The gamepad: left or right to a key or the buttons, A to catch it */
	const bool focused = focus(at, { at.x + width, at.y + h });
	const int slots = static_cast<int>(keys.size()) + 1;
	std::vector<std::pair<ImVec2, ImVec2>> places;

	if (focused) {
		m_focus->part = std::clamp(m_focus->part + side(), 0, slots - 1);
		if (pressed())
			clicked = m_focus->part == slots - 1 ? 2 : m_focus->part;
	}
	for (size_t i = 0; i < keys.size(); i++) {
		const float w = combination_w(i);

		places.push_back({ { x, y }, { x + w, y + cap_h } });
		ImGui::SetCursorScreenPos({ x, y });
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::InvisibleButton("key", { w, cap_h }))
			clicked = static_cast<int>(i);
		ImGui::PopID();
		if (static_cast<int>(i) == catching) {
			list->AddRect({ x, y }, { x + w, y + cap_h }, m_palette.accent, px(5.0f), ImDrawFlags_None, stroke());
			write(list, { x + px(5.0f), y + (cap_h - note) / 2.0f }, m_palette.accent, press, note);
			x += w + apart;
			continue;
		}
		for (size_t k = 0; k < keys[i].size(); k++) {
			const char *const cap = keys[i][k].c_str();
			const float cap_w = key_size(cap).x;

			if (k) {
				write(list, { x + gap, y + (cap_h - small) / 2.0f }, m_palette.ink, "+", small);
				x += 2.0f * gap + plus;
			}
			key(list, { x, y }, cap, true, false);
			x += cap_w;
		}
		x += apart;
	}

	const float pad_x = at.x + width - pad_room - pad_w;

	places.push_back({ { pad_x, y }, { pad_x + pad_w, y + cap_h } });
	if (focused) {
		const auto &[pa, pb] = places[static_cast<size_t>(m_focus->part)];

		frame(list, { pa.x - px(3.0f), pa.y - px(3.0f) }, { pb.x + px(3.0f), pb.y + px(3.0f) }, px(CONTROL_CUT), 0,
		      alpha(m_palette.accent, ACTION_LINE));
	}
	ImGui::SetCursorScreenPos({ pad_x, y });
	if (ImGui::InvisibleButton("pad", { pad_w, cap_h }))
		clicked = 2;
	if (catching == 2) {
		list->AddRect({ pad_x + pad_w - press_pad, y }, { pad_x + pad_w, y + cap_h }, m_palette.accent, cap_h / 2.0f,
		              ImDrawFlags_None, stroke());
		write(list, { pad_x + pad_w - press_pad + px(7.0f), y + (cap_h - small) / 2.0f }, m_palette.accent, press, small);
	} else {
		float right = pad_x + pad_w;

		if (pad.empty())
			write(list, { right - measure(none, small), y + (cap_h - small) / 2.0f }, m_palette.faint, none, small);
		for (size_t i = pad.size(); i-- > 0;) {
			const ImVec2 size = pill_size(pad[i].c_str());

			right -= size.x;
			pill(list, { right, y }, pad[i].c_str());
			if (i) {
				right -= gap + plus;
				write(list, { right, y + (cap_h - small) / 2.0f }, m_palette.faint, "+", small);
				right -= gap;
			}
		}
	}
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return clicked;
}

void
MenuKit::paragraph(const char *title, const char *text)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float width = ImGui::GetContentRegionAvail().x, size = font(SMALL);

	group_title(title);
	list->AddText(ImGui::GetFont(), size, ImGui::GetCursorScreenPos(), m_palette.faint, text, nullptr, width);
	ImGui::Dummy({ 0.0f, wrapped_h(text, size, width) });
}

} /* namespace odin */
