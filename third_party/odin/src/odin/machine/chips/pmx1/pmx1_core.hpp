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

#ifndef ODIN_PMX1_CORE_HPP
#define ODIN_PMX1_CORE_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "odin/machine/chips/pmx1/pmx1_host.hpp"
#include "odin/machine/chips/pmx1/pmx1_ta.hpp"

namespace odin {

/* The primary core of the PMX1 (PowerVR 250): a microcoded engine of four
 * threads (0 the host's commands, 3 the DAC feed) that fetches its
 * instructions from video RAM, and the implicit
 * registers it shares with the host. Built from the chip's programming
 * guide and from the code fragments of the Neon 250 driver, which name the
 * fields and the units of its words.
 *
 * Emulated so far: thread 0 from the host's soft reset, through the
 * start-up code the games upload, its command loop, the asynchronous
 * commands and all the commands of the buffer: the 3D init, the render
 * and the TA job, which feeds and reads the tile accelerator (Pmx1Ta);
 * the 3D core's registers, Interrupt Status and Mask, the soft
 * interrupts. Threads 1 and 3 from their start-up, waiting for their
 * kicker and taking its kicks; thread 3's display loop, which feeds the
 * DAC feed its display list each frame and raises C1IRQ0; thread 2 is
 * not run. The DAC feed only paces its port (no picture). A thread stops at
 * the first instruction outside the forms known, and says so once. */
class Pmx1Core {
public:
	/* HEURISTIC QUEUE: a thread's read queue holds at least 10 reads (the
	 * games queue that many), not known deeper; the translated code wraps
	 * it with a mask (pmx1_host.cpp) */
	static constexpr unsigned THREADS = 4, IMPLICIT = 0x400, QUEUE = 16;
	static_assert(QUEUE < 0x80 && (QUEUE & (QUEUE - 1)) == 0, "a power of two that fits a byte");
	/* D0 and D1 have 32 registers, A0 and A1 16 */
	static constexpr unsigned UNITS = 4, REGISTERS = 32;

	/* What a save state keeps, in 32-bit words: no padding */
	struct State {
		uint32_t next[THREADS];		/* the address each fetches next */
		uint32_t target[THREADS];	/* where a jump goes after its slots */
		uint32_t slots[THREADS];	/* delay slots left before it */
		uint32_t stopped[THREADS];
		uint32_t waiting[THREADS];	/* for a kick */
		uint32_t flags[THREADS];	/* N, Z, C, V */
		uint32_t kicks[THREADS], async[THREADS];	/* the kicker */
		/* The reads each thread queued, oldest first: two words each */
		uint32_t queued[THREADS][QUEUE][2];
		uint32_t queue_first[THREADS], queue_size[THREADS];
		uint32_t regs[UNITS][REGISTERS];	/* D0, D1, A0, A1 */
		uint32_t implicit[IMPLICIT];
		uint32_t rar[2];		/* Read Access Registers 1 and 2 */
		uint32_t port_data;		/* register 0x65 of the E2 port */
		uint32_t implicit_read[THREADS];	/* U8.2: the implicit register read last */
		uint32_t repeat[THREADS];	/* stores left of a repeated store */
		uint32_t cycles;		/* run, U11.0 */
		/* The 3D core's registers, by index / 4, written through the
		 * implicit Index Address and Index Data; the low half of a
		 * 32-bit write, and whether it was given */
		uint32_t regs_3d[0x200];
		uint32_t index_low, index_half;
		uint32_t next_interrupt;	/* status bits set when it next runs */
		/* The DAC feed: the pixel clocks left of the span it plays, those
		 * of the last slice past its end, the remainder of its clock, the
		 * DAC PLL word in use, its Load Register values, the thread (+ 1)
		 * waiting on its port */
		uint32_t dac_left, dac_spare, dac_clock, dac_pll;
		uint32_t dac_frames;		/* its starts of frame (DAC_SOF) */
		uint32_t dac_registers[4];
		uint32_t dac_waiting;
		Pmx1Ta::State ta;
	};

	/* `vram` is the frame buffer; `written` is told what the core writes
	 * there */
	Pmx1Core(std::span<uint8_t> vram, std::function<void(uint32_t offset, uint32_t size)> written);
	/* The TA keeps its state in the core's */
	Pmx1Core(const Pmx1Core &) = delete;
	Pmx1Core &operator=(const Pmx1Core &) = delete;

