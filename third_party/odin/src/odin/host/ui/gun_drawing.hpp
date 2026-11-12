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

#ifndef ODIN_GUN_DRAWING_HPP
#define ODIN_GUN_DRAWING_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "odin/host/input/bindings.hpp"
#include "odin/host/ui/pad_drawing.hpp"

namespace odin {

/* The drawings of the light guns in the menu, both sides of each: those
 * of lightgun-controllers-art, which Batocera shows, in the lines of the
 * menu (gun_drawings.cpp; made from SVG by a tool kept outside the
 * repository). They are drawn as the gamepads are (PadDrawing, PadView);
 * the input of a part is a button of the gun, by the name Batocera gives
 * it. */
enum GunButton : uint8_t {
	GUN_TRIGGER, GUN_ACTION, GUN_START, GUN_SELECT, GUN_SUB1, GUN_SUB2, GUN_SUB3,
	GUN_UP, GUN_DOWN, GUN_LEFT, GUN_RIGHT, GUN_BUTTONS
};

/* The light guns drawn; any other one has the drawing of the Aimtrak, the
 * shape of the GunCon */
enum class GunKind : uint8_t { OTHER, AIMTRAK, GUNCON2, GUNCON3, SINDEN, AE, XGUNNER };

/* The kind of a light gun, by the name Odin gives it (pointer_names.hpp)
 * and its identity */
GunKind gun_kind(std::string_view name, std::string_view identity);
const PadDrawing &gun_drawing(GunKind kind);

/* What each button of a light gun sends as it comes (none: nothing Odin
 * reads), by its kind, name and identity */
std::array<std::optional<HostInput>, GUN_BUTTONS> gun_inputs(GunKind kind, std::string_view name,
                                                             std::string_view identity);

} /* namespace odin */

#endif /* ODIN_GUN_DRAWING_HPP */
