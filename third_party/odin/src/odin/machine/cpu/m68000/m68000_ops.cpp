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

/* The instructions of the 68000. The order of the bus cycles and the idle
 * clocks between them are those of the SingleStepTests 680x0 suite. */

#include "odin/machine/cpu/m68000/m68000.hpp"

#include <bit>
#include <utility>

namespace odin {

namespace {

enum Alu : unsigned { ALU_OR, ALU_AND, ALU_SUB, ALU_ADD, ALU_EOR = 5, ALU_CMP };
/* BTST, BCHG, BCLR, BSET (bits 7-6 of the word); ASd, LSd, ROXd, ROd (the
 * type of the shift) */
enum BitOp : unsigned { BIT_TEST, BIT_CHANGE, BIT_CLEAR, BIT_SET };
enum ShiftOp : unsigned { SHIFT_ARITH, SHIFT_LOGICAL, ROTATE_X, ROTATE };

} /* namespace */

/* OR, AND, SUB, ADD, EOR and CMP: the result and the flags */
uint32_t
M68000::alu(unsigned kind, Size s, uint32_t dst, uint32_t src)
{
	uint32_t r;

	switch (kind) {
	case ALU_OR:
		r = dst | src;
		flags_nz(s, r);
		return r;
	case ALU_AND:
		r = dst & src;
		flags_nz(s, r);
		return r;
	case ALU_EOR:
		r = dst ^ src;
		flags_nz(s, r);
		return r;
	case ALU_ADD:
		r = dst + src;
		flags_add(s, dst, src, r);
		return r;
	case ALU_SUB:
		r = dst - src;
		flags_sub(s, dst, src, r);
		return r;
	default: {
		const uint16_t x = m_sr & SR_X;
		r = dst - src;
		flags_sub(s, dst, src, r);
		m_sr = static_cast<uint16_t>((m_sr & ~SR_X) | x);
		return dst;
	}
	}
}

/* The ALU on <ea> and Dn, its result to <ea> */
void
M68000::alu_to_ea(unsigned kind, Size s, unsigned mode, unsigned reg, unsigned dreg)
{
	const Ea e = ea(mode, reg, s);
	const uint32_t r = alu(kind, s, get(e, s), m_d[dreg] & mask(s));

	prefetch();
	write_rmw(s, e.addr, r);
}

/* The ALU on Dn and <ea>, its result to Dn */
void
M68000::alu_to_dn(unsigned kind, Size s, unsigned mode, unsigned reg, unsigned dreg)
{
	const Ea e = ea(mode, reg, s);
	const uint32_t r = alu(kind, s, m_d[dreg] & mask(s), get(e, s));

	prefetch();
	set_d(dreg, s, r);
	if (s == LONG)
		idle(e.kind == Ea::MEM ? 2 : 4);
}

void
M68000::execute()
{
	switch (m_ir >> 12) {
	case 0x0:
		op_0000();
		break;
	case 0x1:
		op_move(BYTE);
		break;
	case 0x2:
		op_move(LONG);
		break;
	case 0x3:
		op_move(WORD);
		break;
	case 0x4:
		op_0100();
		break;
	case 0x5:
		op_0101();
		break;
	case 0x6:
		op_0110();
		break;
	case 0x7:
		op_0111();
		break;
	case 0x8:
		op_1000();
		break;
	case 0x9:
		op_add_sub(false);
		break;
	case 0xA:
		illegal(10);
		break;
	case 0xB:
		op_1011();
		break;
	case 0xC:
		op_1100();
		break;
	case 0xD:
		op_add_sub(true);
		break;
	case 0xE:
		op_1110();
		break;
	default:
		illegal(11);
		break;
	}
}

/* 0000: bit operations, MOVEP and the immediate operations */
void
M68000::op_0000()
{
	const uint16_t op = m_ir;

	if ((op & 0x0138) == 0x0108) {
		op_movep();
	} else if (op & 0x0100) {
		op_bit((op >> 6) & 3, true);
	} else if ((op & 0x0E00) == 0x0800) {
		op_bit((op >> 6) & 3, false);
	} else {
		op_immediate();
	}
}

/* BTST, BCHG, BCLR, BSET: the bit number from Dn or from an immediate
 * word; modulo 32 in a data register, modulo 8 in memory. */
void
M68000::op_bit(unsigned kind, bool dynamic)
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;
	uint32_t bit = dynamic ? m_d[(op >> 9) & 7] : next_word();

	if (mode == 1 || (mode == 7 && reg > (kind == BIT_TEST ? 4u : 1u)) || (!dynamic && mode == 7 && reg == 4)) {
		illegal();
		return;
	}
	if (mode == 0) {
		bit &= 31;
		uint32_t &d = m_d[reg];
		const bool was = (d >> bit) & 1;
		m_sr = static_cast<uint16_t>(was ? m_sr & ~SR_Z : m_sr | SR_Z);
		prefetch();
		switch (kind) {
		case BIT_TEST:
			idle(2);
			break;
		case BIT_CHANGE:
			d ^= 1u << bit;
			idle(bit < 16 ? 2 : 4);
			break;
		case BIT_CLEAR:
			d &= ~(1u << bit);
			idle(bit < 16 ? 4 : 6);
			break;
		default:
			d |= 1u << bit;
			idle(bit < 16 ? 2 : 4);
			break;
		}
		return;
	}
	const Ea e = ea(mode, reg, BYTE);
	uint32_t v = get(e, BYTE);
	bit &= 7;
	m_sr = static_cast<uint16_t>(((v >> bit) & 1) ? m_sr & ~SR_Z : m_sr | SR_Z);
	if (kind == BIT_TEST) {
		prefetch();
		if (e.kind == Ea::IMM)
			idle(2);
		return;
	}
	if (kind == BIT_CHANGE)
		v ^= 1u << bit;
	else if (kind == BIT_CLEAR)
		v &= ~(1u << bit);
	else
		v |= 1u << bit;
	prefetch();
	write_byte(e.addr, static_cast<uint8_t>(v));
}

