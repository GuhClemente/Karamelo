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
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

/* What is around the picture of the game: a bezel laid over the window,
 * its hole found or read, or the game blurred at its sides. */

#include "odin/host/video/display.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>

#include <jansson.h>

namespace odin {

namespace {

/* The hole a bezel leaves for the game: the pixels joined to its middle
 * that are not opaque (the glass of a screen often is half so), their
 * bounds; none when its middle is opaque */
constexpr uint8_t SEE_THROUGH = 250;

PictureHole
find_hole(const SDL_Surface &rgba)
{
	const int w = rgba.w, h = rgba.h;
	const auto *const pixels = static_cast<const uint8_t *>(rgba.pixels);
	const auto open = [&](int x, int y) {
		return pixels[static_cast<size_t>(y) * static_cast<size_t>(rgba.pitch) + static_cast<size_t>(x) * 4 + 3] < SEE_THROUGH;
	};
	std::vector<uint8_t> seen(static_cast<size_t>(w) * static_cast<size_t>(h));
	std::vector<std::pair<int, int>> next = { { w / 2, h / 2 } };
	int left = w, top = h, right = -1, bottom = -1;

	if (w < 2 || h < 2 || !open(w / 2, h / 2))
		return {};
	seen[static_cast<size_t>(h / 2) * static_cast<size_t>(w) + static_cast<size_t>(w / 2)] = 1;
	while (!next.empty()) {
		const auto [x, y] = next.back();

		next.pop_back();
		left = std::min(left, x);
		right = std::max(right, x);
		top = std::min(top, y);
		bottom = std::max(bottom, y);
		for (const auto &[dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } }) {
			const int nx = x + dx, ny = y + dy;
			const size_t at = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);

			if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[at] || !open(nx, ny))
				continue;
			seen[at] = 1;
			next.push_back({ nx, ny });
		}
	}
	return { static_cast<double>(left) / w, static_cast<double>(top) / h, static_cast<double>(right - left + 1) / w,
		 static_cast<double>(bottom - top + 1) / h, static_cast<double>(w), static_cast<double>(h) };
}

/* The hole as the file <bezel>.info beside a bezel gives it: the size the
 * bezel is drawn for, and what is around the hole on each side,
 * in its pixels; none without such a file */
PictureHole
read_hole(const std::string &info, int bezel_w, int bezel_h)
{
	json_error_t error;
	json_t *const root = json_load_file(info.c_str(), 0, &error);
	const auto number = [root](const char *key) { return json_number_value(json_object_get(root, key)); };
	PictureHole hole;

	if (!root)
		return hole;

	const double w = number("width"), h = number("height");
	const double left = number("left"), top = number("top"), right = number("right"), bottom = number("bottom");

	if (w > 0.0 && h > 0.0 && left >= 0.0 && top >= 0.0 && right >= 0.0 && bottom >= 0.0 && left + right < w &&
	    top + bottom < h)
		hole = { left / w, top / h, (w - left - right) / w, (h - top - bottom) / h, static_cast<double>(bezel_w),
			 static_cast<double>(bezel_h) };
	else
		std::fprintf(stderr, "ERROR: bezel: '%s' gives no hole\n", info.c_str());
	json_decref(root);
	return hole;
}

} /* namespace */

void
Display::set_sides(Sides sides, const std::string &bezel)
{
	m_sides = sides;
	set_bezel(sides == Sides::BEZEL ? bezel : std::string());
}

void
Display::set_bezel(const std::string &path)
{
	SDL_Surface *const png = path.empty() ? nullptr : SDL_LoadPNG(path.c_str());
	SDL_Surface *const rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;

	SDL_DestroySurface(png);
	if (!path.empty() && !rgba)
		std::fprintf(stderr, "ERROR: bezel: '%s': %s\n", path.c_str(), SDL_GetError());
	/* Its hole: as its .info says, else where it is not opaque */
	if (rgba) {
		std::error_code error;
		const std::string info = std::filesystem::path(path).replace_extension(".info").string();

		m_hole = std::filesystem::exists(info, error) ? read_hole(info, rgba->w, rgba->h) : find_hole(*rgba);
	} else {
		m_hole = {};
	}
	run([this, rgba] { make_bezel(rgba); });
	SDL_DestroySurface(rgba);
}

void
Display::make_bezel(SDL_Surface *rgba)
{
	m_graphics->forget_image(std::exchange(m_bezel, {}));
	if (rgba)
		m_bezel = m_graphics->make_image(*rgba, true);
}

void
Display::draw_bezel(unsigned window_w, unsigned window_h)
{
	if (m_bezel.id && bezel_shown(window_w, window_h))
		m_graphics->draw_bezel(m_bezel, window_w, window_h);
}

void
Display::draw_blur(unsigned window_w, unsigned window_h)
{
	const PictureRect rect = picture_rect(window_w, window_h);

	if (m_sides != Sides::BLUR || !m_picture.id || (rect.w == window_w && rect.h == window_h))
		return;
	m_graphics->draw_blurred_sides(m_picture, rect, window_w, window_h);
}

void
Display::set_sinden_border(const SindenBorder &border)
{
	finish();
	m_sinden_border = border;
}

/* Inside the edges of the window, or of the picture where the game draws
 * it, over all that shows */
void
Display::draw_sinden_border(unsigned window_w, unsigned window_h)
{
	if (!m_sinden_border.on)
		return;

	const PictureRect rect =
		m_sinden_border.window ? PictureRect{ 0, 0, window_w, window_h } : picture_rect(window_w, window_h, m_hole);

	m_graphics->draw_white_border(rect, m_sinden_border.size, window_w, window_h);
}

} /* namespace odin */
