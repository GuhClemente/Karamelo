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
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/chips/pmx1/pmx1_core.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

#include "odin/common/bytes.hpp"

/* Words are 32 bits, little-endian. Bits 31-28 the major opcode, 27 S (set
 * the flags), 26 C, 25 I (immediate), 24 U (the second unit of a pair);
 * register numbers at 23-19 (d), 18-14 (s1), 13-9 (s2). With I and not C,
 * a 16-bit immediate at 18-3: bit 2 "move" (no source), 1 sign-extend, 0
 * shift it left by 16. With C and I, an 8-bit immediate at 13-6 and bit 5
 * "to another unit", that unit at 4-1, else a condition at 4-1. Units: 0
 * a store through A0.n (n < 16) or A1.(n - 16), 1 D0, 2 D1, 3 A0, 4 A1, 5
 * PC (one register per thread), 6 the read queue, 8 the ports' answers,
 * 10 the kickers. Writing X to a PC continues that thread at X + 4; in
 * thread 0 the four words after a jump run before it (delay slots), in the
 * real-time threads 1-3 none (INFERRED: the DAC thread's code runs only
 * so, see Pmx1Core::jump). The registers are
 * one file for the four threads; the PCs, flags, read queues and kickers
 * are each thread's own. */

namespace odin {

namespace {

constexpr uint32_t NOP = 0xF1000000;
/* HEURISTIC: the word that opens a thread's code (F1, a register, 2) has
 * an unknown meaning; it passes as a NOP */
constexpr uint32_t ENTRY = 0xF1000002, ENTRY_MASK = 0xFF07FFFF;

enum Unit : unsigned { STORE = 0, D0, D1, A0, A1, PC, RD, PORTS = 8, KICKER = 10, TIMER = 11 };
/* The forms of decoded words; NO is not one known */
enum Form : uint8_t {
	UNDECODED, NO, NOTHING, OPERATE, SET, OPERATE_REGS, SHIFT_REGS, COPY, OPERATE_TO, OPERATE_IF,
	OPERATE_REGS_TO_IF, SHIFT_TO, COMPARE, COMPARE_REGS, COMPARE_IF, BRANCH, MOVE, SWAP, SWAP_PC, WIMP, MOVL,
	MEMORY, INDEXED, REPEATED, PORT, FEED,
};
/* U8.2: the answer to an implicit register read; U8.4: the TA's status */
constexpr uint32_t IMPLICIT_ANSWER = 2, TA_STATUS = 4;
constexpr uint32_t FLAG_V = 1, FLAG_C = 2, FLAG_Z = 4, FLAG_N = 8;

/* Implicit registers: the immediate registers of the host are a window on
 * some of them */
constexpr uint32_t THREAD_ENABLE = 0x000, RESET_VECTOR_LO = 0x010, RESET_VECTOR_HI = 0x011;
constexpr uint32_t SOFTWARE_RESET = 0x169, FB_OFFSET = 0x287, THREAD_KICKER = 0x100;
constexpr uint32_t C1_RST = 1u << 2;	/* the primary core, held in reset at 0 */
/* Interrupt Status (latched, a 1 written clears) and Mask (a 1 masks;
 * bit 15 all); the Primary Core Soft Interrupt, whose rising bits 3-0 set
 * status bits 11-8 */
constexpr uint32_t INTERRUPT_STATUS = 0x14C, INTERRUPT_MASK = 0x14D, MASTER = 1u << 15;
constexpr uint32_t SOFT_INTERRUPT = 0x012;
/* The 3D core's registers: Index Address (bit 15 two 16-bit packets, 14
 * the next index after each register, 10-0 the index) and Index Data */
constexpr uint32_t INDEX_ADDRESS = 0x104, INDEX_DATA = 0x105, INDEX_PACKETS = 1u << 15, INDEX_STEP = 1u << 14;
constexpr uint32_t WAR = 0xC0, RAR2 = 0xC4, RAR1 = 0xC8;
/* The DAC PLL (0 takes effect when 1 is written): Pout = Fref * N / (M *
 * P), M = 4-0 + 1, N = 11-5 + 1, P from 14-12; the HSYNC front porch, back
 * porch and width, in pixel clocks */
constexpr uint32_t DAC_PLL0 = 0x120, DAC_PLL1 = 0x121, HS_FRONT = 0x0A3, HS_BACK = 0x0A4, HS_WIDTH = 0x0A5;
/* Fref: XTAL2 beside the PowerVR on Smashing Drive's board (REF. 010131),
 * "K.T.S KDN-4350HC 14.31818MHz", the guide's "typically 14.31818MHz";
 * the boards have no other crystal for an external DAC clock */
constexpr uint64_t DAC_FREF = 14'318'180;
/* DAC Control: bit 0 (DAC_BLNK) at 0, the DAC puts out blanking, black
 * (guide p. 105), as from its reset; bit 9, the gamma table in 6 bits.
 * The table (GLUT): its address, then red, green and blue a write each,
 * the address one on after each three (p. 104) */
constexpr uint32_t DAC_CONTROL = 0x0A9, DAC_BLNK = 1, DAC_6BIT = 1u << 9;
constexpr uint32_t DAC_LUT_WRITE = 0x0B4, DAC_LUT_DATA = 0x0B5, DAC_LUT_READ = 0x0B7;

struct Window {
	uint32_t offset, implicit;
};
constexpr Window WINDOW[] = {
	{ 0x00, THREAD_ENABLE }, { 0x04, THREAD_ENABLE + 1 }, { 0x08, THREAD_ENABLE + 2 },
	{ 0x0C, THREAD_ENABLE + 3 }, { 0x40, RESET_VECTOR_LO }, { 0x44, RESET_VECTOR_HI },
	{ 0x48, SOFT_INTERRUPT }, { 0x70, INTERRUPT_STATUS }, { 0x74, INTERRUPT_MASK }, { 0xA4, SOFTWARE_RESET },
	{ 0x80, DAC_PLL0 }, { 0x84, DAC_PLL1 },
};

/* Instruction reads always go to the frame buffer, of 64 MB */
constexpr uint32_t FRAME_BUFFER_MASK = 0x03FFFFFF;
constexpr uint32_t DELAY_SLOTS = 4;
constexpr uint32_t PAGE_BITS = 12;
/* The E2 port: a word written to register 0x65, then an address to 0x64,
 * stores that word there (INFERRED: BG0.Status and the fills of the 3D
 * code use it so) */
constexpr uint32_t PORT_ADDRESS = 0x64, PORT_DATA = 0x65;
/* Who wrote an implicit register, for the log */
constexpr uint32_t BY_HOST = 0xFFFFFFFF;

uint32_t
immediate16(uint32_t word)
{
	uint32_t value = (word >> 3) & 0xFFFF;

	if (word & 2)
		value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
	return word & 1 ? value << 16 : value;
}

int32_t
signed_field(uint32_t value, unsigned bits)
{
	return static_cast<int32_t>(value << (32 - bits)) >> (32 - bits);
}

/* An ALU operation and the flags it gives: N and Z; C (no borrow) and V
 * for ADD and SUB as META's; AND, OR and XOR keep C and V (INFERRED: the
 * TA status loop branches on HI after a TST, on the CMP's carry before
 * it); the shifts clear them. `flags` holds the flags before. */
bool
operate(uint32_t op, uint32_t a, uint32_t b, uint32_t &result, uint32_t &flags)
{
	uint32_t cv = 0;

	switch (op) {
	case 0:
		result = a + b;
		cv = (result < a ? FLAG_C : 0) | ((~(a ^ b) & (a ^ result)) >> 31 ? FLAG_V : 0);
		break;
	case 1:
		result = a - b;
		cv = (a >= b ? FLAG_C : 0) | (((a ^ b) & (a ^ result)) >> 31 ? FLAG_V : 0);
		break;
	case 2:
		result = a & b;
		cv = flags & (FLAG_C | FLAG_V);
		break;
	case 3:
		result = a | b;
		cv = flags & (FLAG_C | FLAG_V);
		break;
	case 4:
		result = a ^ b;
		cv = flags & (FLAG_C | FLAG_V);
		break;
	case 5:
		result = a << (b & 31);
		break;
	case 6:
		result = a >> (b & 31);
		break;
	default:
		return false;
	}
	flags = (result >> 31 ? FLAG_N : 0) | (result == 0 ? FLAG_Z : 0) | cv;
	return true;
}

/* Whether condition `cond` holds with the flags `f` (Pmx1Core::known()) */
constexpr bool holds_with(uint32_t cond, uint32_t f)
{
	const bool z = (f & FLAG_Z) != 0, n = (f & FLAG_N) != 0, v = (f & FLAG_V) != 0;

	switch (cond) {
	case 0:
		return true;
	case 1:
		return z;
	case 2:
		return !z;
	case 9:
		return (f & FLAG_C) && !z;
	case 11:
		return n == v;
	case 12:
		return n != v;
	case 13:
		return !z && n == v;
	default:
		return false;
	}
}

/* The values of NZCV a condition holds with, a bit each */
uint16_t passes(uint32_t cond)
{
	uint16_t pass = 0;

	for (uint32_t f = 0; f < 16; f++)
		if (holds_with(cond, f))
			pass = static_cast<uint16_t>(pass | 1u << f);
	return pass;
}

} /* namespace */

Pmx1Core::Pmx1Core(std::span<uint8_t> vram, std::function<void(uint32_t offset, uint32_t size)> written)
	: m_vram(vram), m_written(std::move(written)), m_page_written(vram.size() >> PAGE_BITS),
	  m_code_line(vram.size() >> LINE_BITS),
	  m_ta(vram, m_s.ta, m_s.implicit[FB_OFFSET],
	       [this](uint32_t offset, uint32_t size) { this->written(offset, size); })
{
	/* block() empties them past BLOCK_OPS_MAX, before a block of at most
	 * BLOCK_MAX + DELAY_SLOTS + 1 */
	m_block_ops.reserve(BLOCK_OPS_MAX + 2 * BLOCK_MAX);
	reset();
}

void Pmx1Core::reset()
{
	m_s = State{};
	m_s.implicit[THREAD_ENABLE] = 1;
	m_s.implicit[RESET_VECTOR_LO] = 0xFFF8;
	m_s.implicit[RESET_VECTOR_HI] = 0x0403;
	m_s.implicit[SOFTWARE_RESET] = 0x7FFF;
	m_s.implicit[DAC_PLL0] = m_s.dac_pll = 0x0021;
	m_s.implicit[INTERRUPT_MASK] = 0xFFFF;
	/* A hard reset sends thread 0 to the top of the boot ROM, which is not
	 * emulated (none is known on these boards): it waits for the host's
	 * soft reset */
	m_s.next[0] = 0x0403FFFC;
	m_s.stopped[0] = 1;
	m_others = true;
	interrupt_line();
}

bool Pmx1Core::host_register(uint32_t offset)
{
	for (const Window &w : WINDOW)
		if (w.offset == offset)
			return true;
	return offset == WAR || offset == RAR1 || offset == RAR2;
}

uint32_t Pmx1Core::host_read(uint32_t offset) const
{
	for (const Window &w : WINDOW)
		if (w.offset == offset)
			return m_s.implicit[w.implicit];
	return offset == RAR1 ? m_s.rar[0] : offset == RAR2 ? m_s.rar[1] : 0;
}

/* The Write Access Register: bits 31-30 read (00) or write (01), 29-26
 * the route (0001: answer in RAR1, 1001: in RAR2), 25-16 the implicit
 * register, 15-0 the data */
void Pmx1Core::host_write(uint32_t offset, uint32_t value)
{
	for (const Window &w : WINDOW)
		if (w.offset == offset) {
			implicit_write(w.implicit, value & 0xFFFF, BY_HOST);
			return;
		}
	if (offset != WAR)
		return;

	const uint32_t addr = (value >> 16) & (IMPLICIT - 1), route = (value >> 26) & 0xF;

	if (value >> 30 == 1)
		implicit_write(addr, value & 0xFFFF, BY_HOST);
	else if (value >> 30 == 0 && (route == 0x1 || route == 0x9))
		m_s.rar[route == 0x9] = (value & 0xFFFF0000) | m_s.implicit[addr];
	else if (!m_said_war) {
		m_said_war = true;
		std::fprintf(stderr, "pmx1: WAR %08X, neither a write nor a read\n", value);
	}
}

void Pmx1Core::kick(unsigned thread, uint32_t value)
{
	m_s.kicks[thread] += value & 0x7FFF;
	m_s.async[thread] |= value >> 15 & 1;
	m_s.waiting[thread] = 0;
	m_others |= thread != 0;
}

void Pmx1Core::interrupt(uint32_t bits)
{
	m_s.implicit[INTERRUPT_STATUS] |= bits & 0xFFFF;
	interrupt_line();
}

/* The line to the host: a status bit its mask lets through, none with
 * MASTER */
void Pmx1Core::interrupt_line()
{
	const uint32_t mask = m_s.implicit[INTERRUPT_MASK];

	if (m_interrupt)
		m_interrupt(!(mask & MASTER) && (m_s.implicit[INTERRUPT_STATUS] & ~mask & 0x7FFF));
}

void Pmx1Core::set_state(const State &state)
{
	m_s = state;
	m_others = true;
	interrupt_line();
}

/* A word to Index Data: one 16-bit packet, or two, low first, then the
 * register is written and, with the step, the index moves to the next
 * (INFERRED: by 4; the indices are multiples of 4, and the render writes
 * 63 registers from 0x30, its image's 0x30-0x128) */
void Pmx1Core::index_data(uint32_t data)
{
	const uint32_t index = m_s.implicit[INDEX_ADDRESS];
	uint32_t &reg = m_s.regs_3d[(index & 0x7FF) >> 2];

	if ((index & 3) && !m_said[INDEX_ADDRESS]) {
		m_said[INDEX_ADDRESS] = true;
		std::fprintf(stderr, "pmx1: 3D register index %03X, not a multiple of 4\n", index & 0x7FF);
	}
	if ((index & INDEX_PACKETS) && !m_s.index_half) {
		m_s.index_low = data;
		m_s.index_half = 1;
		return;
	}
	reg = index & INDEX_PACKETS ? data << 16 | m_s.index_low : (reg & 0xFFFF0000) | data;
	m_s.index_half = 0;
	if (index & INDEX_STEP)
		m_s.implicit[INDEX_ADDRESS] = (index & ~0x7FFu) | ((index + 4) & 0x7FF);
	if (m_3d)
		m_3d(index & 0x7FC, reg);
}

void Pmx1Core::implicit_write(uint32_t addr, uint32_t value, uint32_t by)
{
	/* A register that does nothing yet, seen before: kept only */
	if (m_plain[addr]) {
		m_s.implicit[addr] = value;
		return;
	}

	const uint32_t before = m_s.implicit[addr];

	switch (addr) {
	case INTERRUPT_STATUS:
		m_s.implicit[addr] = before & ~value;
		interrupt_line();
		return;
	case INTERRUPT_MASK:
		m_s.implicit[addr] = value;
		interrupt_line();
		return;
	case SOFT_INTERRUPT:
		m_s.implicit[addr] = value;
		interrupt((value & ~before & 0xF) << 8);
		return;
	case INDEX_ADDRESS:
		m_s.implicit[addr] = value;
		m_s.index_half = 0;
		return;
	case DAC_PLL1:
		m_s.implicit[addr] = value;
		m_s.dac_pll = (value & 3) << 16 | m_s.implicit[DAC_PLL0];
		return;
	case DAC_PLL0:
	case HS_FRONT:
	case HS_BACK:
	case HS_WIDTH:
	case DAC_CONTROL:
		m_s.implicit[addr] = value;
		return;
	/* The read and write addresses are one register (p. 104) */
	case DAC_LUT_WRITE:
	case DAC_LUT_READ:
		m_s.implicit[DAC_LUT_WRITE] = m_s.implicit[DAC_LUT_READ] = value;
		m_lut_at = (value & 0xFF) * 3;
		return;
	/* Not applied: the three games load a straight ramp. One that is not
	 * is said once. */
	case DAC_LUT_DATA: {
		const uint32_t entry = m_lut_at / 3, ramp = (m_s.implicit[DAC_CONTROL] & DAC_6BIT) ? entry >> 2 : entry;

		m_s.implicit[addr] = value;
		if ((value & 0xFF) != ramp && !m_said_lut) {
			m_said_lut = true;
			std::fprintf(stderr, "pmx1: the DAC's gamma table is not a straight ramp (entry %u = %02X), not "
			             "emulated\n", static_cast<unsigned>(entry), static_cast<unsigned>(value & 0xFF));
		}
		m_lut_at = (m_lut_at + 1) % (256 * 3);
		return;
	}
	case INDEX_DATA:
		m_s.implicit[addr] = value;
		index_data(value);
		return;
	default:
		break;
	}
	m_s.implicit[addr] = value;
	/* A thread enabled or kicked may run now: the block ends */
	if (addr == SOFTWARE_RESET || addr < THREAD_ENABLE + THREADS)
		m_leave = m_others = true;
	if (addr >= THREAD_KICKER && addr < THREAD_KICKER + THREADS) {
		kick(addr - THREAD_KICKER, value);
		m_leave = true;
		return;
	}
	if (addr == SOFTWARE_RESET) {
		/* Out of reset, thread 0 starts at the reset vector */
		if ((value & C1_RST) && !(before & C1_RST)) {
			m_s.next[0] = (m_s.implicit[RESET_VECTOR_HI] << 16 | m_s.implicit[RESET_VECTOR_LO]) + 4;
			m_s.slots[0] = m_s.stopped[0] = m_s.waiting[0] = 0;
		}
		return;
	}
	if (addr > THREAD_ENABLE && addr < THREAD_ENABLE + THREADS) {
		if (addr == 2 && (value & 1) && !m_said_enabled[addr]) {
			m_said_enabled[addr] = true;
			if (m_verbose)
				std::fprintf(stderr, "pmx1: thread 2 enabled at %08X, not run\n", m_s.next[addr]);
		}
		return;
	}
	if (addr == THREAD_ENABLE || addr == RESET_VECTOR_LO || addr == RESET_VECTOR_HI || addr == FB_OFFSET ||
	    m_said[addr])
		return;
	m_said[addr] = m_plain[addr] = true;
	if (!m_verbose)
		return;
	if (by == BY_HOST)
		std::fprintf(stderr, "pmx1: host w implicit %03X = %04X, not emulated\n", addr, value);
	else
		std::fprintf(stderr, "pmx1: %08X w implicit %03X = %04X, not emulated\n", by, addr, value);
}

/* INFERRED: thread 2 is never run. The programming guide's overview
 * gives the core three threads (background, DAC feed, video in), and the
 * games' start-up enables thread 2 at 0, from where it would run words of
 * zeros into the registers thread 0 is using. */
bool Pmx1Core::runnable(unsigned thread) const
{
	return thread != 2 && (m_s.implicit[THREAD_ENABLE + thread] & 1) && (m_s.implicit[SOFTWARE_RESET] & C1_RST) &&
	       !m_s.stopped[thread] && !m_s.waiting[thread];
}

/* The thread that issues: the first that can run of a fixed order, the
 * real-time threads before thread 0 (patent US5968167: "a fixed priority
 * selector ... launches the highest priority runnable thread", a real-time
 * input taking "precedence over a background processing operation").
 * HEURISTIC: 3 before 1, which the documents do not give; THREADS for none */
unsigned Pmx1Core::issuing() const
{
	if (runnable(3))
		return 3;
	if (runnable(1))
		return 1;
	return runnable(0) ? 0 : THREADS;
}

/* HEURISTIC: one instruction a cycle, and the reads answer at once: so a
 * real-time thread runs until it waits for its kicker or stops, word by
 * word, and thread 0 runs in between, with its blocks and host code. A
 * thread enabled or kicked by thread 0 ends the block it is in. So the
 * DAC thread's first four words, which leave its return link in D0.0,
 * run as soon as the games' start-up enables it, and that start-up then
 * uses D0.0 again: the link is lost. What the boards do is UNKNOWN (how
 * long a thread takes to fetch its first words is not known). */
void Pmx1Core::run(int32_t cycles)
{
	if (m_s.next_interrupt) {
		interrupt(m_s.next_interrupt);
		m_s.next_interrupt = 0;
	}
	/* The host ran since the last run: the code is compared again */
	m_epoch++;

	uint32_t last = 0;	/* rank + 1 of the block run last */

	while (cycles > 0) {
		if (m_others) {
			const unsigned thread = issuing();

			if (thread != 0 && thread != THREADS) {
				step(thread);
				m_s.cycles++;
				cycles--;
				continue;
			}
			m_others = false;
		}
		if (!runnable(0))
			break;
		cycles -= static_cast<int32_t>(run_thread0(last, static_cast<uint32_t>(cycles)));
	}
	for (uint32_t page : m_pages_written) {
		m_page_written[page] = 0;
		m_written(page << PAGE_BITS, 1u << PAGE_BITS);
	}
	m_pages_written.clear();
}

/* Thread 0 for at most `cycles`: a block, its host code, a repeated store
 * or a word. Returns the cycles run. */
uint32_t Pmx1Core::run_thread0(uint32_t &last, uint32_t cycles)
{
	const Block *b = nullptr;

	if (!m_s.slots[0]) {
		b = follow(last, m_s.next[0]);
		last = b ? static_cast<uint32_t>(b - m_blocks.data()) + 1 : 0;
	}
	if (b && b->count <= cycles) {
		const uint32_t place = m_translate ? host_block(last) : 0;

		if (place) {
			last = 0;
			return run_host(place, cycles);
		}
		return run_chained(*b);
	}
	if (b)
		return run_block(*b, cycles);
	if (const uint32_t ran = run_repeated(cycles))
		return ran;
	step(0);
	m_s.cycles++;
	return 1;
}

void Pmx1Core::written(uint32_t offset, uint32_t size)
{
	for (uint32_t page = offset >> PAGE_BITS; page <= (offset + size - 1) >> PAGE_BITS; page++)
		if (!m_page_written[page]) {
			m_page_written[page] = 1;
			m_pages_written.push_back(page);
		}
	for (uint32_t line = offset >> LINE_BITS; line <= (offset + size - 1) >> LINE_BITS; line++)
		if (m_code_line[line]) {
			m_epoch++;
			m_leave = true;
			return;
		}
}

/* The forms a block stops before, and those that issue a jump of thread
 * 0, after which it runs their delay slots and stops. A read of the timer
 * gets the cycles run word by word, which a block counts at its end */
bool Pmx1Core::ends_block(const Op &o)
{
	return o.form == NO || o.form == REPEATED || (o.form == MOVE && (o.unit == KICKER || o.unit == TIMER));
}

bool Pmx1Core::jumps(const Op &o)
{
	switch (o.form) {
	case BRANCH:
	case SWAP_PC:
		return true;
	case MOVE:
	case OPERATE_TO:
	case OPERATE_REGS_TO_IF:
	case SHIFT_TO:
		return o.to == PC && o.d == 0;
	default:
		return false;
	}
}

/* The block at `at` after the block `last` (rank + 1, or 0): the one it
 * went on to the last time, when that one was compared in this epoch, as
 * the SH-4 links its blocks; else block(), which `last` then keeps */
const Pmx1Core::Block *Pmx1Core::follow(uint32_t last, uint32_t at)
{
	if (last && last <= m_blocks.size()) {
		const Block &from = m_blocks[last - 1];

		for (unsigned i = 0; i < 2; i++)
			if (from.link_at[i] == at && from.link[i] && from.link[i] <= m_blocks.size()) {
				const Block &to = m_blocks[from.link[i] - 1];

				if (to.at == at && to.checked == m_epoch)
					return &to;
			}
	}

	const Block *const b = block(at);

	if (b && last && last <= m_blocks.size()) {
		/* The older link goes */
		Block &from = m_blocks[last - 1];

		from.link_at[1] = from.link_at[0];
		from.link[1] = from.link[0];
		from.link_at[0] = at;
		from.link[0] = static_cast<uint32_t>(b - m_blocks.data()) + 1;
	}
	return b;
}

/* The block thread 0 runs from `at`, compared or decoded; nullptr where
 * the run goes word by word */
const Pmx1Core::Block *Pmx1Core::block(uint32_t at)
{
	const uint32_t offset = at & FRAME_BUFFER_MASK;

	if ((at & 3) || offset > m_vram.size() - 4)
		return nullptr;

	uint32_t &rank = m_block_at[(offset >> 2) & (OPS - 1)];

	if (rank && m_blocks[rank - 1].at == at) {
		Block &b = m_blocks[rank - 1];

		if (b.checked != m_epoch) {
			uint32_t i = 0;

			while (i < b.count && m_block_ops[b.first + i].word == le32(m_vram.data() + offset + 4 * i))
				i++;
			if (i == b.count)
				b.checked = m_epoch;
		}
		if (b.checked == m_epoch)
			return b.count ? &b : nullptr;
	}
	if (m_block_ops.size() > BLOCK_OPS_MAX) {
		m_blocks.clear();
		m_block_ops.clear();
		std::fill(m_block_at.begin(), m_block_at.end(), 0);
		std::fill(m_code_line.begin(), m_code_line.end(), 0);
		drop_host();
	}

	Block b{ at, static_cast<uint32_t>(m_block_ops.size()), 0, NO_JUMP, m_epoch, {}, {}, 0 };
	uint32_t end = BLOCK_MAX;

	for (uint32_t i = 0; i < end && offset + 4 * i <= m_vram.size() - 4; i++) {
		Op o = decode(le32(m_vram.data() + offset + 4 * i));

		o.at = at + 4 * i;
		/* A second jump in the slots issues only when the first did not
		 * (BNE, then SWAPEQ PC: the TA job's coroutines), and its own
		 * slots go past them */
		if (ends_block(o) || (b.jump != NO_JUMP && jumps(o)))
			break;
		m_block_ops.push_back(o);
		b.count = i + 1;
		if (b.jump == NO_JUMP && jumps(o)) {
			b.jump = i;
			end = i + 1 + DELAY_SLOTS;
		}
	}

	Op last{};

	last.run = last.step = &Pmx1Core::end;
	last.at = at + 4 * b.count;
	last.form = NOTHING;
	m_block_ops.push_back(last);
	for (uint32_t line = offset >> LINE_BITS; b.count && line <= (offset + 4 * b.count - 1) >> LINE_BITS; line++)
		m_code_line[line] = 1;
	m_blocks.push_back(b);
	rank = static_cast<uint32_t>(m_blocks.size());
	return b.count ? &m_blocks.back() : nullptr;
}

/* Runs a whole block, each op running the next: what step() does for each
 * is done for the block at its end. Its jump, when it has one, is the
 * fifth op from the end, its delay slots the four after; the core counts
 * the cycles, sets the next address, and runs the slots out there. A block
 * left before its end is brought to where step() would have left it.
 * Returns the ops run. */
uint32_t Pmx1Core::run_chained(const Block &b)
{
	const Op *const ops = m_block_ops.data() + b.first;

	m_leave = false;

	const Done done = ops[0].run(*this, ops, 0);

	if (done != Done::YES)
		return left(b, static_cast<uint32_t>(m_exit - ops), done);
	m_s.cycles += b.count;
	m_s.next[0] = b.at + 4 * b.count;
	/* Its jump issued: the slots it ran, those left run word by word */
	if (m_s.slots[0]) {
		m_s.slots[0] -= b.count - 1 - b.jump;
		if (!m_s.slots[0])
			m_s.next[0] = m_s.target[0];
	}
	return b.count;
}

/* A block that ended at op k, which returned `done`: a jump issued is that
 * of the block, at `jump`. Returns the ops run. */
uint32_t Pmx1Core::left(const Block &b, uint32_t k, Done done)
{
	const uint32_t jump = b.jump;
	const Op &o = m_block_ops[b.first + k];

	m_s.cycles += k + 1;
	if (done == Done::NO) {
		if (m_s.slots[0])
			m_s.slots[0] -= k - jump - 1;
		stop(0, o.at, o.word, "this instruction is not emulated yet");
	} else if (done == Done::WAIT) {
		if (m_s.slots[0])
			m_s.slots[0] -= k - jump - 1;
		m_s.next[0] = o.at;
		m_s.waiting[0] = 1;
	} else if (m_s.slots[0] && !m_s.stopped[0]) {
		/* LEAVE: the op set the next address, the slots run out */
		m_s.slots[0] -= k - jump;
		if (!m_s.slots[0])
			m_s.next[0] = m_s.target[0];
	}
	return k + 1;
}

/* Runs at most `cycles` ops of a block, each as step() runs it; returns
 * those run */
uint32_t Pmx1Core::run_block(const Block &b, uint32_t cycles)
{
	const Op *const ops = m_block_ops.data() + b.first;
	const uint32_t count = b.count < cycles ? b.count : cycles;
	uint32_t at = b.at;

	m_leave = false;
	for (uint32_t k = 0; k < count; k++, at += 4) {
		const bool slot = m_s.slots[0] > 0;

		m_s.next[0] = at + 4;

		const Done done = execute(0, ops[k]);

		m_s.cycles++;
		if (done == Done::NO) {
			stop(0, at, ops[k].word, "this instruction is not emulated yet");
			return k + 1;
		}
		if (done == Done::WAIT) {
			m_s.next[0] = at;
			m_s.waiting[0] = 1;
			return k + 1;
		}
		/* Done::LEAVE: as YES, m_leave is set */
		if (slot && !m_s.stopped[0] && --m_s.slots[0] == 0)
			m_s.next[0] = m_s.target[0];
		if (m_leave)
			return k + 1;
	}
	return count;
}

/* The blocks as host code */
void Pmx1Core::set_translate(bool on)
{
	if (on && !m_host) {
		const auto from_core = [this](const void *at) { return HostCode::place_in(this, at); };
		const Pmx1Host::Places places{ from_core(&m_s.flags[0]),        from_core(&m_s.slots[0]),
			                       from_core(&m_s.target[0]),       from_core(&m_s.next[0]),
			                       from_core(&m_host_exit.block),   from_core(&m_host_exit.op),
			                       from_core(&m_host_exit.done),    from_core(&m_host_exit.stale),
			                       from_core(&m_s.implicit[0]),     from_core(m_plain.data()),
			                       from_core(&m_s.implicit_read[0]), from_core(&m_s.queue_size[0]),
			                       from_core(&m_s.queue_first[0]),  from_core(&m_s.queued[0][0][0]),
			                       from_core(&m_s.implicit[FB_OFFSET]),
			                       static_cast<uint32_t>(m_vram.size()), m_page_written.data(), m_code_line.data() };

		m_host = std::make_unique<Pmx1Host>(places, &Pmx1Core::linked);
	}
	m_translate = on && m_host && m_host->usable();
	drop_host();
}

void Pmx1Core::drop_host()
{
	if (!m_host || !m_host->usable())
		return;
	m_host->writable();
	m_host->clear();
	m_unlinked.clear();
	for (Block &b : m_blocks)
		b.host = 0;
}

/* The host code of the block of rank `rank`, written now if it has none */
uint32_t Pmx1Core::host_block(uint32_t rank)
{
	Block &b = m_blocks[rank - 1];

	if (!b.host) {
		if (!m_host->room())
			drop_host();
		b.host = translate(rank);
	}
	return b.host;
}

/* The host code of the block at `at`, 0 for none */
uint32_t Pmx1Core::host_at(uint32_t at) const
{
	const uint32_t offset = at & FRAME_BUFFER_MASK;

	if ((at & 3) || offset > m_vram.size() - 4)
		return 0;

	const uint32_t rank = m_block_at[(offset >> 2) & (OPS - 1)];

	return rank && m_blocks[rank - 1].at == at ? m_blocks[rank - 1].host : 0;
}

/* The way on of the host code, to the block at the next address: those
 * it runs are checked against video RAM, op by op */
uintptr_t Pmx1Core::linked(Pmx1Core &c)
{
	const uint32_t place = c.host_at(c.m_s.next[0]);

	return place ? c.m_host->address(place) : 0;
}

/* The block of rank `rank` as host code: each op checked against video
 * RAM, then done in place or called (its `step`), and at the end what
 * run_chained() does, then on to the block at the next address. */
uint32_t Pmx1Core::translate(uint32_t rank)
{
	Pmx1Host &h = *m_host;
	const Block &b = m_blocks[rank - 1];
	const Op *const ops = m_block_ops.data() + b.first;

	h.writable();

	const uint32_t place = h.begin(rank, b.count);

	for (uint32_t k = 0; k < b.count; k++) {
		h.check(ops[k].at & FRAME_BUFFER_MASK, ops[k].word, k);
		if (!in_place(ops[k], k))
			h.call(reinterpret_cast<uintptr_t>(ops[k].step), &ops[k], k);
	}

	const Op *const jump = b.jump != NO_JUMP ? &ops[b.jump] : nullptr;
	const uint32_t slots = jump ? DELAY_SLOTS - (b.count - 1 - b.jump) : 0;
	const bool known = jump && jump->form == BRANCH;
	const uint32_t taken = known ? jump->at + jump->imm : 0, next = b.at + 4 * b.count;
	uint32_t taken_at = 0, next_at = 0;

	h.end(b.count, jump != nullptr, slots, known, known && jump->cond == 0, taken, known ? host_at(taken) : 0, next,
	      host_at(next), taken_at, next_at);
	if (taken_at)
		m_unlinked[taken].push_back(taken_at);
	if (next_at)
		m_unlinked[next].push_back(next_at);
	/* The jumps to it written before, its own among them, go straight to
	 * it now */
	if (const auto waiting = m_unlinked.find(b.at); waiting != m_unlinked.end()) {
		for (const uint32_t at : waiting->second)
			h.link_to(at, place);
		m_unlinked.erase(waiting);
	}
	return place;
}

/* Op k in place when its form allows: the ALU and the compares on
 * registers, the branches, the moves and the exchanges between registers;
 * the ways WIMP, a store through unit 0, a take and a read queued go most
 * often; what step() does with them, on the core's state where it is. The
 * others, and the odd paths of these (a shift past 31, a branch in the
 * slots of another, a store to a page not told yet...), are called. */
bool Pmx1Core::in_place(const Op &o, uint32_t k)
{
	Pmx1Host &h = *m_host;
	const auto reg = [this](uint32_t unit, uint32_t n) {
		return HostCode::place_in(this, &m_s.regs[(unit - 1) & 3][n & 31]);
	};
	const bool units = o.unit >= D0 && o.unit <= A1, to_units = o.to >= D0 && o.to <= A1;
	const uintptr_t does = reinterpret_cast<uintptr_t>(o.step);
	/* The forms with a condition */
	const auto when = [&]() { return o.cond ? h.unless(passes(o.cond)) : HostCode::Jump{0}; };
	const auto done = [&](HostCode::Jump skip) {
		if (o.cond)
			h.bind(skip);
		return true;
	};

	switch (o.form) {
	case NOTHING:
		return true;
	case SET:
		h.store_value(reg(o.unit, o.d), o.imm);
		return true;
	case COPY:
		h.load(reg(o.unit, o.s2));
		h.store(reg(o.unit, o.d));
		return true;
	case OPERATE:
	case COMPARE:
		h.load(reg(o.unit, o.s1));
		if (o.form == COMPARE)
			h.compare(o.op, o.imm);
		else
			h.operate(o.op, o.imm);
		if (o.set)
			h.flags(o.op);
		if (o.form == OPERATE)
			h.store(reg(o.unit, o.d));
		return true;
	case OPERATE_REGS:
	case SHIFT_REGS:
	case COMPARE_REGS: {
		h.load(reg(o.unit, o.s1));
		h.load_operand(reg(o.unit, o.s2));

		const HostCode::Jump odd = o.op >= 5 ? h.unless_count() : HostCode::Jump{0};

		if (o.form == COMPARE_REGS)
			h.compare_regs(o.op);
		else
			h.operate_regs(o.op);
		if (o.set)
			h.flags(o.op);
		if (o.form != COMPARE_REGS)
			h.store(reg(o.unit, o.d));
		if (o.op >= 5)
			h.otherwise(odd, does, &o, k);
		return true;
	}
	case OPERATE_TO:
		if (!to_units && !(o.to == RD && o.d == 0))
			return false;
		h.load(reg(o.unit, o.s1));
		h.operate(o.op, o.imm);
		if (to_units)
			h.store(reg(o.to, o.d));
		else
			h.queue_read(does, &o, k);
		return true;
	case OPERATE_IF:
	case SHIFT_TO: {
		const HostCode::Jump skip = when();

		h.load(reg(o.unit, o.s1));
		h.operate(o.op, o.imm);
		h.store(reg(o.form == SHIFT_TO ? o.to : o.unit, o.d));
		return done(skip);
	}
	case OPERATE_REGS_TO_IF: {
		if (!to_units && !(o.to == RD && o.d == 0))
			return false;

		const HostCode::Jump skip = when();

		h.load(reg(o.unit, o.s1));
		h.load_operand(reg(o.unit, o.s2));
		h.operate_regs(o.op);
		if (to_units)
			h.store(reg(o.to, o.d));
		else
			h.queue_read(does, &o, k);
		return done(skip);
	}
	case COMPARE_IF: {
		const HostCode::Jump skip = when();

		h.load(reg(o.unit, o.s1));
		h.compare(o.op, o.imm);
		h.flags(o.op);
		return done(skip);
	}
	case BRANCH: {
		const HostCode::Jump skip = when();

		h.branch(o.at + o.imm, DELAY_SLOTS, does, &o, k);
		return done(skip);
	}
	case MOVE: {
		/* A take pops even when its condition fails */
		if (o.unit == RD && to_units) {
			h.take(reg(o.to, o.d), o.cond ? passes(o.cond) : 0xFFFF, does, &o, k);
			return true;
		}
		if (!units || (!to_units && o.to != STORE))
			return false;

		const HostCode::Jump skip = when();

		if (o.to == STORE) {
			h.store_word(reg(o.d < 16 ? A0 : A1, o.d & 15), 0, false, reg(o.unit, o.s1), does, &o, k);
		} else {
			h.load(reg(o.unit, o.s1));
			h.store(reg(o.to, o.d));
		}
		return done(skip);
	}
	case WIMP: {
		const HostCode::Jump skip = when();

		h.wimp(reg(o.unit, o.s1), does, &o, k);
		return done(skip);
	}
	case MEMORY: {
		/* SETD and GETD of a register (Pmx1Core::memory), B6 and C6
		 * keeping the address */
		const uint32_t unit = (o.word >> 1) & 0xF, minor = (o.word >> 24) & 0xF;

		if ((o.word & 0xE1) != 0x20 || unit < D0 || unit > A1 || minor == 7)
			return false;

		const int32_t base = reg((o.word >> 18) & 1 ? A1 : A0, (o.word >> 14) & 15);
		const int32_t data = reg(unit, (o.word >> 19) & 31);
		const uint32_t add = 4 * ((o.word >> 8) & 0x3F);

		if (o.word >> 28 == 0xC)
			h.getd(base, add, minor == 6, data, does, &o, k);
		else
			h.store_word(base, add, minor == 6, data, does, &o, k);
		return true;
	}
	case SWAP: {
		const HostCode::Jump skip = when();

		h.swap(reg(o.unit, o.s1), reg(o.to, o.d));
		return done(skip);
	}
	default:
		return false;
	}
}

/* The blocks from `place` as host code, at most `cycles` ops, each as
 * run_chained() runs it; one left before its end is brought to where
 * step() would have left it, op k run or, when its word changed, not.
 * Returns the ops run. */
uint32_t Pmx1Core::run_host(uint32_t place, uint32_t cycles)
{
	m_leave = false;
	m_host_exit = {};

	const uint32_t ran = m_host->run(*this, place, cycles, m_vram.data());

	m_s.cycles += ran;
	if (!m_host_exit.block)
		return ran;

	const Block &b = m_blocks[m_host_exit.block - 1];
	const uint32_t k = m_host_exit.op;

	if (!m_host_exit.stale)
		return ran + left(b, k, static_cast<Done>(m_host_exit.done));
	/* The words are compared again, the blocks written again */
	m_s.cycles += k;
	if (m_s.slots[0])
		m_s.slots[0] -= k - b.jump - 1;
	m_s.next[0] = b.at + 4 * k;
	m_epoch++;
	drop_host();
	return ran + k;
}

/* A repeated store under way (its count taken), its stores one a cycle as
 * step() runs them, without fetching its word again for each: at most
 * `cycles`; 0 when step() is to run it */
uint32_t Pmx1Core::run_repeated(uint32_t cycles)
{
	const uint32_t at = m_s.next[0], offset = at & FRAME_BUFFER_MASK;

	if (!m_s.repeat[0] || m_s.slots[0] || offset > m_vram.size() - 4)
		return 0;

	const uint32_t word = le32(m_vram.data() + offset);

	if (word >> 24 != 0xBC || (word & 0x21FF) != 0x0059)
		return 0;

	uint32_t *const regs = m_s.regs[((word >> 18) & 1 ? A1 : A0) - 1];
	const uint32_t n = (word >> 14) & 15, m = (word >> 9) & 15, value = m_s.regs[D1 - 1][(word >> 19) & 31];
	uint32_t ran = 0;

	m_leave = false;
	while (ran < cycles) {
		regs[n] += regs[m];
		ran++;
		m_s.cycles++;
		if (!store(regs[n], value)) {
			stop(0, at, word, "this instruction is not emulated yet");
			break;
		}
		if (!--m_s.repeat[0]) {
			m_s.next[0] = at + 4;
			break;
		}
		/* A line of code written: step() fetches the word again */
		if (m_leave)
			break;
	}
	return ran;
}

void Pmx1Core::step(unsigned thread)
{
	const uint32_t at = m_s.next[thread], offset = at & FRAME_BUFFER_MASK;

	if (offset > m_vram.size() - 4) {
		stop(thread, at, 0, "it fetches outside video RAM");
		return;
	}

	const uint32_t word = le32(m_vram.data() + offset);
	const bool slot = m_s.slots[thread] > 0;
	Op &o = m_ops[(offset >> 2) & (OPS - 1)];

	if (o.word != word || o.at != at || o.form == UNDECODED) {
		o = decode(word);
		o.at = at;
	}
	m_s.next[thread] = at + 4;
	switch (execute(thread, o)) {
	case Done::NO:
		stop(thread, at, word, "this instruction is not emulated yet");
		return;
	case Done::WAIT:
		/* It runs again on the next kick */
		m_s.next[thread] = at;
		m_s.waiting[thread] = 1;
		return;
	case Done::YES:
	case Done::LEAVE:
		break;
	}
	if (slot && !m_s.stopped[thread] && --m_s.slots[thread] == 0)
		m_s.next[thread] = m_s.target[thread];
}

void Pmx1Core::stop(unsigned thread, uint32_t at, uint32_t word, const char *why)
{
	m_leave = true;
	m_s.stopped[thread] = 1;
	m_s.next[thread] = at;
	std::fprintf(stderr, "pmx1: thread %u stops at %08X (%08X): %s\n", thread, at, word, why);
}

/* The forms, each written once and made for each operation, condition and
 * flag setting it is decoded with, and for each way to hand over, as the
 * SH-4's instructions are: Chained runs the next op of the block itself,
 * Called comes back to step(). A form that fails, or after which the
 * block must end, says where (m_exit). */
struct Pmx1Ops {
	using Core = Pmx1Core;
	using Done = Core::Done;
	using Op = Core::Op;
	using Run = Op::Run;