/* ORI, ANDI, SUBI, ADDI, EORI, CMPI, and ORI/ANDI/EORI to CCR and SR */
void
M68000::op_immediate()
{
	const uint16_t op = m_ir;
	const unsigned kind = (op >> 9) & 7, mode = (op >> 3) & 7, reg = op & 7;
	const unsigned size_bits = (op >> 6) & 3;

	if (mode == 7 && reg == 4 && (kind == 0 || kind == 1 || kind == 5) && size_bits < 2) {
		op_to_sr_ccr(kind | (size_bits << 4));
		return;
	}
	if (kind == 4 || kind == 7 || size_bits == 3 || mode == 1 || (mode == 7 && reg > 1)) {
		illegal();
		return;
	}
	const Size s = size_bits_to(size_bits);
	const uint32_t src = s == LONG ? next_long() : (next_word() & mask(s));
	const Ea e = ea(mode, reg, s);
	const uint32_t dst = get(e, s);
	const uint32_t r = alu(kind, s, dst, src);

	if (e.kind == Ea::DREG) {
		prefetch();
		if (kind != ALU_CMP)
			set_d(reg, s, r);
		if (s == LONG)
			idle(kind == ALU_CMP ? 2 : 4);
		return;
	}
	prefetch();
	if (kind != ALU_CMP)
		write_rmw(s, e.addr, r);
}

/* ORI, ANDI, EORI to CCR (byte) or to SR (word, privileged) */
void
M68000::op_to_sr_ccr(unsigned kind)
{
	const bool to_sr = (kind >> 4) != 0;

	if (to_sr && !supervisor()) {
		privilege();
		return;
	}
	const uint16_t imm = next_word();
	const uint16_t cur = to_sr ? m_sr : static_cast<uint16_t>(m_sr & 0xFF);
	uint16_t v;
	switch (kind & 7) {
	case 0:
		v = cur | imm;
		break;
	case 1:
		v = cur & imm;
		break;
	default:
		v = cur ^ imm;
		break;
	}
	idle(8);
	if (to_sr)
		set_sr(v);
	else
		set_ccr(v);
	refill();
}

/* MOVEP: bytes at every other address, the high byte first */
void
M68000::op_movep()
{
	const uint16_t op = m_ir;
	const unsigned dreg = (op >> 9) & 7;
	const uint32_t addr = m_a[op & 7] + static_cast<uint32_t>(static_cast<int16_t>(next_word()));

	switch ((op >> 6) & 3) {
	case 0: {
		const uint32_t hi = read_byte(addr);
		const uint32_t lo = read_byte(addr + 2);
		set_d(dreg, WORD, (hi << 8) | lo);
		break;
	}
	case 1: {
		uint32_t v = 0;
		for (unsigned i = 0; i < 4; i++)
			v = (v << 8) | read_byte(addr + 2 * i);
		m_d[dreg] = v;
		break;
	}
	case 2:
		write_byte(addr, static_cast<uint8_t>(m_d[dreg] >> 8));
		write_byte(addr + 2, static_cast<uint8_t>(m_d[dreg]));
		break;
	default:
		for (unsigned i = 0; i < 4; i++)
			write_byte(addr + 2 * i, static_cast<uint8_t>(m_d[dreg] >> (24 - 8 * i)));
		break;
	}
	prefetch();
}

/* MOVE and MOVEA. To -(An) the prefetch comes before the write; to an
 * absolute long address after a memory source, the second address word
 * is fetched after the write. */
void
M68000::op_move(Size s)
{
	const uint16_t op = m_ir;
	const unsigned smode = (op >> 3) & 7, sreg = op & 7;
	const unsigned dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;

	if ((s == BYTE && (smode == 1 || dmode == 1)) || (smode == 7 && sreg > 4) || (dmode == 7 && dreg > 1)) {
		illegal();
		return;
	}
	const Ea src = ea(smode, sreg, s);
	const uint32_t v = get(src, s);

	if (dmode == 1) {
		m_a[dreg] = s == WORD ? sext(WORD, v) : v;
		prefetch();
		return;
	}
	if (dmode == 0) {
		flags_nz(s, v);
		set_d(dreg, s, v);
		prefetch();
		return;
	}
	const uint32_t step = step_of(s, dreg);
	uint32_t addr;

	switch (dmode) {
	case 2:
	case 3:
		addr = m_a[dreg];
		flags_nz(s, v);
		write(s, addr, v);
		if (dmode == 3)
			m_a[dreg] += step;
		prefetch();
		return;
	case 4:
		flags_nz(s, v);
		prefetch();
		if (s == LONG) {
			/* a word at a time, the low one first */
			m_a[dreg] -= 2;
			write_word(m_a[dreg], static_cast<uint16_t>(v));
			m_a[dreg] -= 2;
			write_word(m_a[dreg], static_cast<uint16_t>(v >> 16));
		} else {
			m_a[dreg] -= step;
			write(s, m_a[dreg], v);
		}
		return;
	case 5:
		addr = m_a[dreg] + static_cast<uint32_t>(static_cast<int16_t>(next_word()));
		break;
	case 6:
		idle(2);
		addr = m_a[dreg] + index(next_word());
		break;
	default:
		if (dreg == 0) {
			addr = static_cast<uint32_t>(static_cast<int16_t>(next_word()));
		} else if (src.kind == Ea::MEM) {
			addr = static_cast<uint32_t>(next_word()) << 16 | m_irc;
			flags_nz(s, v);
			write(s, addr, v);
			next_word();
			prefetch();
			return;
		} else {
			addr = next_long();
		}
		break;
	}
	flags_nz(s, v);
	write(s, addr, v);
	prefetch();
}

