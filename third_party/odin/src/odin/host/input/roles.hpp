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

#ifndef ODIN_ROLES_HPP
#define ODIN_ROLES_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace odin {

/* What a control does on a cabinet, under one name for every game: keys
 * and pads are bound to roles, and each control of a game takes a role.
 * The first ones belong to a player (p1., p2.), the last ones to the
 * cabinet. GEAR is a lever moved by GEAR_UP and GEAR_DOWN. */
enum class Role : uint8_t {
	START, SERVICE, UP, DOWN, LEFT, RIGHT,
	BUTTON1, BUTTON2, BUTTON3, BUTTON4, BUTTON5, BUTTON6, BUTTON7, BUTTON8,
	STICK_UP, STICK_DOWN, STICK_LEFT, STICK_RIGHT,
	STICK2_UP, STICK2_DOWN, STICK2_LEFT, STICK2_RIGHT,
	GEAR_UP, GEAR_DOWN, GUN_OFFSCREEN,
	STEER, ACCEL, BRAKE, STICK_X, STICK_Y, STICK2_X, STICK2_Y, GUN_X, GUN_Y,
	GEAR,
	COIN1, COIN2, TEST, BOARD_TEST, BOARD_SERVICE,
	COUNT
};

/* "start", "button1", "stick2_up", "gun_x", "board_test"... */
const char *role_name(Role role);
std::optional<Role> parse_role(std::string_view name);

/* A role of a player that is pressed or not */
bool role_is_switch(Role role);
/* A role of a player that has a position */
bool role_is_analog(Role role);
/* A role of the cabinet, not of a player */
bool role_of_cabinet(Role role);

} /* namespace odin */

#endif /* ODIN_ROLES_HPP */
