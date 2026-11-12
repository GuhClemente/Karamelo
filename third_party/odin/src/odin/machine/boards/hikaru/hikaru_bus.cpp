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
 * Copyright (C) 2011-2013, Stefano Teso
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

/* What each SH-4 of the Hikaru sees: its memory map, its port A, and the
 * external bus the memory controllers open windows on. */

#include "odin/machine/boards/hikaru/hikaru.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <cstring>

#include "odin/common/bytes.hpp"
#include "odin/common/stop_message.hpp"

namespace odin {

namespace {

[[noreturn]] void
fatal(const char *what, uint32_t addr, unsigned size)
{
	stop_fatal("%s (%u bytes at %08X)", what, size, addr);
}

/* A value of a memory, least significant byte first */
uint64_t
load(std::span<const uint8_t> memory, uint32_t offs, unsigned size)
{
	if (offs + size > memory.size())
		fatal("a read past the end of a memory", offs, size);
	return load_bytes(&memory[offs], size);
}

void
store(std::span<uint8_t> memory, uint32_t offs, unsigned size, uint64_t value)
{
	if (offs + size > memory.size())
		fatal("a write past the end of a memory", offs, size);
	store_bytes(&memory[offs], size, value);
}

/* Where the master sees the external bus through its memory controller */
bool
master_window(uint32_t addr)
{
	return (addr >= 0x02000000 && addr <= 0x03FFFFFF) || (addr >= 0x16000000 && addr <= 0x17FFFFFF);
}

/* Where a bit of an offset of the texture RAM goes when the bus writes it
 * untwiddled, see Hikaru::texram_write */
const struct {
	uint32_t bit_in, bit_out;
} TWIDDLE[] = {
	{ 0x000001, 0x000001 },
	{ 0x000800, 0x000002 },
	{ 0x000002, 0x000004 },
	{ 0x001000, 0x000008 },
	{ 0x000004, 0x000010 },
	{ 0x002000, 0x000020 },
	{ 0x000008, 0x000040 },
	{ 0x004000, 0x000080 },
	{ 0x000010, 0x000100 },
	{ 0x008000, 0x000200 },
	{ 0x000020, 0x000400 },
	{ 0x010000, 0x000800 },
	{ 0x000040, 0x001000 },
	{ 0x020000, 0x002000 },
	{ 0x000080, 0x004000 },
	{ 0x040000, 0x008000 },
	{ 0x000100, 0x010000 },
	{ 0x080000, 0x020000 },
	{ 0x000200, 0x040000 },
	{ 0x100000, 0x080000 },
	{ 0x000400, 0x100000 },
};

uint32_t
twiddle(uint32_t offs)
{
	uint32_t out = 0;

	for (const auto &bit : TWIDDLE)
		if (offs & bit.bit_out)
			out |= bit.bit_in;
	return out;
}

} /* namespace */

/* The plain memory the cores read and write in place, page by page, without
 * the bus. A page is plain when the whole of it is: the first page of the
 * command RAM, which starts with the master and slave box, is not. The
 * mirrors of the command RAM go through the bus (a page a core sees twice
 * is no longer written in place). */
void
Hikaru::map_pages()
{
	const uint32_t page_size = Sh4::PAGE_SIZE;
	const auto map = [page_size](Sh4 &cpu, uint32_t first, uint32_t last, std::span<uint8_t> memory,
	                             uint32_t mask, bool read, bool write) {
		for (uint32_t addr = first; addr < last; addr += page_size) {
			uint8_t *const page = &memory[addr & mask];

			cpu.map_page(addr / page_size, read ? page : nullptr, write ? page : nullptr);
		}
	};
	/* The BOOTROM is not written: only its read pointers are taken. */
	const std::span<uint8_t> bootrom(const_cast<uint8_t *>(m_bootrom.data()), m_bootrom.size());

	for (unsigned which : { MASTER, SLAVE }) {
		Sh4 &cpu = *m_sh[which];

		for (uint32_t page = 0; page < 0x20000000 / page_size; page++)
			cpu.map_page(page, nullptr, nullptr);
		map(cpu, 0x0C000000, 0x0E000000, m_ram[which], 0x01FFFFFF, true, true);
		map(cpu, 0x00000000, 0x00200000, bootrom, 0x001FFFFF, true, false);
	}
	map(*m_sh[MASTER], 0x00C00000, 0x00C10000, m_bram, 0x0000FFFF, true, true);
	map(*m_sh[MASTER], 0x14001000, 0x14400000, m_cmdram, 0x003FFFFF, true, true);
	map(*m_sh[MASTER], 0x1B000000, 0x1B800000, m_fb, 0x007FFFFF, true, true);
	map(*m_sh[SLAVE], 0x10001000, 0x10400000, m_cmdram, 0x003FFFFF, true, true);

	/* Others write these in place too: the command RAM (the other SH-4,
	 * the command processor), the frame buffer (the 2D chip) and the
	 * backup RAM (the seat of a linked cabinet). What the bus writes in
	 * the RAM of a SH-4, it tells: ram_written(). */
	const auto shared = [page_size](Sh4 &cpu, uint32_t first, uint32_t last) {
		for (uint32_t addr = first; addr < last; addr += page_size)
			cpu.map_shared(addr / page_size);
	};
	shared(*m_sh[MASTER], 0x00C00000, 0x00C10000);
	shared(*m_sh[MASTER], 0x14001000, 0x14400000);
	shared(*m_sh[MASTER], 0x1B000000, 0x1B800000);
	shared(*m_sh[SLAVE], 0x10001000, 0x10400000);

	/* The windows of the CPUs on the MIE and their bus request registers,
	 * which the games poll all along: the MIE has no way to the memory
	 * of a CPU, see mie_read() and mie_write(). */
	for (uint32_t addr = 0x00800000; addr < 0x00840000; addr += page_size)
		m_sh[MASTER]->map_quiet(addr / page_size);
	m_sh[MASTER]->map_quiet(0x01000000 / page_size);
	m_sh[SLAVE]->map_quiet(0x1B000000 / page_size);
	/* The registers of the display chip, which the master polls: they
	 * change nothing but the chip and its interrupt (Eurasia::write). */
	m_sh[MASTER]->map_quiet(0x1A000000 / page_size);
	quiet_windows();
}

/* The windows of the master open on the ROM board (pages 10 to 3F of the
 * external bus) or on the wave memory of a sound board (the upper half of
 * pages 0C and 0D): what answers there writes no memory of a CPU, and
 * what it raises it tells the CPU. */
void
Hikaru::quiet_windows()
{
	const uint32_t pages = 0x1000000 / Sh4::PAGE_SIZE;

	for (uint32_t window : { 0x02000000u, 0x03000000u, 0x16000000u, 0x17000000u }) {
		const uint32_t page = m_memctl[MASTER].window_page(window), first = window / Sh4::PAGE_SIZE;

		m_sh[MASTER]->set_quiet(first, pages, page >= 0x10 && page <= 0x3F);
		if (page == 0x0C || page == 0x0D)
			m_sh[MASTER]->set_quiet(first + pages / 2, pages / 2, true);
	}
}

/*
 * Master SH-4 Memory Map
 * ======================
 *
 * Area 0	00000000-00200000 Boot ROM
 *		00400000-00400003 ?
 *		00800000-0083FFFF On-board Switches + MIE
 *		00C00000-00C0FFFF Backup RAM
 *		01000000-01000003 ?
 *		01000100-01000103 ?
 *		02000000-02FFFFFF Aperture-02
 *		03000000-03FFFFFF Aperture-03
 * Area 1	04000000-0400003F Memory Controller [Master]
 * Area 3	0C000000-0DFFFFFF RAM
 * Area 5	14000000-1400002F Master/Slave Communication Box
 *		14000030-14FFFFFF Command RAM (4 MB and its mirrors)
 *		15000000-150FFFFF Geometry Processor
 *		16000000-16FFFFFF Aperture-16
 *		17000000-17FFFFFF Aperture-17
 * Area 6	18001000-1800101F GPU Regs
 *		1A000000-1A0FFFFF Image Generator
 *		1B000000-1B7FFFFF Frame Buffer
 *
 * What is at an address answers accesses of some sizes only; another size
 * has no answer.
 */

bool
Hikaru::master_read(uint32_t addr, unsigned size, uint64_t &value)
{
	if (addr >= 0x0C000000 && addr <= 0x0DFFFFFF) {
		value = load(m_ram[MASTER], addr & 0x01FFFFFF, size);
		return true;
	}
	if (addr <= 0x001FFFFF) {
		value = load(m_bootrom, addr, size);
		return true;
	}
	if (addr >= 0x00400000 && addr <= 0x00400001) {
		if (size != 2 && size != 4)
			return false;
		log_access("UNK/M", 'R', size, addr, 0);
		return gpu_read(addr, size, value);
	}
	/* Not logged: the MIE is polled in tight loops. */
	if (addr >= 0x00800000 && addr <= 0x0083FFFF)
		return size != 8 && mie_read(MASTER, addr, size, value);
	if (addr >= 0x00C00000 && addr <= 0x00C0FFFF) {
		value = load(m_bram, addr & 0x0000FFFF, size);
		return true;
	}
	/* The bus request register of the MIE */
	if (addr >= 0x01000000 && addr <= 0x010001FF)
		return (size == 2 || size == 4) && mie_read(MASTER, addr, size, value);
	if (addr >= 0x02000000 && addr <= 0x03FFFFFF)
		return m_memctl[MASTER].read(addr, size, value);
	/* The whole bridge page: registers at 00-3F, the rest reads as zero */
	if (addr >= 0x04000000 && addr <= 0x04FFFFFF) {
		log_access("MEMCTL/M", 'R', size, addr, 0);
		return m_memctl[MASTER].read(addr, size, value);
	}
	if (addr >= 0x14000000 && addr <= 0x1400002F) {
		log_access("MSCOMM/M", 'R', size, addr, 0);
		return mscomm_read(addr, size, value);
	}
	if (addr >= 0x14000030 && addr <= 0x143FFFFF) {
		value = load(m_cmdram, addr & 0x003FFFFF, size);
		return true;
	}
	/* The command RAM answers above its 4 MB: VON4 leaves a program at
	 * 14FFF000, which the slave runs at 10FFF000 (@0C711C84).
	 * HEURISTIC: mirrors, the two upper address lines taken as not
	 * wired; only that page is seen. */
	if (addr >= 0x14400000 && addr <= 0x14FFFFFF) {
		log_access("CMDRAM/M mirrors", 'R', size, addr, 0);
		value = load(m_cmdram, addr & 0x003FFFFF, size);
		return true;
	}
	if (addr >= 0x15000000 && addr <= 0x150FFFFF) {
		if (size != 2 && size != 4)
			return false;
		log_access("GPU/M", 'R', size, addr, 0);
		return gpu_read(addr, size, value);
	}
	if (addr >= 0x16000000 && addr <= 0x17FFFFFF)
		return m_memctl[MASTER].read(addr, size, value);
	if (addr >= 0x18001000 && addr <= 0x1800101F) {
		if (size != 4)
			return false;
		log_access("GPU/M", 'R', size, addr, 0);
		return gpu_read(addr, size, value);
	}
	if (addr >= 0x1A000000 && addr <= 0x1A0FFFFF) {
		log_access("GPU/M", 'R', size, addr, 0);
		return gpu_read(addr, size, value);
	}
	if (addr >= 0x1B000000 && addr <= 0x1B7FFFFF) {
		value = load(m_fb, addr & 0x007FFFFF, size);
		return true;
	}
	return false;
}

bool
Hikaru::master_write(uint32_t addr, unsigned size, uint64_t value)
{
	if (addr >= 0x0C000000 && addr <= 0x0DFFFFFF) {
		store(m_ram[MASTER], addr & 0x01FFFFFF, size, value);
		return true;
	}
	/* Writes to the BOOTROM have no effect, as on the hardware. */
	if (addr <= 0x001FFFFF)
		return true;
	if (addr >= 0x00400000 && addr <= 0x00400001) {
		if (size != 2 && size != 4)
			return false;
		log_access("UNK/M", 'W', size, addr, value);
		return gpu_write(addr, size, value);
	}
	if (addr >= 0x00800000 && addr <= 0x0083FFFF)
		return size != 8 && mie_write(MASTER, addr, size, value);
	if (addr >= 0x00C00000 && addr <= 0x00C0FFFF) {
		store(m_bram, addr & 0x0000FFFF, size, value);
		return true;
	}
	if (addr >= 0x01000000 && addr <= 0x010001FF)
		return (size == 2 || size == 4) && mie_write(MASTER, addr, size, value);
	if (addr >= 0x02000000 && addr <= 0x03FFFFFF)
		return m_memctl[MASTER].write(addr, size, value);
	if (addr >= 0x04000000 && addr <= 0x04FFFFFF) {
		log_access("MEMCTL/M", 'W', size, addr, value);
		return m_memctl[MASTER].write(addr, size, value);
	}
	if (addr >= 0x14000000 && addr <= 0x1400002F) {
		log_access("MSCOMM/M", 'W', size, addr, value);
		return mscomm_write(addr, size, value);
	}
	if (addr >= 0x14000030 && addr <= 0x143FFFFF) {
		store(m_cmdram, addr & 0x003FFFFF, size, value);
		return true;
	}
	if (addr >= 0x14400000 && addr <= 0x14FFFFFF) {
		log_access("CMDRAM/M mirrors", 'W', size, addr, value);
		store(m_cmdram, addr & 0x003FFFFF, size, value);
		return true;
	}
	if (addr >= 0x15000000 && addr <= 0x150FFFFF) {
		if (size != 2 && size != 4)
			return false;
		log_access("GPU/M", 'W', size, addr, value);
		return gpu_write(addr, size, value);
	}
	if (addr >= 0x16000000 && addr <= 0x17FFFFFF)
		return m_memctl[MASTER].write(addr, size, value);
	if (addr >= 0x18001000 && addr <= 0x1800101F) {
		if (size != 4)
			return false;
		log_access("GPU/M", 'W', size, addr, value);
		return gpu_write(addr, size, value);
	}
	if (addr >= 0x1A000000 && addr <= 0x1A0FFFFF) {
		log_access("GPU/M", 'W', size, addr, value);
		return gpu_write(addr, size, value);
	}
	if (addr >= 0x1B000000 && addr <= 0x1B7FFFFF) {
		store(m_fb, addr & 0x007FFFFF, size, value);
		m_fb_writes.wrote(addr & 0x007FFFFF, size);
		return true;
	}
	return false;
}

/*
 * Slave SH-4 Memory Map
 * =====================
 *
 * Area 0	00000000-001FFFFF Boot ROM
 * Area 1	04000000-0400003F Memory Controller [Slave]
 * Area 3	0C000000-0DFFFFFF RAM
 * Area 4	10000000-100000FF Master/Slave Communication Box
 *		10000100-10FFFFFF Command RAM [Slave] (4 MB and its mirrors)
 * Area 5	14000800-1400083F Master's 18000000
 * Area 6	1A800000-1A8000FF MIE ports, see mie_read
 *		1B000100-1B0001FF MIE bus request
 */

bool
Hikaru::slave_read(uint32_t addr, unsigned size, uint64_t &value)
{
	if (addr >= 0x0C000000 && addr <= 0x0DFFFFFF) {
		value = load(m_ram[SLAVE], addr & 0x01FFFFFF, size);
		return true;
	}
	if (addr <= 0x001FFFFF) {
		value = load(m_bootrom, addr, size);
		return true;
	}
	if (addr >= 0x04000000 && addr <= 0x04FFFFFF) {
		log_access("MEMCTL/S", 'R', size, addr, 0);
		return m_memctl[SLAVE].read(addr, size, value);
	}
	if (addr >= 0x10000000 && addr <= 0x1000002F) {
		log_access("MSCOMM/S", 'R', size, addr, 0);
		return mscomm_read(addr, size, value);
	}
	if (addr >= 0x10000100 && addr <= 0x10FFFFFF) {
		value = load(m_cmdram, addr & 0x003FFFFF, size);
		return true;
	}
	if (addr >= 0x14000800 && addr <= 0x1400083F) {
		log_access("UNK/S", 'R', size, addr, 0);
		return slave_config_read(addr, size, value);
	}
	if (addr >= 0x1A800000 && addr <= 0x1A8000FF)
		return size != 8 && mie_read(SLAVE, addr, size, value);
	if (addr >= 0x1B000100 && addr <= 0x1B0001FF)
		return (size == 2 || size == 4) && mie_read(SLAVE, addr, size, value);
	return false;
}

bool
Hikaru::slave_write(uint32_t addr, unsigned size, uint64_t value)
{
	if (addr >= 0x0C000000 && addr <= 0x0DFFFFFF) {
		store(m_ram[SLAVE], addr & 0x01FFFFFF, size, value);
		return true;
	}
	if (addr <= 0x001FFFFF)
		return true;
	if (addr >= 0x04000000 && addr <= 0x04FFFFFF) {
		log_access("MEMCTL/S", 'W', size, addr, value);
		return m_memctl[SLAVE].write(addr, size, value);
	}
	if (addr >= 0x10000000 && addr <= 0x1000002F) {
		log_access("MSCOMM/S", 'W', size, addr, value);
		return mscomm_write(addr, size, value);
	}
	if (addr >= 0x10000100 && addr <= 0x10FFFFFF) {
		log_access("CMDRAM/S", 'W', size, addr, value);
		store(m_cmdram, addr & 0x003FFFFF, size, value);
		return true;
	}
	if (addr >= 0x14000800 && addr <= 0x1400083F) {
		log_access("UNK/S", 'W', size, addr, value);
		return slave_config_write(addr, size, value);
	}
	if (addr >= 0x1A800000 && addr <= 0x1A8000FF)
		return size != 8 && mie_write(SLAVE, addr, size, value);
	if (addr >= 0x1B000100 && addr <= 0x1B0001FF)
		return (size == 2 || size == 4) && mie_write(SLAVE, addr, size, value);
	return false;
}

/* The graphics chips answer 32 bits, and 16 at two addresses. */
bool
Hikaru::gpu_read(uint32_t addr, unsigned size, uint64_t &value)
{
	uint32_t word = 0;

	if (size != 4 && !(size == 2 && (addr == 0x15000010 || addr == 0x00400000)))
		fatal("a read of the graphics chips of that size", addr, size);
	const bool known = m_gpu->read(addr, size, word);
	value = word;
	return known;
}

bool
Hikaru::gpu_write(uint32_t addr, unsigned size, uint64_t value)
{
	if (size != 4 && !(size == 2 && addr == 0x00400000))
		fatal("a write to the graphics chips of that size", addr, size);
	return m_gpu->write(addr, size, static_cast<uint32_t>(value));
}

/*
 * MIE (315-6146)
 * ==============
 *
 * A Z80 at 16 MHz with 2 KB of mask ROM, 34 KB of RAM, GPIO ports, a
 * 93C46 EEPROM on its port F/G pins and the JVS link to the 837-13551
 * I/O board. The SH-4 sees it through three windows:
 *
 * 00800000-0081FFFF  Z80 I/O ports, port n at 00800000 + 2n, even bytes
 *                    00800000 port A (unpopulated DIP switch)
 *                    0080000C port G (Test/Service, EEPROM DO)
 *                    00800010 port direction (= 0x43 after init)
 * 00820000-0083FFFF  Z80 memory, byte n at 00820000 + 2n, even bytes
 *                    0082F000 Z80 0x7800: the ROM jumps to 0x8010 when
 *                             bit 7 is written
 *                    00830000 Z80 0x8000: the program the BOOTROM
 *                             uploads (2316 bytes, ROM offset 0x40C48)
 *                    00838000 Z80 0xC000: mailbox
 * 01000000/01000100  bus request: bit 8 written requests the Z80 bus,
 *                    bit 8 read is clear once it is granted
 *
 * The slave has a window of its own: 1A800000-1A8000FF its ports (the
 * part it uses), 1B000100 its bus request. The BOOTROM uses both at start,
 * see Hikaru::set_resets.
 *
 * The chip lives in mie.cpp, the board wiring in mie_board.cpp.
 */

namespace {

struct MieWindow {
	uint32_t first, last;		/* the ports, then the memory */
	uint32_t busrq_first, busrq_last;
};

/* By processor, Hikaru::MASTER then SLAVE */
constexpr MieWindow MIE_WINDOWS[2] = {
	{ 0x00800000, 0x0083FFFF, 0x01000000, 0x010001FF },
	{ 0x1A800000, 0x1A8000FF, 0x1B000100, 0x1B0001FF },
};

} /* namespace */

bool
Hikaru::mie_read(unsigned which, uint32_t addr, unsigned size, uint64_t &value)
{
	const MieWindow &window = MIE_WINDOWS[which];

	value = 0;
	if (addr >= window.first && addr <= window.last) {
		/* The window is 16 bits wide: a 32-bit access is two words,
		 * the low one first. */
		const unsigned part = (size == 4) ? 2 : size;

		for (unsigned i = 0; i < size; i += part) {
			uint32_t word = 0;

			if (!m_mie->host_read(addr + i - window.first, part, word)) {
				board_error("MIE unhandled R%u %08X", size * 8, addr);
				value = 0;
				return false;
			}
			value |= static_cast<uint64_t>(word << (8 * i));
		}
		return true;
	}
	if (addr >= window.busrq_first && addr <= window.busrq_last) {
		value = m_mie->busrq_read();
		return true;
	}
	return false;
}

bool
Hikaru::mie_write(unsigned which, uint32_t addr, unsigned size, uint64_t value)
{
	const MieWindow &window = MIE_WINDOWS[which];

	if (addr >= window.first && addr <= window.last) {
		const unsigned part = (size == 4) ? 2 : size;

		for (unsigned i = 0; i < size; i += part) {
			if (!m_mie->host_write(addr + i - window.first, part, static_cast<uint32_t>(value >> (8 * i)))) {
				board_error("MIE unhandled W%u %08X = %" PRIX64, size * 8, addr, value);
				return false;
			}
		}
		return true;
	}
	if (addr >= window.busrq_first && addr <= window.busrq_last) {
		m_mie->busrq_write(which, static_cast<uint32_t>(value));
		return true;
	}
	return false;
}

/*
 * Master/Slave Communication Box
 * ==============================
 *
 * Not sure how the comm box works exactly; does it imply any kind of IRQ?
 * Does it allow to start/reset the slave (kind of like the Saturn SMPC)?
 *
 * Master   Slave
 * -------- --------
 * 14000000 10000000	Unknown; Control? Possibly 0 turns off/resets the slave (NMI?); anything else turns it on
 * /        /
 * 14000008 10000008	Box Port 0
 * 1400000C 1000000C	Box Port 1
 * 14000010 10000010	Box Port 2
 * 14000014 10000014	Box Port 3
 * 14000020 10000020	Unknown, as 24 and 28: 32-bit writes kept, reads not answered
 * /        /
 * 1400002E 1000002E	Unknown; 3 is written in __slave_init and read from the master
 */

bool
Hikaru::mscomm_read(uint32_t addr, unsigned size, uint64_t &value)
{
	value = load(m_mscomm, addr & 0x3F, size);
	switch (addr & 0xFF) {
	case 0x00:
	case 0x08:
	case 0x0C:
	case 0x10:
	case 0x14:
		if (size != 4)
			fatal("a port of the master and slave box is 32 bits wide", addr, size);
		break;
	case 0x2E:
		if (size != 2)
			fatal("the port 2E of the master and slave box is 16 bits wide", addr, size);
		break;
	default:
		return false;
	}
	return true;
}

bool
Hikaru::mscomm_write(uint32_t addr, unsigned size, uint64_t value)
{
	switch (addr & 0xFF) {
	case 0x00:
	case 0x08:
	case 0x0C:
	case 0x10:
	case 0x14:
	case 0x20:
	case 0x24:
	case 0x28:
		if (size != 4)
			fatal("a port of the master and slave box is 32 bits wide", addr, size);
		break;
	case 0x2E:
		if (size != 2)
			fatal("the port 2E of the master and slave box is 16 bits wide", addr, size);
		break;
	default:
		return false;
	}
	store(m_mscomm, addr & 0x3F, size, value);
	return true;
}

/* 14000800 of the slave is the configuration space of the command
 * processor, read through its bridge. */
bool
Hikaru::slave_config_read(uint32_t addr, unsigned size, uint64_t &value)
{
	switch (addr) {
	case 0x14000800:
		/* Controlled by 04000010 and 0400001C */
		if (size != 4)
			fatal("14000800 is 32 bits wide", addr, size);
		value = 0x17C311DB;	/* SEGA PCI ID #2 */
		return true;
	default:
		return false;
	}
}

bool
Hikaru::slave_config_write(uint32_t addr, unsigned size, uint64_t)
{
	switch (addr) {
	case 0x1400080D:
		/* Controlled by 04000010 and 0400001C */
		if (size != 1)
			fatal("1400080D is 8 bits wide", addr, size);
		return true;
	case 0x14000804:
	case 0x14000810:
	case 0x14000814:
	case 0x14000818:
	case 0x1400081C:
		if (size != 4)
			fatal("the configuration space at 14000800 is 32 bits wide", addr, size);
		return true;
	default:
		return false;
	}
}

/*
 * Sound Boards
 * ============
 *
 * Apparently SNDBD and SNDBD2 are identical, except that the latter is
 * optional.
 *
 * 700000-701FFF AICA Channels 0-63. Each slot is 128 bytes long.
 * 702000-7027FF AICA ESF (?)
 * 702800-7028BD AICA Global (?)
 * 702C00        ARM Reset
 * 702D00        AICA IRQ L
 * 702D04        AICA IRQ R
 * 703000-7031FF COEF
 * 703200-7033FF MADRS
 * 703400-703BFF MPRO; 3BFE --> AICA DSP start
 *
 * 710000 RTC, high half of the count
 * 710004 RTC, low half
 * 710008 RTC Write Enable
 *
 * 800000-FFFFFF RAM
 *
 * The board itself is in sound_board.cpp.
 */

bool
Hikaru::sound_read(unsigned which, uint32_t addr, unsigned size, uint64_t &value)
{
	const uint32_t offs = addr & 0xFFFFFF;

	if (m_verbose)
		std::printf("%s : AICA/%c R%u @%08X\n", board_string().c_str(), which == MASTER ? 'M' : 'S',
		            8 * size, offs);

	if (!m_sound[which]->host_read(offs, size, value)) {
		board_error("AICA unhandled R%u %08X", size * 8, addr);
		value = 0;
		return false;
	}
	return true;
}

bool
Hikaru::sound_write(unsigned which, uint32_t addr, unsigned size, uint64_t value)
{
	const uint32_t offs = addr & 0xFFFFFF;

	if (m_verbose)
		std::printf("%s : AICA/%c W%u @%08X = %" PRIX64 "\n", board_string().c_str(),
		            which == MASTER ? 'M' : 'S', 8 * size, offs, value);

	switch (m_sound[which]->host_write(offs, size, value)) {
	case SoundBoard::Written::NOWHERE:
		board_error("AICA unhandled W%u %08X = %" PRIX64, size * 8, addr, value);
		return false;
	case SoundBoard::Written::CLOCK_SET:
		if (which == MASTER)
			save_clock();
		break;
	case SoundBoard::Written::DONE:
		break;
	}
	return true;
}

/* The texture RAM written by the bus: as it is when the command processor
 * says so (1500008C), else each 32-bit word as two texels at the place of
 * the bank its offset twiddles to. */
void
Hikaru::texram_write(unsigned bank, uint32_t offs, unsigned size, uint64_t value)
{
	if (m_screen)
		m_screen->before_texture_write(bank, 0, 0, Antarctic::TEXRAM_WIDTH, Antarctic::TEXRAM_HEIGHT);
	texram_store(bank, offs, size, value, m_gpu->antarctic().texture_ram_twiddled());
}

void
Hikaru::texram_store(unsigned bank, uint32_t offs, unsigned size, uint64_t value, bool twiddled)
{
	if (twiddled) {
		store(m_texram[bank], offs, size, value);
		return;
	}
	if (size != 4)
		fatal("a write to the texture RAM that is not 32 bits wide", offs, size);
	store(m_texram[bank], twiddle((offs + 0) >> 1) << 1, 2, swap16(static_cast<uint16_t>(value)));
	store(m_texram[bank], twiddle((offs + 2) >> 1) << 1, 2, swap16(static_cast<uint16_t>(value >> 16)));
}

/* The external bus, see memctl.cpp for what is on it. Bit 31 is the top bit
 * of the page of a window. PHARRIER reads the first word of page 8C, the
 * first sound board with that bit, and stops with "ERROR !! NO AICA
 * EXTENSION BOARD." unless its bit 2 is set (@0C0B2884); the extension
 * board is always there. HEURISTIC: what answers there and its other bits
 * are not known (0); the bit changes nothing on the other pages (the
 * 315-5881 at 8A, the ROM board at 90). */
bool
Hikaru::bus_read(uint32_t bus_addr, unsigned size, uint64_t &value)
{
	value = 0;
	if (bus_addr == 0x8C000000 && size <= 4) {
		value = 0x4;
		return true;
	}
	bus_addr &= 0x7FFFFFFF;

	const uint32_t offs = bus_addr & 0xFFFFFF;
	bool log = false;

	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF) {
		value = load(m_texram[0], offs, size);
	} else if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF) {
		value = load(m_texram[1], offs, size);
	} else if (bus_addr >= 0x0A000000 && bus_addr <= 0x0AFFFFFF) {
		/* The chip that guards the game, see RomBoard */
		value = m_rombd.read_guard(offs, size, m_memctl[m_current].wide_local_bus());
		log = !m_rombd.guarded(offs);
	} else if (bus_addr >= 0x0C000000 && bus_addr <= 0x0CFFFFFF) {
		return sound_read(MASTER, bus_addr, size, value);
	} else if (bus_addr >= 0x0D000000 && bus_addr <= 0x0DFFFFFF) {
		return sound_read(SLAVE, bus_addr, size, value);
	} else if (m_netbd && bus_addr >= 0x0E000000 && bus_addr <= 0x0E03FFFF) {
		/* The network board (netbd_837_13404.hpp) */
		const auto hold = netbd_meet();

		if (size == 8)
			value = m_netbd->host_read(offs, 4) | static_cast<uint64_t>(m_netbd->host_read(offs + 4, 4)) << 32;
		else
			value = m_netbd->host_read(offs, size);
	} else if (bus_addr >= 0x0E000000 && bus_addr <= 0x0E00FFFF) {
		/* Network board: none */
		log = true;
	} else if (bus_addr >= 0x10000000 && bus_addr <= 0x3FFFFFFF) {
		value = m_rombd.read(bus_addr, size);
	} else if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF) {
		value = load(m_ram[SLAVE], bus_addr & 0x01FFFFFF, size);
	} else if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF) {
		value = load(m_ram[MASTER], bus_addr & 0x01FFFFFF, size);
	} else {
		cpu_error("MEMCTL R%u %08X", size * 8, bus_addr);
		return false;
	}
	if (log && m_verbose)
		std::printf("%s : MEMCTL R%u %08X\n", cpu_string(m_current).c_str(), size * 8, bus_addr);
	return true;
}

