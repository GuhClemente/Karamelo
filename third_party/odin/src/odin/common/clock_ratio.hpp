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

#ifndef ODIN_CLOCK_RATIO_HPP
#define ODIN_CLOCK_RATIO_HPP

#include <cstdint>

namespace odin {

/* The ticks of a clock of `to_hz` in `cycles` of a clock of `from_hz`;
 * what is left below a tick stays in `rest` for the next call */
inline uint64_t
take_ticks(uint64_t &rest, uint32_t cycles, uint32_t to_hz, uint32_t from_hz)
{
	rest += static_cast<uint64_t>(cycles) * to_hz;

	const uint64_t ticks = rest / from_hz;

	rest -= ticks * from_hz;
	return ticks;
}

} /* namespace odin */

#endif /* ODIN_CLOCK_RATIO_HPP */