/* 0100: the miscellaneous instructions */
void
M68000::op_0100()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;
	const unsigned size_bits = (op >> 6) & 3;

	if ((op & 0xF1C0) == 0x41C0) {				/* LEA */
		if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) {
			illegal();
			return;
		}
		m_a[(op >> 9) & 7] = control_address(mode, reg);
		prefetch();
		return;
	}
	if ((op & 0xF1C0) == 0x4180) {				/* CHK */
		op_chk();
		return;
	}
	if ((op & 0xF100) == 0x4100) {
		illegal();
		return;
	}

	switch ((op >> 8) & 0xF) {
	case 0x0:
	case 0x2:
	case 0x4:
	case 0x6: {
		const unsigned which = (op >> 9) & 3;	/* NEGX, CLR, NEG, NOT */
		if (size_bits == 3) {
			if (which == 0) {			/* MOVE from SR */
				if (mode == 1 || (mode == 7 && reg > 1)) {
					illegal();
					return;
				}
				const Ea e = ea(mode, reg, WORD);
				if (e.kind == Ea::DREG) {
					set_d(reg, WORD, m_sr);
					prefetch();
					idle(2);
					return;
				}
				read_word(e.addr);
				prefetch();
				write_word(e.addr, m_sr);
				return;
			}
			if (which == 1) {
				illegal();
				return;
			}
			if (which == 3 && !supervisor()) {	/* MOVE to SR */
				privilege();
				return;
			}
			if (mode == 1 || (mode == 7 && reg > 4)) {
				illegal();
				return;
			}
			const Ea e = ea(mode, reg, WORD);	/* MOVE to CCR or SR */
			const uint32_t v = get(e, WORD);
			idle(4);
			if (which == 3)
				set_sr(static_cast<uint16_t>(v));
			else
				set_ccr(v);
			refill();
			return;
		}
		if (mode == 1 || (mode == 7 && reg > 1)) {
			illegal();
			return;
		}
		const Size s = size_bits_to(size_bits);
		const Ea e = ea(mode, reg, s);
		const uint32_t v = get(e, s);
		uint32_t r;
		switch (which) {
		case 0: {					/* NEGX */
			const uint32_t x = (m_sr & SR_X) ? 1 : 0;
			r = 0 - v - x;
			const uint16_t z = m_sr & SR_Z;
			flags_sub(s, 0, v, r);
			if (!(r & mask(s)))
				m_sr = static_cast<uint16_t>((m_sr & ~SR_Z) | z);
			break;
		}
		case 1:						/* CLR */
			r = 0;
			m_sr = static_cast<uint16_t>((m_sr & ~(SR_N | SR_V | SR_C)) | SR_Z);
			break;
		case 2:						/* NEG */
			r = 0 - v;
			flags_sub(s, 0, v, r);
			break;
		default:					/* NOT */
			r = ~v;
			flags_nz(s, r);
			break;
		}
		prefetch();
		if (e.kind == Ea::DREG) {
			set_d(reg, s, r);
			if (s == LONG)
				idle(2);
		} else {
			write_rmw(s, e.addr, r);
		}
		return;
	}
	case 0x8:
		if (size_bits == 0) {					/* NBCD */
			op_nbcd();
			return;
		}
		if (size_bits == 1) {
			if (mode == 0) {				/* SWAP */
				m_d[reg] = (m_d[reg] << 16) | (m_d[reg] >> 16);
				flags_nz(LONG, m_d[reg]);
				prefetch();
				return;
			}
			if (mode == 1 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) {
				illegal();
				return;
			}
			const bool absolute = mode == 7 && reg < 2;	/* PEA */
			const uint32_t addr = control_address(mode, reg);
			if (absolute) {
				push_long(addr);
				prefetch();
			} else {
				prefetch();
				push_long(addr);
			}
			return;
		}
		if (mode == 0) {					/* EXT */
			if (size_bits == 2)
				set_d(reg, WORD, sext(BYTE, m_d[reg]));
			else
				m_d[reg] = sext(WORD, m_d[reg]);
			flags_nz(size_bits == 2 ? WORD : LONG, m_d[reg]);
			prefetch();
			return;
		}
		op_movem(false);
		return;
	case 0xA:
		if (op == 0x4AFC) {
			illegal();
			return;
		}
		if (mode == 1 || (mode == 7 && reg > 1)) {
			illegal();
			return;
		}
		if (size_bits == 3) {					/* TAS */
			const Ea e = ea(mode, reg, BYTE);
			if (e.kind == Ea::DREG) {
				const uint32_t v = m_d[reg] & 0xFF;
				flags_nz(BYTE, v);
				set_d(reg, BYTE, v | 0x80);
				prefetch();
				return;
			}
			const uint32_t v = read_byte(e.addr);
			flags_nz(BYTE, v);
			idle(2);
			write_byte(e.addr, static_cast<uint8_t>(v | 0x80));
			prefetch();
			return;
		} else {						/* TST */
			const Size s = size_bits_to(size_bits);
			const Ea e = ea(mode, reg, s);
			flags_nz(s, get(e, s));
			prefetch();
			return;
		}
	case 0xC:
		if (size_bits >= 2) {
			op_movem(true);
			return;
		}
		illegal();
		return;
	case 0xE:
		op_misc_4e();
		return;
	default:
		illegal();
		return;
	}
}

/* CHK: Dn above the bound traps after 4 clocks, Dn below 0 after 6. N
 * then tells Dn < 0 and is kept otherwise; Z, V and C as the suite has
 * them. */
