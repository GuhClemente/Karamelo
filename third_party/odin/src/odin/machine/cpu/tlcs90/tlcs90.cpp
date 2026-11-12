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

#include "odin/machine/cpu/tlcs90/tlcs90.hpp"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "odin/common/stop_message.hpp"

namespace odin {

/* The groups of eight follow the low three bits of their opcodes. */
enum class Tlcs90::Op : uint8_t {
	NOP, HALT, DI, EI, SWI, INCX, DECX, EX, EXX, DAA, RCF, SCF, CCF, CPL, NEG,
	MUL, DIV,
	ADD, ADC, SUB, SBC, AND, XOR, OR, CP,
	RLC, RRC, RL, RR, SLA, SRA, SLL, SRL,
	LDI, LDIR, LDD, LDDR, CPI, CPIR, CPD, CPDR,
	RLD, RRD,
	LD, LDA, LDAR, PUSH, POP, INC, DEC, INCW, DECW, BIT, RES, SET, TSET,
	DJNZ, JP, JR, CALL, CALLR, RET, RETI, UNKNOWN
};

namespace {

uint8_t
parity_flag(uint8_t v)
{
	return (std::popcount(v) & 1) ? 0 : Tlcs90::FLAG_V;
}

uint8_t
sz(uint8_t v)
{
	return v ? (v & Tlcs90::FLAG_S) : Tlcs90::FLAG_Z;
}

} /* namespace */

void
Tlcs90::reset()
{
	m_pc = 0;
	m_f &= static_cast<uint8_t>(~FLAG_I);
	m_halted = false;
	m_ei_pending = false;
}

uint16_t
Tlcs90::fetch16()
{
	const uint8_t lo = fetch();

	return static_cast<uint16_t>(lo | fetch() << 8);
}

uint16_t
Tlcs90::rd16(uint16_t addr) const
{
	return static_cast<uint16_t>(rd(addr) | rd(static_cast<uint16_t>(addr + 1)) << 8);
}

void
Tlcs90::wr16(uint16_t addr, uint16_t v)
{
	wr(addr, static_cast<uint8_t>(v));
	wr(static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(v >> 8));
}

void
Tlcs90::push(uint16_t v)
{
	m_sp = static_cast<uint16_t>(m_sp - 2);
	wr16(m_sp, v);
}

uint16_t
Tlcs90::pop()
{
	const uint16_t v = rd16(m_sp);

	m_sp = static_cast<uint16_t>(m_sp + 2);
	return v;
}

uint16_t
Tlcs90::r16(uint8_t n) const
{
	switch (n) {
	case BC: return static_cast<uint16_t>(m_r[B] << 8 | m_r[C]);
	case DE: return static_cast<uint16_t>(m_r[D] << 8 | m_r[E]);
	case HL: return static_cast<uint16_t>(m_r[H] << 8 | m_r[L]);
	case IX: return m_ix;
	case IY: return m_iy;
	case SP: return m_sp;
	case AF: return static_cast<uint16_t>(m_r[A] << 8 | m_f);
	default: return 0;
	}
}

void
Tlcs90::set_r16(uint8_t n, uint16_t v)
{
	const uint8_t hi = static_cast<uint8_t>(v >> 8), lo = static_cast<uint8_t>(v);

	switch (n) {
	case BC: m_r[B] = hi; m_r[C] = lo; break;
	case DE: m_r[D] = hi; m_r[E] = lo; break;
	case HL: m_r[H] = hi; m_r[L] = lo; break;
	case IX: m_ix = v; break;
	case IY: m_iy = v; break;
	case SP: m_sp = v; break;
	case AF: m_r[A] = hi; m_f = lo; break;
	default: break;
	}
}

uint8_t
Tlcs90::get8(const Operand &o) const
{
	switch (o.kind) {
	case Operand::Kind::R8: return m_r[o.reg];
	case Operand::Kind::IMM: return static_cast<uint8_t>(o.value);
	case Operand::Kind::MEM: return rd(o.value);
	default: return 0;
	}
}

void
Tlcs90::put8(const Operand &o, uint8_t v)
{
	if (o.kind == Operand::Kind::R8)
		m_r[o.reg] = v;
	else if (o.kind == Operand::Kind::MEM)
		wr(o.value, v);
}

uint16_t
Tlcs90::get16(const Operand &o) const
{
	switch (o.kind) {
	case Operand::Kind::R16: return r16(o.reg);
	case Operand::Kind::IMM: return o.value;
	case Operand::Kind::MEM: return rd16(o.value);
	default: return 0;
	}
}

void
Tlcs90::put16(const Operand &o, uint16_t v)
{
	if (o.kind == Operand::Kind::R16)
		set_r16(o.reg, v);
	else if (o.kind == Operand::Kind::MEM)
		wr16(o.value, v);
}

/* The conditions, cc 0 to 15: F, LT, LE, ULE, OV, MI, Z, C, T, GE, GT,
 * UGT, NOV, PL, NZ, NC */
bool
Tlcs90::test(uint8_t cc) const
{
	const bool s = m_f & FLAG_S, z = m_f & FLAG_Z, v = m_f & FLAG_V, c = m_f & FLAG_C;
	bool yes;

	switch (cc & 7) {
	case 0: yes = false; break;
	case 1: yes = s != v; break;
	case 2: yes = z || s != v; break;
	case 3: yes = c || z; break;
	case 4: yes = v; break;
	case 5: yes = s; break;
	case 6: yes = z; break;
	default: yes = c; break;
	}
	return (cc & 8) ? !yes : yes;
}

/* The memory operand of a prefix: E0-E6 (gg), E3 and EB (mn), E7 and EF
 * (FF00+n), E8-EE (gg) as a destination, F0-F2 and F4-F6 (ix+d), F3 and
 * F7 (HL+A), A taken as signed. */
Tlcs90::Operand
Tlcs90::mem_of(uint8_t prefix, uint8_t d)
{
	const uint8_t low = prefix & 7;

	switch (prefix & 0xF8) {
	case 0xE0:
	case 0xE8:
		if (low == 3)
			return mem(fetch16());
		if (low == 7)
			return mem(static_cast<uint16_t>(0xFF00 | fetch()));
		return based(low, 0);
	default:
		if (low == 3 || low == 7)
			return based(Operand::HL_PLUS_A, 0);
		return based(static_cast<uint8_t>(IX + (low & 3)), d);
	}
}

Tlcs90::Operand
Tlcs90::based(uint8_t base, uint8_t d) const
{
	Operand o = mem(0);

	o.base = base;
	o.disp = static_cast<int8_t>(d);
	o.value = address_of(o);
	return o;
}

uint16_t
Tlcs90::address_of(const Operand &o) const
{
	if (o.base == Operand::HL_PLUS_A)
		return static_cast<uint16_t>(r16(HL) + static_cast<int8_t>(m_r[A]));
	return static_cast<uint16_t>(r16(o.base) + o.disp);
}

Tlcs90::Op
Tlcs90::alu_op(uint8_t n)
{
	return static_cast<Op>(static_cast<uint8_t>(Op::ADD) + (n & 7));
}

Tlcs90::Op
Tlcs90::shift_op(uint8_t n)
{
	return static_cast<Op>(static_cast<uint8_t>(Op::RLC) + (n & 7));
}

void
Tlcs90::decode(Instruction &ins)
{
	const uint8_t b0 = fetch();
	const auto short_mem = [this] { return mem(static_cast<uint16_t>(0xFF00 | fetch())); };
	/* The displacement is read before the PC it counts from; every relative form counts from the instruction + 2
	 * (TMP90C840 data book, MPU90-15); after a 16-bit displacement the PC is one past that. */
	const auto rel8 = [this] { const auto d = static_cast<int8_t>(fetch()); return imm(static_cast<uint16_t>(d + m_pc)); };
	const auto rel16 = [this] { const uint16_t d = fetch16(); return imm(static_cast<uint16_t>(d + m_pc - 1)); };

	ins.wide = false;
	ins.x = ins.y = Operand();
	switch (b0) {
	case 0x00: ins.set(Op::NOP, 2); return;
	case 0x01: ins.set(Op::HALT, 4); return;
	case 0x02: ins.set(Op::DI, 2); return;
	case 0x03: ins.set(Op::EI, 2); return;
	case 0x07: ins.set(Op::INCX, 10, 6); ins.x = short_mem(); return;
	case 0x08: ins.set(Op::EX, 2); ins.wide = true; ins.x = reg16(DE); ins.y = reg16(HL); return;
	case 0x09: ins.set(Op::EX, 2); ins.wide = true; ins.x = reg16(AF); ins.y = reg16(AF2); return;
	case 0x0A: ins.set(Op::EXX, 2); return;
	case 0x0B: ins.set(Op::DAA, 4); return;
	case 0x0C: ins.set(Op::RCF, 2); return;
	case 0x0D: ins.set(Op::SCF, 2); return;
	case 0x0E: ins.set(Op::CCF, 2); return;
	case 0x0F: ins.set(Op::DECX, 10, 6); ins.x = short_mem(); return;
	case 0x10: ins.set(Op::CPL, 2); return;
	case 0x11: ins.set(Op::NEG, 2); return;
	case 0x12: case 0x13:
		ins.set(b0 == 0x12 ? Op::MUL : Op::DIV, 16); ins.y = imm(fetch()); return;
	case 0x14: case 0x15: case 0x16:
		ins.set(Op::ADD, 6); ins.wide = true; ins.x = reg16(static_cast<uint8_t>(IX + b0 - 0x14));
		ins.y = imm(fetch16()); return;
	case 0x17: ins.set(Op::LDAR, 8); ins.y = rel16(); return;
	case 0x18: ins.set(Op::DJNZ, 10); ins.x = reg8(B); ins.y = rel8(); return;
	case 0x19:
		ins.set(Op::DJNZ, 10); ins.wide = true; ins.x = reg16(BC);
		ins.y = rel8(); return;
	case 0x1A: ins.set(Op::JP, 8); ins.x = imm(8); ins.y = imm(fetch16()); return;
	case 0x1B: ins.set(Op::JR, 10); ins.x = imm(8); ins.y = rel16(); return;
	case 0x1C: ins.set(Op::CALL, 14); ins.x = imm(8); ins.y = imm(fetch16()); return;
	case 0x1D: ins.set(Op::CALLR, 16); ins.y = rel16(); return;
	case 0x1E: ins.set(Op::RET, 10); ins.x = imm(8); return;
	case 0x1F: ins.set(Op::RETI, 14); return;
	case 0xFF: ins.set(Op::SWI, 20); return;
	default: break;
	}

	switch (b0 & 0xF8) {
	case 0x20:
		if (b0 == 0x27) { ins.set(Op::LD, 8); ins.x = reg8(A); ins.y = short_mem(); }
		else { ins.set(Op::LD, 2); ins.x = reg8(A); ins.y = reg8(b0 & 7); }
		return;
	case 0x28:
		if (b0 == 0x2F) { ins.set(Op::LD, 8); ins.x = short_mem(); ins.y = reg8(A); }
		else { ins.set(Op::LD, 2); ins.x = reg8(b0 & 7); ins.y = reg8(A); }
		return;
	case 0x30:
		if (b0 == 0x37) { ins.set(Op::LD, 10); ins.x = short_mem(); ins.y = imm(fetch()); }
		else { ins.set(Op::LD, 4); ins.x = reg8(b0 & 7); ins.y = imm(fetch()); }
		return;
	case 0x38:
		ins.wide = true;
		if (b0 == 0x3F) { ins.set(Op::LD, 14); ins.x = short_mem(); ins.y = imm(fetch16()); return; }
		if (b0 == 0x3B) break;
		ins.set(Op::LD, 6); ins.x = reg16(b0 & 7); ins.y = imm(fetch16()); return;
	case 0x40:
		ins.wide = true;
		if (b0 == 0x47) { ins.set(Op::LD, 10); ins.x = reg16(HL); ins.y = short_mem(); return; }
		if (b0 == 0x43) break;
		ins.set(Op::LD, 4); ins.x = reg16(HL); ins.y = reg16(b0 & 7); return;
	case 0x48:
		ins.wide = true;
		if (b0 == 0x4F) { ins.set(Op::LD, 10); ins.x = short_mem(); ins.y = reg16(HL); return; }
		if (b0 == 0x4B) break;
		ins.set(Op::LD, 4); ins.x = reg16(b0 & 7); ins.y = reg16(HL); return;
	case 0x50:
	case 0x58:
		if ((b0 & 7) == 3 || (b0 & 7) == 7)
			break;
		ins.set(b0 < 0x58 ? Op::PUSH : Op::POP, b0 < 0x58 ? 8 : 10);
		ins.wide = true;
		ins.x = reg16((b0 & 7) == SP ? AF : b0 & 7);
		return;
	case 0x60: ins.set(alu_op(b0), 8); ins.x = reg8(A); ins.y = short_mem(); return;
	case 0x68: ins.set(alu_op(b0), 4); ins.x = reg8(A); ins.y = imm(fetch()); return;
	case 0x70: ins.set(alu_op(b0), 10); ins.wide = true; ins.x = reg16(HL); ins.y = short_mem(); return;
	case 0x78: ins.set(alu_op(b0), 6); ins.wide = true; ins.x = reg16(HL); ins.y = imm(fetch16()); return;
	case 0x80:
	case 0x88:
		ins.set(b0 < 0x88 ? Op::INC : Op::DEC, (b0 & 7) == 7 ? 10 : 2);
		ins.x = (b0 & 7) == 7 ? short_mem() : reg8(b0 & 7);
		return;
	case 0x90:
	case 0x98:
		if ((b0 & 7) == 3)
			break;
		if ((b0 & 7) == 7) { ins.set(b0 < 0x98 ? Op::INCW : Op::DECW, 14); ins.x = short_mem(); return; }
		ins.set(b0 < 0x98 ? Op::INC : Op::DEC, 4); ins.wide = true; ins.x = reg16(b0 & 7); return;
	case 0xA0: ins.set(shift_op(b0), 2); ins.x = reg8(A); return;
	case 0xA8: ins.set(Op::BIT, 8); ins.x = imm(b0 & 7); ins.y = short_mem(); return;
	case 0xB0: ins.set(Op::RES, 12); ins.x = imm(b0 & 7); ins.y = short_mem(); return;
	case 0xB8: ins.set(Op::SET, 12); ins.x = imm(b0 & 7); ins.y = short_mem(); return;
	case 0xC0:
	case 0xC8:
		ins.set(Op::JR, 8, 4); ins.x = imm(b0 & 15);
		ins.y = rel8();
		return;
	case 0xE0: case 0xE8: case 0xF0: case 0xF8:
		decode_prefixed(ins, b0);
		return;
	default:
		break;
	}
	ins.set(Op::UNKNOWN, 2);
}

/* The prefixes E0 to FE: a memory operand (E0-F7) or a register (F8-FE),
 * then the opcode of the operation. The states of each form are those of
 * the data book; they grow with the time the address takes. */
void
Tlcs90::decode_prefixed(Instruction &ins, uint8_t b0)
{

	if (b0 >= 0xF8) {
		/* The register g: 8 bits B to A, or 16 bits BC to SP */
		const uint8_t g = b0 & 7, b1 = fetch();

		switch (b1 & 0xF8) {
		case 0x10:
			if (b1 == 0x12 || b1 == 0x13) { ins.set(b1 == 0x12 ? Op::MUL : Op::DIV, 18); ins.y = reg8(g); return; }
			if (b1 >= 0x14 && b1 <= 0x16) {
				ins.set(Op::ADD, 8); ins.wide = true; ins.x = reg16(static_cast<uint8_t>(IX + b1 - 0x14)); ins.y = reg16(g);
				return;
			}
			break;
		case 0x18: ins.set(Op::TSET, 8); ins.x = imm(b1 & 7); ins.y = reg8(g); return;
		case 0x30:
			if (b1 == 0x37) break;
			ins.set(Op::LD, 4); ins.x = reg8(b1 & 7); ins.y = reg8(g); return;
		case 0x38:
			if (b1 == 0x3B || b1 == 0x3F) break;
			ins.set(Op::LD, 6); ins.wide = true; ins.x = reg16(b1 & 7); ins.y = reg16(g); return;
		case 0x58:
			if (b0 != 0xFE) break;
			ins.set(static_cast<Op>(static_cast<uint8_t>(Op::LDI) + (b1 & 7)), 18, 14);
			return;
		case 0x60: ins.set(alu_op(b1), 4); ins.x = reg8(A); ins.y = reg8(g); return;
		case 0x68: ins.set(alu_op(b1), 6); ins.x = reg8(g); ins.y = imm(fetch()); return;
		case 0x70: ins.set(alu_op(b1), 8); ins.wide = true; ins.x = reg16(HL); ins.y = reg16(g); return;
		case 0xA0: ins.set(shift_op(b1), 4); ins.x = reg8(g); return;
		case 0xA8: ins.set(Op::BIT, 4); ins.x = imm(b1 & 7); ins.y = reg8(g); return;
		case 0xB0: ins.set(Op::RES, 4); ins.x = imm(b1 & 7); ins.y = reg8(g); return;
		case 0xB8: ins.set(Op::SET, 4); ins.x = imm(b1 & 7); ins.y = reg8(g); return;
		case 0xD0:
		case 0xD8:
			if (b0 != 0xFE) break;
			ins.set(Op::RET, 14, 6); ins.x = imm(b1 & 15); return;
		default:
			break;
		}
		ins.set(Op::UNKNOWN, 2);
		return;
	}

	/* A memory operand. Source forms: E0-E7, F0-F3; destination forms:
	 * E8-EF, F4-F7. The extra states of each address form, after the
	 * data book: (gg) 0, (mn) 4, (FF00+n) 2, (ix+d) 4, (HL+A) 8. */
	const uint8_t low = b0 & 7;
	const bool indexed = b0 >= 0xF0;
	const uint8_t d = indexed && low != 3 && low != 7 ? fetch() : 0;
	const Operand m = mem_of(b0, d);
	const uint32_t extra = indexed ? (low == 3 || low == 7 ? 8 : 4) : (low == 3 ? 4 : low == 7 ? 2 : 0);
	const uint8_t b1 = fetch();
	const bool destination = (b0 & 0xF8) == 0xE8 || (b0 >= 0xF4 && b0 <= 0xF7);

	if (!destination) {
		switch (b1 & 0xF8) {
		case 0x10:
			if (b1 == 0x10 || b1 == 0x11) { ins.set(b1 == 0x10 ? Op::RLD : Op::RRD, 12 + extra); ins.x = m; return; }
			if (b1 == 0x12 || b1 == 0x13) { ins.set(b1 == 0x12 ? Op::MUL : Op::DIV, 18 + extra); ins.y = m; return; }
			if (b1 >= 0x14 && b1 <= 0x16) {
				ins.set(Op::ADD, 8 + extra); ins.wide = true; ins.x = reg16(static_cast<uint8_t>(IX + b1 - 0x14)); ins.y = m;
				return;
			}
			break;
		case 0x18: ins.set(Op::TSET, 12 + extra); ins.x = imm(b1 & 7); ins.y = m; return;
		case 0x28: ins.set(Op::LD, 6 + extra); ins.x = reg8(b1 & 7); ins.y = m; return;
		case 0x48:
			if (b1 == 0x4B || b1 == 0x4F) break;
			ins.set(Op::LD, 8 + extra); ins.wide = true; ins.x = reg16(b1 & 7); ins.y = m; return;
		case 0x50:
			if (b1 == 0x53 || b1 == 0x57) break;
			ins.set(Op::EX, 14 + extra); ins.wide = true; ins.x = m; ins.y = reg16(b1 & 7); return;
		case 0x60: ins.set(alu_op(b1), 6 + extra); ins.x = reg8(A); ins.y = m; return;
		case 0x70: ins.set(alu_op(b1), 8 + extra); ins.wide = true; ins.x = reg16(HL); ins.y = m; return;
		case 0x80:
			if (b1 == 0x87) { ins.set(Op::INC, 8 + extra); ins.x = m; return; }
			break;
		case 0x88:
			if (b1 == 0x8F) { ins.set(Op::DEC, 8 + extra); ins.x = m; return; }
			break;
		case 0x90:
			if (b1 == 0x97) { ins.set(Op::INCW, 12 + extra); ins.x = m; return; }
			break;
		case 0x98:
			if (b1 == 0x9F) { ins.set(Op::DECW, 12 + extra); ins.x = m; return; }
			break;
		case 0xA0: ins.set(shift_op(b1), 8 + extra); ins.x = m; return;
		case 0xA8: ins.set(Op::BIT, 6 + extra); ins.x = imm(b1 & 7); ins.y = m; return;
		case 0xB0: ins.set(Op::RES, 10 + extra); ins.x = imm(b1 & 7); ins.y = m; return;
		case 0xB8: ins.set(Op::SET, 10 + extra); ins.x = imm(b1 & 7); ins.y = m; return;
		default:
			break;
		}
		ins.set(Op::UNKNOWN, 2);
		return;
	}

	switch (b1 & 0xF8) {
	case 0x20: ins.set(Op::LD, 6 + extra); ins.x = m; ins.y = reg8(b1 & 7); return;
	case 0x30:
		if (b1 == 0x37) { ins.set(Op::LD, 8 + extra); ins.x = m; ins.y = imm(fetch()); return; }
		break;
	case 0x38:
		if (b1 == 0x3F) { ins.set(Op::LD, 12 + extra); ins.wide = true; ins.x = m; ins.y = imm(fetch16()); return; }
		/* LDA rr, the address itself; only the indexed forms */
		if (indexed && b1 != 0x3B) { ins.set(Op::LDA, 6 + extra); ins.wide = true; ins.x = reg16(b1 & 7); ins.y = itself(m); return; }
		break;
	case 0x40:
		if (b1 == 0x43 || b1 == 0x47) break;
		ins.set(Op::LD, 8 + extra); ins.wide = true; ins.x = m; ins.y = reg16(b1 & 7); return;
	case 0x68:
		ins.set(alu_op(b1), (b1 == 0x6F ? 8 : 10) + extra); ins.x = m; ins.y = imm(fetch()); return;
	case 0xC0:
	case 0xC8:
		/* JP [cc,] to the address itself: (gg) is gg, (mn) is mn */
		ins.set(Op::JP, 8 + extra, 6 + extra); ins.x = imm(b1 & 15); ins.y = itself(m); return;
	case 0xD0:
	case 0xD8:
		ins.set(Op::CALL, 14 + extra, 6 + extra); ins.x = imm(b1 & 15); ins.y = itself(m); return;
	default:
		break;
	}
	ins.set(Op::UNKNOWN, 2);
}

void
Tlcs90::interrupt(uint16_t vector)
{
	m_halted = false;
	push(m_pc);
	push(r16(AF));
	m_f &= static_cast<uint8_t>(~FLAG_I);
	m_pc = vector;
}

uint32_t
Tlcs90::step()
{
	if (const uint16_t vector = m_bus.interrupt(m_f & FLAG_I)) {
		interrupt(vector);
		return INTERRUPT_STATES;
	}
	if (m_halted)
		return 2;

	const uint16_t at = m_pc;
	Instruction ins;

	if (at < m_decoded.size() && m_decoded[at].length) {
		const Decoded &known = m_decoded[at];

		ins = known.ins;
		m_pc = static_cast<uint16_t>(at + known.length);
		for (Operand *o : { &ins.x, &ins.y })
			if (o->base != Operand::FIXED)
				o->value = address_of(*o);
	} else {
		decode(ins);
		if (ins.op == Op::UNKNOWN)
			stop_fatal("TLCS-90: unknown instruction at %04X", static_cast<unsigned>(at));

		const unsigned length = static_cast<uint16_t>(m_pc - at);

		if (at + length <= m_decoded.size())
			m_decoded[at] = { ins, static_cast<uint8_t>(length) };
	}
	const bool after_ei = m_ei_pending;
	const uint32_t states = execute(ins);

	/* EI holds off the interrupts for the instruction after it. */
	if (after_ei && ins.op != Op::EI) {
		m_ei_pending = false;
		m_f |= FLAG_I;
	}
	return states;
}

void
Tlcs90::alu8(Op op, const Operand &x, uint8_t v)
{
	const uint8_t a = get8(x);
	const unsigned carry = (m_f & FLAG_C) ? 1 : 0;
	unsigned r;

	switch (op) {
	case Op::ADD:
	case Op::ADC:
		r = a + v + (op == Op::ADC ? carry : 0);
		m_f = static_cast<uint8_t>((m_f & FLAG_I) | sz(static_cast<uint8_t>(r)) | ((a ^ v ^ r) & FLAG_H) |
		                           (((a ^ r) & (v ^ r) & 0x80) ? FLAG_V : 0) | (r > 0xFF ? FLAG_C | FLAG_X : 0));
		put8(x, static_cast<uint8_t>(r));
		return;
	case Op::SUB:
	case Op::SBC:
	case Op::CP:
		r = a - v - (op == Op::SBC ? carry : 0);
		m_f = static_cast<uint8_t>((m_f & FLAG_I) | sz(static_cast<uint8_t>(r)) | ((a ^ v ^ r) & FLAG_H) | FLAG_N |
		                           (((a ^ v) & (a ^ r) & 0x80) ? FLAG_V : 0) | ((r & 0x100) ? FLAG_C | FLAG_X : 0));
		if (op != Op::CP)
			put8(x, static_cast<uint8_t>(r));
		return;
	case Op::AND: r = a & v; break;
	case Op::XOR: r = a ^ v; break;
	default: r = a | v; break;
	}
	m_f = static_cast<uint8_t>((m_f & FLAG_I) | sz(static_cast<uint8_t>(r)) | parity_flag(static_cast<uint8_t>(r)) |
	                           (op == Op::AND ? FLAG_H : 0));
	put8(x, static_cast<uint8_t>(r));
}

/* ADD, ADC, SUB, SBC, AND, XOR, OR, CP on 16 bits. ADD rr,gg between two
 * registers leaves S, Z and V; every other form sets them. */
void
Tlcs90::alu16(Op op, const Operand &x, uint16_t v, bool flags_of_add)
{
	const uint16_t a = get16(x);
	const uint32_t carry = (m_f & FLAG_C) ? 1 : 0;
	uint32_t r;

	switch (op) {
	case Op::ADD:
	case Op::ADC:
		r = a + v + (op == Op::ADC ? carry : 0u);
		if (flags_of_add)
			m_f &= FLAG_S | FLAG_Z | FLAG_I | FLAG_V;
		else
			m_f = static_cast<uint8_t>((m_f & FLAG_I) | (static_cast<uint16_t>(r) ? 0 : FLAG_Z) |
			                           ((r & 0x8000) ? FLAG_S : 0) | (((a ^ r) & (v ^ r) & 0x8000) ? FLAG_V : 0));
		m_f = static_cast<uint8_t>(m_f | (r > 0xFFFF ? FLAG_C | FLAG_X : 0) | (((a ^ v ^ r) & 0x1000) ? FLAG_H : 0));
		put16(x, static_cast<uint16_t>(r));
		return;
	case Op::SUB:
	case Op::SBC:
	case Op::CP:
		r = a - v - (op == Op::SBC ? carry : 0u);
		m_f = static_cast<uint8_t>((m_f & FLAG_I) | FLAG_N | (static_cast<uint16_t>(r) ? 0 : FLAG_Z) |
		                           ((r & 0x8000) ? FLAG_S : 0) | ((r & 0x10000) ? FLAG_C | FLAG_X : 0) |
		                           (((a ^ v ^ r) & 0x1000) ? FLAG_H : 0) | (((a ^ v) & (a ^ r) & 0x8000) ? FLAG_V : 0));
		if (op != Op::CP)
			put16(x, static_cast<uint16_t>(r));
		return;
	case Op::AND: r = a & v; break;
	case Op::XOR: r = a ^ v; break;
	default: r = a | v; break;
	}
	m_f = static_cast<uint8_t>((m_f & FLAG_I) | (r ? 0 : FLAG_Z) | ((r & 0x8000) ? FLAG_S : 0) |
	                           (op == Op::AND ? FLAG_H : 0));
	put16(x, static_cast<uint16_t>(r));
}

/* The rotations and shifts. On A they set C and X and leave S, Z and V;
 * elsewhere S, Z and the parity as well. */
uint8_t
Tlcs90::shift(Op op, uint8_t v, bool on_a)
{
	const unsigned carry = (m_f & FLAG_C) ? 1 : 0;
	unsigned out, r;

	switch (op) {
	case Op::RLC: out = v >> 7; r = static_cast<unsigned>(v << 1) | out; break;
	case Op::RRC: out = v & 1; r = static_cast<unsigned>(v >> 1) | out << 7; break;
	case Op::RL: out = v >> 7; r = static_cast<unsigned>(v << 1) | carry; break;
	case Op::RR: out = v & 1; r = static_cast<unsigned>(v >> 1) | carry << 7; break;
	case Op::SLA:
	case Op::SLL: out = v >> 7; r = static_cast<unsigned>(v << 1); break;
	case Op::SRA: out = v & 1; r = static_cast<unsigned>(v >> 1) | (v & 0x80u); break;
	default: out = v & 1; r = static_cast<unsigned>(v >> 1); break;
	}
	const uint8_t result = static_cast<uint8_t>(r);

	if (on_a)
		m_f &= FLAG_S | FLAG_Z | FLAG_I | FLAG_V;
	else
		m_f = static_cast<uint8_t>((m_f & FLAG_I) | sz(result) | parity_flag(result));
	if (out)
		m_f |= FLAG_C | FLAG_X;
	return result;
}

/* The flags of an 8-bit increment or decrement to `v`: C kept, X when 0 */
void
Tlcs90::flags_inc_dec8(uint8_t v, bool up)
{
	m_f = static_cast<uint8_t>((m_f & (FLAG_I | FLAG_C)) | sz(v) | (v ? 0 : FLAG_X));
	if (up)
		m_f |= static_cast<uint8_t>((v == 0x80 ? FLAG_V : 0) | ((v & 15) == 0 ? FLAG_H : 0));
	else
		m_f |= static_cast<uint8_t>(FLAG_N | (v == 0x7F ? FLAG_V : 0) | ((v & 15) == 15 ? FLAG_H : 0));
}

void
Tlcs90::daa()
{
	const uint8_t a = m_r[A], lo = a & 15, hi = a >> 4;
	const bool c = m_f & FLAG_C, n = m_f & FLAG_N, h = m_f & FLAG_H;
	uint8_t diff;

	if (c)
		diff = (lo <= 9 && !h) ? 0x60 : 0x66;
	else if (lo >= 10)
		diff = hi <= 8 ? 0x06 : 0x66;
	else if (hi >= 10)
		diff = h ? 0x66 : 0x60;
	else
		diff = h ? 0x06 : 0x00;
	m_r[A] = static_cast<uint8_t>(n ? a - diff : a + diff);
	m_f = static_cast<uint8_t>(sz(m_r[A]) | parity_flag(m_r[A]) | (m_f & (FLAG_I | FLAG_N)));
	if (c || (lo <= 9 ? hi >= 10 : hi >= 9))
		m_f |= FLAG_X | FLAG_C;
	if (n ? h && lo <= 5 : lo >= 10)
		m_f |= FLAG_H;
}

uint32_t
Tlcs90::execute(const Instruction &ins)
{
	const Operand &x = ins.x, &y = ins.y;

	switch (ins.op) {
	case Op::NOP:
		break;
	case Op::HALT:
		m_halted = true;
		break;
	case Op::DI:
		m_ei_pending = false;
		m_f &= static_cast<uint8_t>(~FLAG_I);
		break;
	case Op::EI:
		if (!(m_f & FLAG_I))
			m_ei_pending = true;
		break;
	case Op::SWI:
		interrupt(0x0010);
		break;
	case Op::INCX:
	case Op::DECX: {
		if (!(m_f & FLAG_X))
			return ins.states_not_taken;
		const bool up = ins.op == Op::INCX;
		const uint8_t v = static_cast<uint8_t>(get8(x) + (up ? 1 : 0xFF));

		put8(x, v);
		flags_inc_dec8(v, up);
		break;
	}
	case Op::EX:
		if (y.kind == Operand::Kind::R16 && y.reg == AF2) {
			std::swap(m_r[A], m_alt[6]);
			std::swap(m_f, m_alt[7]);
		} else {
			const uint16_t v = get16(x);

			put16(x, get16(y));
			put16(y, v);
		}
		break;
	case Op::EXX:
		for (unsigned i = 0; i < 6; i++)
			std::swap(m_r[i], m_alt[i]);
		break;
	case Op::DAA:
		daa();
		break;
	case Op::RCF:
		m_f &= FLAG_S | FLAG_Z | FLAG_I | FLAG_V;
		break;
	case Op::SCF:
		m_f = static_cast<uint8_t>((m_f & (FLAG_S | FLAG_Z | FLAG_I | FLAG_V)) | FLAG_X | FLAG_C);
		break;
	case Op::CCF:
		m_f = static_cast<uint8_t>((m_f & (FLAG_S | FLAG_Z | FLAG_I | FLAG_V)) |
		                           ((m_f & FLAG_C) ? FLAG_H : FLAG_X | FLAG_C));
		break;
	case Op::CPL:
		m_r[A] = static_cast<uint8_t>(~m_r[A]);
		m_f |= FLAG_H | FLAG_N;
		break;
	case Op::NEG: {
		const uint8_t v = m_r[A];

		m_r[A] = 0;
		alu8(Op::SUB, reg8(A), v);
		break;
	}
	case Op::MUL:
		set_r16(HL, static_cast<uint16_t>(m_r[L] * get8(y)));
		break;
	case Op::DIV: {
		const uint16_t hl = r16(HL);
		const uint8_t v = get8(y);

		if (!v) {
			m_f |= FLAG_V;
			set_r16(HL, static_cast<uint16_t>(hl << 8 | ((hl >> 8) ^ 0xFF)));
		} else {
			const unsigned q = hl / v;

			m_r[H] = static_cast<uint8_t>(hl % v);
			m_r[L] = static_cast<uint8_t>(q);
			m_f = static_cast<uint8_t>(q > 0xFF ? m_f | FLAG_V : m_f & ~FLAG_V);
		}
		break;
	}
	case Op::ADD: case Op::ADC: case Op::SUB: case Op::SBC:
	case Op::AND: case Op::XOR: case Op::OR: case Op::CP:
		if (ins.wide)
			alu16(ins.op, x, get16(y), ins.op == Op::ADD && y.kind == Operand::Kind::R16);
		else
			alu8(ins.op, x, get8(y));
		break;
	case Op::RLC: case Op::RRC: case Op::RL: case Op::RR:
	case Op::SLA: case Op::SRA: case Op::SLL: case Op::SRL:
		put8(x, shift(ins.op, get8(x), x.kind == Operand::Kind::R8 && x.reg == A && ins.states == 2));
		break;
	case Op::LDI: case Op::LDIR: case Op::LDD: case Op::LDDR: {
		const bool up = ins.op == Op::LDI || ins.op == Op::LDIR;
		const uint16_t step = up ? 1 : 0xFFFF;
		const uint16_t bc = static_cast<uint16_t>(r16(BC) - 1);

		wr(r16(DE), rd(r16(HL)));
		set_r16(DE, static_cast<uint16_t>(r16(DE) + step));
		set_r16(HL, static_cast<uint16_t>(r16(HL) + step));
		set_r16(BC, bc);
		m_f = static_cast<uint8_t>((m_f & (FLAG_S | FLAG_Z | FLAG_I | FLAG_X | FLAG_C)) | (bc ? FLAG_V : 0));
		if ((ins.op == Op::LDIR || ins.op == Op::LDDR) && bc) {
			m_pc = static_cast<uint16_t>(m_pc - 2);
			return ins.states;
		}
		return ins.states_not_taken;
	}
	case Op::CPI: case Op::CPIR: case Op::CPD: case Op::CPDR: {
		const bool up = ins.op == Op::CPI || ins.op == Op::CPIR;
		const uint8_t v = rd(r16(HL)), r = static_cast<uint8_t>(m_r[A] - v);
		const uint16_t bc = static_cast<uint16_t>(r16(BC) - 1);

		set_r16(HL, static_cast<uint16_t>(r16(HL) + (up ? 1 : 0xFFFF)));
		set_r16(BC, bc);
		m_f = static_cast<uint8_t>((m_f & (FLAG_I | FLAG_C)) | sz(r) | ((m_r[A] ^ v ^ r) & FLAG_H) | FLAG_N |
		                           (bc ? FLAG_V : 0));
		if ((ins.op == Op::CPIR || ins.op == Op::CPDR) && bc && r) {
			m_pc = static_cast<uint16_t>(m_pc - 2);
			return ins.states;
		}
		return ins.states_not_taken;
	}
	case Op::RLD:
	case Op::RRD: {
		const uint8_t a = m_r[A], v = get8(x);

		if (ins.op == Op::RLD) {
			put8(x, static_cast<uint8_t>(v << 4 | (a & 15)));
			m_r[A] = static_cast<uint8_t>((a & 0xF0) | v >> 4);
		} else {
			put8(x, static_cast<uint8_t>(v >> 4 | a << 4));
			m_r[A] = static_cast<uint8_t>((a & 0xF0) | (v & 15));
		}
		m_f = static_cast<uint8_t>((m_f & (FLAG_I | FLAG_C)) | sz(m_r[A]) | parity_flag(m_r[A]));
		break;
	}
	case Op::LD:
		if (ins.wide)
			put16(x, get16(y));
		else
			put8(x, get8(y));
		break;
	case Op::LDA:
	case Op::LDAR:
		put16(x.kind == Operand::Kind::NONE ? reg16(HL) : x, y.value);
		break;
	case Op::PUSH:
		push(get16(x));
		break;
	case Op::POP:
		put16(x, pop());
		break;
	case Op::INC:
	case Op::DEC:
		if (ins.wide) {
			/* rr: X tells the result is 0, nothing else changes */
			const uint16_t v = static_cast<uint16_t>(get16(x) + (ins.op == Op::INC ? 1 : 0xFFFF));

			put16(x, v);
			m_f = static_cast<uint8_t>(v ? m_f & ~FLAG_X : m_f | FLAG_X);
		} else {
			const bool up = ins.op == Op::INC;
			const uint8_t v = static_cast<uint8_t>(get8(x) + (up ? 1 : 0xFF));

			put8(x, v);
			flags_inc_dec8(v, up);
		}
		break;
	case Op::INCW:
	case Op::DECW: {
		const uint16_t a = get16(x);
		const bool up = ins.op == Op::INCW;
		const uint16_t r = static_cast<uint16_t>(a + (up ? 1 : 0xFFFF));

		put16(x, r);
		m_f = static_cast<uint8_t>((m_f & (FLAG_I | FLAG_C)) | (up ? 0 : FLAG_N) | (r ? 0 : FLAG_Z | FLAG_X) |
		                           ((r & 0x8000) ? FLAG_S : 0) | (((a ^ r ^ 1) & 0x1000) ? FLAG_H : 0) |
		                           ((up ? r == 0x8000 : a == 0x8000) ? FLAG_V : 0));
		break;
	}
	case Op::BIT:
	case Op::TSET: {
		const uint8_t v = get8(y), bit = static_cast<uint8_t>(1u << x.value);

		m_f = static_cast<uint8_t>((m_f & (FLAG_I | FLAG_C)) | FLAG_H | ((v & bit) ? (v & bit & FLAG_S) : FLAG_Z | FLAG_V));
		if (ins.op == Op::TSET)
			put8(y, v | bit);
		break;
	}
	case Op::RES:
		put8(y, static_cast<uint8_t>(get8(y) & ~(1u << x.value)));
		break;
	case Op::SET:
		put8(y, static_cast<uint8_t>(get8(y) | 1u << x.value));
		break;
	case Op::DJNZ:
		if (ins.wide) {
			const uint16_t bc = static_cast<uint16_t>(r16(BC) - 1);

			set_r16(BC, bc);
			if (bc)
				m_pc = y.value;
		} else if (--m_r[B]) {
			m_pc = y.value;
		}
		break;
	case Op::JP:
	case Op::JR:
		if (!test(static_cast<uint8_t>(x.value)))
			return ins.states_not_taken;
		m_pc = y.value;
		break;
	case Op::CALL:
		if (!test(static_cast<uint8_t>(x.value)))
			return ins.states_not_taken;
		push(m_pc);
		m_pc = y.value;
		break;
	case Op::CALLR:
		push(m_pc);
		m_pc = y.value;
		break;
	case Op::RET:
		if (!test(static_cast<uint8_t>(x.value)))
			return ins.states_not_taken;
		m_pc = pop();
		break;
	case Op::RETI:
		set_r16(AF, pop());
		m_pc = pop();
		break;
	case Op::UNKNOWN:	/* step() stops where it decodes one */
		break;
	}
	return ins.states;
}

void
Tlcs90::save(Tlcs90State &out) const
{
	out = { m_r[A], m_f, m_r[B], m_r[C], m_r[D], m_r[E], m_r[H], m_r[L],
	        m_alt[6], m_alt[7], m_alt[0], m_alt[1], m_alt[2], m_alt[3], m_alt[4], m_alt[5],
	        m_ix, m_iy, m_sp, m_pc, m_halted, m_ei_pending };
}

void
Tlcs90::load(const Tlcs90State &in)
{
	m_r[A] = in.a; m_f = in.f; m_r[B] = in.b; m_r[C] = in.c; m_r[D] = in.d; m_r[E] = in.e; m_r[H] = in.h; m_r[L] = in.l;
	m_alt[6] = in.a2; m_alt[7] = in.f2; m_alt[0] = in.b2; m_alt[1] = in.c2; m_alt[2] = in.d2; m_alt[3] = in.e2;
	m_alt[4] = in.h2; m_alt[5] = in.l2;
	m_ix = in.ix; m_iy = in.iy; m_sp = in.sp; m_pc = in.pc;
	m_halted = in.halted; m_ei_pending = in.ei_pending;
}

} /* namespace odin */
