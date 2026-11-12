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

#ifndef ODIN_HOST_GRAPHICS_HPP
#define ODIN_HOST_GRAPHICS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "odin/host/video/picture.hpp"

struct ImDrawData;
struct ImDrawList;
struct SDL_Surface;
struct SDL_Window;

namespace odin {

class Drawing;

/* A picture the renderer drew, as the graphics card holds it (0: none),
 * and its size */
struct HostPicture {
	uint32_t id = 0;
	unsigned w = 0, h = 0;
};

/* An image Odin gave the card, a bezel or a picture of the menu (0: none),
 * and its size */
struct HostImage {
	uint32_t id = 0;
	float w = 0.0f, h = 0.0f;
};

/* Where a bezel goes over a window: the largest of its size that fits, in
 * the middle; x from the left, `below` from the bottom, in pixels */
struct BezelArea {
	int x, below, w, h;
};

inline BezelArea
bezel_area(HostImage bezel, unsigned window_w, unsigned window_h)
{
	const float k = std::min(static_cast<float>(window_w) / bezel.w, static_cast<float>(window_h) / bezel.h);
	const int w = static_cast<int>(std::lround(bezel.w * k)), h = static_cast<int>(std::lround(bezel.h * k));

	return { (static_cast<int>(window_w) - w) / 2, (static_cast<int>(window_h) - h) / 2, w, h };
}

/* The four bands of a border `size` pixels wide inside `rect`, from the top
 * left corner of the window: the top, the bottom, then the left and the
 * right between them; thinner where the rectangle has no room */
inline std::array<PictureRect, 4>
border_bands(PictureRect rect, unsigned size)
{
	const unsigned t = std::min({ size, rect.w / 2, rect.h / 2 }), side = rect.h - 2 * t;

	return { { { rect.x, rect.y, rect.w, t }, { rect.x, rect.y + rect.h - t, rect.w, t },
	           { rect.x, rect.y + t, t, side }, { rect.x + rect.w - t, rect.y + t, t, side } } };
}

/* The sides lit by the picture (sides.frag): the mean of the picture at
 * the first two sizes, each texel the mean of what it covers; the sides
 * drawn at the third, whatever the window (they are smooth), then
 * stretched over it; from the light near the picture to the far one at the
 * edge of the window */
inline constexpr int SIDES_W[3] = { 40, 20, 384 }, SIDES_H[3] = { 30, 15, 216 };
inline constexpr float SIDES_NEAR = 0.6f, SIDES_FAR = 0.05f;

/* The background of the menu: how far apart the taps of its blur are, in
 * texels of the half picture, and how many times it goes back and forth */
inline constexpr float BLUR_STEP = 1.5f;
inline constexpr unsigned BLUR_ROUNDS = 2;

/* The graphics card of the host: the one door between Odin and the
 * graphics API. What Odin asks of the card is said here in
 * its own words, never in those of the API: show the window, lay the bezel
 * over it, blur the sides of the game, the background and the images of
 * the menu. host_graphics_gl.cpp does it with OpenGL, host_graphics_vk.cpp
 * with Vulkan. The pictures of the
 * board are drawn by the renderer (renderer.hpp) through the Drawing
 * the card makes for it (drawing.hpp); it hands its picture over as
 * a HostPicture.
 *
 * Everything runs on the thread that has taken the card, the drawing
 * thread (display.hpp), but making it, which takes it, and destroying it. */
class HostGraphics {
public:
	/* The cards Vulkan sees, as [display] gpu names them (the second of a
	 * make "... (2)"), or none: asked once */
	static const std::vector<std::string> &gpus();

	/* OpenGL: what SDL must be told before the window is made, the flags
	 * of the window; then the card for that window, made that size,
	 * taken by the calling thread, or nothing, with the reason on stderr */
	static void prepare_gl();
	static uint64_t window_flags_gl();
	static std::unique_ptr<HostGraphics> make_gl(SDL_Window *window, unsigned window_w, unsigned window_h);
	/* Vulkan: the flags of the window, then the card for it, the one named
	 * `gpu` if it can (empty: the best one) (host_graphics_vk.cpp), or
	 * nothing with the reason on stderr */
	static uint64_t window_flags_vk();
	static std::unique_ptr<HostGraphics> make_vk(SDL_Window *window, unsigned window_w, unsigned window_h,
	                                             const std::string &gpu);
	virtual ~HostGraphics() = default;

	/* The name of the card, whether Vulkan draws on it (else OpenGL), and
	 * the API name */
	virtual const std::string &name() const = 0;
	virtual bool vulkan() const = 0;
	virtual const char *api() const = 0;
	/* The calling thread takes the card, or lets it go for another */
	virtual void take_thread() = 0;
	virtual void leave_thread() = 0;

	/* The window: all black; shown; waiting for the display or not from
	 * the next one on (false: the display cannot be waited for) */
	virtual void clear_window(unsigned window_w, unsigned window_h) = 0;
	virtual void show_window() = 0;
	virtual bool set_waits(bool wait) = 0;
	/* Bytes of red, green and blue, the bottom line first: of a picture,
	 * or of the window as drawn (no picture), that wide and high */
	virtual void read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels) = 0;

	/* An image from pixels of RGBA, made smaller smoothly; made bigger
	 * smoothly, or in blocks as Odin shows the screen of a game. Forgotten
	 * (none: nothing). */
	virtual HostImage make_image(const SDL_Surface &rgba, bool smooth) = 0;
	virtual void forget_image(HostImage image) = 0;

	/* The bezel over the window, as large as fits, in the middle */
	virtual void draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h) = 0;
	/* The picture, placed at `rect`, blurred and dimmer around itself
	 * over the whole window */
	virtual void draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h) = 0;
	/* A white border inside `rect` over what the window shows
	 * (border_bands) */
	virtual void draw_white_border(PictureRect rect, unsigned size, unsigned window_w, unsigned window_h) = 0;

	/* The background of the menu: a picture, or the window as drawn,
	 * blurred at half its size; then drawn over the window, w by h in its
	 * middle, at that light */
	virtual void blur_background(HostPicture picture) = 0;
	virtual void blur_window(unsigned window_w, unsigned window_h) = 0;
	virtual void draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light) = 0;
	/* Dear ImGui: its drawing started, the frame it built drawn over the
	 * window (`what`, for an error), stopped; an image as it takes it */
	virtual void start_ui() = 0;
	virtual void draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what) = 0;
	virtual void stop_ui() = 0;
	virtual uint64_t ui_image(HostImage image) const = 0;
	/* The images in blocks (make_image) drawn in blocks in the list from
	 * here on, till `on` false: Dear ImGui would smooth them */
	virtual void ui_blocks(ImDrawList *list, bool on) const = 0;
	/* The images drawn smaller in the list from here on, till `on` false,
	 * read over their levels (make_image): Dear ImGui's OpenGL reads only
	 * the largest */
	virtual void ui_levels(ImDrawList *list, bool on) const = 0;

	/* What the card does for the renderer of a game */
	virtual std::unique_ptr<Drawing> make_drawing() = 0;
};

} /* namespace odin */

#endif /* ODIN_HOST_GRAPHICS_HPP */