void
M68000::op_chk()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;

	if (mode == 1 || (mode == 7 && reg > 4)) {
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, WORD);
	const int16_t bound = static_cast<int16_t>(get(e, WORD));
	const int16_t v = static_cast<int16_t>(m_d[(op >> 9) & 7]);

	prefetch();
	m_sr &= static_cast<uint16_t>(~(SR_Z | SR_V | SR_C));
	if (v == 0)
		m_sr |= SR_Z;
	if (v > bound || v < 0) {
		m_sr = static_cast<uint16_t>(v < 0 ? m_sr | SR_N : m_sr & ~SR_N);
		idle(v > bound ? 4 : 6);
		exception(6, m_pc);
		return;
	}
	idle(6);
}

/* MOVEM: the mask word, then one transfer per bit. From memory, one more
 * word is read after the last register; words are sign-extended. To
 * -(An), the registers go in reverse order, the low word first. */
void
M68000::op_movem(bool to_registers)
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;
	const Size s = (op & 0x40) ? LONG : WORD;
	const uint32_t step = s == LONG ? 4 : 2;

	if (mode < 2 || (to_registers && mode == 4) || (!to_registers && mode == 3) ||
	    (mode == 7 && reg > (to_registers ? 3u : 1u))) {
		illegal();
		return;
	}
	const uint16_t list = next_word();
	uint32_t addr;
	if (mode == 3 || mode == 4)
		addr = m_a[reg];
	else
		addr = ea(mode, reg, s).addr;

	if (to_registers) {
		/* the 68000 has already stepped An when the first read faults */
		if (mode == 3 && (addr & 1))
			m_a[reg] = addr + 2;
		for (unsigned i = 0; i < 16; i++) {
			if (!(list & (1u << i)))
				continue;
			const uint32_t v = s == LONG ? read_long(addr) : sext(WORD, read_word(addr));
			if (i < 8)
				m_d[i] = v;
			else
				m_a[i - 8] = v;
			addr += step;
		}
		read_word(addr);
		if (mode == 3)
			m_a[reg] = addr;
		prefetch();
		return;
	}
	if (mode == 4) {
		for (unsigned i = 0; i < 16; i++) {
			if (!(list & (1u << i)))
				continue;
			const uint32_t v = i < 8 ? m_a[7 - i] : m_d[15 - i];
			addr -= step;
			if (s == LONG)
				write_long_low_first(addr, v);
			else
				write_word(addr, static_cast<uint16_t>(v));
		}
		m_a[reg] = addr;
		prefetch();
		return;
	}
	for (unsigned i = 0; i < 16; i++) {
		if (!(list & (1u << i)))
			continue;
		const uint32_t v = i < 8 ? m_d[i] : m_a[i - 8];
		if (s == LONG)
			write_long(addr, v);
		else
			write_word(addr, static_cast<uint16_t>(v));
		addr += step;
	}
	prefetch();
}

/* 4E40-4EFF: TRAP, LINK, UNLK, MOVE USP, the 4E7x group, JSR, JMP */
void
M68000::op_misc_4e()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;

	if ((op & 0xFFF0) == 0x4E40) {				/* TRAP */
		idle(4);
		exception(32 + (op & 15), m_au - 2);
		return;
	}
	if ((op & 0xFFF8) == 0x4E50) {				/* LINK */
		const uint32_t disp = sext(WORD, next_word());
		m_a[7] -= 4;
		write_long(m_a[7], m_a[reg]);
		m_a[reg] = m_a[7];
		m_a[7] += disp;
		prefetch();
		return;
	}
	if ((op & 0xFFF8) == 0x4E58) {				/* UNLK */
		m_a[7] = m_a[reg];
		m_a[reg] = pop_long();
		prefetch();
		return;
	}
	if ((op & 0xFFF0) == 0x4E60) {				/* MOVE An,USP / USP,An */
		if (!supervisor()) {
			privilege();
			return;
		}
		if (op & 8)
			m_a[reg] = m_other_sp;
		else
			m_other_sp = m_a[reg];
		prefetch();
		return;
	}
	switch (op) {
	case 0x4E70:						/* RESET */
		if (!supervisor()) {
			privilege();
			return;
		}
		idle(128);
		m_bus.reset_devices();
		prefetch();
		return;
	case 0x4E71:						/* NOP */
		prefetch();
		return;
	case 0x4E72: {						/* STOP */
		if (!supervisor()) {
			privilege();
			return;
		}
		const uint16_t v = next_word();
		set_sr(v);
		m_pc = m_au - 2;
		m_stopped = true;
		return;
	}
	case 0x4E73: {						/* RTE */
		if (!supervisor()) {
			privilege();
			return;
		}
		uint16_t sr;
		const uint32_t pc = pop_frame(sr);

		set_sr(sr);
		jump(pc);
		return;
	}
	case 0x4E75:						/* RTS */
		jump(pop_long());
		return;
	case 0x4E76:						/* TRAPV */
		prefetch();
		if (m_sr & SR_V)
			exception(7, m_pc);
		return;
	case 0x4E77: {						/* RTR */
		uint16_t ccr;
		const uint32_t pc = pop_frame(ccr);

		set_ccr(ccr);
		jump(pc);
		return;
	}
	default:
		break;
	}
	if ((op & 0xFF80) != 0x4E80 || mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) {
		illegal();
		return;
	}
	/* JSR and JMP: the last extension word is used from IRC, the queue
	 * then comes from the target. */
	uint32_t target;
	uint32_t next = m_au - 2;
	switch (mode) {
	case 2:
		target = m_a[reg];
		break;
	case 5:
		idle(2);
		target = m_a[reg] + sext(WORD, m_irc);
		next += 2;
		break;
	case 6:
		idle(6);
		target = m_a[reg] + index(m_irc);
		next += 2;
		break;
	default:
		switch (reg) {
		case 0:
			idle(2);
			target = sext(WORD, m_irc);
			next += 2;
			break;
		case 1: {
			const uint32_t hi = next_word();
			target = (hi << 16) | m_irc;
			next = m_au;
			break;
		}
		case 2:
			idle(2);
			target = m_au - 2 + sext(WORD, m_irc);
			next += 2;
			break;
		default:
			idle(6);
			target = m_au - 2 + index(m_irc);
			next += 2;
			break;
		}
		break;
	}
	if (op & 0x40) {
		jump(target);
		return;
	}
	m_au = target;
	m_ir = fetch_word(m_au);
	push_long(next);
	m_irc = fetch_word(m_au + 2);
	m_au += 4;
	m_pc = target;
}

