/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
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


#ifndef ODIN_FB_WRITES_HPP
#define ODIN_FB_WRITES_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "odin/machine/cpu/sh4/sh4.hpp"

namespace odin {

/* Dates the writes to the framebuffer RAM by page of 4 KB, for the screen
 * to take again only what may have changed. The board tells the writes it
 * makes; those the master SH-4 makes in place are seen by watching its
 * pages (Sh4::watch), when the screen asks. A date only grows. */
class FbWrites {
public:
	static constexpr size_t PAGE = 4096;
	static_assert(PAGE == Sh4::PAGE_SIZE);

	explicit FbWrites(size_t size) : m_cpu(nullptr), m_first(0), m_date(1), m_dates(size / PAGE, 0) {}

	/* The CPU that has the RAM in its pages, from its page `first` on */
	void set_cpu(Sh4 *cpu, uint32_t first)
	{
		m_cpu = cpu;
		m_first = first;
	}

	void wrote(size_t offs, size_t size)
	{
		if (size == 0 || offs >= m_dates.size() * PAGE)
			return;
		const size_t last = std::min((offs + size - 1) / PAGE, m_dates.size() - 1);

		std::fill(m_dates.begin() + static_cast<std::ptrdiff_t>(offs / PAGE),
		          m_dates.begin() + static_cast<std::ptrdiff_t>(last + 1), m_date);
	}
	void wrote_all() { std::fill(m_dates.begin(), m_dates.end(), m_date); }

	/* The latest date of a write that may have fallen in those bytes. The
	 * pages the CPU wrote since they were watched take the date of now,
	 * and are watched again. */
	uint64_t written(size_t offs, size_t size)
	{
		uint64_t latest = 0;

		if (size == 0 || offs + size > m_dates.size() * PAGE)
			return m_date;
		for (size_t page = offs / PAGE; page <= (offs + size - 1) / PAGE; page++) {
			const uint32_t cpu_page = m_first + static_cast<uint32_t>(page);

			if (!m_cpu || !m_cpu->watched(cpu_page)) {
				m_dates[page] = m_date;
				if (m_cpu)
					m_cpu->watch(cpu_page);
			}
			latest = std::max(latest, m_dates[page]);
		}
		return latest;
	}

	/* A new date: the writes from now on are later than every date given
	 * so far. */
	uint64_t next() { return ++m_date; }

private:
	Sh4 *m_cpu;
	uint32_t m_first;
	uint64_t m_date;
	std::vector<uint64_t> m_dates;
};

} /* namespace odin */

#endif /* ODIN_FB_WRITES_HPP */
