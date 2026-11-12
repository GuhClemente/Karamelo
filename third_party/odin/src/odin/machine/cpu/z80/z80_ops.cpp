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

/* Z80 decoders. Opcodes are split into the fields x = bits 7-6,
 * y = bits 5-3, z = bits 2-0, p = y >> 1, q = y & 1, one function per
 * page: unprefixed, CB, ED, DD/FD and DD CB/FD CB. */

#include "odin/machine/cpu/z80/z80.hpp"
#include "odin/machine/cpu/z80/z80_tables.hpp"

namespace odin {

void Z80::exec_main(uint8_t op)
{
	switch (op >> 6) {
	case 0:
		exec_x0(op);
		break;
	case 1:
		if (op == 0x76) {
			m_halted = true;
		} else {
			set_r(u8((op >> 3) & 7), get_r(u8(op & 7)));
		}
		break;
	case 2:
		alu(u8((op >> 3) & 7), get_r(u8(op & 7)));
		break;
	default:
		exec_x3(op);
		break;
	}
}

void Z80::exec_x0(uint8_t op)
{
	uint8_t y = u8((op >> 3) & 7);
	uint8_t z = u8(op & 7);
	uint8_t p = u8(y >> 1);
	switch (z) {
	case 0:
		exec_x0_z0(y);
		break;
	case 1:
		if (y & 1) {
			add16(get_rp(p));
		} else {
			set_rp(p, imm16());
		}
		break;
	case 2:
		exec_x0_z2(y);
		break;
	case 3:
		set_rp(p, u16((y & 1) ? get_rp(p) - 1 : get_rp(p) + 1));
		break;
	case 4:
		set_r(y, inc8(get_r(y)));
		break;
	case 5:
		set_r(y, dec8(get_r(y)));
		break;
	case 6:
		set_r(y, imm8());
		break;
	default:
		exec_x0_z7(y);
		break;
	}
}

/* NOP, EX AF,AF', DJNZ, JR, JR cc. */
void Z80::exec_x0_z0(uint8_t y)
{
	switch (y) {
	case 0:
		break;
	case 1: {
		uint8_t a = m_a;
		uint8_t f = m_f;
		m_a = m_a2;
		m_f = m_f2;
		m_a2 = a;
		m_f2 = f;
		break;
	}
	case 2:
		m_b--;
		jr(m_b != 0);
		break;
	case 3: {
		uint8_t d = imm8();
		m_pc = u16(m_pc + static_cast<int8_t>(d));
		m_wz = m_pc;
		break;
	}
	default:
		jr(cond(u8(y - 4)));
		break;
	}
}

/* Loads between A or HL and memory. WZ follows the address; the
 * accumulator stores also leave A in its high byte. */
void Z80::exec_x0_z2(uint8_t y)
{
	switch (y) {
	case 0:
		wr(bc(), m_a);
		m_wz = u16(((bc() + 1) & 0xFF) | (m_a << 8));
		break;
	case 1:
		m_a = rd(bc());
		m_wz = u16(bc() + 1);
		break;
	case 2:
		wr(de(), m_a);
		m_wz = u16(((de() + 1) & 0xFF) | (m_a << 8));
		break;
	case 3:
		m_a = rd(de());
		m_wz = u16(de() + 1);
		break;
	case 4: {
		uint16_t nn = imm16();
		wr16(nn, xy());
		m_wz = u16(nn + 1);
		break;
	}
	case 5: {
		uint16_t nn = imm16();
		set_xy(rd16(nn));
		m_wz = u16(nn + 1);
		break;
	}
	case 6: {
		uint16_t nn = imm16();
		wr(nn, m_a);
		m_wz = u16(((nn + 1) & 0xFF) | (m_a << 8));
		break;
	}
	default: {
		uint16_t nn = imm16();
		m_a = rd(nn);
		m_wz = u16(nn + 1);
		break;
	}
	}
}

/* RLCA, RRCA, RLA, RRA, DAA, CPL, SCF, CCF. SCF and CCF take X and Y
 * from A, or from A or F when the previous instruction left F alone. */
void Z80::exec_x0_z7(uint8_t y)
{
	uint8_t keep = u8(XYF & ~m_q_prev);
	switch (y) {
	case 0:
	case 1:
	case 2:
	case 3:
		rot_a(y);
		break;
	case 4:
		daa();
		break;
	case 5:
		m_a = u8(~m_a);
		set_f(u8((m_f & (SF | ZF | PF | CF)) | HF | NF | (m_a & XYF)));
		break;
	case 6:
		set_f(u8((m_f & (SF | ZF | PF)) | CF | (((m_f & keep) | m_a) & XYF)));
		break;
	default:
		set_f(u8((m_f & (SF | ZF | PF)) | ((m_f & CF) << 4) | (((m_f & keep) | m_a) & XYF)
			| ((~m_f) & CF)));
		break;
	}
}

void Z80::exec_x3(uint8_t op)
{
	uint8_t y = u8((op >> 3) & 7);
	uint8_t z = u8(op & 7);
	switch (z) {
	case 0:
		ret_cond(cond(y));
		break;
	case 1:
		exec_x3_z1(y);
		break;
	case 2:
		jp_cond(cond(y));
		break;
	case 3:
		exec_x3_z3(y);
		break;
	case 4:
		call_cond(cond(y));
		break;
	case 5:
		exec_x3_z5(y);
		break;
	case 6:
		alu(y, imm8());
		break;
	default:
		push(m_pc);
		m_pc = u16(y * 8);
		m_wz = m_pc;
		break;
	}
}

/* POP rp, RET, EXX, JP (HL), LD SP,HL. */
void Z80::exec_x3_z1(uint8_t y)
{
	uint8_t p = u8(y >> 1);
	if (!(y & 1)) {
		uint16_t v = pop();
		if (p == 3) {
			m_a = u8(v >> 8);
			m_f = u8(v);
		} else {
			set_rp(p, v);
		}
		return;
	}
	switch (p) {
	case 0:
		m_pc = pop();
		m_wz = m_pc;
		break;
	case 1: {
		uint8_t b = m_b, c = m_c, d = m_d, e = m_e, h = m_h, l = m_l;
		m_b = m_b2; m_c = m_c2; m_d = m_d2; m_e = m_e2; m_h = m_h2; m_l = m_l2;
		m_b2 = b; m_c2 = c; m_d2 = d; m_e2 = e; m_h2 = h; m_l2 = l;
		break;
	}
	case 2:
		m_pc = xy();
		break;
	default:
		m_sp = xy();
		break;
	}
}

/* JP nn, CB page, OUT (n),A, IN A,(n), EX (SP),HL, EX DE,HL, DI, EI. */
void Z80::exec_x3_z3(uint8_t y)
{
	switch (y) {
	case 0:
		m_pc = imm16();
		m_wz = m_pc;
		break;
	case 1:
		exec_cb();
		break;
	case 2: {
		uint8_t n = imm8();
		out(pair(m_a, n), m_a);
		m_wz = u16(((n + 1) & 0xFF) | (m_a << 8));
		break;
	}
	case 3: {
		uint16_t port = pair(m_a, imm8());
		m_a = in(port);
		m_wz = u16(port + 1);
		break;
	}
	case 4:
		ex_sp();
		break;
	case 5: {
		/* EX DE,HL ignores the index prefix. */
		uint8_t h = m_h, l = m_l;
		m_h = m_d; m_l = m_e;
		m_d = h; m_e = l;
		break;
	}
	case 6:
		m_iff1 = m_iff2 = false;
		break;
	default:
		m_iff1 = m_iff2 = true;
		m_after_ei = true;
		break;
	}
}

/* PUSH rp, CALL nn, DD/ED/FD pages. */
void Z80::exec_x3_z5(uint8_t y)
{
	uint8_t p = u8(y >> 1);
	if (!(y & 1)) {
		push(p == 3 ? pair(m_a, m_f) : get_rp(p));
		return;
	}
	switch (p) {
	case 0: {
		uint16_t nn = imm16();
		push(m_pc);
		m_pc = nn;
		m_wz = m_pc;
		break;
	}
	case 1:
		exec_xy(m_ixh, m_ixl);
		break;
	case 2:
		exec_ed();
		break;
	default:
		exec_xy(m_iyh, m_iyl);
		break;
	}
}

/* Conditional control flow. The address bytes are read either way, so
 * WZ always gets them; the taken path adds its extra T-states. */

void Z80::jr(bool taken)
{
	uint8_t d = imm8();
	if (taken) {
		m_pc = u16(m_pc + static_cast<int8_t>(d));
		m_wz = m_pc;
		m_cycles += 5;
	}
}

void Z80::jp_cond(bool taken)
{
	uint16_t nn = imm16();
	m_wz = nn;
	if (taken) {
		m_pc = nn;
	}
}

void Z80::call_cond(bool taken)
{
	uint16_t nn = imm16();
	m_wz = nn;
	if (taken) {
		push(m_pc);
		m_pc = nn;
		m_cycles += 7;
	}
}

void Z80::ret_cond(bool taken)
{
	if (taken) {
		m_pc = pop();
		m_wz = m_pc;
		m_cycles += 6;
	}
}

/* CB page: rotates, BIT, RES, SET on a register or (HL). BIT n,(HL)
 * shows WZ's high byte in X and Y. */
void Z80::exec_cb()
{
	uint8_t op = fetch();
	m_cycles += CYCLES_CB[op];
	uint8_t y = u8((op >> 3) & 7);
	uint8_t z = u8(op & 7);
	uint8_t v = get_r(z);
	switch (op >> 6) {
	case 0:
		set_r(z, rot(y, v));
		break;
	case 1:
		bit(y, v, z == 6 ? u8(m_wz >> 8) : v);
		break;
	case 2:
		set_r(z, u8(v & ~(1u << y)));
		break;
	default:
		set_r(z, u8(v | (1u << y)));
		break;
	}
}

/* ED page. Every slot outside x = 1 and the block group is a NOP. */
void Z80::exec_ed()
{
	uint8_t op = fetch();
	m_cycles += CYCLES_ED[op];
	uint8_t x = u8(op >> 6);
	uint8_t y = u8((op >> 3) & 7);
	uint8_t z = u8(op & 7);
	if (x == 1) {
		exec_ed_x1(op);
	} else if (x == 2 && z < 4 && y >= 4) {
		exec_ed_block(y, z);
	}
}

/* IN r,(C), OUT (C),r, SBC/ADC HL,rp, LD (nn),rp, LD rp,(nn), NEG,
 * RETN/RETI, IM n, and the z = 7 group. */
void Z80::exec_ed_x1(uint8_t op)
{
	static constexpr uint8_t IM_MODE[8] = { 0, 0, 1, 2, 0, 0, 1, 2 };
	uint8_t y = u8((op >> 3) & 7);
	uint8_t z = u8(op & 7);
	uint8_t p = u8(y >> 1);
	switch (z) {
	case 0: {
		uint8_t v = in(bc());
		m_wz = u16(bc() + 1);
		set_f(u8((m_f & CF) | SZP[v]));
		if (y != 6) {
			set_r(y, v);
		}
		break;
	}
	case 1:
		/* OUT (C),0: an NMOS part drives 0 on the bus. */
		out(bc(), y == 6 ? u8(0) : get_r(y));
		m_wz = u16(bc() + 1);
		break;
	case 2:
		if (y & 1) {
			adc16(get_rp(p));
		} else {
			sbc16(get_rp(p));
		}
		break;
	case 3: {
		uint16_t nn = imm16();
		if (y & 1) {
			set_rp(p, rd16(nn));
		} else {
			wr16(nn, get_rp(p));
		}
		m_wz = u16(nn + 1);
		break;
	}
	case 4: {
		uint8_t v = m_a;
		m_a = 0;
		sub8(v, 0);
		break;
	}
	case 5:
		/* RETI and RETN both restore IFF1 from IFF2. */
		m_pc = pop();
		m_wz = m_pc;
		m_iff1 = m_iff2;
		break;
	case 6:
		m_im = IM_MODE[y];
		break;
	default:
		exec_ed_x1_z7(y);
		break;
	}
}

/* LD I,A; LD R,A; LD A,I; LD A,R; RRD; RLD; two NOPs. */
void Z80::exec_ed_x1_z7(uint8_t y)
{
	switch (y) {
	case 0:
		m_i = m_a;
		break;
	case 1:
		m_r = m_a;
		break;
	case 2:
		ld_a_ir(m_i);
		break;
	case 3:
		ld_a_ir(m_r);
		m_r_read = true;
		break;
	case 4:
		rrd();
		break;
	case 5:
		rld();
		break;
	default:
		break;
	}
}

/* LDI/LDD/CPI/CPD/INI/IND/OUTI/OUTD and their repeating forms. A
 * repeat rewinds PC onto the ED byte and costs 5 more T-states; LDIR
 * and CPIR then show the instruction address in WZ, X and Y. */
void Z80::exec_ed_block(uint8_t y, uint8_t z)
{
	bool up = !(y & 1);
	bool repeat = y >= 6;
	switch (z) {
	case 0:
		block_ld(up);
		if (repeat && bc() != 0) {
			m_pc = u16(m_pc - 2);
			m_wz = u16(m_pc + 1);
			set_f(u8((m_f & ~XYF) | ((m_pc >> 8) & XYF)));
			m_cycles += 5;
		}
		break;
	case 1:
		block_cp(up);
		if (repeat && bc() != 0 && !(m_f & ZF)) {
			m_pc = u16(m_pc - 2);
			m_wz = u16(m_pc + 1);
			set_f(u8((m_f & ~XYF) | ((m_pc >> 8) & XYF)));
			m_cycles += 5;
		}
		break;
	case 2: {
		uint8_t v = block_in(up);
		if (repeat && m_b != 0) {
			m_pc = u16(m_pc - 2);
			block_io_repeat_flags(v);
			m_cycles += 5;
		}
		break;
	}
	default: {
		uint8_t v = block_out(up);
		if (repeat && m_b != 0) {
			m_pc = u16(m_pc - 2);
			block_io_repeat_flags(v);
			m_cycles += 5;
		}
		break;
	}
	}
}

/* DD and FD pages. H, L and (HL) become IXH, IXL and (IX+d), except
 * in LD r,(IX+d) and LD (IX+d),r where r stays H or L. A run of
 * prefixes keeps the last one, each earlier byte costing a NOP; ED
 * drops the prefix altogether. */
void Z80::exec_xy(uint8_t &xh, uint8_t &xl)
{
	m_xh = &xh;
	m_xl = &xl;
	uint8_t op = fetch();
	while (op == 0xDD || op == 0xFD) {
		m_cycles += 4;
		m_xh = (op == 0xDD) ? &m_ixh : &m_iyh;
		m_xl = (op == 0xDD) ? &m_ixl : &m_iyl;
		op = fetch();
	}
	if (op == 0xED) {
		m_cycles += 4;
		m_xh = &m_h;
		m_xl = &m_l;
		exec_ed();
	} else if (op == 0xCB) {
		exec_xycb();
	} else {
		m_cycles += CYCLES_XY[op];
		if (uses_hl_mem(op)) {
			uint8_t d = imm8();
			m_ea = u16(xy() + static_cast<int8_t>(d));
			m_wz = m_ea;
			m_indexed = true;
			if ((op & 0xC0) == 0x40) {
				m_xh = &m_h;
				m_xl = &m_l;
			}
		}
		exec_main(op);
	}
	m_xh = &m_h;
	m_xl = &m_l;
	m_indexed = false;
}

/* DD CB d op: every opcode works on (IX+d); the result also lands in
 * the register named by z, except for BIT. The last byte is read as
 * data, not fetched, so R does not count it. */
void Z80::exec_xycb()
{
	uint8_t d = imm8();
	m_ea = u16(xy() + static_cast<int8_t>(d));
	m_wz = m_ea;
	uint8_t op = imm8();
	m_cycles += CYCLES_XYCB[op];
	uint8_t y = u8((op >> 3) & 7);
	uint8_t z = u8(op & 7);
	uint8_t v = rd(m_ea);
	uint8_t res;
	switch (op >> 6) {
	case 0:
		res = rot(y, v);
		break;
	case 1:
		bit(y, v, u8(m_ea >> 8));
		return;
	case 2:
		res = u8(v & ~(1u << y));
		break;
	default:
		res = u8(v | (1u << y));
		break;
	}
	wr(m_ea, res);
	m_xh = &m_h;
	m_xl = &m_l;
	if (z != 6) {
		set_r(z, res);
	}
}

} /* namespace odin */