/* 0101: ADDQ, SUBQ, Scc, DBcc */
void
M68000::op_0101()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;

	if (((op >> 6) & 3) == 3) {
		const bool t = cond((op >> 8) & 15);
		if (mode == 1) {					/* DBcc */
			if (t) {
				idle(4);
				next_word();
				prefetch();
				return;
			}
			const uint32_t disp = sext(WORD, m_irc);
			const uint16_t count = static_cast<uint16_t>(m_d[reg] - 1);
			set_d(reg, WORD, count);
			idle(2);
			if (count != 0xFFFF) {
				jump(m_au - 2 + disp);
				return;
			}
			read_word(m_au - 2 + disp);
			next_word();
			prefetch();
			return;
		}
		if (mode == 7 && reg > 1) {
			illegal();
			return;
		}
		const Ea e = ea(mode, reg, BYTE);			/* Scc */
		if (e.kind == Ea::DREG) {
			set_d(reg, BYTE, t ? 0xFF : 0);
			prefetch();
			if (t)
				idle(2);
			return;
		}
		read_byte(e.addr);
		prefetch();
		write_byte(e.addr, t ? 0xFF : 0);
		return;
	}
	const Size s = size_bits_to((op >> 6) & 3);
	const uint32_t q = ((op >> 9) & 7) ? ((op >> 9) & 7) : 8;
	const bool add = !(op & 0x100);

	if ((s == BYTE && mode == 1) || (mode == 7 && reg > 1)) {
		illegal();
		return;
	}
	if (mode == 1) {
		m_a[reg] = add ? m_a[reg] + q : m_a[reg] - q;
		prefetch();
		idle(s == WORD ? 4 : 2);
		return;
	}
	const Ea e = ea(mode, reg, s);
	const uint32_t r = alu(add ? ALU_ADD : ALU_SUB, s, get(e, s), q);
	prefetch();
	if (e.kind == Ea::DREG) {
		set_d(reg, s, r);
		if (s == LONG)
			idle(4);
		return;
	}
	write_rmw(s, e.addr, r);
}

/* 0110: BRA, BSR, Bcc */
void
M68000::op_0110()
{
	const uint16_t op = m_ir;
	const unsigned cc = (op >> 8) & 15;
	const bool is_word = (op & 0xFF) == 0;
	const uint32_t disp = is_word ? sext(WORD, m_irc) : sext(BYTE, op);
	const uint32_t base = m_au - 2;

	if (cc == 1) {							/* BSR */
		idle(2);
		push_long(base + (is_word ? 2 : 0));
		jump(base + disp);
		return;
	}
	if (cond(cc)) {
		idle(2);
		jump(base + disp);
		return;
	}
	idle(4);
	if (is_word)
		next_word();
	prefetch();
}

/* 0111: MOVEQ */
void
M68000::op_0111()
{
	const uint16_t op = m_ir;

	if (op & 0x100) {
		illegal();
		return;
	}
	const uint32_t v = sext(BYTE, op);
	m_d[(op >> 9) & 7] = v;
	flags_nz(LONG, v);
	prefetch();
}

/* 1000: OR, DIVU, DIVS, SBCD */
void
M68000::op_1000()
{
	const uint16_t op = m_ir;

	if ((op & 0x1C0) == 0x0C0) {
		op_divu();
		return;
	}
	if ((op & 0x1C0) == 0x1C0) {
		op_divs();
		return;
	}
	if ((op & 0x1F0) == 0x100) {
		op_abcd_sbcd(false);
		return;
	}
	const Size s = size_bits_to((op >> 6) & 3);
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;

	if (op & 0x100) {						/* OR Dn,<ea> */
		if (mode < 2 || (mode == 7 && reg > 1)) {
			illegal();
			return;
		}
		alu_to_ea(ALU_OR, s, mode, reg, dreg);
		return;
	}
	if (mode == 1 || (mode == 7 && reg > 4)) {
		illegal();
		return;
	}
	alu_to_dn(ALU_OR, s, mode, reg, dreg);				/* OR <ea>,Dn */
}

/* 1001 and 1101: SUB and ADD, SUBA and ADDA, SUBX and ADDX */
void
M68000::op_add_sub(bool add)
{
	const uint16_t op = m_ir;
	const unsigned opmode = (op >> 6) & 7;
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;
	const unsigned kind = add ? ALU_ADD : ALU_SUB;

	if (opmode == 3 || opmode == 7) {				/* ADDA, SUBA */
		const Size s = opmode == 3 ? WORD : LONG;
		if (mode == 7 && reg > 4) {
			illegal();
			return;
		}
		const Ea e = ea(mode, reg, s);
		const uint32_t v = s == WORD ? sext(WORD, get(e, s)) : get(e, s);
		m_a[dreg] = add ? m_a[dreg] + v : m_a[dreg] - v;
		prefetch();
		idle(s == WORD || e.kind != Ea::MEM ? 4 : 2);
		return;
	}
	const Size s = size_bits_to(opmode & 3);
	if ((opmode & 4) && mode < 2) {
		op_addx_subx(add);
		return;
	}
	if (opmode & 4) {						/* Dn,<ea> */
		if (mode == 7 && reg > 1) {
			illegal();
			return;
		}
		alu_to_ea(kind, s, mode, reg, dreg);
		return;
	}
	if ((s == BYTE && mode == 1) || (mode == 7 && reg > 4)) {	/* <ea>,Dn */
		illegal();
		return;
	}
	alu_to_dn(kind, s, mode, reg, dreg);
}

