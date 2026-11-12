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

#include "odin/machine/cpu/arm7/arm7_host.hpp"

#if defined(ODIN_HOST_AARCH64)

#include "odin/machine/core/arm64_encode.hpp"

namespace odin {

using namespace arm64;

namespace {

constexpr uint32_t SIZE = 8u << 20;

/* x19 the CPU, w20 the cycles spent, w21 those to spend, x22 the memory
 * of the ARM7, x23 the instructions run, x24 the page of the memory of the
 * block running, x25 the table of the blocks; A, B, D, E in w0 to w3, and
 * the carry of the host in w15, as x86 leaves it in CF */
constexpr uint8_t SPENT = 20, LIMIT = 21, RAM = 22, COUNT = 23, PAGE = 24, BLOCKS = 25, CARRY = 15;
constexpr uint32_t LDR_LITERAL_W = 0x18000000, ADR = 0x10000000;
/* The page of x24, and the bytes a load reaches from x22 alone */
constexpr uint32_t PAGE_BYTES = 1u << 12, NEAR = 4 * 4096, NONE = ~0u;

uint8_t r(Arm7Host::Reg reg) { return static_cast<uint8_t>(reg); }

} /* namespace */

Arm7Host::Arm7Host(const Places &places, Link link)
	: HostCode(SIZE), m_places(places), m_link(link), m_follow(0), m_block(0), m_enter(nullptr)
{
	if (usable()) {
		clear();
	}
}

void Arm7Host::clear()
{
	writable();
	way_in();
	HostCode::copy(SPENT, X2);
	HostCode::copy(LIMIT, X3);
	HostCode::load(RAM, X19, m_places.ram_base, 8);
	HostCode::copy(COUNT, ZR, true);
	HostCode::add_value(BLOCKS, X19, m_places.blocks);
	way_in_end();
	HostCode::load(X9, X19, m_places.instructions, 8);
	emit(ADD | WIDE | (COUNT << 16) | (X9 << 5) | X9);
	HostCode::store(X9, X19, m_places.instructions, 8);
	HostCode::copy(X0, SPENT);
	way_out();

	/* The way on: Arm7::linked() done here while no turn is kept, its
	 * tests in the same order; back to C++ where it gives 0. */
	const Places &p = m_places;
	m_follow = m_used;
	HostCode::load(X9, X19, p.kept, 1);
	const Jump kept = jump_if_zero(X9, false);
	HostCode::load(X9, X19, p.pc, 4);
	emit(ANDS_VALUE | (1 << 10) | (X9 << 5) | ZR);			/* tst w9, #3 */
	jump_to_if(NE, m_leave);
	HostCode::load(X10, X19, p.ram_size, 4);
	emit(SUBS | (X10 << 16) | (X9 << 5) | ZR);
	jump_to_if(HS, m_leave);
	HostCode::load(X10, X19, p.ram_mask, 4);
	emit(AND | (X10 << 16) | (X9 << 5) | X10);			/* at */
	move(X11, p.blocks_end);
	emit(SUBS | (X11 << 16) | (X10 << 5) | ZR);
	jump_to_if(HS, m_leave);
	emit(lsr(12) | (X10 << 5) | X11);
	emit(indexed(LDR_X) | (1 << 12) | (X11 << 16) | (BLOCKS << 5) | X11);	/* the page */
	jump_to_if_zero(X11, true, m_leave);
	emit(ubfx(2, 10) | (X10 << 5) | X10);
	emit(indexed(LDR_W) | (1 << 12) | (X10 << 16) | (X11 << 5) | X10);	/* the place */
	jump_to_if_zero(X10, true, m_leave);
	const uint32_t back = (0u - m_used) & 0x1FFFFF;			/* adr x11, the code */
	emit(ADR | ((back & 3) << 29) | ((back >> 2) << 5) | X11);
	emit(ADD | WIDE | (X10 << 16) | (X11 << 5) | X0);
	emit(BR | (X0 << 5));

	bind(kept);
	HostCode::load(X9, X19, p.instructions, 8);
	emit(ADD | WIDE | (COUNT << 16) | (X9 << 5) | X9);
	HostCode::store(X9, X19, p.instructions, 8);
	HostCode::copy(COUNT, ZR, true);
	HostCode::copy(X0, X19, true);
	call_to(reinterpret_cast<uintptr_t>(m_link));
	jump_to_if_zero(X0, true, m_leave);
	emit(BR | (X0 << 5));

	m_enter = reinterpret_cast<uint32_t (*)(Arm7 *, const uint8_t *, uint32_t, uint32_t)>(m_code);
}

void Arm7Host::store_value(int32_t place, uint32_t value)
{
	if (value == 0) {
		HostCode::store(ZR, X19, place, 4);
		return;
	}
	move(X9, value);
	HostCode::store(X9, X19, place, 4);
}

void Arm7Host::count()
{
	HostCode::add_value(COUNT, COUNT, 1);
}

void Arm7Host::cycles_spent(uint32_t pc)
{
	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	m_exits.push_back({jump_if(HS), pc + 4});
}

uint32_t Arm7Host::begin(uint32_t pc)
{
	writable();
	m_block = pc;
	m_stale.clear();
	m_exits.clear();
	m_words.clear();
	m_page = NONE;
	return m_used;
}

/* The word in the memory from x22, or from x24 past what x22 reaches; the
 * word it should be from after the block. A block keeps to one page. */
void Arm7Host::check(uint32_t at, uint32_t pc, uint32_t op)
{
	const uint32_t page = at & ~(PAGE_BYTES - 1);
	const bool near = page + PAGE_BYTES <= NEAR;

	if (!near && m_page == NONE && (page >> 12) < 4096) {
		emit(ADD_VALUE | WIDE | (1 << 22) | ((page >> 12) << 10) | (RAM << 5) | PAGE);
		m_page = page;
	}
	if (page == m_page) {
		HostCode::load(X9, PAGE, at - page, 4);
	} else {
		HostCode::load(X9, RAM, at, 4);
	}
	emit(LDR_LITERAL_W | X10);
	m_words.push_back({Jump{m_used - 4}, op});
	emit(SUBS | (X10 << 16) | (X9 << 5) | ZR);
	m_stale.push_back({jump_if(NE), pc});
}

/* CPSR keeps NZCV where the host does, and conditions 0x0 to 0xD mean the
 * same on both. 0xF never passes on the ARM7 but always on the host. */
HostCode::Jump Arm7Host::unless(uint32_t cc, uint16_t)
{
	if (cc == 0xF) {
		return jump_ahead();
	}
	HostCode::load(X9, X19, m_places.cpsr, 4);
	emit(MSR_NZCV | X9);
	return jump_if(static_cast<Cond>(cc ^ 1));
}

void Arm7Host::call(uint32_t pc, uint32_t op, Run does)
{
	const Places &p = m_places;

	store_value(p.cycles, 0);
	store_value(p.op_pc, pc);
	store_value(p.pc, pc + 4);
	HostCode::copy(X0, X19, true);
	move(X1, op);
	call_to(reinterpret_cast<uintptr_t>(does));
	HostCode::load(X9, X19, p.cycles, 4);
	emit(ADD | (X9 << 16) | (SPENT << 5) | SPENT);
	count();

	/* An interrupt to take: FIQ or IRQ raised and not masked */
	HostCode::load(X9, X19, p.fiq_line, 1);
	HostCode::load(X10, X19, p.irq_line, 1);
	emit(lsl(6) | (X9 << 5) | X9);
	emit(ORR | (7 << 10) | (X10 << 16) | (X9 << 5) | X9);
	HostCode::load(X10, X19, p.cpsr, 4);
	emit(BICS | (X10 << 16) | (X9 << 5) | ZR);
	jump_to_if(NE, m_leave);

	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	jump_to_if(HS, m_leave);
	HostCode::load(X9, X19, p.pc, 4);
	move(X10, pc + 4);
	emit(SUBS | (X10 << 16) | (X9 << 5) | ZR);
	jump_to_if(NE, m_follow);
}

void Arm7Host::spent(uint32_t pc, uint32_t cycles)
{
	HostCode::add_value(SPENT, SPENT, cycles, false);
	count();
	cycles_spent(pc);
}

void Arm7Host::went(uint32_t cycles)
{
	HostCode::add_value(SPENT, SPENT, cycles, false);
	count();
	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	jump_to_if(HS, m_leave);
	jump_back(m_follow);
}

uint32_t Arm7Host::branch(uint32_t pc, uint32_t target, bool link, uint32_t direct)
{
	if (link) {
		store_value(m_places.r + 4 * 14, pc + 4);
	}
	HostCode::add_value(SPENT, SPENT, 3, false);
	count();
	store_value(m_places.pc, target);
	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	jump_to_if(HS, m_leave);
	HostCode::load(X9, X19, m_places.kept, 1);
	jump_to_if_zero(X9, false, m_follow);
	/* The way on until the block of the target is written */
	const uint32_t at = m_used;
	jump_back(direct ? direct : m_follow);
	return direct ? 0 : at;
}

void Arm7Host::link_to(uint32_t at, uint32_t place)
{
	writable();
	relink(at, place);
}

void Arm7Host::failed(Jump skip, uint32_t pc)
{
	const Jump on = jump_ahead();

	bind(skip);
	HostCode::add_value(SPENT, SPENT, 1, false);
	HostCode::load(X9, X19, m_places.failed, 8);
	HostCode::add_value(X9, X9, 1);
	HostCode::store(X9, X19, m_places.failed, 8);
	cycles_spent(pc);
	bind(on);
}

uint32_t Arm7Host::end(uint32_t next, uint32_t direct)
{
	store_value(m_places.pc, next);
	HostCode::load(X9, X19, m_places.kept, 1);
	jump_to_if_zero(X9, false, m_follow);
	const uint32_t at = m_used;
	jump_back(direct ? direct : m_follow);
	for (const Out &out : m_stale) {
		bind(out.jump);
		store_value(m_places.pc, out.pc);
		store_value(m_places.stale_block, m_block);
		move(X9, 1);
		HostCode::store(X9, X19, m_places.stale, 1);
		jump_back(m_leave);
	}
	for (const Out &out : m_exits) {
		bind(out.jump);
		store_value(m_places.pc, out.pc);
		jump_back(m_leave);
	}
	for (const Out &word : m_words) {
		bind(word.jump);
		emit(word.pc);
	}
	return direct ? 0 : at;
}

void Arm7Host::load(Reg reg, uint32_t n)
{
	HostCode::load(r(reg), X19, m_places.r + static_cast<int32_t>(4 * n), 4);
}

void Arm7Host::load_value(Reg reg, uint32_t value)
{
	move(r(reg), value);
}

void Arm7Host::store(uint32_t n, Reg reg)
{
	HostCode::store(r(reg), X19, m_places.r + static_cast<int32_t>(4 * n), 4);
}

void Arm7Host::store_pc(Reg reg)
{
	HostCode::store(r(reg), X19, m_places.pc, 4);
}

void Arm7Host::copy(Reg dst, Reg src)
{
	HostCode::copy(r(dst), r(src));
}

/* The carry comes in from w15: as is for Adc, turned over for Sbb, whose
 * carry_from_c(true) gave the borrow of x86. The flags come out as AArch64
 * sets them, C no borrow after a subtraction, as flags_arithmetic takes. */
void Arm7Host::alu(Alu op, Reg dst, Reg src)
{
	static constexpr uint32_t CODE[] = { ADDS, ADCS, SUBS, SBCS, AND, ORR, EOR };

	if (op == Alu::Adc) {
		emit(SUBS_VALUE | (1 << 10) | (CARRY << 5) | ZR);	/* C = w15 */
	} else if (op == Alu::Sbb) {
		emit(SUBS | (CARRY << 16) | (ZR << 5) | ZR);		/* C = !w15 */
	}
	emit(CODE[static_cast<uint8_t>(op)] | (r(src) << 16) | (r(dst) << 5) | r(dst));
}

void Arm7Host::invert(Reg reg)
{
	emit(ORN | (r(reg) << 16) | (ZR << 5) | r(reg));
}

/* The last bit out to w15, as x86 leaves it in CF */
void Arm7Host::shift(Shift kind, Reg reg, uint32_t amount)
{
	const uint8_t x = r(reg);

	switch (kind) {
	case Shift::Left:
		emit(ubfx(32 - amount, 1) | (x << 5) | CARRY);
		emit(lsl(amount) | (x << 5) | x);
		break;
	case Shift::Right:
		emit(ubfx(amount - 1, 1) | (x << 5) | CARRY);
		emit(lsr(amount) | (x << 5) | x);
		break;
	case Shift::RightSigned:
		emit(ubfx(amount - 1, 1) | (x << 5) | CARRY);
		emit(asr(amount) | (x << 5) | x);
		break;
	default:
		emit(EXTR | (x << 16) | (amount << 10) | (x << 5) | x);	/* ror */
		emit(lsr(31) | (x << 5) | CARRY);
		break;
	}
}

void Arm7Host::rotate_through_carry(Reg reg)
{
	const uint8_t x = r(reg);

	emit(AND_VALUE | (x << 5) | X9);				/* w9 = bit 0 */
	emit(lsr(1) | (x << 5) | x);
	emit(ORR | (31 << 10) | (CARRY << 16) | (x << 5) | x);
	HostCode::copy(CARRY, X9);
}

void Arm7Host::carry_from_c(bool inverted)
{
	HostCode::load(CARRY, X19, m_places.cpsr, 4);
	emit(ubfx(29, 1) | (CARRY << 5) | CARRY);
	if (inverted) {
		emit(EOR_VALUE | (CARRY << 5) | CARRY);		/* ^ 1 */
	}
}

void Arm7Host::keep_carry(Reg reg)
{
	HostCode::copy(r(reg), CARRY);
}

/* NZCV of the host as they are: they sit where CPSR has them */
void Arm7Host::flags_arithmetic(Reg, bool)
{
	emit(MRS_NZCV | X9);
	HostCode::load(X10, X19, m_places.cpsr, 4);
	and_value(X10, X10, 0x0FFFFFFF, X11);
	emit(ORR | (X9 << 16) | (X10 << 5) | X10);
	HostCode::store(X10, X19, m_places.cpsr, 4);
}

void Arm7Host::flags_logical(Reg result, const Reg *carry)
{
	const uint8_t x = r(result);

	and_value(X9, x, 0x80000000, X11);				/* N */
	emit(SUBS_VALUE | (x << 5) | ZR);
	emit(CSINC | (ZR << 16) | (NE << 12) | (ZR << 5) | X10);	/* Z */
	emit(ORR | (30 << 10) | (X10 << 16) | (X9 << 5) | X9);
	if (carry) {
		emit(ORR | (29 << 10) | (r(*carry) << 16) | (X9 << 5) | X9);
	}
	HostCode::load(X10, X19, m_places.cpsr, 4);
	and_value(X10, X10, carry ? 0x1FFFFFFF : 0x3FFFFFFF, X11);
	emit(ORR | (X9 << 16) | (X10 << 5) | X10);
	HostCode::store(X10, X19, m_places.cpsr, 4);
}

void Arm7Host::flags_logical_value(Reg result, bool carry)
{
	const uint8_t x = r(result);

	and_value(X9, x, 0x80000000, X11);
	emit(SUBS_VALUE | (x << 5) | ZR);
	emit(CSINC | (ZR << 16) | (NE << 12) | (ZR << 5) | X10);
	emit(ORR | (30 << 10) | (X10 << 16) | (X9 << 5) | X9);
	if (carry) {
		uint32_t n, rr, s;

		logical_mask(0x20000000, false, n, rr, s);
		emit(ORR_VALUE | (rr << 16) | (s << 10) | (X9 << 5) | X9);
	}
	HostCode::load(X10, X19, m_places.cpsr, 4);
	and_value(X10, X10, 0x1FFFFFFF, X11);
	emit(ORR | (X9 << 16) | (X10 << 5) | X10);
	HostCode::store(X10, X19, m_places.cpsr, 4);
}

void Arm7Host::mask(Reg reg, uint32_t value)
{
	and_value(r(reg), r(reg), value, X9);
}

void Arm7Host::add_value(Reg reg, uint32_t value)
{
	HostCode::add_value(r(reg), r(reg), value, false);
}

HostCode::Jump Arm7Host::unless_below(Reg reg, uint32_t limit)
{
	if (limit < 4096) {
		emit(SUBS_VALUE | (limit << 10) | (r(reg) << 5) | ZR);
	} else {
		move(X9, limit);
		emit(SUBS | (X9 << 16) | (r(reg) << 5) | ZR);
	}
	return jump_if(HS);
}

/* [x22 + w3] */
void Arm7Host::ram_load(Reg dst, bool one_byte)
{
	emit(indexed_uxtw(one_byte ? LDRB : LDR_W) | (X3 << 16) | (RAM << 5) | r(dst));
}

void Arm7Host::ram_store(Reg src, bool one_byte)
{
	emit(indexed_uxtw(one_byte ? STRB : STR_W) | (X3 << 16) | (RAM << 5) | r(src));
}

void Arm7Host::rotate_by_b(Reg reg)
{
	emit(RORV | (X1 << 16) | (r(reg) << 5) | r(reg));
}

HostCode::Jump Arm7Host::skip_ahead()
{
	return jump_ahead();
}

} /* namespace odin */

#endif
