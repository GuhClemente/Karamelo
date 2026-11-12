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

#ifndef ODIN_BOARD_HPP
#define ODIN_BOARD_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace odin {

/* What the session needs of a running arcade board, whoever made it: the
 * game it runs and its frame; one more frame; whether its cabinet is to be
 * switched off and on (a seat of a link taken); its save states; the
 * outputs of its cabinet; its sound (the optional board's, or the main
 * one's); the torque it asks of a wheel (none: no motor) */
class Board {
public:
	virtual ~Board() = default;

	virtual const std::string &game_name() const = 0;
	virtual unsigned frame() const = 0;
	virtual void run_frame() = 0;
	virtual bool restart_asked() const = 0;
	virtual bool save_state(const std::string &path) = 0;
	virtual bool load_state(const std::string &path) = 0;
	virtual std::span<const uint8_t> io_outputs() const = 0;
	/* How hard each output drives what it runs, 0 to 255, output n at
	 * n - 1 (a motor of the cabinet); none: all or nothing */
	virtual std::span<const uint8_t> output_levels() const { return {}; }
	virtual const std::vector<int16_t> &sound(bool optional) const = 0;
	virtual std::optional<int> drive_torque() const = 0;
	/* How it links to other Odins (the page Link): whether it has a link
	 * (a network board, a cable), linked to them or looped back, the port
	 * it could not open, each end plugged in; none: it never links */
	struct LinkStatus {
		bool board = false, linked = false, failed = false, previous = false, next = false;
	};
	virtual LinkStatus link_status() const { return {}; }
	/* How long its last frame lasts in its own time, in nanoseconds: a
	 * frame of its screen, 1/60 s unless it says otherwise */
	virtual uint64_t frame_nanoseconds() const { return 1000000000 / 60; }
};

} /* namespace odin */

#endif /* ODIN_BOARD_HPP */
