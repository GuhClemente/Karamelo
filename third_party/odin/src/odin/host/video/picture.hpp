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

#ifndef ODIN_PICTURE_HPP
#define ODIN_PICTURE_HPP

#include <algorithm>
#include <cmath>

namespace odin {

/* The hole a bezel leaves for the game (Display::set_bezel): where it is
 * in the bezel, in shares of its width and height from its top left
 * corner, and the size of the bezel; none while w is 0 */
struct PictureHole {
	double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
	double bezel_w = 0.0, bezel_h = 0.0;
};

/* The bezel shows where the window is wider than the screen of the
 * hardware, as large as the window holds it, in the middle */
inline bool
bezel_shown(unsigned window_w, unsigned window_h)
{
	return window_w * 3 > window_h * 4;
}

/* Where the picture of the hardware goes in a window, from its top left
 * corner: the largest rectangle of the shape of its screen, 4 by 3, that
 * fits, in the middle; in the hole of the bezel where the bezel shows. The
 * drawing places it so, the mouse is read so. */
struct PictureRect {
	unsigned x, y, w, h;
};

inline PictureRect
picture_rect(unsigned window_w, unsigned window_h, const PictureHole &hole = {})
{
	unsigned w = window_w, h = window_w * 3 / 4;

	if (hole.w > 0.0 && hole.h > 0.0 && bezel_shown(window_w, window_h)) {
		const double k = std::min(window_w / hole.bezel_w, window_h / hole.bezel_h);
		const double hx = (window_w - hole.bezel_w * k) / 2.0 + hole.x * hole.bezel_w * k;
		const double hy = (window_h - hole.bezel_h * k) / 2.0 + hole.y * hole.bezel_h * k;
		const double hw = hole.w * hole.bezel_w * k, hh = hole.h * hole.bezel_h * k;
		const double gw = std::min(hw, hh * 4.0 / 3.0), gh = gw * 3.0 / 4.0;

		return { static_cast<unsigned>(std::lround(hx + (hw - gw) / 2.0)),
			 static_cast<unsigned>(std::lround(hy + (hh - gh) / 2.0)), static_cast<unsigned>(std::lround(gw)),
			 static_cast<unsigned>(std::lround(gh)) };
	}
	if (h > window_h) {
		h = window_h;
		w = window_h * 4 / 3;
	}
	return { (window_w - w) / 2, (window_h - h) / 2, w, h };
}

/* Where a pixel of a window is on its picture: 0 to 1 each way from the
 * top left corner, outside for a pixel off the picture. */
struct PicturePoint {
	double x, y;
};

inline PicturePoint
picture_point(int x, int y, unsigned window_w, unsigned window_h, const PictureHole &hole = {})
{
	const PictureRect rect = picture_rect(window_w, window_h, hole);

	if (rect.w == 0 || rect.h == 0)
		return { -1.0, -1.0 };
	return { (x + 0.5 - rect.x) / rect.w, (y + 0.5 - rect.y) / rect.h };
}

} /* namespace odin */

#endif /* ODIN_PICTURE_HPP */