/* The bytes bus_read() gives from bus_addr on as they are in a memory, up
 * to the end of it; nothing where a read does more. */
std::span<const uint8_t>
Hikaru::bus_span(uint32_t bus_addr) const
{
	const std::span<const uint8_t> texram0 = m_texram[0], texram1 = m_texram[1];
	const std::span<const uint8_t> slave = m_ram[SLAVE], master = m_ram[MASTER];

	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF)
		return texram0.subspan(bus_addr & 0xFFFFFF);
	if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF)
		return texram1.subspan(bus_addr & 0xFFFFFF);
	if (bus_addr >= 0x10000000 && bus_addr <= 0x3FFFFFFF)
		return m_rombd.plain(bus_addr);
	if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF)
		return slave.subspan(bus_addr & 0x01FFFFFF);
	if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF)
		return master.subspan(bus_addr & 0x01FFFFFF);
	return {};
}

/* The RAMs a write of the bus stores into as they are, from bus_addr on */
std::span<uint8_t>
Hikaru::bus_ram(uint32_t bus_addr)
{
	if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF)
		return std::span<uint8_t>(m_ram[SLAVE]).subspan(bus_addr & 0x01FFFFFF);
	if (bus_addr >= 0x48000000 && bus_addr <= 0x483FFFFF)
		return std::span<uint8_t>(m_cmdram).subspan(bus_addr & 0x003FFFFF);
	if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF)
		return std::span<uint8_t>(m_ram[MASTER]).subspan(bus_addr & 0x01FFFFFF);
	return {};
}

