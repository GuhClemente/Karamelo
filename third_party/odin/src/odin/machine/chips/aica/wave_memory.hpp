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

#ifndef ODIN_WAVE_MEMORY_HPP
#define ODIN_WAVE_MEMORY_HPP

#include "odin/common/bytes.hpp"

#include <cstdint>

namespace odin {

/* A register of the AICA among its bytes: 16 bits, the low byte first */
inline uint32_t
aica_reg(const uint8_t *regs, uint32_t offset)
{
	return le16(regs + offset);
}

/* The wave memory as the voices and the effect processor of the AICA read
 * and write it; `mask` is its size less one. The board may watch some pages
 * of it: while its ARM7 counts the turns of a program without running them
 * (Arm7::set_turn_replay), it must hear of a read of a page those turns
 * change, and of a write to a page they read or change, before it
 * happens. */
class WaveMemory {
public:
	class Watcher {
	public:
		virtual ~Watcher() = default;
		virtual void reading(uint32_t addr, uint32_t size) = 0;
		virtual void writing(uint32_t addr, uint32_t size) = 0;
	};

	/* Pages of 4 KB, as the ARM7 watches them (Arm7::WATCH_PAGE) */
	static constexpr uint32_t PAGE_BITS = 12;
	/* A bit for each page, page n in bit n & 7 of byte n >> 3; nullptr:
	 * no page watched */
	struct Watch {
		Watcher *watcher;
		const uint8_t *reads;
		const uint8_t *writes;
	};

	WaveMemory(uint8_t *ram, uint32_t mask, const Watch &watch) : m_ram(ram), m_mask(mask), m_watch(watch) {}

	uint8_t read8(uint32_t addr) const
	{
		addr &= m_mask;
		if (watched(m_watch.reads, addr))
			m_watch.watcher->reading(addr, 1);
		return m_ram[addr];
	}
	/* A word of 16 bits: bit 0 of its address is ignored, and both bytes
	 * are in one page */
	uint16_t read16(uint32_t addr) const
	{
		addr &= m_mask & ~1u;
		if (watched(m_watch.reads, addr))
			m_watch.watcher->reading(addr, 2);
		return static_cast<uint16_t>(m_ram[addr] | (m_ram[addr + 1] << 8));
	}
	void write16(uint32_t addr, uint16_t value) const
	{
		addr &= m_mask & ~1u;
		if (watched(m_watch.writes, addr))
			m_watch.watcher->writing(addr, 2);
		m_ram[addr] = static_cast<uint8_t>(value);
		m_ram[addr + 1] = static_cast<uint8_t>(value >> 8);
	}

private:
	static bool watched(const uint8_t *pages, uint32_t addr)
	{
		const uint32_t page = addr >> PAGE_BITS;

		return pages && ((pages[page >> 3] >> (page & 7)) & 1);
	}

	uint8_t *m_ram;
	uint32_t m_mask;
	Watch m_watch;
};

} /* namespace odin */

#endif /* ODIN_WAVE_MEMORY_HPP */
