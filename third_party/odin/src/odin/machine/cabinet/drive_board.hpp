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

#ifndef ODIN_DRIVE_BOARD_HPP
#define ODIN_DRIVE_BOARD_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "odin/machine/cabinet/jvs.hpp"

namespace odin {

/* The drive board of a cabinet, the motor of its wheel, as the outputs of
 * the I/O board command it: the command, and the force it asks for when
 * the board itself does not run (no program, or ODIN_DRIVE=imitation;
 * drive_838_13366 runs it). NASCAR: 838-13366 running EPR-20512, as Sega
 * Rally 2. The command is a byte on eight outputs, TX0 first, low when set
 * (deluxe wiring: CN9 pins 25-32 of the 837-13844; the decoding of
 * Flycast, measured here: 00 at power-on, C1 and C7 to start, 40 at rest).
 * Its commands (njz3, DRIVEBOARD.md): 00-3F a force to the left (00 none),
 * 40-7F to the right (40 none), 8X the tests, C6, C7, CB, FF the others. */
struct DriveBoard {
	/* The strongest force each way */
	static constexpr uint8_t MOST = 0x3F;

	/* The command on the outputs of the board, TX0 on first_output (1 to
	 * 32 as JVS counts them, output 1 bit 7 of the first byte) */
	static uint8_t command(std::span<const uint8_t> outputs, unsigned first_output)
	{
		uint8_t set = 0;

		for (unsigned bit = 0; bit < 8; bit++)
			if (jvs::output_on(outputs, first_output + bit - 1))
				set = static_cast<uint8_t>(set | (1u << bit));
		return static_cast<uint8_t>(~set);
	}

	/* The force of a command, -MOST (all to the left) to MOST (all to the
	 * right); none for the other commands */
	static int force_of(uint8_t command)
	{
		if (command >= 0x80)
			return 0;
		return command < 0x40 ? -command : command - 0x40;
	}

	/* What a move of the wheel by 1 of its way in a frame can make the
	 * force jump by, and the most it can: NASCAR at full force, its wheel
	 * read in 80 steps each way (0C03F2B8), a step of 1/256 of the way:
	 * its damping half of 600 times its steps, 960, its self-aligning 500,
	 * its hold 100 (850 seen); 52 seen at most */
	static constexpr double FROM_WHEEL = 1600;
	static constexpr double MOST_FROM_WHEEL = 50;

	/* How hard a pad shakes for a jump of the force from one frame to the
	 * next, 0 to 0xFFFF, the wheel having moved by wheel_moved (0 to 1 of
	 * its way) in a frame of the last ones. NASCAR gives one torque of its
	 * physics (0C03F34A: a float times 63, 40 added when it is below 0),
	 * seven terms added (0C038F32): three follow the wheel (a hold that
	 * grows with the speed, the self-aligning of the front tyres, a damping
	 * that kicks at each step the wheel takes), four the car (its front
	 * suspensions, the walls, a buzz of 5 at most, the draft). A wall makes
	 * it jump by up to 51 with the wheel still (measured in Odin).
	 * HEURISTIC: 4 of a jump goes, and what the move of the
	 * wheel can give, the rest twice over against MOST. */
	static uint16_t jolt_of(int jump, double wheel_moved)
	{
		const double shock = std::abs(jump) - 4 - std::min(MOST_FROM_WHEEL, FROM_WHEEL * wheel_moved);

		return shock <= 0 ? 0 : static_cast<uint16_t>(std::lround(std::min(1.0, 2 * shock / MOST) * 0xFFFF));
	}
};

} /* namespace odin */

#endif /* ODIN_DRIVE_BOARD_HPP */
