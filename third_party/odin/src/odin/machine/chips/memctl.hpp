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
 * Valkyrie
 * Copyright (C) 2011, 2012, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_MEMCTL_HPP
#define ODIN_MEMCTL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace odin {

/* The memory controller of a SH-4 of the Hikaru: it opens windows of the
 * address space of the processor on the external bus of the board, and
 * moves blocks along that bus by itself. See memctl.cpp for its registers
 * and for the bus. */
class MemCtl {
public:
	/* The board around the controller */
	class Host {
	public:
		virtual ~Host() = default;
		/* An access of the external bus, 1, 2, 4 or 8 bytes; bit 31
		 * is the top bit of the page of the window. False when
		 * nothing answers. */
		virtual bool bus_read(uint32_t bus_addr, unsigned size, uint64_t &value) = 0;
		virtual bool bus_write(uint32_t bus_addr, unsigned size, uint64_t value) = 0;
		/* Moves up to `words` 32-bit words from src to dst at once, as
		 * that many reads and writes of the bus, one word after the
		 * other, would. Returns how many it moved: 0 has the controller
		 * move the next word by bus_read() and bus_write(). */
		virtual uint32_t bus_copy(uint32_t src, uint32_t dst, uint32_t words)
		{
			(void)src;
			(void)dst;
			(void)words;
			return 0;
		}
		/* For accesses in a row from bus_addr on: the bytes reads give,
		 * or writes store, as they are in a memory, up to where an
		 * access would do more. Whatever the accesses tell the board
		 * there, it is told now. Empty where an access does more. */
		virtual std::span<const uint8_t> bus_read_span(uint32_t bus_addr)
		{
			(void)bus_addr;
			return {};
		}
		virtual std::span<uint8_t> bus_write_span(uint32_t bus_addr)
		{
			(void)bus_addr;
			return {};
		}
		/* The interrupt line of the controller may have moved. */
		virtual void memctl_irq_changed() = 0;
		/* A register of the windows was written. */
		virtual void memctl_windows_changed() {}
		/* Something the program did that has no answer here. */
		virtual void memctl_error(const char *what) = 0;
	};

	static constexpr size_t NUM_REGS = 0x40;

	MemCtl(Host &host, bool master);

	void reset();

	/* The processor side: the page 04000000-04FFFFFF, with the registers
	 * in its first 0x40 bytes, and the windows at the 02, 03, 16, 17 and 18
	 * million-pages. False when nothing answers on the external bus. */
	bool read(uint32_t addr, unsigned size, uint64_t &value);
	bool write(uint32_t addr, unsigned size, uint64_t value);
	/* The same for accesses in a row from addr on, as Host::bus_read_span()
	 * and bus_write_span(), within the window. */
	std::span<const uint8_t> read_span(uint32_t addr);
	std::span<uint8_t> write_span(uint32_t addr);
	/* The page of the external bus a window is open on */
	uint32_t window_page(uint32_t addr) const { return window_bank(addr) & 0x7F; }

	/* The local bus of the ROM board is 32 bits wide. HEURISTIC: bit 3 of
	 * +26, as the programs set it: FC for the 32-bit FPGA of SGNASCAR, F4
	 * and F6 for the 315-5881 that AIRTRIX, SWRACER and PHARRIER reach 16
	 * bits at a time, FD then F4 for the two halves of the probe of the
	 * BOOTROM (ROM 0x1F3A). */
	bool wide_local_bus() const { return (reg(0x26, 1) & 0x08) != 0; }
	/* IRL1 of the master SH-4, held while bit 12 of +04 is set: the
	 * handler of the games (PHARRIER @0C0102A0) tests the bit and writes
	 * back what it read to let the line go. */
	bool irq_line() const { return (reg(0x04, 2) & 0x1000) != 0; }

	/* The registers, for the save states */
	std::span<uint8_t> registers() { return m_regs; }

private:
	uint32_t reg(uint32_t offs, unsigned size) const;
	void set_reg(uint32_t offs, unsigned size, uint64_t value);
	uint32_t window_bank(uint32_t addr) const;
	static bool writes_window(uint32_t offs, unsigned size);
	void run_transfer();
	void tell_bridge_page(char rw, unsigned size, uint32_t addr, uint64_t value);

	Host &m_host;
	std::array<uint8_t, NUM_REGS> m_regs;
	bool m_master;
	/* The page above the registers was already told about */
	bool m_bridge_page_told;
};

} /* namespace odin */

#endif /* ODIN_MEMCTL_HPP */