/* The sound board of a page of the bus, for spans of its wave memory;
 * none when each access is printed. */
SoundBoard *
Hikaru::wave_board(uint32_t bus_addr)
{
	return m_verbose ? nullptr : m_sound[(bus_addr >> 24) == 0x0C ? MASTER : SLAVE].get();
}

/* MemCtl::Host: as bus_span(), and the wave memories */
std::span<const uint8_t>
Hikaru::bus_read_span(uint32_t bus_addr)
{
	if (bus_addr >= 0x0C000000 && bus_addr <= 0x0DFFFFFF) {
		SoundBoard *board = wave_board(bus_addr);

		return board ? board->host_ram_read(bus_addr & 0xFFFFFF) : std::span<const uint8_t>();
	}
	return bus_span(bus_addr);
}

/* MemCtl::Host: the RAMs and the wave memories */
std::span<uint8_t>
Hikaru::bus_write_span(uint32_t bus_addr)
{
	if (bus_addr >= 0x0C000000 && bus_addr <= 0x0DFFFFFF) {
		SoundBoard *board = wave_board(bus_addr);

		return board ? board->host_ram_write(bus_addr & 0xFFFFFF) : std::span<uint8_t>();
	}
	return bus_ram(bus_addr);
}

/* MemCtl::Host: from a memory of the bus to the RAM of a SH-4, the command
 * RAM or a bank of the texture RAM, word after word as bus_read() and
 * bus_write() would; the texture RAM is told once, which is what each
 * write would do again. */
