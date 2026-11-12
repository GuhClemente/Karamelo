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

#ifndef ODIN_PMX1_HOST_HPP
#define ODIN_PMX1_HOST_HPP

#include <cstdint>
#include <vector>

#include "odin/machine/core/host_code.hpp"

namespace odin {

class Pmx1Core;

/* Host code for the blocks of the PMX1's primary core, which does what
 * Pmx1Core::run_chained() does: each op is checked against video RAM, then
 * done in place or called; at the end of a block the next address is set,
 * and the run goes on to the block there, directly once it is written.
 *
 * The ops run are counted in a register of the host, a block at a time,
 * and returned. A block left before its end (an op that did not finish,
 * a word that is not the one it was written for) says where in the core,
 * and Pmx1Core brings the thread to where step() would have left it.
 *
 * Only this class knows the host: Pmx1Core::translate() says what a block
 * does. Written for x86-64; elsewhere usable() is false and the core keeps
 * to its decoded blocks. */
class Pmx1Host : public HostCode {
public:
	/* The host code of the block at the next address, or 0 for the way
	 * back to C++ */
	using Link = uintptr_t (*)(Pmx1Core &);
	/* Where the host code finds the state of the core, in bytes from it:
	 * thread 0's flags, delay slots, jump target and next address, and the
	 * words a block that was left writes: its rank + 1, the op, what that
	 * op returned (Pmx1Core::Done) and whether its word changed; the
	 * implicit registers, those that only keep what is written (a byte
	 * each), the answer of a read; thread 0's read queue (its size, its
	 * oldest, its entries of two words); the implicit FB_OFFSET. Video RAM
	 * holds `vram_size` bytes; `pages` and `lines` have a byte for each of
	 * its pages of 4 KB written in the run and each line of 64 bytes that
	 * holds code. */
	struct Places {
		int32_t flags, slots, target, next, exit_block, exit_op, exit_done, exit_stale;
		int32_t implicit, plain, answer, queue_size, queue_first, queued, fb_offset;
		uint32_t vram_size;
		const uint8_t *pages, *lines;
	};

	Pmx1Host(const Places &places, Link link);

	/* Forgets every block. */
	void clear();
	/* Room left for the longest block there is? */
	bool room() const { return m_used + LONGEST <= m_size; }

	/* Starts the block of rank `rank` (+ 1), of `count` ops; returns its
	 * place, never 0. */
	uint32_t begin(uint32_t rank, uint32_t count);
	/* Back to C++ before op `k` when video RAM holds another word than
	 * `value` at `offset`. */
	void check(uint32_t offset, uint32_t value, uint32_t k);
	/* Op `k` called: does(core, op, 0), back to C++ when it does not
	 * return Done::YES. */
	void call(uintptr_t does, const void *op, uint32_t k);
	/* A jump past the op unless the flags pass: `pass` has the bit of
	 * each value of NZCV they pass with. */
	Jump unless(uint16_t pass);