	/* A hard reset */
	void reset();
	/* The immediate registers of the host this class answers, by offset */
	static bool host_register(uint32_t offset);
	uint32_t host_read(uint32_t offset) const;
	void host_write(uint32_t offset, uint32_t value);
	/* A write to Thread Kicker n (by the host at 0xB0 + 4n, or to the
	 * implicit 0x100 + n): the low 15 bits are added to its count, bit
	 * 15 (an asynchronous command) is ORed */
	void kick(unsigned thread, uint32_t value);
	/* Runs the threads for that many cycles of the core */
	void run(int32_t cycles);
	/* The DAC feed plays for 1/per_second of a second */
	void run_dac(uint32_t per_second);
	/* The starts of frame the DAC feed has taken */
	uint32_t dac_frames() const { return m_s.dac_frames; }
	/* -v: the implicit registers it does not emulate and thread 2 told on
	 * stderr, once each (the games' start-up writes them all) */
	void set_verbose(bool verbose) { m_verbose = verbose; }
	/* The DAC puts out black (DAC Control, DAC_BLNK at 0) */
	bool dac_blanked() const;
	/* `written` is told of each write to a 3D register (its index, a
	 * multiple of 4), `interrupt` of the interrupt line to the host */
	void set_3d(std::function<void(uint32_t index, uint32_t value)> written) { m_3d = std::move(written); }
	void set_interrupt(std::function<void(bool on)> line) { m_interrupt = std::move(line); }
	uint32_t register_3d(uint32_t index) const { return m_s.regs_3d[(index >> 2) & 0x1FF]; }
	std::span<const uint32_t, 0x200> registers_3d() const { return m_s.regs_3d; }
	/* Sets bits of Interrupt Status (the 3D core's PVR_IRQ, 7): now, or
	 * when the core next runs, so that it sees them before the host */
	void interrupt(uint32_t bits);
	void interrupt_next(uint32_t bits) { m_s.next_interrupt |= bits; }
	/* Whether the blocks run as host code (Pmx1Host) where the host
	 * allows it, rather than from their decoded ops: off until set */
	void set_translate(bool on);

	const State &state() const { return m_s; }
	void set_state(const State &state);

private:
	friend struct Pmx1Ops;
	/* LEAVE: done, and the block must end after it */
	enum class Done { YES, NO, WAIT, LEAVE };
	/* A word decoded once: its form, its fields, and the word, to know it
	 * is still the one at its address */
	struct Op {
		using Run = Done (*)(Pmx1Core &, const Op *, unsigned thread);

		Run run, step;			/* the form made for its fields: in a block, alone */
		uint32_t word, imm, at;
		uint8_t form, cond, op, unit;	/* unit: its own, or the source */
		uint8_t d, s1, s2, to;
		uint8_t set;			/* the flags */
	};
	static constexpr uint32_t OPS = 1u << 16;	/* 256 KB of code */
	/* Consecutive words decoded once, run one after the other without
	 * reading the memory again: to the end of the delay slots of their
	 * first jump, or before a form the run word by word does (a repeated
	 * store, a wait for a kicker, a read of the timer, a form not known)
	 * or a second jump in those slots, the slots left then run word by
	 * word. Compared with the memory
	 * again in each run, the host writing only between two, and when
	 * the core or the TA writes a line of 64 bytes that holds code. */
	struct Block {
		uint32_t at, first, count;	/* its address, its ops in m_block_ops */
		uint32_t jump;			/* the op that may issue its jump, NO_JUMP */
		uint64_t checked;		/* m_epoch when compared */
		/* The blocks it went on to, rank + 1, and their addresses */
		uint32_t link[2], link_at[2];
		uint32_t host;			/* the place of its host code, 0 for none */
	};
	/* The demos of the games make 19,000 ops of blocks at most */
	static constexpr uint32_t BLOCK_MAX = 64, BLOCK_OPS_MAX = 1u << 16, LINE_BITS = 6, NO_JUMP = ~0u;