uint32_t
Hikaru::bus_copy(uint32_t src, uint32_t dst, uint32_t words)
{
	const std::span<const uint8_t> from = bus_span(src);
	uint32_t n = static_cast<uint32_t>(std::min<size_t>(words, from.size() / 4));
	std::span<uint8_t> to;

	if (n == 0)
		return 0;
	if ((dst >= 0x04000000 && dst <= 0x043FFFFF) || (dst >= 0x06000000 && dst <= 0x063FFFFF)) {
		const unsigned bank = (dst >> 25) & 1;
		const uint32_t offs = dst & 0xFFFFFF;
		const bool twiddled = m_gpu->antarctic().texture_ram_twiddled();

		n = std::min(n, (static_cast<uint32_t>(m_texram[bank].size()) - offs) / 4);
		if (m_screen)
			m_screen->before_texture_write(bank, 0, 0, Antarctic::TEXRAM_WIDTH, Antarctic::TEXRAM_HEIGHT);
		for (uint32_t i = 0; i < n; i++)
			texram_store(bank, offs + 4 * i, 4, load_bytes(&from[4 * i], 4), twiddled);
		return n;
	}
	to = bus_ram(dst);
	n = std::min(n, static_cast<uint32_t>(to.size() / 4));
	/* One word after the other: a copy onto itself does what the bus
	 * would. */
	for (uint32_t i = 0; i < n; i++) {
		uint32_t word;

		std::memcpy(&word, &from[4 * i], 4);
		std::memcpy(&to[4 * i], &word, 4);
	}
	ram_written(dst, 4 * size_t{n});
	return n;
}

