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

#ifndef ODIN_ARM7_HOST_HPP
#define ODIN_ARM7_HOST_HPP

#include <cstdint>
#include <vector>

#include "odin/machine/core/host_code.hpp"

namespace odin {

class Arm7;

/* Host code for the blocks of an ARM7, which does what Arm7::run_decoded()
 * does one instruction at a time: each instruction is checked against the
 * memory, its condition tested, then it is either called or done in place;
 * after it, the block goes back to C++ when the cycles are spent or an
 * interrupt is to be taken, and on to the block at PC when the program
 * went elsewhere.
 *
 * PC and the address of the instruction are written to the CPU only where
 * something reads them: before a call, and on the way back to C++, where
 * each place has its own way out. The instructions run are counted in a
 * register of the host and added to the CPU on the way back. A word that
 * changed sends the program back to C++ with the address of its block,
 * which is written again before it runs again.
 *
 * Only this class knows the host: arm7_translate.cpp says what a block
 * does. On a host it does not write for, or that refuses the memory,
 * usable() is false and the ARM7 keeps to its reader. Written for x86-64
 * and AArch64 (HostCode). */
class Arm7Host : public HostCode {
public:
	using Run = void (*)(Arm7 &, uint32_t);
	/* The host code of the block at PC, or 0 for the way back to C++ */
	using Link = uintptr_t (*)(Arm7 &);
	/* Where the host code finds the state of the CPU, in bytes from it;
	 * `r` is r0 of the active registers, `kept` the byte that is not 0
	 * while a turn is kept and `failed` the count of the instructions
	 * whose condition failed (see Arm7::run_translated). `blocks` is the
	 * table of pages of Arm7::linked(), `blocks_end` (a value) the end of
	 * the memory it covers. */
	struct Places {
		int32_t r, pc, op_pc, cpsr, cycles, instructions, irq_line, fiq_line, ram_base, stale, stale_block, kept,
		        failed, ram_size, ram_mask, blocks;
		uint32_t blocks_end;
	};

	Arm7Host(const Places &places, Link link);

	/* Forgets every block. */
	void clear();
	/* Room left for the longest block there is? */
	bool room() const { return m_used + LONGEST <= m_size; }

	/* Starts the block at `pc`. Returns its place, never 0. */
	uint32_t begin(uint32_t pc);
	/* Back to C++ before the instruction at `pc`, at offset `at` of the
	 * memory, when the memory holds another word than `op` there. */
	void check(uint32_t at, uint32_t pc, uint32_t op);
	/* A jump past the instruction when its condition `cc` fails: `pass`
	 * has the bit of each value of NZCV it passes with. */
	Jump unless(uint32_t cc, uint16_t pass);
	/* The instruction called, then what may follow it: an interrupt to
	 * take, the cycles spent, another PC. */
	void call(uint32_t pc, uint32_t op, Run does);
	/* After an instruction done in place that spent `cycles` and went on
	 * to the next one */
	void spent(uint32_t pc, uint32_t cycles);
	/* After one that spent `cycles` and wrote PC: on to the block there */
	void went(uint32_t cycles);
	/* B or BL in place, then on to `target`: to the host code at `direct`
	 * when it is not 0 and no turn is kept, else to the block found then.
	 * Without `direct`, returns where link_to() puts the block of the
	 * target once it is written. */
	uint32_t branch(uint32_t pc, uint32_t target, bool link, uint32_t direct);
	/* The branch that branch() gave `at` for goes to the block at `place`
	 * from now on, when no turn is kept. */
	void link_to(uint32_t at, uint32_t place);
	/* The instruction at `pc` whose condition failed: the fetch alone. */
	void failed(Jump skip, uint32_t pc);
	/* The end of the block, on to `next` as branch() goes to its target,
	 * then its ways out; returns as branch() does. */
	uint32_t end(uint32_t next, uint32_t direct);

