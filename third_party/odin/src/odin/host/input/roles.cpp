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

#include "odin/host/input/roles.hpp"

namespace odin {

namespace {

constexpr unsigned COUNT = static_cast<unsigned>(Role::COUNT);

const char *const NAMES[COUNT] = {
	"start", "service", "up", "down", "left", "right",
	"button1", "button2", "button3", "button4", "button5", "button6", "button7", "button8",
	"stick_up", "stick_down", "stick_left", "stick_right",
	"stick2_up", "stick2_down", "stick2_left", "stick2_right",
	"gear_up", "gear_down", "gun_offscreen",
	"steer", "accel", "brake", "stick_x", "stick_y", "stick2_x", "stick2_y", "gun_x", "gun_y",
	"gear",
	"coin1", "coin2", "test", "board_test", "board_service"
};

} /* namespace */

const char *
role_name(Role role)
{
	return NAMES[static_cast<unsigned>(role)];
}

std::optional<Role>
parse_role(std::string_view name)
{
	for (unsigned i = 0; i < COUNT; i++)
		if (name == NAMES[i])
			return static_cast<Role>(i);
	return std::nullopt;
}

bool
role_is_switch(Role role)
{
	return role <= Role::GUN_OFFSCREEN;
}

bool
role_is_analog(Role role)
{
	return role >= Role::STEER && role <= Role::GUN_Y;
}

bool
role_of_cabinet(Role role)
{
	return role >= Role::COIN1 && role < Role::COUNT;
}

} /* namespace odin */