/* ADDX and SUBX: Z only cleared; -(An),-(An) reads and writes a long
 * low word first. */
void
M68000::op_addx_subx(bool add)
{
	const uint16_t op = m_ir;
	const Size s = size_bits_to((op >> 6) & 3);
	const unsigned rx = op & 7, ry = (op >> 9) & 7;
	const uint32_t x = (m_sr & SR_X) ? 1 : 0;
	const uint16_t z = m_sr & SR_Z;
	uint32_t src, dst, daddr = 0;

	if (op & 8) {
		const uint32_t step = step_of(s, rx);
		const uint32_t dstep = step_of(s, ry);
		idle(2);
		if (s == LONG) {
			/* a word at a time, the low one first */
			m_a[rx] -= 2;
			const uint32_t lo = read_word(m_a[rx]);
			m_a[rx] -= 2;
			src = (static_cast<uint32_t>(read_word(m_a[rx])) << 16) | lo;
			m_a[ry] -= 2;
			const uint32_t dlo = read_word(m_a[ry]);
			m_a[ry] -= 2;
			daddr = m_a[ry];
			dst = (static_cast<uint32_t>(read_word(daddr)) << 16) | dlo;
		} else {
			m_a[rx] -= step;
			src = read(s, m_a[rx]);
			m_a[ry] -= dstep;
			daddr = m_a[ry];
			dst = read(s, daddr);
		}
	} else {
		src = m_d[rx] & mask(s);
		dst = m_d[ry] & mask(s);
	}
	const uint32_t r = add ? dst + src + x : dst - src - x;
	if (add)
		flags_add(s, dst, src, r);
	else
		flags_sub(s, dst, src, r);
	if (!(r & mask(s)))
		m_sr = static_cast<uint16_t>((m_sr & ~SR_Z) | z);

	if (op & 8) {
		if (s == LONG) {
			write_word(daddr + 2, static_cast<uint16_t>(r));
			prefetch();
			write_word(daddr, static_cast<uint16_t>(r >> 16));
		} else {
			prefetch();
			write(s, daddr, r);
		}
		return;
	}
	set_d(ry, s, r);
	prefetch();
	if (s == LONG)
		idle(4);
}

/* 1011: CMP, CMPA, CMPM, EOR */
void
M68000::op_1011()
{
	const uint16_t op = m_ir;
	const unsigned opmode = (op >> 6) & 7;
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;

	if (opmode == 3 || opmode == 7) {				/* CMPA */
		const Size s = opmode == 3 ? WORD : LONG;
		if (mode == 7 && reg > 4) {
			illegal();
			return;
		}
		const Ea e = ea(mode, reg, s);
		const uint32_t v = s == WORD ? sext(WORD, get(e, s)) : get(e, s);
		alu(ALU_CMP, LONG, m_a[dreg], v);
		prefetch();
		idle(2);
		return;
	}
	const Size s = size_bits_to(opmode & 3);
	if (opmode & 4) {
		if (mode == 1) {
			op_cmpm();
			return;
		}
		if (mode == 7 && reg > 1) {				/* EOR Dn,<ea> */
			illegal();
			return;
		}
		const Ea e = ea(mode, reg, s);
		const uint32_t r = alu(ALU_EOR, s, get(e, s), m_d[dreg] & mask(s));
		prefetch();
		if (e.kind == Ea::DREG) {
			set_d(reg, s, r);
			if (s == LONG)
				idle(4);
			return;
		}
		write_rmw(s, e.addr, r);
		return;
	}
	if ((s == BYTE && mode == 1) || (mode == 7 && reg > 4)) {	/* CMP <ea>,Dn */
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, s);
	alu(ALU_CMP, s, m_d[dreg] & mask(s), get(e, s));
	prefetch();
	if (s == LONG)
		idle(2);
}

void
M68000::op_cmpm()
{
	const uint16_t op = m_ir;
	const Size s = size_bits_to((op >> 6) & 3);
	const Ea src = ea(3, op & 7, s);
	const uint32_t a = get(src, s);
	const Ea dst = ea(3, (op >> 9) & 7, s);
	const uint32_t b = get(dst, s);

	alu(ALU_CMP, s, b, a);
	prefetch();
}

/* 1100: AND, MULU, MULS, ABCD, EXG */
void
M68000::op_1100()
{
	const uint16_t op = m_ir;

	if ((op & 0x1C0) == 0x0C0) {
		op_mul(false);
		return;
	}
	if ((op & 0x1C0) == 0x1C0) {
		op_mul(true);
		return;
	}
	if ((op & 0x1F0) == 0x100) {
		op_abcd_sbcd(true);
		return;
	}
	if ((op & 0x1F8) == 0x140 || (op & 0x1F8) == 0x148 || (op & 0x1F8) == 0x188) {
		op_exg();
		return;
	}
	const Size s = size_bits_to((op >> 6) & 3);
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;

	if (op & 0x100) {						/* AND Dn,<ea> */
		if (mode < 2 || (mode == 7 && reg > 1)) {
			illegal();
			return;
		}
		alu_to_ea(ALU_AND, s, mode, reg, dreg);
		return;
	}
	if (mode == 1 || (mode == 7 && reg > 4)) {
		illegal();
		return;
	}
	alu_to_dn(ALU_AND, s, mode, reg, dreg);				/* AND <ea>,Dn */
}

