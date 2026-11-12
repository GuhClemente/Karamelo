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

#ifndef ODIN_PAD_VIEW_HPP
#define ODIN_PAD_VIEW_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "imgui/imgui.h"
#include "odin/host/input/control_panel.hpp"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/ui/pad_drawing.hpp"

namespace odin {

/* A gamepad drawn in the menu, as large as it fits in a box
 * and in its middle. Its parts light with what the gamepad does: a button
 * pressed fills with the accent, a trigger fills as far as it is pulled and
 * says how far, a stick moves; a chosen part is outlined with the accent,
 * and so is a stick moved far enough, or its well shows where it goes. */
class PadView {
public:
	PadView(const PadDrawing &drawing, ImVec2 a, ImVec2 b);

	/* Where a point of the drawing is on the screen */
	ImVec2 at(float x, float y) const { return { m_origin.x + x * m_scale, m_origin.y + y * m_scale }; }
	/* The box of a part on the screen, at rest: its corners */
	std::array<ImVec2, 2> box(size_t part) const;

	/* The parts outlined: a bit each (a drawing has 32 at most); a stroke of
	 * one is one point times `px` */
	void draw(ImDrawList *list, const MenuPalette &palette, const PadState &now, uint32_t chosen, float px) const;

private:
	/* How a part is moved now: by a vector, and scaled about a point */
	struct Move {
		ImVec2 by = {};
		float scale = 1.0f;
		ImVec2 about = {};
	};

	/* How far a part is worked, 0 to 1 */
	static float value(const PadPart &part, const PadState &now);
	Move move(const PadPart &part, const PadState &now) const;
	ImVec2 point(uint32_t index, const Move &m) const;
	void fill(ImDrawList *list, const PadShape &shape, const Move &m, ImU32 colour) const;
	void stroke(ImDrawList *list, const PadShape &shape, const Move &m, ImU32 colour, float width) const;
	size_t drawn_after(uint16_t shape, uint16_t after) const;
	/* The shapes of the part drawn after the shape `after` */
	void lit(ImDrawList *list, const MenuPalette &palette, const PadPart &part, const PadState &now,
	         size_t after) const;
	void outline(ImDrawList *list, const MenuPalette &palette, const PadPart &part, const PadState &now,
	             float px, size_t after) const;

	const PadDrawing &m_drawing;
	ImVec2 m_origin;
	float m_scale;
	mutable std::vector<ImVec2> m_points;
};

} /* namespace odin */

#endif /* ODIN_PAD_VIEW_HPP */