	/* The steps of an instruction done in place, on the registers of
	 * the ARM7 where they are and four registers of the host that nothing
	 * keeps from one instruction to the next. Shift and Alu leave the
	 * carry of the host as x86 sets it. */
	enum class Reg : uint8_t { A, B, D, E };
	enum class Alu : uint8_t { Add, Adc, Sub, Sbb, And, Or, Xor };
	enum class Shift : uint8_t { Left, Right, RightSigned, Rotate };
	void load(Reg reg, uint32_t n);
	void load_value(Reg reg, uint32_t value);
	void store(uint32_t n, Reg reg);
	void store_pc(Reg reg);
	void copy(Reg dst, Reg src);
	void alu(Alu op, Reg dst, Reg src);
	void invert(Reg reg);
	/* by 1 to 31 */
	void shift(Shift kind, Reg reg, uint32_t amount);
	/* Right by one, the carry of the host coming in on top */
	void rotate_through_carry(Reg reg);
	/* The carry of the host = C of CPSR, or not C */
	void carry_from_c(bool inverted);
	/* reg = the carry of the host, 0 or 1 */
	void keep_carry(Reg reg);
	/* NZCV from the result and the flags of the host after Add, Adc, Sub
	 * or Sbb; C is no borrow after Sub and Sbb. */
	void flags_arithmetic(Reg result, bool borrow);
	/* N and Z from the result, C from `carry` (0 or 1) or, with none,
	 * as it was; V as it was. */
	void flags_logical(Reg result, const Reg *carry);
	void flags_logical_value(Reg result, bool carry);
	/* reg = reg & value */
	void mask(Reg reg, uint32_t value);
	/* reg = reg + value */
	void add_value(Reg reg, uint32_t value);
	/* A jump when reg >= limit, as unsigned numbers */
	Jump unless_below(Reg reg, uint32_t limit);
	/* The memory of the ARM7 at the offset in E: dst = its word or its
	 * byte, or src written there */
	void ram_load(Reg dst, bool one_byte);
	void ram_store(Reg src, bool one_byte);
	/* reg rotated right by the low five bits of B */
	void rotate_by_b(Reg reg);
	/* A jump past what follows, to bind later */
	Jump skip_ahead();

	/* Runs the blocks from that place with `done` of `cycles` spent;
	 * returns the cycles spent when it is back. */
	uint32_t run(Arm7 &cpu, uint32_t place, uint32_t done, uint32_t cycles) const
	{
		ready();
		return m_enter(&cpu, m_code + place, done, cycles);
	}

private:
	/* Bytes of the longest block: Arm7::BLOCK_MAX instructions of at
	 * most 384 bytes each, and its end and ways out. */
#if defined(ODIN_HOST_AARCH64)
	static constexpr uint32_t LONGEST = 98304;
#else
	static constexpr uint32_t LONGEST = 32768;
#endif

	struct Out {
		Jump jump;
		uint32_t pc;
	};

	/* `opcode` with ModRM naming `reg` and [rbx + place] */
	void field(uint8_t opcode, uint8_t reg, int32_t place);
	void store_value(int32_t place, uint32_t value);
	void count();
	void cycles_spent(uint32_t pc);
	static uint8_t host(Reg reg);

	Places m_places;
	Link m_link;
	uint32_t m_follow;	/* place of the way on to the block at PC */
	uint32_t m_block;	/* the address of the block being written */
	/* Its ways back to C++: for a word that changed before the
	 * instruction at pc, and with PC to write */
	std::vector<Out> m_stale, m_exits;
#if defined(ODIN_HOST_AARCH64)
	/* The words the block was written for, loaded from after it, and the
	 * page of the memory x24 holds for it, ~0 for none */
	std::vector<Out> m_words;
	uint32_t m_page = 0;
#endif
	uint32_t (*m_enter)(Arm7 *, const uint8_t *, uint32_t, uint32_t);
};

} /* namespace odin */

#endif /* ODIN_ARM7_HOST_HPP */
