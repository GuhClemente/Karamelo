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

#ifndef ODIN_PAD_DRAWING_HPP
#define ODIN_PAD_DRAWING_HPP

#include <array>
#include <cstdint>
#include <span>

#include <SDL3/SDL.h>

namespace odin {

/* The drawings of the gamepads in the menu, seen from the front with their
 * top edge: AL2009man's Gamepad-Asset-Pack (MIT, pad_drawings_al2009man.cpp)
 * for the Xbox 360, Xbox One and Series, PlayStation 4 and 5, Switch Pro and
 * Steam Controller gamepads, Odin's for the DualShock 3 and any other one
 * (pad_drawings.cpp). Their data is made from SVG by a tool kept outside
 * the repository. */

/* The tones of the menu a shape takes (menu_kit.hpp): the body, a part on
 * it, a line drawn as a shape, a detail, a light, an arrow on a part; gold
 * is the accent, for what lights only */
enum class PadTone : uint8_t { NONE, BODY, PART, LINE, DETAIL, LIGHT, ARROW, GOLD };

/* A run of points: a line, or a closed outline */
struct PadContour {
	uint32_t first;
	uint16_t count;
	bool closed;
};

/* A shape: how it is filled (triangles on the points of its first ring)
 * and stroked (its contours, that many tenths of a point wide on the
 * screen, in the tone of the outlines or of the details); drawn with the
 * drawing or only when its part lights or is chosen; the part it moves
 * with (-1: none) */
struct PadShape {
	PadTone fill;
	uint8_t stroke;
	bool faint;
	bool drawn;
	int8_t part;
	uint16_t first_contour, contours;
	uint16_t first_ring, rings;
	uint32_t first_index, indices;
};

/* What works a part: a button, a trigger (an axis, 0 to 1), a stick (its
 * two axes move it, its button lights it) or the well a stick moves in */
enum class PadPartKind : uint8_t { BUTTON, TRIGGER, STICK, WELL };

/* A part of a gamepad. Its input: an SDL_GamepadButton, an SDL_GamepadAxis
 * or a stick (0 left, 1 right). A button that sinks when pressed; how far a
 * stick moves or a part is pushed down, in units of the drawing. The box its
 * leader line points at. A well: the circle its stick's direction is shown
 * on; a trigger: where its value is written (centre, baseline, size), in it
 * or above it. Its lit shapes are drawn after the shape `lit_after`, the
 * shapes that show it chosen after `sel_after` (in `PadDrawing::lists`), or
 * before a body that comes first (PadView::drawn_after); an arrow that
 * turns dark on gold (-1: none). */
struct PadPart {
	PadPartKind kind;
	uint8_t input;
	bool press;
	float travel;
	std::array<float, 4> box;
	std::array<float, 3> spot;
	bool inside;
	uint16_t lit_after, sel_after;
	uint16_t first_lit, lits, first_sel, sels;
	int16_t arrow;
};

/* A drawing: its extent (left, top, right, bottom), the width of the
 * outline of a chosen part, its points (x and y in eighths of a unit), its
 * contours, triangles, shapes in the order drawn, parts and their lists of
 * shapes */
struct PadDrawing {
	std::array<float, 4> bounds;
	float chosen_width;
	std::span<const int16_t> points;
	std::span<const PadContour> contours;
	std::span<const uint16_t> indices;
	std::span<const PadShape> shapes;
	std::span<const PadPart> parts;
	std::span<const uint16_t> lists;
};

/* The kinds of gamepad the menu draws and names: those of SDL
 * (SDL_GamepadType), and the Steam Controller of 2015, which SDL counts as
 * a standard gamepad. The kind of a gamepad of that type, maker and
 * product (its USB ids). */
inline constexpr int PAD_STEAM_CONTROLLER = SDL_GAMEPAD_TYPE_COUNT;
int pad_kind(SDL_GamepadType type, uint16_t vendor, uint16_t product);

/* The drawing of a kind of gamepad, and its name ("Xbox 360", "Generic") */
const PadDrawing &pad_drawing(int kind);
const char *pad_drawing_name(int kind);

/* The drawings of AL2009man's pack (pad_drawings_al2009man.cpp) */
extern const PadDrawing PAD_XBOX360, PAD_XBOXONE, PAD_PS4, PAD_PS5, PAD_SWITCH_PRO, PAD_STEAM;

} /* namespace odin */

#endif /* ODIN_PAD_DRAWING_HPP */
