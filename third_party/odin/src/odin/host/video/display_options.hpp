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


#ifndef ODIN_DISPLAY_OPTIONS_HPP
#define ODIN_DISPLAY_OPTIONS_HPP

#include <cstdint>
#include <string>

/* What the user chooses of the screen (display.hpp), in a header of its own
 * so that the options do not take the window and the graphics API in. */

namespace odin {

/* How the frames wait for the display: not at all (one may be torn), each
 * of them, or those on time (a late one is shown at once, so that the game
 * keeps its 60 a second) */
enum class DisplayVsync : uint8_t { OFF, ON, ADAPTIVE };
/* Around the picture where the window is wider than it: black, the picture
 * itself blurred and dimmer, or the bezel of the game (black without one) */
enum class DisplaySides : uint8_t { OFF, BLUR, BEZEL };
/* What draws: OpenGL, or Vulkan (taken when Odin starts) */
enum class DisplayApi : uint8_t { OPENGL, VULKAN };
/* How the meshes flat to the screen (the HUD, its text) are drawn at a
 * scale above 1: finer as the rest; their corners on the pixels of the
 * hardware's screen; or over the pixels they cover there (Renderer) */
enum class DisplayUpscaleMode : uint8_t { RAW, SNAP, NATIVE };

/* A white border over the window for a light gun that finds the screen by
 * it (a Sinden): whether there is one, inside the edges of the window or of
 * the picture of the game, so many pixels of the window wide */
inline constexpr unsigned SINDEN_BORDER_SIZE = 2, LEAST_SINDEN_BORDER_SIZE = 2, MOST_SINDEN_BORDER_SIZE = 30;
struct SindenBorder {
	bool on = false;
	bool window = false;
	unsigned size = SINDEN_BORDER_SIZE;

	bool operator==(const SindenBorder &) const = default;
};

struct DisplayOptions {
	unsigned scale;		/* the 3D that many times finer than the screen, 1 to 6 */
	unsigned anisotropy;	/* of the texture filter, 1 for none as on the board */
	unsigned window;	/* the window that many times the screen */
	bool fullscreen;
	bool show_speed;	/* the frames a second in the title */
	DisplayVsync vsync;
	/* The window of that size in points instead, the one the user gave it
	 * by hand (0: as `window` says) */
	unsigned window_w, window_h;
	/* The filter of the picture by its name in odin.ini (a preset
	 * filters/<name>.glslp: picture_filter_gl.hpp); empty: none */
	std::string filter;
	DisplayApi api;
	DisplayUpscaleMode upscale_mode;
	/* The card Vulkan draws on by its name (HostGraphics::gpus); empty: the
	 * best one */
	std::string gpu;
};

} /* namespace odin */

#endif /* ODIN_DISPLAY_OPTIONS_HPP */
