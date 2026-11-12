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

#include "odin/machine/chips/d71054.hpp"

#include <cstring>

namespace odin {

D71054::D71054()
{
	reset();
}

void
D71054::reset()
{
	std::memset(&m_s, 0, sizeof(m_s));
	m_s.next_pulse = UINT64_MAX;
	m_unknown = false;
}

/* Counter 0 in mode 2 clocked by counter 2 in mode 3: one pulse every
 * count 0 x count 2 clocks (a count of 0 is 65536); 0 when not so set. */
uint64_t
D71054::period() const
{
	if (!m_s.loaded[0] || !m_s.loaded[2] || m_s.mode[0] != 2 || m_s.mode[2] != 3)
		return 0;
	const uint64_t n0 = m_s.count[0] ? m_s.count[0] : 65536;
	const uint64_t n2 = m_s.count[2] ? m_s.count[2] : 65536;

	return n0 * n2;
}

void
D71054::write(uint8_t reg, uint8_t value, uint64_t now)
{
	if (reg == 3) {
		const unsigned counter = value >> 6;

		/* Read-back (counter 3) and the latch (RW 00) are not used */
		if (counter == 3 || !(value & 0x30)) {
			m_unknown = true;
			return;
		}
		/* LSB then MSB, binary; modes 6 and 7 are 2 and 3 */
		uint8_t mode = (value >> 1) & 7;

		if (mode > 5)
			mode = static_cast<uint8_t>(mode - 4);
		if ((value & 0x30) != 0x30 || (value & 1) || (counter == 0 && mode != 2) || (counter == 2 && mode != 3))
			m_unknown = true;
		m_s.mode[counter] = mode;
		m_s.high_next[counter] = 0;
		m_s.loaded[counter] = 0;
		if (counter == 0 || counter == 2)
			m_s.next_pulse = UINT64_MAX;
		return;
	}
	const unsigned counter = reg & 3;

	if (!m_s.high_next[counter]) {
		m_s.count[counter] = static_cast<uint16_t>((m_s.count[counter] & 0xFF00) | value);
		m_s.high_next[counter] = 1;
		return;
	}
	m_s.count[counter] = static_cast<uint16_t>((m_s.count[counter] & 0x00FF) | (value << 8));
	m_s.high_next[counter] = 0;
	m_s.loaded[counter] = 1;
	if ((counter == 0 || counter == 2) && period())
		m_s.next_pulse = now + period();
}

void
D71054::pulse_taken()
{
	if (m_s.next_pulse != UINT64_MAX)
		m_s.next_pulse += period();
}

} /* namespace odin */
