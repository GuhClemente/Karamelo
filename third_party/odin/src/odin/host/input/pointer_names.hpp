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

#ifndef ODIN_POINTER_NAMES_HPP
#define ODIN_POINTER_NAMES_HPP

#include <cstdio>
#include <string>

namespace odin {

/* What the readers of the pointers (evdev.cpp, rawinput.cpp) both say of
 * one: its identity by its make and model, VID:PID, which stays when the
 * port changes; the name shown, its product ("Pointer" without one) and
 * its VID:PID when it has one */
inline std::string
vidpid_identity(unsigned vid, unsigned pid)
{
	char text[32];

	std::snprintf(text, sizeof(text), "vidpid:%04x:%04x", vid, pid);
	return text;
}

inline std::string
pointer_name(const std::string &product, unsigned vid, unsigned pid)
{
	std::string name = product.empty() ? std::string("Pointer") : product;

	if (vid && pid) {
		char tag[32];

		std::snprintf(tag, sizeof(tag), " [%04X:%04X]", vid, pid);
		name += tag;
	}
	return name;
}

} /* namespace odin */

#endif /* ODIN_POINTER_NAMES_HPP */