void
M68000::op_exg()
{
	const uint16_t op = m_ir;
	const unsigned rx = (op >> 9) & 7, ry = op & 7;

	switch (op & 0x1F8) {
	case 0x140:
		std::swap(m_d[rx], m_d[ry]);
		break;
	case 0x148:
		std::swap(m_a[rx], m_a[ry]);
		break;
	default:
		std::swap(m_d[rx], m_a[ry]);
		break;
	}
	prefetch();
	idle(2);
}

/* MULU: 38 clocks plus 2 per bit set in the source; MULS: plus 2 per
 * change between two bits of the source shifted left by one. */
void
M68000::op_mul(bool is_signed)
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;

	if (mode == 1 || (mode == 7 && reg > 4)) {
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, WORD);
	const uint32_t src = get(e, WORD);
	uint32_t r;
	unsigned n;

	if (is_signed) {
		r = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(src)) *
		                          static_cast<int32_t>(static_cast<int16_t>(m_d[dreg])));
		n = static_cast<unsigned>(std::popcount(((src << 1) ^ src) & 0xFFFF));
	} else {
		r = src * (m_d[dreg] & 0xFFFF);
		n = static_cast<unsigned>(std::popcount(src));
	}
	m_d[dreg] = r;
	flags_nz(LONG, r);
	prefetch();
	idle(34 + 2 * n);
}

/* DIVU: the clocks of the shift-and-subtract the 68000 does (yacht.txt
 * flowchart). Overflow sets V and leaves Dn, N and Z. */
void
M68000::op_divu()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;

	if (mode == 1 || (mode == 7 && reg > 4)) {
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, WORD);
	const uint32_t divisor = get(e, WORD);
	uint32_t dividend = m_d[dreg];

	if (divisor == 0) {
		divide_by_zero();
		return;
	}
	if ((dividend >> 16) >= divisor) {
		m_sr = static_cast<uint16_t>((m_sr & ~SR_C) | SR_V);
		idle(6);
		prefetch();
		return;
	}
	unsigned clocks = 38 * 2;
	const uint32_t hdivisor = divisor << 16;
	for (unsigned i = 0; i < 15; i++) {
		if (static_cast<int32_t>(dividend) < 0) {
			dividend <<= 1;
			dividend -= hdivisor;
		} else {
			dividend <<= 1;
			if (dividend >= hdivisor) {
				dividend -= hdivisor;
				clocks += 2;
			} else {
				clocks += 4;
			}
		}
	}
	const uint32_t quot = m_d[dreg] / divisor, rem = m_d[dreg] % divisor;
	m_d[dreg] = (rem << 16) | quot;
	flags_nz(WORD, quot);
	idle(clocks - 4);
	prefetch();
}

/* DIVS: the microcycles of the 68000 divide loop (yacht.txt flowchart,
 * as Clock Signal counts them). Overflow sets V and leaves Dn, N and Z. */
void
M68000::op_divs()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7, dreg = (op >> 9) & 7;

	if (mode == 1 || (mode == 7 && reg > 4)) {
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, WORD);
	const int32_t divisor = static_cast<int16_t>(get(e, WORD));
	const int32_t dividend = static_cast<int32_t>(m_d[dreg]);

	if (divisor == 0) {
		divide_by_zero();
		return;
	}
	/* 6 or 7 microcycles into the loop; an overflow stops there */
	unsigned micro = dividend < 0 ? 7 : 6;
	const int64_t q = static_cast<int64_t>(dividend) / divisor;
	if (q > 32767 || q < -32768) {
		m_sr = static_cast<uint16_t>((m_sr & ~SR_C) | SR_V);
		idle(micro * 2);
		prefetch();
		return;
	}
	/* 49 for the loop, one more per 0 among bits 15-1 of the absolute
	 * quotient, then the exit by the signs */
	const uint32_t aquot = static_cast<uint32_t>(q < 0 ? -q : q);
	micro += 49 + static_cast<unsigned>(std::popcount(~aquot & 0xFFFEu));
	micro += divisor < 0 ? 4 : dividend < 0 ? 5 : 3;
	const int32_t rem = dividend % divisor;
	m_d[dreg] = (static_cast<uint32_t>(rem) << 16) | (static_cast<uint32_t>(q) & 0xFFFF);
	flags_nz(WORD, static_cast<uint32_t>(q));
	idle(micro * 2);
	prefetch();
}

/* ABCD and SBCD: the binary sum or difference, then the decimal
 * correction; V and N as the 68000 leaves them (bit 7 before and after the
 * correction). Z is only cleared. */
uint32_t
M68000::bcd(bool add, uint32_t dst, uint32_t src)
{
	const uint32_t x = (m_sr & SR_X) ? 1 : 0;
	uint32_t res, carry, v;

	if (add) {
		const uint32_t ss = dst + src + x;
		const uint32_t bc = ((dst & src) | (~ss & (dst | src))) & 0x88;
		const uint32_t dc = (((ss + 0x66) ^ ss) & 0x110) >> 1;
		const uint32_t corf = (bc | dc) - ((bc | dc) >> 2);
		res = ss + corf;
		carry = ((bc | (ss & ~res)) >> 7) & 1;
		v = ((~ss & res) >> 7) & 1;
	} else {
		const uint32_t dd = dst - src - x;
		const uint32_t bc = ((~dst & src) | (dd & ~dst) | (dd & src)) & 0x88;
		const uint32_t corf = bc - (bc >> 2);
		res = dd - corf;
		carry = ((bc | (~dd & res)) >> 7) & 1;
		v = ((dd & ~res) >> 7) & 1;
	}
	uint16_t f = m_sr & static_cast<uint16_t>(~(SR_X | SR_N | SR_V | SR_C));
	if (carry)
		f |= SR_X | SR_C;
	if (v)
		f |= SR_V;
	if (res & 0x80)
		f |= SR_N;
	if (res & 0xFF)
		f &= static_cast<uint16_t>(~SR_Z);
	m_sr = f;
	return res & 0xFF;
}

