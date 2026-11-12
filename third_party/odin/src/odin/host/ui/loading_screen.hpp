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

#ifndef ODIN_LOADING_SCREEN_HPP
#define ODIN_LOADING_SCREEN_HPP

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace odin {

/* The screen shown while the programs of a game are compiled, before its
 * first frame: black, its title in the middle, and low down a bar that
 * fills as they are, NOW LOADING above it. It is laid out on a picture of
 * the size of the screen of the board, which the renderer places in the
 * window as it does that screen. */
struct LoadingRect {
	unsigned x, y, w, h;	/* from the top left corner */
	float shade;		/* 0 black to 1 white */
};

struct LoadingScreen {
	static constexpr unsigned WIDTH = 640, HEIGHT = 480;
	/* A character is 5 by 7, one column apart from the next, drawn
	 * MAX_SCALE times larger, or less for a title wider than
	 * TITLE_WIDTH. */
	static constexpr unsigned GLYPH_W = 5, GLYPH_H = 7, ADVANCE = 6;
	static constexpr unsigned MAX_SCALE = 4, TITLE_WIDTH = 576;
	static constexpr unsigned BAR_X = 192, BAR_Y = 432, BAR_W = 256, BAR_H = 4;
	/* The label: twice the size of the font, its foot LABEL_GAP above
	 * the bar, from its left end */
	static constexpr char LABEL[] = "NOW LOADING";
	static constexpr unsigned LABEL_SCALE = 2, LABEL_GAP = 6;
	static constexpr float TITLE_SHADE = 1.0f, LABEL_SHADE = 0.6f, TRACK_SHADE = 0.25f, FILL_SHADE = 1.0f;

	/* The rectangles to fill on black: the title, the label, then the
	 * bar, its track and what is done of it (none at 0). */
	static std::vector<LoadingRect> layout(std::string_view title, unsigned done, unsigned total);
	/* The same in the window, where the screen of the board is, at x, y,
	 * w by h (picture_rect); those that come to nothing left out */
	static std::vector<LoadingRect> placed(std::string_view title, unsigned done, unsigned total, unsigned x,
	                                       unsigned y, unsigned w, unsigned h);
	/* The drawing of a character, a row of five bits from the top, the
	 * high one on the left. Small letters are drawn as capitals, any
	 * other character than printable ASCII as a space. */
	static const std::array<uint8_t, GLYPH_H> &glyph(char c);
};

} /* namespace odin */

#endif /* ODIN_LOADING_SCREEN_HPP */
