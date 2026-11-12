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

#ifndef ODIN_LOGO_HPP
#define ODIN_LOGO_HPP

namespace odin {

/* The logo of Odin: the rune ᚨ, ansuz, in gold. Its outline is that of
 * Junicode 2.226, by Peter S. Baker (SIL Open Font License 1.1,
 * fonts/LICENSE-junicode), in a square of 24, its ink from y 2 to 22:
 * clockwise from the foot of the stem, up, then the two branches down to the
 * right. Regular in the menu, bold for the icon of the window. Its gold: red,
 * green, blue. */
inline constexpr float RUNE_OUTLINE[11][2] = {
	{ 8.77f, 22.0f }, { 8.77f, 2.0f }, { 10.81f, 2.0f }, { 15.17f, 5.56f }, { 14.23f, 6.88f }, { 10.81f, 4.21f },
	{ 10.81f, 7.24f }, { 15.23f, 10.86f }, { 14.23f, 12.26f }, { 10.81f, 9.56f }, { 10.81f, 22.0f },
};
inline constexpr float RUNE_OUTLINE_BOLD[11][2] = {
	{ 8.16f, 22.0f }, { 8.16f, 2.0f }, { 11.24f, 2.0f }, { 15.73f, 5.69f }, { 14.49f, 7.48f }, { 11.24f, 4.92f },
	{ 11.24f, 7.84f }, { 15.84f, 11.61f }, { 14.49f, 13.57f }, { 11.24f, 10.98f }, { 11.24f, 22.0f },
};
inline constexpr unsigned char RUNE_GOLD[3] = { 0xD8, 0xAA, 0x4C };

} /* namespace odin */

#endif /* ODIN_LOGO_HPP */