/* The RAM of a SH-4 written by the bus: that CPU is told, see
 * Sh4::written(). The command RAM is shared, see map_pages(). */
void
Hikaru::ram_written(uint32_t bus_addr, size_t size)
{
	if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF)
		m_sh[SLAVE]->written(0x0C000000 | (bus_addr & 0x01FFFFFF), size);
	else if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF)
		m_sh[MASTER]->written(0x0C000000 | (bus_addr & 0x01FFFFFF), size);
}

bool
Hikaru::bus_write(uint32_t bus_addr, unsigned size, uint64_t value)
{
	bus_addr &= 0x7FFFFFFF;

	const uint32_t offs = bus_addr & 0xFFFFFF;
	bool log = false;

	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF) {
		texram_write(0, offs, size, value);
	} else if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF) {
		texram_write(1, offs, size, value);
	} else if (bus_addr >= 0x0A000000 && bus_addr <= 0x0AFFFFFF) {
		m_rombd.write_guard(offs, size, value, m_memctl[m_current].wide_local_bus());
		log = !m_rombd.guarded(offs);
	} else if (bus_addr >= 0x0C000000 && bus_addr <= 0x0CFFFFFF) {
		return sound_write(MASTER, bus_addr, size, value);
	} else if (bus_addr >= 0x0D000000 && bus_addr <= 0x0DFFFFFF) {
		return sound_write(SLAVE, bus_addr, size, value);
	} else if (m_netbd && bus_addr >= 0x0E000000 && bus_addr <= 0x0E03FFFF) {
		const auto hold = netbd_meet();

		if (size == 8) {
			m_netbd->host_write(offs, 4, static_cast<uint32_t>(value));
			m_netbd->host_write(offs + 4, 4, static_cast<uint32_t>(value >> 32));
		} else {
			m_netbd->host_write(offs, size, static_cast<uint32_t>(value));
		}
	} else if (bus_addr >= 0x0E000000 && bus_addr <= 0x0E00FFFF) {
		log = true;
	} else if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF) {
		store(m_ram[SLAVE], bus_addr & 0x01FFFFFF, size, value);
		ram_written(bus_addr, size);
	} else if (bus_addr >= 0x48000000 && bus_addr <= 0x483FFFFF) {
		store(m_cmdram, bus_addr & 0x003FFFFF, size, value);
	} else if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF) {
		store(m_ram[MASTER], bus_addr & 0x01FFFFFF, size, value);
		ram_written(bus_addr, size);
	} else if (!m_rombd.write(bus_addr, size, value)) {
		cpu_error("MEMCTL W%u %08X = %" PRIX64, size * 8, bus_addr, value);
		return false;
	}
	if (log && m_verbose)
		std::printf("%s : MEMCTL W%u %08X = %" PRIX64 "\n", cpu_string(m_current).c_str(), size * 8,
		            bus_addr, value);
	return true;
}