void
M68000::op_abcd_sbcd(bool add)
{
	const uint16_t op = m_ir;
	const unsigned rx = op & 7, ry = (op >> 9) & 7;

	if (op & 8) {
		idle(2);
		m_a[rx] -= step_of(BYTE, rx);
		const uint32_t src = read_byte(m_a[rx]);
		m_a[ry] -= step_of(BYTE, ry);
		const uint32_t dst = read_byte(m_a[ry]);
		const uint32_t r = bcd(add, dst, src);
		prefetch();
		write_byte(m_a[ry], static_cast<uint8_t>(r));
		return;
	}
	set_d(ry, BYTE, bcd(add, m_d[ry] & 0xFF, m_d[rx] & 0xFF));
	prefetch();
	idle(2);
}

void
M68000::op_nbcd()
{
	const uint16_t op = m_ir;
	const unsigned mode = (op >> 3) & 7, reg = op & 7;

	if (mode == 1 || (mode == 7 && reg > 1)) {
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, BYTE);
	const uint32_t r = bcd(false, 0, get(e, BYTE));
	prefetch();
	if (e.kind == Ea::DREG) {
		set_d(reg, BYTE, r);
		idle(2);
		return;
	}
	write_byte(e.addr, static_cast<uint8_t>(r));
}

/* 1110: shifts and rotates */
void
M68000::op_1110()
{
	if (((m_ir >> 6) & 3) == 3)
		op_shift_memory();
	else
		op_shift_register();
}

namespace {

struct ShiftResult {
	uint32_t value;
	bool c, x, v;
};

/* One bit at a time: the carry is the last bit out, V for ASL is any
 * change of the sign bit on the way. */
ShiftResult
shift_bits(unsigned kind, bool left, uint32_t v, uint32_t m, uint32_t top, unsigned count, bool x)
{
	ShiftResult r{v & m, false, x, false};

	if (count == 0) {
		r.c = kind == ROTATE_X ? x : false;
		return r;
	}
	for (unsigned i = 0; i < count; i++) {
		const uint32_t old = r.value;
		bool out;
		if (left) {
			out = (old & top) != 0;
			uint32_t in = 0;
			if (kind == ROTATE)
				in = out;
			else if (kind == ROTATE_X)
				in = r.x;
			r.value = ((old << 1) | in) & m;
			if (kind == SHIFT_ARITH && ((old ^ r.value) & top))
				r.v = true;
		} else {
			out = (old & 1) != 0;
			uint32_t in = 0;
			if (kind == SHIFT_ARITH)
				in = (old & top) ? top : 0;
			else if (kind == ROTATE)
				in = out ? top : 0;
			else if (kind == ROTATE_X)
				in = r.x ? top : 0;
			r.value = ((old >> 1) | in) & m;
		}
		r.c = out;
		if (kind != ROTATE)
			r.x = out;
	}
	if (kind == ROTATE_X)
		r.c = r.x;
	/* ASR past the size: C and X cleared, as the suite has it (Musashi
	 * gives the sign there; not checked on a chip) */
	if (kind == SHIFT_ARITH && !left && count > static_cast<unsigned>(std::countr_zero(top)) + 1)
		r.c = r.x = false;
	return r;
}

} /* namespace */

/* Register shifts: 6 + 2n clocks, 8 + 2n for a long */
void
M68000::op_shift_register()
{
	const uint16_t op = m_ir;
	const Size s = size_bits_to((op >> 6) & 3);
	const unsigned kind = (op >> 3) & 3, reg = op & 7;
	const bool left = (op & 0x100) != 0;
	const unsigned count = (op & 0x20) ? (m_d[(op >> 9) & 7] & 63) : (((op >> 9) & 7) ? ((op >> 9) & 7) : 8);
	const ShiftResult r = shift_bits(kind, left, m_d[reg], mask(s), msb(s), count, (m_sr & SR_X) != 0);

	uint16_t f = m_sr & static_cast<uint16_t>(~(SR_N | SR_Z | SR_V | SR_C | (count && kind != ROTATE ? SR_X : 0)));
	if (r.c)
		f |= SR_C;
	if (count && kind != ROTATE && r.x)
		f |= SR_X;
	if (r.v)
		f |= SR_V;
	if ((r.value & mask(s)) == 0)
		f |= SR_Z;
	if (r.value & msb(s))
		f |= SR_N;
	m_sr = f;
	set_d(reg, s, r.value);
	prefetch();
	idle((s == LONG ? 4 : 2) + 2 * count);
}

/* Memory shifts: one bit of a word */
void
M68000::op_shift_memory()
{
	const uint16_t op = m_ir;
	const unsigned kind = (op >> 9) & 3, mode = (op >> 3) & 7, reg = op & 7;
	const bool left = (op & 0x100) != 0;

	if (op & 0x0800 || mode < 2 || (mode == 7 && reg > 1)) {
		illegal();
		return;
	}
	const Ea e = ea(mode, reg, WORD);
	const ShiftResult r = shift_bits(kind, left, read_word(e.addr), 0xFFFF, 0x8000, 1, (m_sr & SR_X) != 0);

	uint16_t f = m_sr & static_cast<uint16_t>(~(SR_N | SR_Z | SR_V | SR_C | (kind != ROTATE ? SR_X : 0)));
	if (r.c)
		f |= SR_C;
	if (kind != ROTATE && r.x)
		f |= SR_X;
	if (r.v)
		f |= SR_V;
	if (r.value == 0)
		f |= SR_Z;
	if (r.value & 0x8000)
		f |= SR_N;
	m_sr = f;
	prefetch();
	write_word(e.addr, static_cast<uint16_t>(r.value));
}

} /* namespace odin */
