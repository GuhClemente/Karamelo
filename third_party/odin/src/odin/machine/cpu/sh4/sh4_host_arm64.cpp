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

#include "odin/machine/cpu/sh4/sh4_host.hpp"

#include "odin/machine/cpu/sh4/sh4.hpp"

#if defined(ODIN_HOST_AARCH64)

#include "odin/machine/core/arm64_encode.hpp"

namespace odin {

using namespace arm64;

namespace {

constexpr uint32_t SIZE = 32u << 20;

/* x19 the CPU, x20 its registers, x21 and x22 the tables of pages, x23 the
 * epochs of the code, x24 the instructions of the block, x25 the blocks
 * found first, x26 the host code; A is w0 */
constexpr uint8_t WORDS = 20, READS = 21, WRITES = 22, EPOCHS = 23, OPS = 24, NEAR = 25, CODE = 26;
constexpr uint8_t A = 0;
constexpr uint8_t X12 = 12, X13 = 13, X14 = 14;
constexpr uint32_t NONE = ~0u;

int64_t at(uint32_t rank) { return 4 * static_cast<int64_t>(rank); }

constexpr uint32_t MUL = 0x1B007C00, SMULL = 0x9B207C00, UMULL = 0x9BA07C00, LSLV = 0x1AC02000,
	ASRV = 0x1AC02800, REV16 = 0x5AC00400, BFM = 0x33000000;

/* rd = rn rotated right by `count` */
constexpr uint32_t ror(uint8_t rd, uint8_t rn, uint32_t count)
{
	return EXTR | (uint32_t{rn} << 16) | (count << 10) | (uint32_t{rn} << 5) | rd;
}
/* rd = cond ? 1 : 0 */
constexpr uint32_t cset(uint8_t rd, uint32_t cond)
{
	return CSINC | (31u << 16) | ((cond ^ 1u) << 12) | (31u << 5) | rd;
}

} /* namespace */

/* AArch64 always fuses, as the reader does (sh4_ops.cpp) */
Sh4Host::Sh4Host(uint32_t *words, const Memory &memory, const Places &places)
	: HostCode(SIZE), m_words(words), m_memory(memory), m_places(places), m_fused(true), m_enter(nullptr)
{
	if (usable()) {
		clear();
	}
}

void Sh4Host::clear()
{
	writable();
	way_in();
	move(WORDS, reinterpret_cast<uintptr_t>(m_words), true);
	move(READS, reinterpret_cast<uintptr_t>(m_memory.reads), true);
	move(WRITES, reinterpret_cast<uintptr_t>(m_memory.writes), true);
	move(EPOCHS, reinterpret_cast<uintptr_t>(m_memory.code_epochs), true);
	move(NEAR, reinterpret_cast<uintptr_t>(m_places.near), true);
	move(CODE, reinterpret_cast<uintptr_t>(m_code), true);
	way_in_end();
	way_out();
	m_enter = reinterpret_cast<void (*)(Sh4 *, const uint8_t *)>(m_code);
}

uint32_t Sh4Host::begin(const Sh4Op *ops)
{
	const uint32_t place = m_used;

	writable();
	move(OPS, reinterpret_cast<uintptr_t>(ops), true);
	m_held = NONE;
	return place;
}

void Sh4Host::call(Call fn, uint32_t op)
{
	copy(X0, X19, true);
	add_value(X1, OPS, int64_t{op} * static_cast<int64_t>(sizeof(Sh4Op)));
	call_to(reinterpret_cast<uintptr_t>(fn));
	m_held = NONE;
}

void Sh4Host::call_link(Link fn, uint32_t pc)
{
	copy(X0, X19, true);
	move(X1, pc);
	call_to(reinterpret_cast<uintptr_t>(fn));
	m_held = NONE;
}

void Sh4Host::link_fast(uint32_t pc)
{
	enter_fast(true, pc, 0);
}

void Sh4Host::leave_fast(uint32_t pc_rank, int32_t given)
{
	enter_fast(false, pc_rank, given);
}

/* link() and enter() at once, with the pc in w1, the entry in x10, the
 * cycles left in w14: every test before anything is stored, the misses to
 * the call, which then decides as if nothing ran. */
void Sh4Host::enter_fast(bool known, uint32_t pc, int32_t given)
{
	const Places &p = m_places;
	Jump miss[8];
	uint32_t misses = 0;

	if (known) {
		move(X1, pc);
		add_value(X10, NEAR, int64_t{(pc >> 1) & p.near_mask} * p.block_size, true);
	} else {
		HostCode::load(X9, X19, p.event, 1);
		miss[misses++] = jump_if_zero(X9, false, false);
		HostCode::load(X1, WORDS, at(pc), 4);
		emit(ubfx(1, static_cast<uint32_t>(__builtin_popcount(p.near_mask))) | (X1 << 5) | X10);
		move(X11, p.block_size);
		emit(UMADDL | (X11 << 16) | (NEAR << 10) | (X10 << 5) | X10);	/* x10 = near + w10 * w11 */
	}
	HostCode::load(X16, X10, p.block_host, 4);
	miss[misses++] = jump_if_zero(X16, true, false);
	HostCode::load(X12, X10, p.block_pc, 4);
	emit(SUBS | (X1 << 16) | (X12 << 5) | ZR);
	miss[misses++] = jump_if(NE);
	HostCode::load(X12, X10, p.block_checked, 4);
	HostCode::load(X9, X19, m_memory.epoch, 4);
	emit(SUBS | (X9 << 16) | (X12 << 5) | ZR);
	miss[misses++] = jump_if(NE);
	HostCode::load(X13, X10, p.block_count, 2);
	HostCode::load(X14, X19, p.remaining, 4);
	if (given != 0) {
		add_value(X14, X14, given, false);
	}
	emit(SUBS | (X14 << 16) | (X13 << 5) | ZR);
	miss[misses++] = jump_if(GT);
	/* The wait: its passes are for the call to count. */
	HostCode::load(X9, X19, p.wait_pc, 4);
	emit(SUBS | (X9 << 16) | (X1 << 5) | ZR);
	miss[misses++] = jump_if(EQ);
	HostCode::load(X9, X19, p.countdown, 4);
	emit(SUBS_VALUE | (1 << 10) | (X9 << 5) | ZR);
	miss[misses++] = jump_if(LE);
	emit(SUB_VALUE | (1 << 10) | (X9 << 5) | X9);
	HostCode::store(X9, X19, p.countdown, 4);
	HostCode::store(X10, X19, p.blk, 8);
	emit(SUB | (X13 << 16) | (X14 << 5) | X14);
	HostCode::store(X14, X19, p.remaining, 4);
	emit(ADD | WIDE | (X16 << 16) | (CODE << 5) | X0);
	emit(BR | (X0 << 5));
	for (uint32_t k = 0; k < misses; k++) {
		bind(miss[k]);
	}
}

void Sh4Host::loop_if_told()
{
	jump_to_if_zero(X0, false, m_leave);
}

Sh4Host::Jump Sh4Host::jump_if_told()
{
	return jump_if_zero(X0, false, true);
}

void Sh4Host::go_where_told()
{
	emit(SUBS_VALUE | WIDE | (1 << 10) | (X0 << 5) | ZR);		/* cmp x0, 1 */
	jump_to_if(EQ, m_leave);
	emit(BR | (X0 << 5));
}

void Sh4Host::operand(uint8_t, uint32_t) {}

/* A word A holds already is not loaded again: every step that changes A or
 * the word another way, every call and every join forgets it. */
void Sh4Host::load(uint32_t rank)
{
	if (m_held == rank) {
		return;
	}
	HostCode::load(A, WORDS, at(rank), 4);
	m_held = rank;
}

void Sh4Host::store(uint32_t rank)
{
	HostCode::store(A, WORDS, at(rank), 4);
	m_held = rank;
}

void Sh4Host::store_value(uint32_t rank, uint32_t value)
{
	if (m_held == rank) {
		m_held = NONE;
	}
	if (value == 0) {
		HostCode::store(ZR, WORDS, at(rank), 4);
		return;
	}
	move(X9, value);
	HostCode::store(X9, WORDS, at(rank), 4);
}

/* Add, Sub, And, Or, Xor, Compare: the last three of the six set the
 * flags that flag() reads, as x86 does */
void Sh4Host::alu(Alu op, uint32_t rank)
{
	static constexpr uint32_t CODE[] = { ADD, SUBS, ANDS, ORR, EOR, SUBS };

	HostCode::load(X9, WORDS, at(rank), 4);
	emit(CODE[static_cast<uint8_t>(op)] | (X9 << 16) | (A << 5) | (op == Alu::Compare ? ZR : A));
	m_float_compared = false;
	if (op != Alu::Compare) {
		m_held = NONE;
	}
}

void Sh4Host::alu_value(Alu op, uint32_t value)
{
	uint32_t n, r, s;
	const uint8_t to = op == Alu::Compare ? ZR : A;

	m_float_compared = false;
	if (op != Alu::Compare) {
		m_held = NONE;
	}
	switch (op) {
	case Alu::Add:
		add_value(A, A, value, false);
		return;
	case Alu::Sub:
	case Alu::Compare:
		if (value < 4096) {
			emit(SUBS_VALUE | (value << 10) | (A << 5) | to);
			return;
		}
		move(X9, value);
		emit(SUBS | (X9 << 16) | (A << 5) | to);
		return;
	case Alu::Xor:
		if (value == 0xFFFFFFFF) {
			emit(ORN | (A << 16) | (ZR << 5) | A);		/* mvn */
			return;
		}
		break;
	default:
		break;
	}
	static constexpr uint32_t MASKED[] = { 0, 0, ANDS_VALUE, ORR_VALUE, EOR_VALUE, 0 };
	static constexpr uint32_t BY_REG[] = { 0, 0, ANDS, ORR, EOR, 0 };
	if (logical_mask(value, false, n, r, s)) {
		emit(MASKED[static_cast<uint8_t>(op)] | (r << 16) | (s << 10) | (A << 5) | A);
	} else {
		move(X9, value);
		emit(BY_REG[static_cast<uint8_t>(op)] | (X9 << 16) | (A << 5) | A);
	}
}

void Sh4Host::shift(Shift kind, uint32_t count)
{
	const uint32_t code = kind == Shift::Left ? lsl(count) : kind == Shift::Right ? lsr(count) : asr(count);

	emit(code | (A << 5) | A);
	m_held = NONE;
}

/* x86 tells "above" after a comparison of floats as GT does: false when
 * either is not a number */
void Sh4Host::flag(If found, uint32_t rank)
{
	static constexpr Cond CONDS[] = { EQ, HS, HI, GE, GT };
	const Cond cond = m_float_compared && found == If::Above ? GT : CONDS[static_cast<uint8_t>(found)];

	emit(CSINC | (ZR << 16) | ((cond ^ 1u) << 12) | (ZR << 5) | A);	/* cset */
	HostCode::store(A, WORDS, at(rank), 4);
	m_held = rank;
}

Sh4Host::Jump Sh4Host::jump_if_word(uint32_t rank, bool set)
{
	if (m_held == rank) {
		return jump_if_zero(A, !set);
	}
	HostCode::load(X9, WORDS, at(rank), 4);
	return jump_if_zero(X9, !set);
}

Sh4Host::Jump Sh4Host::jump_if_bits(uint32_t rank, uint32_t mask, bool set)
{
	uint32_t n, r, s;

	HostCode::load(X9, WORDS, at(rank), 4);
	if (logical_mask(mask, false, n, r, s)) {
		emit(ANDS_VALUE | (r << 16) | (s << 10) | (X9 << 5) | ZR);
	} else {
		move(X10, mask);
		emit(ANDS | (X10 << 16) | (X9 << 5) | ZR);
	}
	return jump_if(set ? NE : EQ);
}

Sh4Host::Jump Sh4Host::jump_if_odd()
{
	emit(ANDS_VALUE | (A << 5) | ZR);				/* tst w0, 1 */
	return jump_if(NE);
}

void Sh4Host::float_load(uint32_t f, uint32_t rank)
{
	load_float(static_cast<uint8_t>(f), WORDS, at(rank));
}

void Sh4Host::float_store(uint32_t rank, uint32_t f)
{
	if (m_held == rank) {
		m_held = NONE;
	}
	store_float(static_cast<uint8_t>(f), WORDS, at(rank));
}

/* s<f> first, as the reader and x86 take it: that picks the NaN of two */
void Sh4Host::float_op(Float op, uint32_t f, uint32_t rank)
{
	static constexpr uint32_t CODE[] = { FADD, FSUB, FMUL };

	load_float(3, WORDS, at(rank));
	emit(CODE[static_cast<uint8_t>(op)] | (3 << 16) | (f << 5) | f);
}

void Sh4Host::float_compare(uint32_t f, uint32_t rank)
{
	load_float(3, WORDS, at(rank));
	emit(FCMP | (3 << 16) | (f << 5));
	m_float_compared = true;
}

void Sh4Host::float_from(uint32_t f, uint32_t rank)
{
	HostCode::load(X9, WORDS, at(rank), 4);
	emit(SCVTF | (X9 << 5) | f);
}

/* 0x80000000 out of the range of 32 bits, or for what is not a number, as
 * the reader gives it: the conversion on 64 bits, kept when it fits */
void Sh4Host::float_to(uint32_t rank)
{
	m_held = NONE;
	load_float(3, WORDS, at(rank));
	emit(FCVTZS_X | (3 << 5) | X9);
	move(X10, 0x80000000);
	emit(CMP_SXTW | (X9 << 16) | (X9 << 5) | ZR);
	emit(CSEL | (X10 << 16) | (EQ << 12) | (X9 << 5) | A);
	emit(FCMP | (3 << 16) | (3 << 5));
	emit(CSEL | (X10 << 16) | (VC << 12) | (A << 5) | A);
}

void Sh4Host::float_fused()
{
	emit(FMADD | (2 << 16) | (1 << 10) | (0 << 5) | 0);
}

bool Sh4Host::vectors() const { return true; }

/* As sh4_ops.cpp computes it: a1 * b1, then a0, a2, a3 fused in turn */
void Sh4Host::float_ipr(uint32_t n, uint32_t m)
{
	m_held = NONE;
	load_float(0, WORDS, at(n + 1));
	load_float(1, WORDS, at(m + 1));
	emit(FMUL | (1 << 16) | (0 << 5) | 2);
	for (const uint32_t k : {0u, 2u, 3u}) {
		load_float(0, WORDS, at(n + k));
		load_float(1, WORDS, at(m + k));
		emit(FMADD | (1 << 16) | (2 << 10) | (0 << 5) | 2);
	}
	store_float(2, WORDS, at(n + 3));
}

/* The four sums at once, each as sh4_ops.cpp computes it: v1 times the
 * second row of the matrix, then v0, v2 and v3 times theirs, fused; each v
 * broadcast first, so that the operands keep the order of the reader */
void Sh4Host::float_trv(uint32_t n, uint32_t x)
{
	static constexpr uint32_t ORDER[4][2] = { {1, 5}, {0, 4}, {2, 6}, {3, 7} };

	m_held = NONE;
	add_value(X9, WORDS, at(n));
	add_value(X10, WORDS, at(x));
	emit(LDR_Q | (X9 << 5) | 0);
	for (uint32_t row = 0; row < 4; row++) {
		emit(LDR_Q | (row << 10) | (X10 << 5) | (4 + row));
	}
	for (const auto &[lane, row] : ORDER) {
		emit(DUP_S | (lane << 19) | (0 << 5) | 1);
		emit((lane == 1 ? FMUL_4S : FMLA_4S) | (row << 16) | (1 << 5) | 2);
	}
	emit(STR_Q | (X9 << 5) | 2);
}

/* w9 Rn, w10 Rm, w11 T, as the reader computes them; T taken from the
 * carries of each of its two steps, as it does */
bool Sh4Host::whole(Whole op, uint32_t n, uint32_t m, uint32_t t, uint32_t mach)
{
	const auto get = [this](uint8_t reg, uint32_t rank) { HostCode::load(reg, WORDS, at(rank), 4); };
	const auto put = [this](uint32_t rank, uint8_t reg) { HostCode::store(reg, WORDS, at(rank), 4); };
	const auto two = [&](uint32_t code) {
		get(X9, n);
		get(X10, m);
		emit(code | (X10 << 16) | (X9 << 5) | X9);
	};
	/* w9 = w9 + w10 + w11 or w9 - w10 - w11, w12 the carry or the
	 * borrow of either */
	const auto chain = [&](uint32_t code, Cond carry) {
		emit(code | (X10 << 16) | (X9 << 5) | X9);
		emit(cset(12, carry));
		emit(code | (X11 << 16) | (X9 << 5) | X9);
		emit(CSINC | (12u << 5) | (ZR << 16) | (static_cast<uint32_t>(carry ^ 1) << 12) | 12);
		put(n, X9);
		put(t, 12);
	};
	m_held = NONE;
	switch (op) {
	case Whole::SwapB:
		get(X10, m);
		emit(REV16 | (X10 << 5) | X11);
		emit(BFM | (15u << 10) | (X11 << 5) | X10);		/* bfxil w10, w11, 0, 16 */
		put(n, X10);
		return true;
	case Whole::SwapW:
		get(X10, m);
		emit(ror(X10, X10, 16));
		put(n, X10);
		return true;
	case Whole::Xtrct:
		get(X9, n);
		get(X10, m);
		emit(EXTR | (X9 << 16) | (16u << 10) | (X10 << 5) | X9);	/* Rm:Rn >> 16 */
		put(n, X9);
		return true;
	case Whole::AddC:
		get(X9, n);
		get(X10, m);
		get(X11, t);
		chain(ADDS, HS);
		return true;
	case Whole::SubC:
		get(X9, n);
		get(X10, m);
		get(X11, t);
		chain(SUBS, LO);
		return true;
	case Whole::NegC:
		emit(MOVZ | X9);
		get(X10, m);
		get(X11, t);
		chain(SUBS, LO);
		return true;
	case Whole::MulL:
		two(MUL);
		put(mach + 1, X9);
		return true;
	case Whole::MulSW:
	case Whole::MulUW: {
		const uint32_t half = (op == Whole::MulSW ? SBFM : UBFM) | (15u << 10);

		get(X9, n);
		get(X10, m);
		emit(half | (X9 << 5) | X9);
		emit(half | (X10 << 5) | X10);
		emit(MUL | (X10 << 16) | (X9 << 5) | X9);
		put(mach + 1, X9);
		return true;
	}
	case Whole::DMulS:
	case Whole::DMulU:
		/* MACH:MACL, MACL the high word in memory */
		two(op == Whole::DMulS ? SMULL : UMULL);
		emit(EXTR | WIDE | (1u << 22) | (X9 << 16) | (32u << 10) | (X9 << 5) | X9);
		HostCode::store(X9, WORDS, at(mach), 8);
		return true;
	case Whole::RotL:
	case Whole::RotR:
		get(X9, n);
		if (op == Whole::RotL) {
			emit(lsr(31) | (X9 << 5) | X11);
		} else {
			emit(AND_VALUE | (X9 << 5) | X11);			/* and w11, w9, 1 */
		}
		emit(ror(X9, X9, op == Whole::RotL ? 31 : 1));
		put(n, X9);
		put(t, X11);
		return true;
	case Whole::RotCL:
		get(X9, n);
		get(X11, t);
		emit(lsr(31) | (X9 << 5) | 12);
		emit(ORR | (X9 << 16) | (1u << 10) | (X11 << 5) | X9);	/* orr w9, w11, w9, lsl 1 */
		put(n, X9);
		put(t, 12);
		return true;
	case Whole::RotCR:
		get(X9, n);
		get(X11, t);
		emit(AND_VALUE | (X9 << 5) | 12);
		emit(lsr(1) | (X9 << 5) | X9);
		emit(ORR | (X11 << 16) | (31u << 10) | (X9 << 5) | X9);	/* orr w9, w9, w11, lsl 31 */
		put(n, X9);
		put(t, 12);
		return true;
	case Whole::ShAd:
	case Whole::ShLd:
		/* Rm < 0: Rn >> (~Rm & 31) then >> 1, which is all of the sign or 0
		 * for a count of 32 */
		get(X9, n);
		get(X10, m);
		emit(LSLV | (X10 << 16) | (X9 << 5) | 12);
		emit(ORN | (X10 << 16) | (ZR << 5) | 13);			/* mvn w13, w10 */
		emit((op == Whole::ShAd ? ASRV : LSRV) | (13u << 16) | (X9 << 5) | 13);
		emit((op == Whole::ShAd ? asr(1) : lsr(1)) | (13u << 5) | 13);
		emit(ANDS_VALUE | (1u << 16) | (X10 << 5) | ZR);		/* tst w10, 0x80000000 */
		emit(CSEL | (12u << 16) | (NE << 12) | (13u << 5) | X9);	/* csel w9, w13, w12, ne */
		put(n, X9);
		return true;
	}
	return false;
}

void Sh4Host::load_pair(uint32_t rank)
{
	HostCode::load(A, WORDS, at(rank), 8);
	m_held = NONE;
}

void Sh4Host::store_pair(uint32_t rank)
{
	HostCode::store(A, WORDS, at(rank), 8);
	m_held = NONE;
}

void Sh4Host::add_remaining(int32_t value)
{
	HostCode::load(X9, X19, m_places.remaining, 4);
	add_value(X9, X9, value, false);
	HostCode::store(X9, X19, m_places.remaining, 4);
}

void Sh4Host::bind(const Slow &slow)
{
	for (uint32_t k = 0; k < slow.count; k++) {
		bind(slow.jumps[k]);
	}
}

void Sh4Host::bind(Jump jump)
{
	HostCode::bind(jump);
	m_held = NONE;
}

uint32_t Sh4Host::join()
{
	m_held = NONE;
	return here();
}

/* x2 = the page of the address in w0 in that table; the long way when
 * there is none or the address is not aligned. w1 keeps the address over
 * the bits of the page, w0 the offset in it. */
void Sh4Host::page(uint8_t table, uint32_t size, Slow &slow)
{
	m_held = NONE;
	emit(lsr(m_memory.page_bits) | (A << 5) | X1);
	emit(indexed(LDR_X) | (1 << 12) | (X1 << 16) | (table << 5) | X2);	/* ldr x2, [table, x1, lsl 3] */
	slow.jumps[slow.count++] = jump_if_zero(X2, true, true);
	if (size > 1) {
		uint32_t n, r, s;

		logical_mask(size - 1, false, n, r, s);
		emit(ANDS_VALUE | (r << 16) | (s << 10) | (A << 5) | ZR);
		slow.jumps[slow.count++] = jump_if(NE);
	}
	and_value(A, A, (1u << m_memory.page_bits) - 1, X9);
}

void Sh4Host::read(uint32_t size, Slow &slow)
{
	page(READS, size, slow);
	const uint32_t code = size == 1 ? LDRSB : size == 2 ? LDRSH : size == 4 ? LDR_W : LDR_X;

	emit(indexed_uxtw(code) | (A << 16) | (X2 << 5) | A);
}

void Sh4Host::write(uint32_t size, uint32_t rank, Slow &slow)
{
	page(WRITES, size, slow);
	and_value(X1, X1, m_memory.physical_pages - 1, X9);
	emit(indexed(LDR_W) | (1 << 12) | (X1 << 16) | (EPOCHS << 5) | X10);	/* ldr w10, [x23, x1, lsl 2] */
	HostCode::load(X9, X19, m_memory.epoch, 4);
	emit(SUBS | (X9 << 16) | (X10 << 5) | ZR);
	slow.jumps[slow.count++] = jump_if(EQ);
	move(X9, 1);
	HostCode::store(X9, X19, m_memory.touched, 1);
	HostCode::load(X1, WORDS, at(rank), size == 8 ? 8 : 4);
	const uint32_t code = size == 1 ? STRB : size == 2 ? STRH : size == 4 ? STR_W : STR_X;

	emit(indexed_uxtw(code) | (A << 16) | (X2 << 5) | X1);
}

void Sh4Host::read_at(const uint8_t *where, uint32_t size)
{
	m_held = NONE;
	move(X0, reinterpret_cast<uintptr_t>(where), true);
	HostCode::load(A, X0, 0, size == 1 ? 1 : size == 2 ? 2 : 4, size < 4);
}

} /* namespace odin */

#endif