/* What a SH-4 asks of the board */

bool
Hikaru::Bus::read(uint32_t addr, uint32_t size, uint64_t &value)
{
	return (m_which == MASTER) ? m_board.master_read(addr, size, value) :
	                             m_board.slave_read(addr, size, value);
}

bool
Hikaru::Bus::write(uint32_t addr, uint32_t size, uint64_t value)
{
	return (m_which == MASTER) ? m_board.master_write(addr, size, value) :
	                             m_board.slave_write(addr, size, value);
}

/* The windows of the master on the external bus; what else is plain, the
 * CPUs have in their pages. */
std::span<const uint8_t>
Hikaru::Bus::read_span(uint32_t addr)
{
	if (m_which == MASTER && master_window(addr))
		return m_board.m_memctl[MASTER].read_span(addr);
	return {};
}

std::span<uint8_t>
Hikaru::Bus::write_span(uint32_t addr)
{
	if (m_which == MASTER && master_window(addr))
		return m_board.m_memctl[MASTER].write_span(addr);
	return {};
}

void
Hikaru::Bus::span_written(uint32_t addr, size_t size)
{
	if (m_which == MASTER && master_window(addr))
		m_board.ram_written((m_board.m_memctl[MASTER].window_page(addr) << 24) | (addr & 0xFFFFFF), size);
}

