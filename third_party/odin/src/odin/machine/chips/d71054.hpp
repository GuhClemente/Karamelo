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

#ifndef ODIN_D71054_HPP
#define ODIN_D71054_HPP

#include <array>
#include <cstdint>

namespace odin {

/* NEC D71054, the three counters of an 8254, as the 837-13741 uses them
 * (epr-21868 0120-0145): counter 2 in mode 3 divides the clock, counters
 * 0 and 1 in mode 2 divide its output; the output of counter 0 is the
 * interrupt input of the 315-6146. HEURISTIC: that wiring and the clock
 * (16 MHz, the clock of the Z80: 16 MHz / 1000 / 16 makes the 1 ms the
 * numbers of the program aim at); the first pulse a whole period after
 * counter 0 is loaded. Times in clocks of the board. */
class D71054 {
public:
	struct State {
		std::array<uint16_t, 3> count;
		std::array<uint8_t, 3> mode;
		std::array<uint8_t, 3> high_next;	/* the next write is the MSB */
		std::array<uint8_t, 3> loaded;
		uint64_t next_pulse;			/* UINT64_MAX: none */
	};

	D71054();
	void reset();
	/* Registers 0-2 the counters, 3 the control word, at clock `now` */
	void write(uint8_t reg, uint8_t value, uint64_t now);
	/* The clock of the next pulse of counter 0, UINT64_MAX for none */
	uint64_t next_pulse() const { return m_s.next_pulse; }
	/* That pulse given: the next one */
	void pulse_taken();
	/* A setting the model does not run, for the board to tell */
	bool unknown() const { return m_unknown; }

	void save(State &out) const { out = m_s; }
	void load(const State &in) { m_s = in; }

private:
	uint64_t period() const;

	State m_s;
	bool m_unknown = false;
};

} /* namespace odin */

#endif /* ODIN_D71054_HPP */
