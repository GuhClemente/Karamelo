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

#include "odin/host/ui/pad_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace odin {

namespace {

/* A point of a drawing is in eighths of a unit */
constexpr float EIGHTH = 1.0f / 8.0f;
/* How lit a stick is when its button is pressed, how far it moves before
 * it is outlined, and before its well shows where it goes */
constexpr float STICK_LIT = 0.9f;
constexpr float STICK_HOT = 0.3f;
constexpr float STICK_SHOWN = 0.04f;
/* How much a button sinks when pressed */
constexpr float SINK = 0.07f;
/* A trigger says how far it is pulled from there; the accent from there */
constexpr float TRIGGER_SHOWN = 0.005f;
constexpr float TRIGGER_LIT = 0.02f;
/* The width of the edge drawn around a fill, so that it is smooth */
constexpr float EDGE = 1.0f;

float
axis(int16_t value)
{
	return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

ImU32
with_alpha(ImU32 colour, float alpha)
{
	const float a = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);

	return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a + 0.5f) << IM_COL32_A_SHIFT);
}

ImU32
tone(const MenuPalette &palette, PadTone t)
{
	switch (t) {
	case PadTone::BODY: return palette.body;
	case PadTone::PART: return palette.part;
	case PadTone::LINE: return palette.stroke;
	case PadTone::DETAIL: return palette.outline;
	case PadTone::LIGHT: return palette.ink;
	case PadTone::ARROW: return palette.arrow;
	case PadTone::GOLD: return palette.accent;
	case PadTone::NONE: break;
	}
	return 0;
}

} /* namespace */

PadView::PadView(const PadDrawing &drawing, ImVec2 a, ImVec2 b) : m_drawing(drawing)
{
	const auto &[left, top, right, bottom] = drawing.bounds;
	const float w = right - left, h = bottom - top;

	m_scale = std::max(0.0f, std::min((b.x - a.x) / w, (b.y - a.y) / h));
	m_origin = { (a.x + b.x - w * m_scale) / 2.0f - left * m_scale, (a.y + b.y - h * m_scale) / 2.0f - top * m_scale };
}

std::array<ImVec2, 2>
PadView::box(size_t part) const
{
	const auto &[left, top, right, bottom] = m_drawing.parts[part].box;

	return { at(left, top), at(right, bottom) };
}

float
PadView::value(const PadPart &part, const PadState &now)
{
	switch (part.kind) {
	case PadPartKind::BUTTON: return now.buttons[part.input] ? 1.0f : 0.0f;
	case PadPartKind::TRIGGER: return std::max(0.0f, axis(now.axes[part.input]));
	case PadPartKind::STICK: return now.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK + part.input] ? STICK_LIT : 0.0f;
	case PadPartKind::WELL: break;
	}
	return 0.0f;
}

PadView::Move
PadView::move(const PadPart &part, const PadState &now) const
{
	Move m;

	if (part.kind == PadPartKind::STICK) {
		m.by = { axis(now.axes[2 * part.input]) * part.travel * m_scale,
		         axis(now.axes[2 * part.input + 1]) * part.travel * m_scale };
		return m;
	}

	const float v = value(part, now);

	m.by = { 0.0f, v * part.travel * m_scale };
	if (part.press) {
		m.scale = 1.0f - SINK * v;
		m.about = at((part.box[0] + part.box[2]) / 2.0f, (part.box[1] + part.box[3]) / 2.0f);
	}
	return m;
}

ImVec2
PadView::point(uint32_t index, const Move &m) const
{
	ImVec2 p = at(static_cast<float>(m_drawing.points[2 * index]) * EIGHTH,
	              static_cast<float>(m_drawing.points[2 * index + 1]) * EIGHTH);

	if (m.scale != 1.0f)
		p = { m.about.x + (p.x - m.about.x) * m.scale, m.about.y + (p.y - m.about.y) * m.scale };
	return { p.x + m.by.x, p.y + m.by.y };
}