/*
 * Port A
 * ======
 *
 * The master SH-4 port A is used for a variety of things. The bitmask is
 * as follows:
 *
 * [M] xxxx xxii iiee eexM
 *
 * x = Unused
 * i = IRQ causes
 * e = MAINBD EEPROM
 * M = master-to-slave communication
 *
 * Bit M is the NMI pin of the slave SH-4: the master pulls it low for three
 * writes and the slave, at each interrupt, clears the NMI flag of its DMAC
 * as the hardware manual asks (read DMAOR, write 0x8001, @0C002000). MAME
 * names the pin IRL3 instead (hikaru.cpp, main SH4 port A).
 *
 * [S] xxxx xxxx xxxx xxSx
 *
 * S = slave-to-master communication?
 *
 * Note: GPIOIC is never access by the bootrom.
 */

uint16_t
Hikaru::Bus::port_a_read()
{
	if (m_which == SLAVE)
		return m_board.m_porta[SLAVE];

	const uint8_t lines = m_board.m_irq_lines;

	/* Bit 5 is the EEPROM DO pin (MAME hikaru.cpp main_io_r). Bits 6 to 9
	 * are the lines of update_irqs, low while they ask; nothing is known
	 * on the other pins, pulled up. */
	return static_cast<uint16_t>(0xFC1F |
	       (m_board.m_eeprom.do_read() ? 0x20 : 0) |
	       ((lines & 0x01) ? 0 : 0x040) |	/* ANT_INT */
	       ((lines & 0x10) ? 0 : 0x080) |	/* SZK_INT */
	       ((lines & 0x20) ? 0 : 0x100) |	/* CN4_INT */
	       ((lines & 0x80) ? 0 : 0x200));	/* CN3_INT1 */
}