	struct Chained {
		static Done next(Core &c, const Op *o, unsigned thread) { return o[1].run(c, o + 1, thread); }
	};
	struct Called {
		static Done next(Core &, const Op *, unsigned) { return Done::YES; }
	};

	template <uint32_t OP>
	static uint32_t apply(uint32_t a, uint32_t b, uint32_t &flags)
	{
		uint32_t result = 0;

		operate(OP, a, b, result, flags);
		return result;
	}

	template <uint32_t COND>
	static bool holds(const Core &c, unsigned thread)
	{
		return holds_with(COND, c.m_s.flags[thread]);
	}

	static uint32_t *regs(Core &c, const Op *o) { return c.m_s.regs[(o->unit - 1u) & 3]; }

	static Done fail(Core &c, const Op *o)
	{
		c.m_exit = o;
		return Done::NO;
	}
	/* After a form that may end the block (a write to the code, a reset):
	 * it set the next address first, as step() does */
	template <class Way>
	static Done after(Core &c, const Op *o, unsigned thread)
	{
		if (c.m_leave) {
			c.m_exit = o;
			return Done::LEAVE;
		}
		return Way::next(c, o, thread);
	}
	template <class Way>
	static Done done(Core &c, const Op *o, unsigned thread, Done d)
	{
		if (d == Done::YES)
			return after<Way>(c, o, thread);
		c.m_exit = o;
		return d;
	}