/* Its triangles, and around them a thin edge of the same colour where no
 * stroke hides the steps of the pixels */
void
PadView::fill(ImDrawList *list, const PadShape &shape, const Move &m, ImU32 colour) const
{
	if (!shape.indices || !(colour & IM_COL32_A_MASK))
		return;

	const uint32_t base = m_drawing.contours[shape.first_ring].first;
	uint32_t count = 0;

	for (uint16_t r = 0; r < shape.rings; r++)
		count += m_drawing.contours[shape.first_ring + r].count;

	const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();

	list->PrimReserve(static_cast<int>(shape.indices), static_cast<int>(count));
	for (uint32_t i = 0; i < count; i++)
		list->PrimWriteVtx(point(base + i, m), uv, colour);
	for (uint32_t i = 0; i < shape.indices; i++)
		list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx - count + m_drawing.indices[shape.first_index + i]));

	if (shape.stroke || ((colour >> IM_COL32_A_SHIFT) & 0xFF) != 0xFF)
		return;
	for (uint16_t r = 0; r < shape.rings; r++) {
		const PadContour &c = m_drawing.contours[shape.first_ring + r];

		m_points.resize(c.count);
		for (uint32_t i = 0; i < c.count; i++)
			m_points[i] = point(c.first + i, m);
		list->AddPolyline(m_points.data(), static_cast<int>(c.count), colour, ImDrawFlags_Closed, EDGE);
	}
}

void
PadView::stroke(ImDrawList *list, const PadShape &shape, const Move &m, ImU32 colour, float width) const
{
	for (uint16_t k = 0; k < shape.contours; k++) {
		const PadContour &c = m_drawing.contours[shape.first_contour + k];

		m_points.resize(c.count);
		for (uint32_t i = 0; i < c.count; i++)
			m_points[i] = point(c.first + i, m);
		list->AddPolyline(m_points.data(), static_cast<int>(c.count), colour,
		                  c.closed ? ImDrawFlags_Closed : ImDrawFlags_None, width);
	}
}

/* The shape after which a lit or chosen shape is drawn: `after`, or the
 * last before a body that comes first. A gun is drawn from both sides: the
 * body of each side covers its own part, lit as at rest. */
size_t
PadView::drawn_after(uint16_t shape, uint16_t after) const
{
	for (size_t s = shape + 1u; s <= after; s++)
		if (m_drawing.shapes[s].fill == PadTone::BODY)
			return s - 1;
	return after;
}

/* The shapes that light, the accent as strong as the part is worked; a
 * trigger fills from the bottom of each shape as far as it is pulled */
void
PadView::lit(ImDrawList *list, const MenuPalette &palette, const PadPart &part, const PadState &now,
             size_t after) const
{
	const float v = value(part, now);

	if (v <= 0.0f)
		return;

	const Move m = move(part, now);

	for (uint16_t k = 0; k < part.lits; k++) {
		const uint16_t index = m_drawing.lists[part.first_lit + k];
		const PadShape &shape = m_drawing.shapes[index];

		if (drawn_after(index, part.lit_after) != after)
			continue;
		if (part.kind != PadPartKind::TRIGGER) {
			fill(list, shape, m, with_alpha(palette.accent, v));
			continue;
		}

		float top = 1e9f, bottom = -1e9f, left = 1e9f, right = -1e9f;

		for (uint16_t r = 0; r < shape.rings; r++) {
			const PadContour &c = m_drawing.contours[shape.first_ring + r];

			for (uint32_t i = 0; i < c.count; i++) {
				const ImVec2 p = point(c.first + i, m);

				top = std::min(top, p.y), bottom = std::max(bottom, p.y);
				left = std::min(left, p.x), right = std::max(right, p.x);
			}
		}
		list->PushClipRect({ left - 1.0f, bottom - v * (bottom - top) }, { right + 1.0f, bottom + 1.0f }, true);
		fill(list, shape, m, palette.accent);
		list->PopClipRect();
	}
}