	/* The steps of an op done in place, on the core's registers where they
	 * are: the value at `place` loaded, the operand loaded, the result
	 * stored. `op` is the core's ALU operation (0 ADD, 1 SUB, 2 AND, 3 OR,
	 * 4 XOR, 5 LSL, 6 LSR). */
	void load(int32_t place);
	void load_operand(int32_t place);
	void store(int32_t place);
	void store_value(int32_t place, uint32_t value);
	void operate(uint32_t op, uint32_t value);
	void operate_regs(uint32_t op);
	/* CMP (1) or TST (2) */
	void compare(uint32_t op, uint32_t value);
	void compare_regs(uint32_t op);
	/* The flags of the op just done: NZCV after ADD and SUB (C no
	 * borrow), N and Z with C and V kept after AND, OR and XOR, cleared
	 * after a shift */
	void flags(uint32_t op);
	void swap(int32_t a, int32_t b);
	/* A jump when the operand is past 31, for a shift */
	Jump unless_count();
	/* Where `odd` goes: op `k` called, then on after the op in place */
	void otherwise(Jump odd, uintptr_t does, const void *op, uint32_t k);
	/* A branch to `target` after `slots` delay slots: op `k` called when
	 * a jump is already on its way, which it refuses */
	void branch(uint32_t target, uint32_t slots, uintptr_t does, const void *op, uint32_t k);
	/* The ways an op takes most often, done in place; op `k` called for
	 * the others. WIMP of the value at `value`: a write to a register that
	 * only keeps it, or a read. The word at `value` stored at the address
	 * at `address` plus `add` when its page is already told and holds no
	 * code, that address kept at `address` first with `back`. GETD: that
	 * address queued as a read, then the oldest taken to `to`. The oldest
	 * read taken, to `to` when the flags pass (`pass`, as unless()), the
	 * queue not empty. The result of the op just done queued as the
	 * address of a read, the queue not full. */
	void wimp(int32_t value, uintptr_t does, const void *op, uint32_t k);
	void store_word(int32_t address, uint32_t add, bool back, int32_t value, uintptr_t does, const void *op,
	                uint32_t k);
	void getd(int32_t address, uint32_t add, bool back, int32_t to, uintptr_t does, const void *op, uint32_t k);
	void take(int32_t to, uint16_t pass, uintptr_t does, const void *op, uint32_t k);
	void queue_read(uintptr_t does, const void *op, uint32_t k);

	/* The end of the block: on to `next`; or, when its jump issued
	 * (`jumps`), back to C++ with `slots` of its delay slots left past the
	 * block, or with none on to its target: `taken` when it is a branch
	 * (`known`), which always issues (`always`), else the target it set.
	 * On to the host code at `direct` when it is not 0, else to the block
	 * found then; the place where link_to() puts that block once it is
	 * written goes to `taken_at` and `next_at` (0 for none). */
	void end(uint32_t count, bool jumps, uint32_t slots, bool known, bool always, uint32_t taken,
	         uint32_t taken_direct, uint32_t next, uint32_t next_direct, uint32_t &taken_at, uint32_t &next_at);
	/* The jump end() gave `at` for goes to `place` from now on. */
	void link_to(uint32_t at, uint32_t place);

	/* Runs the blocks from that place for at most `cycles` ops; returns
	 * the ops of the blocks run whole. */
	uint32_t run(Pmx1Core &core, uint32_t place, uint32_t cycles, const uint8_t *vram) const;

private:
	/* Bytes of the longest block: 68 ops of at most 384 bytes each, and
	 * its end and ways out */
	static constexpr uint32_t LONGEST = 32768;

#if defined(ODIN_HOST_X86_64)
	struct Out {
		Jump jump;
		uint32_t k;
	};
	struct Slow {
		std::vector<Jump> jumps;
		uintptr_t does;
		const void *op;
		uint32_t k, resume;
	};

	/* `opcode` with ModRM naming `reg` and [rbx + place] */
	void field(uint8_t opcode, uint8_t reg, int32_t place);
	/* `opcode` with ModRM naming `reg` and [rbx + index * scale + place] */
	void field_indexed(uint8_t opcode, uint8_t reg, uint8_t index, uint8_t scale, int32_t place);
	/* The jumps to `jumps` when the address in ecx is not that of a word of
	 * video RAM of the core, `room` bytes before its end; ecx = its offset */
	void in_vram(std::vector<Jump> &jumps, uint32_t room);
	Jump jump_ahead();
	void jump_on(uint32_t direct, uint32_t &at);

	Places m_places;
	Link m_link;
	uint32_t m_follow;	/* the way on to the block at the next address */
	uint32_t m_rank;	/* of the block being written */
	std::vector<Out> m_stale, m_done;
	std::vector<Slow> m_slow;
	uint32_t (*m_enter)(Pmx1Core *, const uint8_t *, uint32_t, const uint8_t *);
#endif
};

} /* namespace odin */

#endif /* ODIN_PMX1_HOST_HPP */
