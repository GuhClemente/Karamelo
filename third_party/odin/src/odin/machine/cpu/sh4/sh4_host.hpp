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

#ifndef ODIN_SH4_HOST_HPP
#define ODIN_SH4_HOST_HPP

#include <cstdint>

#include "odin/machine/core/host_code.hpp"

namespace odin {

class Sh4;
struct Sh4Op;

#if defined(ODIN_HOST_X86_64)
/* Whether the host does FMA, for the reader (sh4_ops.cpp) and the host code;
 * for the tests, ODIN_SH4_NO_FMA=1 takes the way of the hosts without it */
bool host_has_fma();
#endif

/* Host code for the blocks of a SH-4: the memory it is written in, and the
 * few host instructions a block is made of. A block calls its instructions
 * one after the other; each call returns 0 when the block goes on, 1 when
 * the run loop must have the hand back, anything else is the host code of
 * the block to go to. A simple instruction is not called: the block works
 * on the registers of the CPU itself, with the few steps further down.
 *
 * Only this class knows the host: sh4_translate.cpp says what a block
 * does, with the functions below. On a host it does not write for, or that
 * refuses the memory, usable() is false and the SH-4 keeps to its reader.
 * Written for x86-64 and AArch64 (HostCode). */
class Sh4Host : public HostCode {
public:
	using Call = uintptr_t (*)(Sh4 &, const Sh4Op *);
	/* Where host code finds the plain memory of the CPU, see Sh4::read()
	 * and Sh4::write(): the page of an address to read and to write
	 * (nullptr: not plain), the epoch each physical page of code was last
	 * compared in, and, from the CPU, where its epoch and the flag of a
	 * store are. */
	struct Memory {
		const uint8_t *const *reads;
		uint8_t *const *writes;
		const uint32_t *code_epochs;
		uint32_t page_bits, physical_pages;
		int32_t epoch, touched;
	};
	/* Where the end of a block finds what it changes, from the CPU; and
	 * what Sh4::enter() looks at: the table of the blocks found first,
	 * the layout of an entry, where the CPU keeps the rest. */
	struct Places {
		int32_t remaining;
		int32_t event, blk, wait_pc, countdown;
		const void *near;
		uint32_t near_mask, block_size, block_pc, block_count, block_checked, block_host;
	};
	using Link = uintptr_t (*)(Sh4 &, uint32_t);
	/* The jumps of an access to the call that makes it the long way: the
	 * guard of a move of the FPU, no page, not aligned, code there */
	struct Slow {
		Jump jumps[4];
		uint32_t count;
	};
	enum class Alu : uint8_t { Add, Sub, And, Or, Xor, Compare };
	enum class Shift : uint8_t { Left, Right, RightSigned };
	/* What the last Sub, And or Compare found of its register against
	 * the other value: equal (a result of zero), at least or above as
	 * unsigned numbers, then as signed ones */
	enum class If : uint8_t { Equal, AtLeast, Above, AtLeastSigned, AboveSigned };

	/* `words`: the registers of the CPU, 32 bits each, which the steps
	 * below name by their rank. */
	Sh4Host(uint32_t *words, const Memory &memory, const Places &places);

	/* Forgets every block. */
	void clear();
	/* Room left for the longest block there is? */
	bool room() const { return m_used + LONGEST <= m_size; }