	static Done no(Core &c, const Op *o, unsigned) { return fail(c, o); }

	template <class Way>
	static Done nothing(Core &c, const Op *o, unsigned thread)
	{
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t OP, bool SET>
	static Done operate_imm(Core &c, const Op *o, unsigned thread)
	{
		uint32_t *const r = regs(c, o);
		uint32_t flags = c.m_s.flags[thread];

		r[o->d] = apply<OP>(r[o->s1], o->imm, flags);
		if constexpr (SET)
			c.m_s.flags[thread] = flags;
		return Way::next(c, o, thread);
	}

	template <class Way>
	static Done set(Core &c, const Op *o, unsigned thread)
	{
		regs(c, o)[o->d] = o->imm;
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t OP, bool SET>
	static Done operate_regs(Core &c, const Op *o, unsigned thread)
	{
		uint32_t *const r = regs(c, o);
		uint32_t flags = c.m_s.flags[thread];

		if constexpr (OP >= 5) {
			if (r[o->s2] > 31)
				return fail(c, o);
		}
		r[o->d] = apply<OP>(r[o->s1], r[o->s2], flags);
		if constexpr (SET)
			c.m_s.flags[thread] = flags;
		return Way::next(c, o, thread);
	}

	template <class Way>
	static Done copy(Core &c, const Op *o, unsigned thread)
	{
		uint32_t *const r = regs(c, o);

		r[o->d] = r[o->s2];
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t OP>
	static Done operate_to(Core &c, const Op *o, unsigned thread)
	{
		uint32_t flags = c.m_s.flags[thread];
		const uint32_t result = apply<OP>(regs(c, o)[o->s1], o->imm, flags);

		return c.write_unit(thread, o->to, o->d, result) ? Way::next(c, o, thread) : fail(c, o);
	}

	template <class Way, uint32_t OP, uint32_t COND>
	static Done operate_if(Core &c, const Op *o, unsigned thread)
	{
		if (holds<COND>(c, thread)) {
			uint32_t *const r = regs(c, o);
			uint32_t flags = c.m_s.flags[thread];

			r[o->d] = apply<OP>(r[o->s1], o->imm, flags);
		}
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t OP, uint32_t COND>
	static Done operate_regs_to_if(Core &c, const Op *o, unsigned thread)
	{
		if (!holds<COND>(c, thread))
			return Way::next(c, o, thread);

		uint32_t *const r = regs(c, o);
		uint32_t flags = c.m_s.flags[thread];
		const uint32_t result = apply<OP>(r[o->s1], r[o->s2], flags);

		return c.write_unit(thread, o->to, o->d, result) ? Way::next(c, o, thread) : fail(c, o);
	}

	template <class Way, uint32_t OP, uint32_t COND>
	static Done shift_to(Core &c, const Op *o, unsigned thread)
	{
		if (!holds<COND>(c, thread))
			return Way::next(c, o, thread);

		uint32_t flags = c.m_s.flags[thread];
		const uint32_t result = apply<OP>(regs(c, o)[o->s1], o->imm, flags);

		return c.write_unit(thread, o->to, o->d, result) ? Way::next(c, o, thread) : fail(c, o);
	}

	template <class Way, uint32_t OP>
	static Done compare(Core &c, const Op *o, unsigned thread)
	{
		apply<OP>(regs(c, o)[o->s1], o->imm, c.m_s.flags[thread]);
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t OP>
	static Done compare_regs(Core &c, const Op *o, unsigned thread)
	{
		uint32_t *const r = regs(c, o);

		apply<OP>(r[o->s1], r[o->s2], c.m_s.flags[thread]);
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t OP, uint32_t COND>
	static Done compare_if(Core &c, const Op *o, unsigned thread)
	{
		if (holds<COND>(c, thread))
			apply<OP>(regs(c, o)[o->s1], o->imm, c.m_s.flags[thread]);
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t COND>
	static Done branch(Core &c, const Op *o, unsigned thread)
	{
		/* The displacement counts from the word the jump takes effect
		 * after: its last delay slot, or itself in a real-time thread */
		const uint32_t to = o->at + o->imm - (thread ? 4 * DELAY_SLOTS : 0);

		return !holds<COND>(c, thread) || c.jump(thread, to) ? Way::next(c, o, thread) : fail(c, o);
	}

	/* The moves by where from (a register, a take, or another: Core::move)
	 * and where to (a register, a store, or another: write_unit) */
	enum : uint32_t { REGS, TAKE, OTHER, STORED };

	template <class Way, uint32_t COND, uint32_t FROM, uint32_t TO>
	static Done move(Core &c, const Op *o, unsigned thread)
	{
		uint32_t value = 0;

		/* INFERRED: a take pops even when its condition fails; the TA
		 * status loop queues a read for every status and keeps it only
		 * when the tile changes */
		if (!holds<COND>(c, thread))
			return FROM == TAKE && !c.take(thread, value) ? fail(c, o) : Way::next(c, o, thread);
		if constexpr (FROM == OTHER) {
			c.m_s.next[thread] = o->at + 4;
			return done<Way>(c, o, thread, c.move(thread, *o));
		} else {
			if constexpr (FROM == REGS)
				value = regs(c, o)[o->s1];
			else if (!c.take(thread, value))
				return fail(c, o);
			if constexpr (TO == REGS) {
				c.m_s.regs[o->to - 1][o->d] = value;
				return Way::next(c, o, thread);
			} else if constexpr (TO == STORED) {
				c.m_s.next[thread] = o->at + 4;
				if (!c.store(c.m_s.regs[(o->d < 16 ? A0 : A1) - 1][o->d & 15], value))
					return fail(c, o);
				return after<Way>(c, o, thread);
			} else {
				return c.write_unit(thread, o->to, o->d, value) ? Way::next(c, o, thread) : fail(c, o);
			}
		}
	}

	template <class Way, uint32_t COND>
	static Done swap(Core &c, const Op *o, unsigned thread)
	{
		if (holds<COND>(c, thread))
			std::swap(regs(c, o)[o->s1], c.m_s.regs[o->to - 1][o->d]);
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t COND>
	static Done swap_pc(Core &c, const Op *o, unsigned thread)
	{
		if (o->d != thread)
			return fail(c, o);
		if (holds<COND>(c, thread)) {
			uint32_t &source = regs(c, o)[o->s1];

			if (!c.jump(thread, source + 4))
				return fail(c, o);
			source = o->at + (thread ? 0 : 4 * DELAY_SLOTS);
		}
		return Way::next(c, o, thread);
	}

	template <class Way, uint32_t COND>
	static Done wimp(Core &c, const Op *o, unsigned thread)
	{
		if (!holds<COND>(c, thread))
			return Way::next(c, o, thread);
		c.m_s.next[thread] = o->at + 4;
		c.port_write(thread, regs(c, o)[o->s1], o->at);
		return after<Way>(c, o, thread);
	}

	template <class Way>
	static Done movl(Core &c, const Op *o, unsigned thread)
	{
		c.m_s.next[thread] = o->at + 4;
		return done<Way>(c, o, thread, c.movl(thread, o->word));
	}
	template <class Way>
	static Done memory(Core &c, const Op *o, unsigned thread)
	{
		c.m_s.next[thread] = o->at + 4;
		return done<Way>(c, o, thread, c.memory(thread, o->word));
	}
	template <class Way>
	static Done indexed(Core &c, const Op *o, unsigned thread)
	{
		c.m_s.next[thread] = o->at + 4;
		return done<Way>(c, o, thread, c.indexed(thread, o->word));
	}
	template <class Way>
	static Done port(Core &c, const Op *o, unsigned thread)
	{
		c.m_s.next[thread] = o->at + 4;
		return done<Way>(c, o, thread, c.port(thread, o->word));
	}
	template <class Way>
	static Done feed(Core &c, const Op *o, unsigned thread)
	{
		c.m_s.next[thread] = o->at + 4;
		return done<Way>(c, o, thread, c.feed(thread, o->word));
	}
	/* Run word by word only: it stays on its word */
	static Done repeated(Core &c, const Op *o, unsigned thread)
	{
		const Done d = c.repeated(thread, o->word, o->at);

		return d == Done::YES ? Done::YES : fail(c, o);
	}

	/* The tables of the forms made for each operation and condition */
	template <class Way, bool SET, size_t... OP>
	static constexpr std::array<Run, 7> operate_imm_table(std::index_sequence<OP...>)
	{
		return { &operate_imm<Way, OP, SET>... };
	}
	template <class Way, bool SET, size_t... OP>
	static constexpr std::array<Run, 7> operate_regs_table(std::index_sequence<OP...>)
	{
		return { &operate_regs<Way, OP, SET>... };
	}
	template <class Way, uint32_t OP, size_t... C>
	static constexpr std::array<Run, 16> operate_if_table(std::index_sequence<C...>)
	{
		return { &operate_if<Way, OP, C>... };
	}
	template <class Way, uint32_t OP, size_t... C>
	static constexpr std::array<Run, 16> operate_regs_to_if_table(std::index_sequence<C...>)
	{
		return { &operate_regs_to_if<Way, OP, C>... };
	}
	template <class Way, uint32_t OP, size_t... C>
	static constexpr std::array<Run, 16> shift_to_table(std::index_sequence<C...>)
	{
		return { &shift_to<Way, OP, C>... };
	}
	template <class Way, uint32_t OP, size_t... C>
	static constexpr std::array<Run, 16> compare_if_table(std::index_sequence<C...>)
	{
		return { &compare_if<Way, OP, C>... };
	}
	template <class Way, size_t... C>
	static constexpr std::array<Run, 16> branch_table(std::index_sequence<C...>)
	{
		return { &branch<Way, C>... };
	}
	template <class Way, uint32_t FROM, uint32_t TO, size_t... C>
	static constexpr std::array<Run, 16> move_table(std::index_sequence<C...>)
	{
		return { &move<Way, C, FROM, TO>... };
	}
	template <class Way, size_t... C>
	static constexpr std::array<Run, 16> swap_table(std::index_sequence<C...>)
	{
		return { &swap<Way, C>... };
	}
	template <class Way, size_t... C>
	static constexpr std::array<Run, 16> swap_pc_table(std::index_sequence<C...>)
	{
		return { &swap_pc<Way, C>... };
	}
	template <class Way, size_t... C>
	static constexpr std::array<Run, 16> wimp_table(std::index_sequence<C...>)
	{
		return { &wimp<Way, C>... };
	}

	template <class Way>
	static Run run_of(const Op &o)
	{
		static constexpr auto OPS = std::make_index_sequence<7>();
		static constexpr auto CONDS = std::make_index_sequence<16>();
		static constexpr std::array<Run, 7> OPERATE_IMM[2] = { operate_imm_table<Way, false>(OPS),
			                                               operate_imm_table<Way, true>(OPS) };
		static constexpr std::array<Run, 7> OPERATE_REGS[2] = { operate_regs_table<Way, false>(OPS),
			                                                operate_regs_table<Way, true>(OPS) };
		static constexpr std::array<Run, 3> OPERATE_TO = { &operate_to<Way, 0>, &operate_to<Way, 1>,
			                                           &operate_to<Way, 2> };
		static constexpr std::array<Run, 16> OPERATE_IF[5] = {
			operate_if_table<Way, 0>(CONDS), operate_if_table<Way, 1>(CONDS),
			operate_if_table<Way, 2>(CONDS), operate_if_table<Way, 3>(CONDS),
			operate_if_table<Way, 4>(CONDS)
		};
		static constexpr std::array<Run, 16> OPERATE_REGS_TO_IF[5] = {
			operate_regs_to_if_table<Way, 0>(CONDS), operate_regs_to_if_table<Way, 1>(CONDS),
			operate_regs_to_if_table<Way, 2>(CONDS), operate_regs_to_if_table<Way, 3>(CONDS),
			operate_regs_to_if_table<Way, 4>(CONDS)
		};
		static constexpr std::array<Run, 16> SHIFT_TO[2] = { shift_to_table<Way, 5>(CONDS),
			                                             shift_to_table<Way, 6>(CONDS) };
		static constexpr std::array<Run, 16> COMPARE_IF[2] = { compare_if_table<Way, 1>(CONDS),
			                                               compare_if_table<Way, 2>(CONDS) };
		static constexpr std::array<Run, 16> BRANCH = branch_table<Way>(CONDS);
		static constexpr std::array<Run, 16> MOVE[3][3] = {
			{ move_table<Way, REGS, REGS>(CONDS), move_table<Way, REGS, STORED>(CONDS),
			  move_table<Way, REGS, OTHER>(CONDS) },
			{ move_table<Way, TAKE, REGS>(CONDS), move_table<Way, TAKE, STORED>(CONDS),
			  move_table<Way, TAKE, OTHER>(CONDS) },
			{ move_table<Way, OTHER, OTHER>(CONDS), move_table<Way, OTHER, OTHER>(CONDS),
			  move_table<Way, OTHER, OTHER>(CONDS) },
		};
		static constexpr std::array<Run, 16> SWAP = swap_table<Way>(CONDS);
		static constexpr std::array<Run, 16> SWAP_PC = swap_pc_table<Way>(CONDS);
		static constexpr std::array<Run, 16> WIMP = wimp_table<Way>(CONDS);

		switch (o.form) {
		case Form::NOTHING:
			return &nothing<Way>;
		case Form::OPERATE:
			return OPERATE_IMM[o.set][o.op];
		case Form::SET:
			return &set<Way>;
		case Form::OPERATE_REGS:
		case Form::SHIFT_REGS:
			return OPERATE_REGS[o.set][o.op];
		case Form::COPY:
			return &copy<Way>;
		case Form::OPERATE_TO:
			return OPERATE_TO[o.op];
		case Form::OPERATE_IF:
			return OPERATE_IF[o.op][o.cond];
		case Form::OPERATE_REGS_TO_IF:
			return OPERATE_REGS_TO_IF[o.op][o.cond];
		case Form::SHIFT_TO:
			return SHIFT_TO[o.op - 5][o.cond];
		case Form::COMPARE:
			return o.op == 1 ? &compare<Way, 1> : &compare<Way, 2>;
		case Form::COMPARE_REGS:
			return o.op == 1 ? &compare_regs<Way, 1> : &compare_regs<Way, 2>;
		case Form::COMPARE_IF:
			return COMPARE_IF[o.op - 1][o.cond];
		case Form::BRANCH:
			return BRANCH[o.cond];
		case Form::MOVE: {
			const uint32_t from = o.unit >= D0 && o.unit <= A1 ? REGS : o.unit == RD ? TAKE : OTHER;
			const uint32_t to = o.to >= D0 && o.to <= A1 ? 0 : o.to == STORE ? 1 : 2;

			return MOVE[from][to][o.cond];
		}
		case Form::SWAP:
			return SWAP[o.cond];
		case Form::SWAP_PC:
			return SWAP_PC[o.cond];
		case Form::WIMP:
			return WIMP[o.cond];
		case Form::MOVL:
			return &movl<Way>;
		case Form::MEMORY:
			return &memory<Way>;
		case Form::INDEXED:
			return &indexed<Way>;
		case Form::REPEATED:
			return &repeated;
		case Form::PORT:
			return &port<Way>;
		case Form::FEED:
			return &feed<Way>;
		default:
			return &no;
		}
	}
};

/* The conditions proven by a use: EQ and NE; GE and LT, a pair that
 * clamps the TA's start to its 4,096 words (CMP x,#0x1000; ADDGE; ADDLT);
 * HI, unsigned greater, that runs the TA status loop while a status is
 * not the end of list (CMP 0x80000000,status); GT, that steps a tile's X
 * to the next row past the last (CMP x,last; ADDGT x,first; ADDGT y,1).
 * The others follow META's order by inference only, and wait for theirs */
bool Pmx1Core::known(uint32_t code)
{
	return code <= 2 || code == 9 || (code >= 11 && code <= 13);
}

/* A word's form and fields, checked once. The ALU: majors 0-6 on D0/D1
 * (ADD SUB AND OR XOR LSL LSR) and 9 on A0/A1 (ADD). With I and not C a
 * 16-bit immediate, with bit 2 no source: MOV, and SUB from 0 (INFERRED:
 * the fills of the tile tables with -1 and -4); the shifts by the count
 * at 13-9. With neither, the registers, bit 2 "move" (d = s2, INFERRED);
 * the shifts by a register, by the amounts seen, below 32. With C and I
 * and bit 5, to another unit (bit 0 is not understood, and changes
 * nothing seen): ADD, SUB, and AND (the pass number, TAFLAGS & 0xFF).
 * With C, conditional: an 8-bit immediate (I), or s2 to unit 8-5. LSL and
 * LSR with C and I: register 18-14 shifted by the count at 13-9 to unit
 * 8-5 . register 23-19, on condition 4-1 (INFERRED: its four uses, all
 * unconditional, go to D0, D1 and A1; bit 0 changes nothing seen). CMP (S
 * clear) or TST (S set) of a D0/D1 register: register 23-19 with a 16-bit
 * immediate (I), register 18-14 with register 13-9 (neither C nor I), or
 * register 18-14 with the 8-bit immediate at 13-6 on condition 4-1 (C and
 * I). F0: condition 4-1, target = its address + 16 + 4 * the signed word
 * displacement at 18-5. */
Pmx1Core::Op Pmx1Core::decode(uint32_t word)
{
	const uint32_t major = word >> 28, minor = (word >> 24) & 0xF;
	const bool s = (word >> 27) & 1, c = (word >> 26) & 1, i = (word >> 25) & 1, u = (word >> 24) & 1;
	Op o{};

	o.word = word;
	o.form = NO;
	o.cond = static_cast<uint8_t>((word >> 1) & 0xF);
	o.d = static_cast<uint8_t>((word >> 19) & 31);
	o.s1 = static_cast<uint8_t>((word >> 14) & 31);
	o.s2 = static_cast<uint8_t>((word >> 9) & 31);
	o.set = s;
	if (word == NOP || (word & ENTRY_MASK) == ENTRY) {
		o.form = NOTHING;
	} else if (major == 0xF && minor == 0) {
		if (!(word & 1) && !o.d && known(o.cond)) {
			o.form = BRANCH;
			o.imm = 16 + 4 * static_cast<uint32_t>(signed_field((word >> 5) & 0x3FFF, 14));
		}
	} else if (major == 0xA && minor == 4) {
		const uint32_t to = (word >> 10) & 0xF, from = (word >> 5) & 0xF;

		o.to = static_cast<uint8_t>(to);
		o.unit = static_cast<uint8_t>(from);
		if (!known(o.cond))
			return o;
		if ((word >> 9) & 1) {
			if (from >= D0 && from <= A1 && (to == PC || (to >= D0 && to <= A1)))
				o.form = to == PC ? SWAP_PC : SWAP;
		} else {
			o.form = MOVE;
		}
	} else if (major == 0xA && minor == 0xE) {
		o.form = MOVL;
	} else if (major == 0xA && minor == 5) {
		/* The implicit-register port: the value of unit 8-5, register
		 * 18-14, as a WAR word, on condition 4-1 */
		const uint32_t unit = (word >> 5) & 0xF;

		o.unit = static_cast<uint8_t>(unit);
		if (!o.d && !(word & 0x3E01) && unit >= D0 && unit <= A1 && known(o.cond))
			o.form = WIMP;
	} else if (major == 0xB && minor == 0xC) {
		o.form = REPEATED;
	} else if (major == 0xB && (minor == 0 || minor == 4)) {
		o.form = INDEXED;
	} else if ((major == 0xB || major == 0xC) && (minor == 2 || minor == 6 || (major == 0xB && minor == 7))) {
		o.form = MEMORY;
	} else if (major == 0xE && minor == 2) {
		o.form = PORT;
	} else if (major == 0xE && minor == 4) {
		o.form = FEED;
	} else if (major == 0x8) {
		o.op = (minor & 8) ? 2 : 1;
		o.unit = minor & 1 ? D1 : D0;
		o.set = 1;
		switch (minor & 6) {
		case 2:
			if (!(word & 4)) {
				o.form = COMPARE;
				o.s1 = o.d;
				o.imm = immediate16(word);
			}
			break;
		case 0:
			if (!(word & 0x00F801FF)) {
				o.form = COMPARE_REGS;
			}
			break;
		case 6:
			if (!(word & 0x00F80021) && known(o.cond)) {
				o.form = COMPARE_IF;
				o.imm = (word >> 6) & 0xFF;
			}
			break;
		default:
			break;
		}
	} else if (major <= 0x6 || major == 0x9) {
		const uint32_t op = major == 0x9 ? 0 : major;

		o.op = static_cast<uint8_t>(op);
		o.unit = major == 0x9 ? (u ? A1 : A0) : (u ? D1 : D0);
		if (s && (major == 0x9 || c))
			return o;
		if (c && i && op >= 5) {
			const uint32_t to = (word >> 5) & 0xF;

			if (known(o.cond) && to >= D0 && to <= A1) {
				o.form = SHIFT_TO;
				o.to = static_cast<uint8_t>(to);
				o.unit = u ? D1 : D0;
				o.imm = (word >> 9) & 31;
			}
		} else if (i && !c) {
			if (op >= 5) {
				if (!(word & 0x1FF)) {
					o.form = OPERATE;
					o.imm = o.s2;
				}
			} else if (word & 4) {
				if (op <= 1 && !s) {
					o.form = SET;
					o.imm = op == 0 ? immediate16(word) : 0 - immediate16(word);
				}
			} else {
				o.form = OPERATE;
				o.s1 = o.d;
				o.imm = immediate16(word);
			}
		} else if (!i && !c) {
			if (!(word & 0x1FB) && !((word & 4) && (s || op >= 5)))
				o.form = word & 4 ? COPY : op >= 5 ? SHIFT_REGS : OPERATE_REGS;
		} else if (i && (word & 0x20)) {
			if (op <= 2) {
				o.form = OPERATE_TO;
				o.to = static_cast<uint8_t>((word >> 1) & 0xF);
				o.imm = (word >> 6) & 0xFF;
			}
		} else if (!(word & 1) && op < 5 && known(o.cond)) {
			o.form = i ? OPERATE_IF : OPERATE_REGS_TO_IF;
			o.to = static_cast<uint8_t>((word >> 5) & 0xF);
			o.imm = (word >> 6) & 0xFF;
		}
	}
	o.run = Pmx1Ops::run_of<Pmx1Ops::Chained>(o);
	o.step = Pmx1Ops::run_of<Pmx1Ops::Called>(o);
	return o;
}

Pmx1Core::Done Pmx1Core::execute(unsigned thread, const Op &o)
{
	return o.step(*this, &o, thread);
}

/* A4: unit 13-10 . register 23-19 gets unit 8-5 . register 18-14, on
 * condition 4-1; the source 6 takes the oldest read, 8.2 the implicit
 * register read last, 10.n reads thread n's kicker (waiting until it is
 * kicked) and 10.7 the running thread's, without waiting. With bit 9 the
 * two registers are exchanged; to a PC, the jump leaves its return in the
 * source, the address after its delay slots less 4 (INFERRED: the TA
 * job's coroutines return so to three callers). This is a move whose
 * condition holds. */
Pmx1Core::Done Pmx1Core::move(unsigned thread, const Op &o)
{
	uint32_t value = 0;

	switch (o.unit) {
	case RD:
		if (!take(thread, value))
			return Done::NO;
		break;
	case PORTS:
		if (o.s1 != IMPLICIT_ANSWER)
			return Done::NO;
		value = m_s.implicit_read[thread];
		break;
	case TIMER:
		/* HEURISTIC: a free-running count of the core's cycles; the TA
		 * job and the render only put it in their reports */
		if (o.s1 != 0)
			return Done::NO;
		value = m_s.cycles;
		break;
	case KICKER: {
		if (o.s1 >= THREADS && o.s1 != 7)
			return Done::NO;
		const unsigned which = o.s1 == 7 ? thread : o.s1;

		value = m_s.async[which] << 15 | (m_s.kicks[which] & 0x7FFF);
		if (o.s1 != 7) {
			if (!value)
				return Done::WAIT;
			m_s.kicks[which] -= m_s.kicks[which] > 0;
			m_s.async[which] = 0;
		}
		break;
	}
	default:
		if (o.unit < D0 || o.unit > A1)
			return Done::NO;
		value = m_s.regs[o.unit - 1][o.s1];
		break;
	}
	/* INFERRED: a store through U0.26 writes where SETD [A1.10] does on
	 * another path, and one through U0.5 where a B0 store does */
	if (o.to == STORE)
		return store(m_s.regs[(o.d < 16 ? A0 : A1) - 1][o.d & 15], value) ? Done::YES : Done::NO;
	return write_unit(thread, o.to, o.d, value) ? Done::YES : Done::NO;
}

/* AE: one read (RD.0) or TA status (U8.4) at 8-5 . 23-19, its low word to
 * D0 . 18-14 and its high word to D1 . 13-9 */
Pmx1Core::Done Pmx1Core::movl(unsigned thread, uint32_t word)
{
	const uint32_t from = (word >> 5) & 0xF, source = (word >> 19) & 31;
	uint32_t value = 0, high = 0;

	if (!(from == RD && source == 0 && take(thread, value, &high)) &&
	    !(from == PORTS && source == TA_STATUS && m_ta.status(value, high)))
		return Done::NO;
	m_s.regs[D0 - 1][(word >> 14) & 31] = value;
	m_s.regs[D1 - 1][(word >> 9) & 31] = high;
	return Done::YES;
}

/* B2 SETD and C2 GETD: the address is A0 or A1 (bit 18) . register 17-14
 * plus 4 * the word offset at 13-8, unsigned (stores and their reloads
 * agree only so); B6 and C6 add it to the register first and use the sum.
 * The data is unit 4-1 . register 23-19, a take when the unit is 6. GETD
 * queues its read, then takes the oldest one. B7 SETL is B6 storing a
 * whole take, 8 bytes (INFERRED: 16 of them copy the 128 bytes of the
 * background object, and the 3D registers' image 16 bytes a turn). */
Pmx1Core::Done Pmx1Core::memory(unsigned thread, uint32_t word)
{
	const uint32_t base = (word >> 18) & 1 ? A1 : A0, unit = (word >> 1) & 0xF, d = (word >> 19) & 31;
	const uint32_t minor = (word >> 24) & 0xF;
	uint32_t &reg = m_s.regs[base - 1][(word >> 14) & 15];
	const uint32_t address = reg + 4 * ((word >> 8) & 0x3F);
	uint32_t value = 0, high = 0;

	if ((word & 0xE1) != 0x20 || unit < D0 || (unit > A1 && unit != RD))
		return Done::NO;
	if (minor == 6 || minor == 7)
		reg = address;
	if (minor == 7)
		return unit == RD && take(thread, value, &high) && store(address, value) && store(address + 4, high)
		               ? Done::YES
		               : Done::NO;
	if (word >> 28 == 0xC) {
		if (unit == RD || !queue_read(thread, address) || !take(thread, value))
			return Done::NO;
		m_s.regs[unit - 1][d] = value;
		return Done::YES;
	}
	if (unit != RD)
		value = m_s.regs[unit - 1][d];
	else if (!take(thread, value))
		return Done::NO;
	return store(address, value) ? Done::YES : Done::NO;
}

/* B0: [Ab.n + Ab.m] := unit 4-1 . register 23-19 (b bit 18, n 17-14, m
 * 12-9); B4 the same, Ab.n += Ab.m first (INFERRED: the TA status loop
 * stores so the first pointer of a new list block, at the list base plus
 * the block, and the TA job the region headers, a pass's stride apart) */
Pmx1Core::Done Pmx1Core::indexed(unsigned /*thread*/, uint32_t word)
{
	uint32_t *const regs = m_s.regs[((word >> 18) & 1 ? A1 : A0) - 1];
	const uint32_t unit = (word >> 1) & 0xF, address = regs[(word >> 14) & 15] + regs[(word >> 9) & 15];

	if ((word & 0x21E1) != 0x0020 || unit < D0 || unit > A1)
		return Done::NO;
	if ((word >> 24) & 4)
		regs[(word >> 14) & 15] = address;
	return store(address, m_s.regs[unit - 1][(word >> 19) & 31]) ? Done::YES : Done::NO;
}

/* BC: a store repeated, Ab.n += Ab.m (b bit 18, n 17-14, m 12-9) then
 * [Ab.n] := D1 . 23-19, one a cycle, the thread staying on it. INFERRED:
 * as many times as the D0 register compared just before with -2, -1 and
 * 0, plus 3; the five uses fill the tile tables and the region headers,
 * whose sizes less 3 that register holds. */
Pmx1Core::Done Pmx1Core::repeated(unsigned thread, uint32_t word, uint32_t at)
{
	uint32_t &left = m_s.repeat[thread];

	if ((word & 0x21FF) != 0x0059 || m_s.slots[thread])
		return Done::NO;
	if (!left) {
		static constexpr uint32_t PRELUDE[] = { 0x8207FFF2, 0x8207FFFA, 0x82000000 };
		uint32_t words[3] = {};

		for (unsigned i = 0; i < 3; i++) {
			const uint32_t offset = (at - 12 + 4 * i) & FRAME_BUFFER_MASK;

			if (offset > m_vram.size() - 4)
				return Done::NO;
			words[i] = le32(m_vram.data() + offset);
			if ((words[i] & 0xFF07FFFF) != PRELUDE[i] || ((words[i] ^ words[0]) & 0x00F80000))
				return Done::NO;
		}

		const int32_t count = static_cast<int32_t>(m_s.regs[D0 - 1][(words[0] >> 19) & 31]) + 3;

		if (count < 1)
			return Done::NO;
		left = static_cast<uint32_t>(count);
	}

	uint32_t *const regs = m_s.regs[((word >> 18) & 1 ? A1 : A0) - 1];
	uint32_t &address = regs[(word >> 14) & 15];

	address += regs[(word >> 9) & 15];
	if (!store(address, m_s.regs[D1 - 1][(word >> 19) & 31]))
		return Done::NO;
	if (--left)
		m_s.next[thread] = at;
	return Done::YES;
}

/* E2: register 23-19 of D0 (bits 4-3 = 3) or A0 (2) written (bit 5) to
 * register 13-6 of the port; bits 18-14 are not understood. With bits 2-0
 * = 100 and the rest clear, a TA instruction: the low word A0 . 23-19,
 * the high word A1 . 18-14 (the six uses send the low words the TA
 * specification gives its four instructions). */
Pmx1Core::Done Pmx1Core::port(unsigned /*thread*/, uint32_t word)
{
	const uint32_t which = (word >> 6) & 0xFF, kind = (word >> 3) & 3;

	if ((word & 0x3FFF) == 4) {
		m_ta.instruction(m_s.regs[A0 - 1][(word >> 19) & 31], m_s.regs[A1 - 1][(word >> 14) & 31]);
		return Done::YES;
	}
	if ((word & 7) || !(word & 0x20) || kind < 2)
		return Done::NO;

	const uint32_t value = m_s.regs[(kind == 3 ? D0 : A0) - 1][(word >> 19) & 31];

	if (which == PORT_DATA) {
		m_s.port_data = value;
		return Done::YES;
	}
	if (which == PORT_ADDRESS)
		return store(value, m_s.port_data) ? Done::YES : Done::NO;
	return Done::NO;
}

bool Pmx1Core::write_unit(unsigned thread, unsigned unit, unsigned reg, uint32_t value)
{
	if (unit >= D0 && unit <= A1) {
		m_s.regs[unit - 1][reg] = value;
		return true;
	}
	/* PC.n = X: thread n continues at X + 4, the running one after its
	 * delay slots */
	if (unit == PC && reg < THREADS) {
		if (reg == thread)
			return jump(thread, value + 4);
		m_s.next[reg] = value + 4;
		return true;
	}
	return unit == RD && reg == 0 && queue_read(thread, value);
}

/* A jump issued in the delay slots of another is not known. INFERRED: the
 * real-time threads jump at once. Block 8 of the games (the DAC thread)
 * makes sense only so: its branches land on the reads their code takes,
 * its loop's return to T3LINK is the target of the dwStop test and not in
 * the slots of its last jump, and its blocks end on a jump followed by the
 * fill 0xDEADBEEF (so do the real-time fragments of the Neon 250 driver).
 * Thread 0's code needs its four slots (most hold NOPs). A separate
 * instruction cache for real-time code ("KM_FB_ICACHE2 HEAP -- holds
 * real-time code", Highlander Kernel Manager API) may be why. */
bool Pmx1Core::jump(unsigned thread, uint32_t to)
{
	if (thread) {
		m_s.next[thread] = to;
		return true;
	}
	if (m_s.slots[thread])
		return false;
	m_s.slots[thread] = DELAY_SLOTS;
	m_s.target[thread] = to;
	return true;
}

/* A read is queued as two words, taken whole or by its first word.
 * HEURISTIC: the words are read when queued, the hardware returning them
 * a few cycles later */
bool Pmx1Core::queue_read(unsigned thread, uint32_t address)
{
	uint32_t offset = 0;

	if (m_s.queue_size[thread] == QUEUE || !frame_buffer(address, offset) || offset > m_vram.size() - 8)
		return false;

	uint32_t *const entry = m_s.queued[thread][(m_s.queue_first[thread] + m_s.queue_size[thread]) % QUEUE];

	entry[0] = le32(m_vram.data() + offset);
	entry[1] = le32(m_vram.data() + offset + 4);
	m_s.queue_size[thread]++;
	return true;
}

bool Pmx1Core::take(unsigned thread, uint32_t &low, uint32_t *high)
{
	if (!m_s.queue_size[thread])
		return false;

	const uint32_t *const entry = m_s.queued[thread][m_s.queue_first[thread]];

	low = entry[0];
	if (high)
		*high = entry[1];
	m_s.queue_first[thread] = (m_s.queue_first[thread] + 1) % QUEUE;
	m_s.queue_size[thread]--;
	return true;
}

/* Data accesses go to the frame buffer when the top 6 bits of their
 * address are its offset, to the host interface otherwise (not emulated) */
bool Pmx1Core::frame_buffer(uint32_t address, uint32_t &offset) const
{
	offset = address & FRAME_BUFFER_MASK;
	return address >> 26 == (m_s.implicit[FB_OFFSET] & 0x3F) && !(address & 3) && offset <= m_vram.size() - 4;
}

bool Pmx1Core::store(uint32_t address, uint32_t value)
{
	uint32_t offset = 0;

	if (!frame_buffer(address, offset))
		return false;
	put_le32(m_vram.data() + offset, value);
	/* written(offset, 4) for a word, which stays in one page and one line */
	if (!m_page_written[offset >> PAGE_BITS]) {
		m_page_written[offset >> PAGE_BITS] = 1;
		m_pages_written.push_back(offset >> PAGE_BITS);
	}
	if (m_code_line[offset >> LINE_BITS]) [[unlikely]] {
		m_epoch++;
		m_leave = true;
	}
	return true;
}

/* E4: the DAC feed's port (bits 2-0 = 3; 1 is the Video In port's, not
 * emulated). The oldest read goes to the DAC feed, and a read of A0 .
 * 23-19 (A1 from 16) is queued, which the code takes next (INFERRED: block
 * 8 sends so the display lists' 64-bit words, the format of the guide's DAC
 * Feed / Primary Core Interface, one after the other). The DAC feed takes a
 * Load Register at once, a Span when it is done with the last (guide
 * p. 98-100): the thread waits until then. INFERRED: the port holds
 * nothing more. So block 8's frame (C1IRQ0, the VSYNC count, the read of
 * dwFlip) starts as the bottom blanking, its last span, starts: the
 * "incrementing VSYNC count" and the flips that "take affect ... at vSync
 * time" of the RTComms description (PMX Device Driver Services API, 4.1).
 * With one instruction held it would start 480 lines before; the games
 * run alike with 0 to 16 held, their pictures a frame apart at times. */
Pmx1Core::Done Pmx1Core::feed(unsigned thread, uint32_t word)
{
	const uint32_t d = (word >> 19) & 31;
	uint32_t low = 0, high = 0;

	if ((word & 0x3FFF) != 0x000B)
		return Done::NO;
	if (m_s.dac_left) {
		m_s.dac_waiting = thread + 1;
		return Done::WAIT;
	}
	if (!take(thread, low, &high))
		return Done::NO;
	if (high >> 30 == 1) {
		m_s.dac_registers[high & 3] = low;
	} else if (high >> 30 == 0) {
		const uint32_t clocks = (((high >> 10) & 0x7FF) + 1) * span_line(high);

		m_s.dac_left = clocks - std::min(m_s.dac_spare, clocks);
		m_s.dac_spare = 0;
		m_s.dac_frames += (high >> 29) & 1;
	}
	return queue_read(thread, m_s.regs[(d < 16 ? A0 : A1) - 1][d & 15]) ? Done::YES : Done::NO;
}

bool Pmx1Core::dac_blanked() const
{
	return !(m_s.implicit[DAC_CONTROL] & DAC_BLNK);
}

/* The pixel clocks of a line of a span: the HSYNC's front porch, width and
 * back porch when its display list word (DAC_HS) asks for one, then its
 * pixels, from its width in 64-bit words, the byte of its first and of
 * its last pixel (the penultimate word's in 24 bits, guide p. 98) and its
 * pixel format, through the scaler if it is on. INFERRED: each porch and
 * the width last one clock more than written; then a frame of the games
 * (804 clocks a line, 528 lines, 25.21 MHz) lasts as long as their TMU1's
 * frame (59.3916 against 59.3910 Hz), against 59.61 Hz as written */
uint32_t Pmx1Core::span_line(uint32_t high) const
{
	static constexpr uint32_t BYTES[8] = { 1, 2, 2, 3, 4, 2, 3, 2 };
	const uint32_t page = m_s.dac_registers[2] & 0xFFFFFF00, at = (page + 8 * ((high >> 23) & 31)) & FRAME_BUFFER_MASK;
	const uint32_t dlw_low = at <= m_vram.size() - 8 ? le32(m_vram.data() + at) : 0;
	const uint32_t dlw_high = at <= m_vram.size() - 8 ? le32(m_vram.data() + at + 4) : 0;
	const uint32_t bytes = BYTES[(dlw_high >> 8) & 7], words = high & 0x3FF;
	const uint32_t end = (dlw_low >> 24) & 7, last = 8 * (bytes == 3 && end >= 6 && words ? words - 1 : words) + end;
	const uint32_t first = (dlw_low >> 20) & 7;
	uint32_t pixels = (last > first ? last - first : 0) / bytes + 1;

	if ((dlw_high >> 5) & 1) {
		const uint32_t factor = dlw_low & 0x7FF;

		if (factor)
			pixels = (pixels - 1) * ((0x400000u / factor) >> 11) + ((dlw_low >> 12) & 15);
	}
	return pixels + ((dlw_high >> 7) & 1 ? m_s.implicit[HS_FRONT] + m_s.implicit[HS_BACK] + m_s.implicit[HS_WIDTH] + 3 : 0);
}

/* The DAC feed plays for 1/per_second of a second, at the DAC PLL's pixel
 * clock */
void Pmx1Core::run_dac(uint32_t per_second)
{
	/* The clocks of a slice, whole and the fraction of per_clock left,
	 * worked out again when the PLL changes */
	if (m_dac_pll != m_s.dac_pll || m_dac_per_second != per_second) {
		static constexpr uint32_t P[8] = { 1, 2, 2, 4, 2, 4, 4, 8 };
		const uint32_t m = (m_s.dac_pll & 31) + 1, n = ((m_s.dac_pll >> 5) & 0x7F) + 1;
		const uint64_t per_clock = uint64_t{ m } * P[(m_s.dac_pll >> 12) & 7] * per_second;

		m_dac_pll = m_s.dac_pll;
		m_dac_per_second = per_second;
		m_dac_per_clock = static_cast<uint32_t>(per_clock);
		m_dac_whole = static_cast<uint32_t>(DAC_FREF * n / per_clock);
		m_dac_part = static_cast<uint32_t>(DAC_FREF * n % per_clock);
	}

	uint32_t clocks = m_dac_whole;

	m_s.dac_clock += m_dac_part;
	if (m_s.dac_clock >= m_dac_per_clock) {
		m_s.dac_clock -= m_dac_per_clock;
		clocks++;
	}
	/* The clocks of the slice after the span's end start the next, which
	 * the thread gives in the next slice */
	m_s.dac_spare = clocks > m_s.dac_left ? clocks - m_s.dac_left : 0;
	m_s.dac_left -= std::min(clocks, m_s.dac_left);
	if (!m_s.dac_left && m_s.dac_waiting) {
		m_s.waiting[m_s.dac_waiting - 1] = 0;
		m_s.dac_waiting = 0;
		m_others = true;
	}
}

/* A WAR word from the core: a write, or a read (bits 31-26 clear) that
 * answers in U8.2 in the format of the host's RARs: the read word's high
 * half, the register in the low half. The ring offset and the interrupt
 * status it reads so use the low half only. */
void Pmx1Core::port_write(unsigned thread, uint32_t value, uint32_t at)
{
	const uint32_t addr = (value >> 16) & (IMPLICIT - 1);

	if (value >> 30 == 1) {
		implicit_write(addr, value & 0xFFFF, at);
	} else if (value >> 26 == 0) {
		m_s.implicit_read[thread] = (value & 0xFFFF0000) | m_s.implicit[addr];
	} else {
		stop(thread, at, value, "neither an implicit write nor a read");
	}
}

} /* namespace odin */
