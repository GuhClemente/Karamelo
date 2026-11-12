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

#include "odin/machine/chips/memctl.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "odin/common/bytes.hpp"
#include "odin/common/stop_message.hpp"

/*
 * Memory Controller
 * =================
 *
 * The Hikaru mainboard is connected to a number of external boards: the
 * ROM board (ROMBD), the primary sound board, an optional sound board, an
 * optional network board.
 *
 * Access to these devices is performed through an 'external BUS'. The
 * memory controller decides which external device is mapped to what
 * part of the SH-4 address space.
 *
 * There are two of these memory controllers, one for each SH-4: the two
 * 315-6154 beside the two CPUs (MAME).
 *
 * Aside from memory device mapping, they also provide a DMA facility to
 * transfer data directly from different devices on the external BUS.
 *
 *
 * Mapping
 * =======
 *
 * Direct access to these external devices is possible by mapping them in
 * the SH-4 with the memory controller: in particular, they get mapped within
 * a couple of apertures: one is located at 02000000-03FFFFFF, the other
 * at 16000000-17FFFFFF. Other portions of the SH-4 address space (possibly
 * all of it) may be managed in a similar way, but apparently their emulation
 * is not critical (their mappings don't seem to change.)
 *
 * What is mapped to the apertures is determined by the  MMIOs. See
 * window_bank () for the details. Each byte in the MMIOs (+10, +14,
 * and +18 are confirmed) controls a 16 MB aperture. See @0C006688.
 *
 *
 * MMIO Ports
 * ==========
 *
 *  Offset	+3       +2       +1       +0
 *  +0x00	IIIIIIII IIIIIIII IIIIIIII IIIIIIII	Controller ID
 *  +0x04	---u---- -------- ---sEEEE EEFFFFFF	DMA Status
 *  +0x08	-------- -------- -------- --------
 *  +0x0C	-------- -------- -------- --------
 *  +0x10	dddddddd cccccccc bbbbbbbb aaaaaaaa	Aperture Addresses 0
 *  +0x14	hhhhhhhh gggggggg ffffffff eeeeeeee	Aperture Addresses 1
 *  +0x18	llllllll kkkkkkkk jjjjjjjj iiiiiiii	Aperture Addresses 2
 *  +0x1C	pppppppp oooooooo nnnnnnnn mmmmmmmm	Aperture Control 0?
 *  +0x20	tttttttt ssssssss rrrrrrrr qqqqqqqq	Aperture Control 1?
 *  +0x24	xxxxxxxx wwwwwwww vvvvvvvv uuuuuuuu	Aperture Control 2?
 *  +0x28	-------- -------- -------- --------
 *  +0x2C	-------- -------- -------- --------
 *  +0x30	SSSSSSSS SSSSSSSS SSSSSSSS SSS-----	DMA Source Address
 *  +0x34	DDDDDDDD DDDDDDDD DDDDDDDD DDD-----	DMA Destination Address
 *  +0x38	-------C LLLLLLLL LLLLLLLL LLLLLLLL	DMA Control
 *  +0x3C	-------- -------- -------- XXXXXXXX	Unknown
 *
 * Fields	Meaning			Values			References
 * -----------------------------------------------------------------------
 * +0x00	I = ID (0 for master, ~0 for slave)
 * +0x04	u = Unknown
 *		s = DMA status
 *		E = BUS error bits, Master
 *		F = BUS error bits, Slave
 * +0x10	a = Controls 14000080+
 *		b = Controls 15000000?
 *					40 [m]			@0C00BDFC
 *		c = Controls 16xxxxxx	40 [m]			@0C0016A4
 *					02 [m]			@0C00BDFC
 *		d = Controls 17xxxxxx	41 [m]			@0C0016A4
 *					04 [m]			@0C001C70
 *					06 [m]			@0C001C70
 *					03 [m]			@0C00BDFC
 * +0x14	e = Unknown		C0 [m]			@0C0016A4
 *					E6 [s]			@0C00BE70
 *					70 [s] !!!!!!!!!!!
 *		f = Unknown		C1 [m]			@0C0016A4
 *					EE [s]			@0C00BE70
 *		g = Unknown		F2 [m]			@0C0016A4
 *					C2 [m]			@0C00BDFC
 *					F4 [s]			@0C00BE70
 *		h = Unknown		F3 [m]			@0C0016A4
 *					C3 [m]			@0C00BDFC
 *					CC [s]			@0C00BE70
 * +0x18	i = Controls 00xxxxxx ?
 *		j = Controls 01xxxxxx ?
 *		k = Controls 02xxxxxx				@0C0016A4
 *		l = Controls 03xxxxxx				@0C007964,@0C001F3C
 * +0x1C	m = Controls 18xxxxxx[m]			@0C001CC4
 *		    Controls 14xxxxxx[s]	00,01
 *		n = Unknown
 *		o = Unknown			01 [m]		@0C00BDFC
 *						01 [s]		@0C00BE70
 *		p = Unknown
 * +0x20	q = Unknown			FE [m]		@0C00xxxx, @0C00BDFC
 *		r = Unknown			00 [m]
 *		s = Unknown			FE [m]
 *		t = Unknown			00 [m]
 * +0x24	u = Unknown			E6 [m]
 *		v = Unknown			5E [m]
 *						EE [m] MIE	@0C00BDFC
 *		w = Unknown			F4 [m] (E4, B4, F4 while accessing banks D, E in AIRTRIX)
 *						FD [m] SNDBD	@0C001F3C, @0C00BDFC
 *		x = Unknown			CC [m]		@0C00BDFC, @0C007820 => NIBBLES
 * +0x30	S = DMA source address
 * +0x34	D = DMA destination address
 * +0x38	C = DMA control (begin/busy)
 *		L = DMA transfer length in 32-bit words
 *		    See @0C008640
 * +0x3C	X = 0C to access the SNDBD1:027028BC
 *		    A2 to access the SNDBD2:027028BC
 *		    See @0C001748
 *
 * NOTE: other interesting evidence is at PH:@0C0124B8.
 *
 * NOTE: accessing 3C may alter other registers; for instance, the code at
 * @0C001748  saves/restores 04000018 before accessing 0400003C.
 *
 * NOTE: accessing the bus (apertures) may give rise to errors, both during
 * DMA operation and during normal access; these errors get reported in
 * fields E and F.
 *
 * NOTE: 0400001E (field 'o') may be related to cache and/or master-slave
 * synchronization. See PH:@0C0D6740 and its only caller.
 *
 * NOTE: direct access to the external BUS is likely prohibited when the DMA
 * is running. See the IRL2 handler, AICA case.
 *
 *
 * DMA Operation
 * =============
 *
 * The DMA is likely used to transfer data from the main RAM to devices on
 * different boards (ROMBD, SNDBD, SNDBD2, NETBD). DMA operation is initiated
 * by setting bit 24 of +38. Upon termination, the MEMCTL raises IRL 1 on
 * the master SH-4, and sets bit 12 of +04 and its corresponding error field.
 *
 * See @0C008640 for more details.
 *
 * NOTE: it may be the case that bit 12 and the error field are mutually
 * exclusive.
 *
 *
 * External BUS Address Space
 * ==========================
 *
 * The address space of the external BUS is as follows:
 *
 * 04000000-043FFFFF	Texture RAM Bank 0
 * 06000000-063FFFFF	Texture RAM Bank 1
 * 0A000000-0A00FFFF	Unknown; ROMBD-related
 * 0C000000-0CFFFFFF	Sound Board 1
 * 0D000000-0DFFFFFF	Sound Board 2 [Optional]
 * 0E000000-0E00FFFF	Network Board [Optional]
 * 10000000-3FFFFFFF	ROMBD (EPROM, MASKROM, EEPROM get mapped here)
 * 40000000-41FFFFFF	Slave RAM
 * 48000000-483FFFFF	GPU CMDRAM
 * 4C000000-4C3FFFFF	GPU Unknown
 * 70000000-71FFFFFF	Master RAM
 *
 * NOTE: apparently the external bus is 31 bits wide or less. The MSB is
 * used in mysterious ways. For instance, the code uses the MEMCTL DMA to
 * read ROM data, but accesses the following ranges:
 *
 * 90000000-9FFFFFFF - 80000000 = 10000000-1FFFFFFF	EPROM
 * A0000000-AFFFFFFF - 80000000 = 20000000-2FFFFFFF	MASKROM
 */