	/* Starts the block of the instructions at `ops`. Returns its place,
	 * never 0. */
	uint32_t begin(const Sh4Op *ops);
	/* Calls an instruction: fn(cpu, &ops[op]). */
	void call(Call fn, uint32_t op);
	/* What the call returned, if not 0: back to the run loop. */
	void loop_if_told();
	/* The same, to a place of the block bound later. */
	Jump jump_if_told();
	/* What the call returned: 1, back to the run loop; else there. */
	void go_where_told();
	/* Calls fn(cpu, pc), for the host code of the block at `pc` or 1. */
	void call_link(Link fn, uint32_t pc);
	/* Straight to the host code of the block at `pc` when Sh4::enter()
	 * takes it with no pass of the wait left, as link() would; else on
	 * with nothing changed, to the call that follows. */
	void link_fast(uint32_t pc);
	/* The same for an end that gives back `given` cycles first, to PC
	 * (the word), when no event came: see end_leave. */
	void leave_fast(uint32_t pc_rank, int32_t given);
	/* A jump to a place bound later when the word is not 0, or when it
	 * is 0. */
	Jump jump_if_word(uint32_t rank, bool set);
	/* A jump bound later when the low bit of A is set. */
	Jump jump_if_odd();
	/* A jump bound later when the bits `mask` of the word are not all
	 * clear, or when they are. */
	Jump jump_if_bits(uint32_t rank, uint32_t mask, bool set);
	/* Floats of single precision, in the float registers 0 to 2 of the
	 * host, which nothing keeps either */
	enum class Float : uint8_t { Add, Sub, Mul };
	void float_load(uint32_t f, uint32_t rank);
	void float_store(uint32_t rank, uint32_t f);
	/* f = f <op> the word */
	void float_op(Float op, uint32_t f, uint32_t rank);
	/* Then flag(If::Above) tells f > the word, false when either is not
	 * a number. */
	void float_compare(uint32_t f, uint32_t rank);
	/* f = the integer of the word */
	void float_from(uint32_t f, uint32_t rank);
	/* A = the float of the word cut to an integer, 0x80000000 out of
	 * range or for what is not a number */
	void float_to(uint32_t rank);
	/* Float 0 = float 0 * float 2 + float 1, rounded once: only on a host
	 * that can, see fused(). */
	bool fused() const { return m_fused; }
	void float_fused();
	/* FIPR and FTRV as the reader computes them, only on a host that has
	 * vectors: the word n + 3 = the inner product of the words from n and
	 * from m; the words from n = the matrix in the words from x by them */
	bool vectors() const;
	void float_ipr(uint32_t n, uint32_t m);
	void float_trv(uint32_t n, uint32_t x);
	/* Instructions of the integer unit done whole, as sh4_ops.cpp does
	 * them, on Rn and Rm (the words n and m), T and MACH, MACL (the words
	 * mach and mach + 1). False, and nothing written, on a host without. */
	enum class Whole : uint8_t {
		SwapB, SwapW, Xtrct, AddC, SubC, NegC, MulL, MulSW, MulUW, DMulS, DMulU,
		RotL, RotR, RotCL, RotCR, ShAd, ShLd
	};
	bool whole(Whole op, uint32_t n, uint32_t m, uint32_t t, uint32_t mach);
	/* Two words that follow each other, the first one the low half of A */
	void load_pair(uint32_t rank);
	void store_pair(uint32_t rank);
	/* Adds to the cycles left in the slice. */
	void add_remaining(int32_t value);

	/* The steps of an instruction written in place, in the one register
	 * of the host they work in, A. Nothing of the program stays in it that
	 * a later step relies on: a host may skip loading a word it holds
	 * already, up to the next call, join or bind. */
	void load(uint32_t rank);
	void store(uint32_t rank);
	void store_value(uint32_t rank, uint32_t value);
	/* A = A <op> the word, or the value; Compare changes nothing. */
	void alu(Alu op, uint32_t rank);
	void alu_value(Alu op, uint32_t value);
	void shift(Shift kind, uint32_t count);
	/* The word takes 1 or 0. Nothing but load() and store() may come
	 * between the step that found it and this one. Uses the register A. */
	void flag(If found, uint32_t rank);
	/* A = `size` bytes at the address in A, extended by their sign, when
	 * the memory there is plain and the address aligned; else on to the
	 * long way. Eight bytes fill A on 64 bits. */
	void read(uint32_t size, Slow &slow);
	/* The low `size` bytes of the word to the address in A, the same way;
	 * the long way too where code compared in this epoch is. Eight bytes
	 * are that word and the next one. */
	void write(uint32_t size, uint32_t rank, Slow &slow);
	/* A = `size` bytes at that address of the host, with their sign. */
	void read_at(const uint8_t *at, uint32_t size);
	/* The jumps of an access to its long way, bound here */
	void bind(const Slow &slow);
	void bind(Jump jump);
	/* Here, where a long way comes back to: A is not known there. */
	uint32_t join();

	/* Runs the blocks from that place until one asks for the run loop. */
	void run(Sh4 &cpu, uint32_t place) const
	{
		ready();
		m_enter(&cpu, m_code + place);
	}

private:
	/* Bytes of the longest block: BLOCK_MAX instructions and as many
	 * delay slots, each a call and a test or an access to the memory
	 * with its long way, and as many ends as branches. */
#if defined(ODIN_HOST_AARCH64)
	static constexpr uint32_t LONGEST = 65536;
#else
	static constexpr uint32_t LONGEST = 16384;
#endif

	void operand(uint8_t reg, uint32_t rank);
	void enter_fast(bool known, uint32_t pc, int32_t given);
	void page(uint8_t table, uint32_t size, Slow &slow);

	uint32_t *m_words;
	Memory m_memory;
	Places m_places;
	bool m_fused;
	bool m_float_compared = false;	/* the flags are those of floats */
	uint32_t m_held = ~0u;		/* the word A holds, ~0u: none known */
	void (*m_enter)(Sh4 *, const uint8_t *);
};

} /* namespace odin */

#endif /* ODIN_SH4_HOST_HPP */
