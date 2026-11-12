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

/* The font of the menu, in the binary as its file is (menu.cpp):
 * odin_font_<name> starts it, odin_font_<name>_end follows it.
 *
 * fonts/cozette.ttf is CozetteVector.ttf of Cozette v.1.30.0
 * (https://github.com/the-moonwitch/Cozette, MIT, fonts/LICENSE-cozette)
 * cut to the letters of Latin, Greek and Cyrillic, Vietnamese, the
 * punctuation, the currencies and the letterlike signs, its names kept:
 *
 *   pyftsubset CozetteVector.ttf --unicodes=U+0020-007E,U+00A0-024F,
 *     U+0370-03FF,U+0400-052F,U+1E00-1EFF,U+2000-206F,U+20A0-20CF,
 *     U+2100-214F --name-IDs='*' --name-languages='*' --name-legacy
 *     --notdef-outline --output-file=cozette.ttf */

#include "../embedded.inc"

	READ_ONLY

#define FONT(name, file) \
	.global C_NAME(odin_font_##name) NEXT .global C_NAME(odin_font_##name##_end) NEXT .balign 16 NEXT \
	C_NAME(odin_font_##name): .incbin file NEXT C_NAME(odin_font_##name##_end):

FONT(cozette, "fonts/cozette.ttf")

	STACK_NOTE