namespace odin {

namespace {

[[noreturn]] void
fatal(const char *what, unsigned size, uint32_t addr)
{
	stop_fatal("memory controller: %s (%u bytes at %08X)", what, size, addr);
}

/* The page of the bridge, 04000000-04FFFFFF, with the registers in its
 * first NUM_REGS bytes */
constexpr uint32_t BRIDGE_PAGE = 0x04000000;

bool
in_registers(uint32_t addr)
{
	return addr >= BRIDGE_PAGE && addr < BRIDGE_PAGE + MemCtl::NUM_REGS;
}

bool
in_bridge_page(uint32_t addr)
{
	return addr >= BRIDGE_PAGE && addr <= BRIDGE_PAGE + 0xFFFFFF;
}

} /* namespace */

MemCtl::MemCtl(Host &host, bool master)
	: m_host(host), m_regs{}, m_master(master), m_bridge_page_told(false)
{
}

void
MemCtl::reset()
{
	m_regs.fill(0);
	set_reg(0x00, 4, m_master ? 0 : 0xFFFFFFFF);
}

uint32_t
MemCtl::reg(uint32_t offs, unsigned size) const
{
	if (size > 4 || offs + size > NUM_REGS)
		fatal("a register read of that size", size, offs);
	return static_cast<uint32_t>(load_bytes(&m_regs[offs], size));
}

void
MemCtl::set_reg(uint32_t offs, unsigned size, uint64_t value)
{
	if (size > 8 || offs + size > NUM_REGS)
		fatal("a register write of that size", size, offs);
	store_bytes(&m_regs[offs], size, value);
}

/* Whether a write of the registers reaches a byte of the windows */
bool
MemCtl::writes_window(uint32_t offs, unsigned size)
{
	const auto reaches = [offs, size](uint32_t first, uint32_t end) { return offs < end && offs + size > first; };

	return reaches(0x12, 0x14) || reaches(0x1A, 0x1D);
}

/* The page of the external bus a window is open on: a byte of the registers
 * for each window. */
uint32_t
MemCtl::window_bank(uint32_t addr) const
{
	switch (addr >> 24) {
	case 0x02:
		return m_regs[0x1A];
	case 0x03:
		return m_regs[0x1B];
	case 0x16:
		return m_regs[0x12];
	case 0x17:
		return m_regs[0x13];
	case 0x18:
		return m_regs[0x1C];
	default:
		return 0;
	}
}

void
MemCtl::tell_bridge_page(char rw, unsigned size, uint32_t addr, uint64_t value)
{
	char text[96];

	if (m_bridge_page_told)
		return;
	m_bridge_page_told = true;
	if (rw == 'R')
		std::snprintf(text, sizeof(text), "unmapped bridge page R%u %08X (reported once)", size * 8, addr);
	else
		std::snprintf(text, sizeof(text), "unmapped bridge page W%u %08X = %" PRIX64 " (reported once)",
		              size * 8, addr, value);
	m_host.memctl_error(text);
}

bool
MemCtl::read(uint32_t addr, unsigned size, uint64_t &value)
{
	if (in_registers(addr)) {
		if (size == 8) {
			value = reg(addr & 0x3F, 4) | (static_cast<uint64_t>(reg((addr & 0x3F) + 4, 4)) << 32);
			return true;
		}
		value = reg(addr & 0x3F, size);
		return true;
	}
	if (in_bridge_page(addr)) {
		/* Rest of the bridge page: registers 40-FF (MAME hikaru.cpp
		 * sh4_map_main maps 04000000-040000FF) and nothing above. The
		 * game loader reads 04000040-04FF03FF for SWRACER and SGNASCAR,
		 * whose tables ask for more than window 03 holds (0C00B8E6);
		 * reads as zero (HEURISTIC). */
		value = 0;
		tell_bridge_page('R', size, addr, 0);
		return true;
	}

	value = 0;
	return m_host.bus_read((window_bank(addr) << 24) | (addr & 0xFFFFFF), size, value);
}

bool
MemCtl::write(uint32_t addr, unsigned size, uint64_t value)
{
	if (in_registers(addr)) {
		switch (addr & 0xFF) {
		case 0x04: {
			if (size != 2)
				fatal("the status is 16 bits wide", size, addr);
			/* A bit written at 1 clears the bit it reads: the end
			 * of a transfer (bit 12, see irq_line) and the bus
			 * errors of the master (11 to 6) and of the slave (5
			 * to 0). Bits 15 to 13 stay. */
			const uint32_t old = reg(0x04, 2);

			set_reg(0x04, 2, (old & 0xE000) | (old & ~value & 0x1FFF));
			m_host.memctl_irq_changed();
			return true;
		}
		case 0x06:
			if (size != 2)
				fatal("+06 is 16 bits wide", size, addr);
			break;
		case 0x30:
		case 0x34:
		case 0x38:
			if (size != 4)
				fatal("the transfer registers are 32 bits wide", size, addr);
			break;
		}
		set_reg(addr & 0x3F, size, value);
		if (writes_window(addr & 0x3F, size))
			m_host.memctl_windows_changed();
		if ((addr & 0xFF) == 0x38)
			run_transfer();
		return true;
	}
	if (in_bridge_page(addr)) {
		tell_bridge_page('W', size, addr, value);
		return true;
	}

	return m_host.bus_write((window_bank(addr) << 24) | (addr & 0xFFFFFF), size, value);
}

std::span<const uint8_t>
MemCtl::read_span(uint32_t addr)
{
	if (in_bridge_page(addr))
		return {};

	const uint32_t offs = addr & 0xFFFFFF;
	const std::span<const uint8_t> span = m_host.bus_read_span((window_page(addr) << 24) | offs);

	return span.first(std::min<size_t>(span.size(), 0x1000000 - offs));
}

std::span<uint8_t>
MemCtl::write_span(uint32_t addr)
{
	if (in_bridge_page(addr))
		return {};

	const uint32_t offs = addr & 0xFFFFFF;
	const std::span<uint8_t> span = m_host.bus_write_span((window_page(addr) << 24) | offs);

	return span.first(std::min<size_t>(span.size(), 0x1000000 - offs));
}

/* The transfer copies the whole block as soon as it is started, like the
 * 315-6154 in MAME: games write the staging buffer for the next block right
 * after starting a transfer (von4 model loader, 0C055FD4), so a transfer
 * spread over several scanlines would read overwritten data. */
void
MemCtl::run_transfer()
{
	uint32_t length = reg(0x38, 4);

	if (!(length & 0x01000000))
		return;

	uint32_t src = reg(0x30, 4), dst = reg(0x34, 4);

	length &= 0xFFFFFF;
	while (length) {
		uint32_t moved = m_host.bus_copy(src & 0x7FFFFFFF, dst & 0x7FFFFFFF, length);

		if (moved == 0) {
			uint64_t word = 0;

			m_host.bus_read(src & 0x7FFFFFFF, 4, word);
			m_host.bus_write(dst & 0x7FFFFFFF, 4, static_cast<uint32_t>(word));
			moved = 1;
		}
		src += 4 * moved;
		dst += 4 * moved;
		length -= moved;
	}

	/* Done: the addresses stay where the transfer ended, the status
	 * tells its end and no error. */
	set_reg(0x30, 4, src);
	set_reg(0x34, 4, dst);
	set_reg(0x38, 4, 0);
	set_reg(0x04, 2, 0x1000);
	if (!m_master)
		stop_fatal("the bridge of the slave ended a DMA: where its interrupt goes is not known");
	m_host.memctl_irq_changed();
}

} /* namespace odin */
