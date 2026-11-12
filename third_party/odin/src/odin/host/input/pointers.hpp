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

#ifndef ODIN_POINTERS_HPP
#define ODIN_POINTERS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace odin {

/* The light guns and the mice, each read on its own so that each player
 * aims with their own: the system merges them into one cursor, and SDL
 * tells them apart only in relative mode. evdev on Linux (evdev.cpp), Raw
 * Input on Windows (rawinput.cpp), none elsewhere. A device keeps its
 * identity across replugs: on Linux its serial, else VID:PID, else its
 * port, else its name; on Windows VID:PID, else its port; two of a kind
 * take their ports. Ported from xemu's pointers. */
class Pointers {
public:
	/* A device: its identity, "Product [VID:PID]", plugged in now (one
	 * unplugged stays listed), aiming at a place of the screen (a gun, a
	 * tablet) or moving (a mouse) */
	struct Device {
		std::string identity, name;
		bool present = false, absolute = false;
	};
	/* Where a device points, in pixels of the window, and its buttons: bit
	 * 0 left, 1 middle, 2 right, 3 and 4 the side ones, 5 to 12 the extra
	 * buttons of a gun (evdev BTN_1 to BTN_8); those pressed since the
	 * aim was last taken, however short (a click within a frame) */
	struct Aim {
		float x = 0.0f, y = 0.0f;
		uint32_t buttons = 0, pressed = 0;
	};
	static constexpr size_t MOST = 8;

	explicit Pointers(SDL_Window *window);
	~Pointers();
	Pointers(const Pointers &) = delete;
	Pointers &operator=(const Pointers &) = delete;

	/* The devices that came and went, and the events since the last call:
	 * once a frame; the window they aim at */
	void poll();
	SDL_Window *window() const { return m_window; }
	const std::vector<Device> &devices() const;
	/* The aim of a device (nothing: unplugged, or it has not pointed yet);
	 * its presses given once */
	std::optional<Aim> take(const std::string &identity);
	/* Those devices held for the game alone where the host can, the others
	 * let go */
	void hold(std::span<const std::string> identities);

private:
	struct Backend;

	SDL_Window *m_window;
	std::unique_ptr<Backend> m_backend;
};

} /* namespace odin */

#endif /* ODIN_POINTERS_HPP */