/* A chosen part: its lines filled and its other shapes outlined with the
 * accent; its details keep their width */
void
PadView::outline(ImDrawList *list, const MenuPalette &palette, const PadPart &part, const PadState &now,
                 float px, size_t after) const
{
	const Move m = move(part, now);

	for (uint16_t k = 0; k < part.sels; k++) {
		const uint16_t index = m_drawing.lists[part.first_sel + k];
		const PadShape &shape = m_drawing.shapes[index];

		if (drawn_after(index, part.sel_after) != after)
			continue;
		if (shape.fill == PadTone::LINE)
			fill(list, shape, m, palette.accent);
		else
			stroke(list, shape, m, palette.accent,
			       (shape.faint ? static_cast<float>(shape.stroke) / 10.0f : m_drawing.chosen_width) * px);
	}
}

void
PadView::draw(ImDrawList *list, const MenuPalette &palette, const PadState &now, uint32_t chosen, float px) const
{
	const std::span<const PadPart> parts = m_drawing.parts;
	bool wells = false;

	for (const PadPart &part : parts)
		wells |= part.kind == PadPartKind::WELL;

	const auto moved = [&](const PadPart &part) {
		return std::hypot(axis(now.axes[2 * part.input]), axis(now.axes[2 * part.input + 1]));
	};

	for (size_t s = 0; s < m_drawing.shapes.size(); s++) {
		const PadShape &shape = m_drawing.shapes[s];

		if (shape.drawn) {
			const Move m = shape.part >= 0 ? move(parts[static_cast<size_t>(shape.part)], now) : Move();
			ImU32 colour = tone(palette, shape.fill);

			for (const PadPart &part : parts)
				if (part.arrow == static_cast<int16_t>(s) && value(part, now) > 0.5f)
					colour = palette.on_accent;
			fill(list, shape, m, colour);
			if (shape.stroke)
				stroke(list, shape, m, shape.faint ? palette.outline : palette.stroke,
				       static_cast<float>(shape.stroke) / 10.0f * px);
		}
		for (size_t p = 0; p < parts.size(); p++) {
			const PadPart &part = parts[p];
			const bool hot = part.kind == PadPartKind::STICK && !wells && moved(part) > STICK_HOT;

			lit(list, palette, part, now, s);
			if ((p < 32 && (chosen >> p & 1)) || hot)
				outline(list, palette, part, now, px, s);
		}
	}

	/* Where a stick goes, on its well; how far a trigger is pulled */
	for (const PadPart &part : parts) {
		if (part.kind == PadPartKind::WELL) {
			const float m = std::min(1.0f, moved(part));

			if (m <= STICK_SHOWN)
				continue;

			const float a = std::atan2(axis(now.axes[2 * part.input + 1]), axis(now.axes[2 * part.input]));
			const float sweep = 0.25f + m * 0.9f;

			list->PathArcTo(at(part.spot[0], part.spot[1]), part.spot[2] * m_scale, a - sweep, a + sweep);
			list->PathStroke(with_alpha(palette.accent, 0.3f + 0.7f * m), ImDrawFlags_None, 4.0f * m_scale);
		} else if (part.kind == PadPartKind::TRIGGER) {
			const float v = value(part, now);

			if (v <= TRIGGER_SHOWN)
				continue;

			char text[8];
			std::snprintf(text, sizeof text, "%d %%", static_cast<int>(std::lround(v * 100.0f)));

			const Move m = move(part, now);
			const float size = MenuKit::text_size(part.spot[2] * m_scale);
			const ImVec2 size_text = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.0f, text);
			const ImVec2 base = at(part.spot[0], part.spot[1]);
			const ImU32 colour = part.inside && v > 0.45f ? palette.on_accent :
			                     v > TRIGGER_LIT          ? palette.accent :
			                                                palette.faint;

			list->AddText(ImGui::GetFont(), size, { base.x - size_text.x / 2.0f, base.y + m.by.y - size_text.y * 0.8f },
			              colour, text);
		}
	}
}

} /* namespace odin */