void
Hikaru::Bus::port_a_write(uint16_t pins)
{
	m_board.m_porta[m_which] = pins;

	if (m_which == SLAVE) {
		/* Bit 0 is the line of the slave to the master, on IRL3 for
		 * MAME (hikaru.cpp, slave SH4 port A); the games leave it
		 * high. */
		if (!(pins & 1))
			stop_fatal("the slave SH-4 pulls its line to the master, which is not implemented");
		return;
	}

	/* 93C46: bit 2 DI, bit 3 SK, bit 4 CS (MAME hikaru.cpp main_io_w) */
	m_board.m_eeprom.set_pins((pins >> 4) & 1, (pins >> 3) & 1, (pins >> 2) & 1, false);

	/* The NMI of the slave comes with the falling edge (its ICR.NMIE is
	 * clear). */
	m_board.m_sh[SLAVE]->set_nmi(pins & 1);
}

/* An access nothing answers is told the first time: a program that makes
 * one makes it again at every frame. */
void
Hikaru::Bus::unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value)
{
	const uint64_t access = addr | (static_cast<uint64_t>(size) << 32) | (static_cast<uint64_t>(is_write) << 40) |
	                        (static_cast<uint64_t>(m_which) << 41);
	static const char WRITE[] = "unhandled W%u @%08X = %" PRIX64 " (told once)", READ[] = "unhandled R%u @%08X (told once)";
	char text[80];

	if (!m_board.m_unanswered_told.insert(access).second)
		return;
	if (is_write)
		std::snprintf(text, sizeof(text), WRITE, 8 * size, addr, value);
	else
		std::snprintf(text, sizeof(text), READ, 8 * size, addr);
	m_board.tell_error(is_write ? WRITE : READ, m_board.cpu_string(m_which), text);
}

/* The instruction at the address the board looks at is about to run: the
 * idle loop of the slave. */
void
Hikaru::Bus::hook()
{
	if (m_which == SLAVE)
		m_board.slave_idle_loop();
}

} /* namespace odin */