	bool runnable(unsigned thread) const;
	unsigned issuing() const;
	uint32_t run_thread0(uint32_t &last, uint32_t cycles);
	void step(unsigned thread);
	const Block *block(uint32_t at);
	const Block *follow(uint32_t last, uint32_t at);
	uint32_t run_block(const Block &b, uint32_t cycles);
	uint32_t run_repeated(uint32_t cycles);
	uint32_t run_chained(const Block &b);
	uint32_t left(const Block &b, uint32_t k, Done done);
	uint32_t host_block(uint32_t rank);
	uint32_t translate(uint32_t rank);
	bool in_place(const Op &o, uint32_t k);
	uint32_t run_host(uint32_t place, uint32_t cycles);
	uint32_t host_at(uint32_t at) const;
	static uintptr_t linked(Pmx1Core &c);
	void drop_host();
	/* The op after the last of a block */
	static Done end(Pmx1Core &, const Op *, unsigned) { return Done::YES; }
	static bool ends_block(const Op &o);
	static bool jumps(const Op &o);
	static Op decode(uint32_t word);
	static bool known(uint32_t code);
	Done execute(unsigned thread, const Op &o);
	Done move(unsigned thread, const Op &o);
	Done movl(unsigned thread, uint32_t word);
	Done memory(unsigned thread, uint32_t word);
	Done repeated(unsigned thread, uint32_t word, uint32_t at);
	Done indexed(unsigned thread, uint32_t word);
	Done port(unsigned thread, uint32_t word);
	Done feed(unsigned thread, uint32_t word);
	uint32_t span_line(uint32_t high) const;
	bool write_unit(unsigned thread, unsigned unit, unsigned reg, uint32_t value);
	bool jump(unsigned thread, uint32_t to);
	bool queue_read(unsigned thread, uint32_t address);
	bool take(unsigned thread, uint32_t &low, uint32_t *high = nullptr);
	bool frame_buffer(uint32_t address, uint32_t &offset) const;
	bool store(uint32_t address, uint32_t value);
	void port_write(unsigned thread, uint32_t value, uint32_t at);
	void implicit_write(uint32_t addr, uint32_t value, uint32_t by);
	void index_data(uint32_t data);
	void interrupt_line();
	void stop(unsigned thread, uint32_t at, uint32_t word, const char *why);
	void written(uint32_t offset, uint32_t size);

	std::span<uint8_t> m_vram;
	std::function<void(uint32_t, uint32_t)> m_written;
	/* The pages of 4 KB written in a run, told at its end: the host does
	 * not run meanwhile */
	std::vector<uint8_t> m_page_written;
	std::vector<uint32_t> m_pages_written;
	std::vector<Op> m_ops = std::vector<Op>(OPS);
	std::vector<Block> m_blocks;
	/* Reserved once: the host code calls its ops where they are */
	std::vector<Op> m_block_ops;
	std::vector<uint32_t> m_block_at = std::vector<uint32_t>(OPS);	/* rank + 1 in m_blocks */
	std::vector<uint8_t> m_code_line;
	/* run_dac(): the PLL word and slices it last worked for, and a slice's
	 * clocks, whole and the part of per_clock */
	uint32_t m_dac_pll = ~0u, m_dac_per_second = 0, m_dac_per_clock = 1, m_dac_whole = 0, m_dac_part = 0;
	uint64_t m_epoch = 1;
	bool m_leave = false;	/* the block run must end after this op */
	/* Whether a thread other than 0 may be able to run: set when one is
	 * enabled or kicked, cleared when none can */
	bool m_others = true;
	const Op *m_exit = nullptr;	/* the op a chained block ended at, before its end */
	std::unique_ptr<Pmx1Host> m_host;
	bool m_translate = false;
	/* The jumps of the host code to a block not written yet, by its
	 * address */
	std::unordered_map<uint32_t, std::vector<uint32_t>> m_unlinked;
	/* Where the host code left a block before its end (Pmx1Host::Places):
	 * its rank + 1, 0 for none */
	struct HostExit {
		uint32_t block, op, done, stale;
	} m_host_exit{};
	std::function<void(uint32_t, uint32_t)> m_3d;
	std::function<void(bool)> m_interrupt;
	State m_s{};
	Pmx1Ta m_ta;
	/* Implicit registers and threads already seen not emulated (told with
	 * -v) */
	std::array<bool, IMPLICIT> m_said{};
	bool m_said_war = false;
	/* Implicit registers that do nothing yet, seen before: a write is kept */
	std::array<bool, IMPLICIT> m_plain{};
	std::array<bool, THREADS> m_said_enabled{};
	/* The next byte of the gamma table, 0 to 767 (red, green, blue), and
	 * whether one off the ramp was said: for that message only */
	uint32_t m_lut_at = 0;
	bool m_said_lut = false;
	bool m_verbose = false;
};

} /* namespace odin */

#endif
