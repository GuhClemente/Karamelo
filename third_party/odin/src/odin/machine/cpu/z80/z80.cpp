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

/* Z80 core: state, bus helpers, interrupts, flags and the arithmetic
 * shared by the decoders in z80_ops.cpp. Timings are the instruction
 * tables of the Z80 CPU User Manual (UM008011); undocumented flag
 * results follow "The Undocumented Z80 Documented" and the block
 * instruction findings reproduced in MAME's z80.lst. */

#include "odin/machine/cpu/z80/z80.hpp"
#include "odin/machine/cpu/z80/z80_tables.hpp"

#include <format>

namespace odin {

Z80::Z80(Z80Bus &bus)
	: m_bus(bus), m_trace(nullptr)
{
	m_int_line = false;
	m_b = m_c = m_d = m_e = m_h = m_l = 0;
	m_a2 = m_f2 = m_b2 = m_c2 = m_d2 = m_e2 = m_h2 = m_l2 = 0;
	reset();
}

/* RESET clears PC, I, R, both IFFs and selects IM 0; AF and SP come out
 * as FFFF. IX and IY read FFFF on real chips too. The other registers
 * are left alone by the silicon; the constructor zeroes them once. */
void Z80::reset()
{
	m_a = m_f = 0xFF;
	m_sp = 0xFFFF;
	m_ixh = m_ixl = m_iyh = m_iyl = 0xFF;
	m_pc = 0;
	m_wz = 0;
	m_i = 0;
	m_r = 0;
	m_r_read = false;
	m_iff1 = m_iff2 = false;
	m_im = 0;
	m_halted = false;
	m_nmi_pending = false;
	m_after_ei = m_after_ldair = false;
	m_q = m_q_prev = 0;
	m_xh = &m_h;
	m_xl = &m_l;
	m_indexed = false;
	m_ea = 0;
	m_op_pc = 0;
	m_cycles = 0;
}


uint32_t Z80::step()
{
	m_cycles = 0;
	if (m_nmi_pending) {
		take_nmi();
	} else if (m_int_line && m_iff1 && !m_after_ei) {
		take_int();
	}
	m_after_ei = false;
	m_after_ldair = false;
	m_op_pc = m_pc;
	if (m_halted) {
		/* A halted CPU keeps running M1 cycles and discards the byte. */
		rd(m_pc);
		incr_r();
		return m_cycles + 4;
	}
	m_q_prev = m_q;
	m_q = 0;
	uint8_t op = fetch();
	m_cycles += CYCLES_MAIN[op];
	exec_main(op);
	return m_cycles;
}

void Z80::save(Z80State &out) const
{
	out.a = m_a; out.f = m_f; out.b = m_b; out.c = m_c;
	out.d = m_d; out.e = m_e; out.h = m_h; out.l = m_l;
	out.a2 = m_a2; out.f2 = m_f2; out.b2 = m_b2; out.c2 = m_c2;
	out.d2 = m_d2; out.e2 = m_e2; out.h2 = m_h2; out.l2 = m_l2;
	out.ixh = m_ixh; out.ixl = m_ixl; out.iyh = m_iyh; out.iyl = m_iyl;
	out.sp = m_sp; out.pc = m_pc; out.wz = m_wz;
	out.i = m_i; out.r = m_r;
	out.iff1 = m_iff1; out.iff2 = m_iff2; out.im = m_im;
	out.halted = m_halted;
	out.int_line = m_int_line; out.nmi_pending = m_nmi_pending;
	out.after_ei = m_after_ei; out.after_ldair = m_after_ldair;
	out.q = m_q; out.q_prev = m_q_prev;
}

void Z80::load(const Z80State &in)
{
	m_a = in.a; m_f = in.f; m_b = in.b; m_c = in.c;
	m_d = in.d; m_e = in.e; m_h = in.h; m_l = in.l;
	m_a2 = in.a2; m_f2 = in.f2; m_b2 = in.b2; m_c2 = in.c2;
	m_d2 = in.d2; m_e2 = in.e2; m_h2 = in.h2; m_l2 = in.l2;
	m_ixh = in.ixh; m_ixl = in.ixl; m_iyh = in.iyh; m_iyl = in.iyl;
	m_sp = in.sp; m_pc = in.pc; m_wz = in.wz;
	m_i = in.i; m_r = in.r;
	m_iff1 = in.iff1 != 0; m_iff2 = in.iff2 != 0; m_im = u8(in.im & 3);
	m_halted = in.halted != 0;
	m_int_line = in.int_line != 0; m_nmi_pending = in.nmi_pending != 0;
	m_after_ei = in.after_ei != 0; m_after_ldair = in.after_ldair != 0;
	m_q = in.q; m_q_prev = in.q_prev;
	m_xh = &m_h;
	m_xl = &m_l;
	m_indexed = false;
}

/* Bus access. The trace point is one pointer test when off. */

void Z80::trace(char rw, uint16_t addr, uint8_t v) const
{
	std::fputs(std::format("z80 {:04x} {} {:04x} 1 {:02x}\n", m_op_pc, rw, addr, v).c_str(), m_trace);
}

uint8_t Z80::rd(uint16_t addr)
{
	uint8_t v = m_bus.read8(addr);
	if (m_trace) {
		trace('r', addr, v);
	}
	return v;
}

void Z80::wr(uint16_t addr, uint8_t v)
{
	if (m_trace) {
		trace('w', addr, v);
	}
	m_bus.write8(addr, v);
}

uint8_t Z80::in(uint16_t port)
{
	uint8_t v = m_bus.in8(port);
	if (m_trace) {
		trace('i', port, v);
	}
	return v;
}

void Z80::out(uint16_t port, uint8_t v)
{
	if (m_trace) {
		trace('o', port, v);
	}
	m_bus.out8(port, v);
}

/* Opcode fetch: an M1 cycle, so R counts it. */
uint8_t Z80::fetch()
{
	uint8_t op = rd(m_pc);
	m_pc++;
	incr_r();
	return op;
}

uint8_t Z80::imm8()
{
	uint8_t v = rd(m_pc);
	m_pc++;
	return v;
}

uint16_t Z80::imm16()
{
	uint8_t lo = imm8();
	uint8_t hi = imm8();
	return pair(hi, lo);
}

uint16_t Z80::rd16(uint16_t addr)
{
	uint8_t lo = rd(addr);
	uint8_t hi = rd(u16(addr + 1));
	return pair(hi, lo);
}

void Z80::wr16(uint16_t addr, uint16_t v)
{
	wr(addr, u8(v));
	wr(u16(addr + 1), u8(v >> 8));
}

void Z80::push(uint16_t v)
{
	m_sp--;
	wr(m_sp, u8(v >> 8));
	m_sp--;
	wr(m_sp, u8(v));
}

uint16_t Z80::pop()
{
	uint8_t lo = rd(m_sp);
	m_sp++;
	uint8_t hi = rd(m_sp);
	m_sp++;
	return pair(hi, lo);
}

/* Register file. Index 4 and 5 follow the current prefix (H/L, IXH/IXL
 * or IYH/IYL); index 6 is (HL) or (IX+d). */

uint8_t Z80::get_r(uint8_t idx)
{
	switch (idx) {
	case 0: return m_b;
	case 1: return m_c;
	case 2: return m_d;
	case 3: return m_e;
	case 4: return *m_xh;
	case 5: return *m_xl;
	case 6: return rd(hl_addr());
	default: return m_a;
	}
}

void Z80::set_r(uint8_t idx, uint8_t v)
{
	switch (idx) {
	case 0: m_b = v; break;
	case 1: m_c = v; break;
	case 2: m_d = v; break;
	case 3: m_e = v; break;
	case 4: *m_xh = v; break;
	case 5: *m_xl = v; break;
	case 6: wr(hl_addr(), v); break;
	default: m_a = v; break;
	}
}

uint16_t Z80::get_rp(uint8_t idx) const
{
	switch (idx) {
	case 0: return bc();
	case 1: return de();
	case 2: return xy();
	default: return m_sp;
	}
}

void Z80::set_rp(uint8_t idx, uint16_t v)
{
	switch (idx) {
	case 0: set_bc(v); break;
	case 1: set_de(v); break;
	case 2: set_xy(v); break;
	default: m_sp = v; break;
	}
}

/* NZ, Z, NC, C, PO, PE, P, M. */
bool Z80::cond(uint8_t cc) const
{
	static constexpr uint8_t MASK[4] = { ZF, CF, PF, SF };
	bool set = (m_f & MASK[cc >> 1]) != 0;
	return set == ((cc & 1) != 0);
}

/* Interrupts. NMI is 11 T-states, IM 1 is 13, IM 2 is 19; IM 0 costs
 * the executed opcode plus two wait states. An INT right after LD A,I or
 * LD A,R clears P/V on NMOS parts. */

void Z80::take_nmi()
{
	leave_halt();
	if (m_after_ldair) {
		m_f &= u8(~PF);
	}
	m_iff1 = false;
	incr_r();
	push(m_pc);
	m_pc = 0x0066;
	m_wz = m_pc;
	m_nmi_pending = false;
	m_cycles += 11;
}

void Z80::take_int()
{
	leave_halt();
	m_iff1 = m_iff2 = false;
	incr_r();
	uint8_t vec = m_bus.int_ack();
	m_op_pc = m_pc;
	switch (m_im) {
	case 2:
		push(m_pc);
		m_pc = rd16(pair(m_i, vec));
		m_wz = m_pc;
		m_cycles += 19;
		break;
	case 1:
		push(m_pc);
		m_pc = 0x0038;
		m_wz = m_pc;
		m_cycles += 13;
		break;
	default:
		/* IM 0 executes the byte on the bus, normally an RST. A longer
		 * opcode would take its operands from memory at PC, not from
		 * further acknowledge cycles. */
		m_cycles += 2 + CYCLES_MAIN[vec];
		m_q_prev = m_q;
		m_q = 0;
		exec_main(vec);
		break;
	}
	if (m_after_ldair) {
		m_f &= u8(~PF);
	}
}

/* 8-bit arithmetic. P/V is overflow for ADD/SUB and parity otherwise;
 * X and Y copy bits 3 and 5 of the result, except CP which shows the
 * operand. */

void Z80::alu(uint8_t op, uint8_t v)
{
	switch (op) {
	case 0: add8(v, 0); break;
	case 1: add8(v, m_f & CF); break;
	case 2: sub8(v, 0); break;
	case 3: sub8(v, m_f & CF); break;
	case 4: m_a &= v; set_f(u8(SZP[m_a] | HF)); break;
	case 5: m_a ^= v; set_f(SZP[m_a]); break;
	case 6: m_a |= v; set_f(SZP[m_a]); break;
	default: cp8(v); break;
	}
}

void Z80::add8(uint8_t v, uint8_t carry)
{
	uint32_t res = m_a + v + carry;
	set_f(u8(SZ[res & 0xFF] | ((res >> 8) & CF) | ((m_a ^ v ^ res) & HF)
		| (((m_a ^ res) & (v ^ res) & 0x80) >> 5)));
	m_a = u8(res);
}

void Z80::sub8(uint8_t v, uint8_t carry)
{
	uint32_t res = m_a - v - carry;
	set_f(u8(SZ[res & 0xFF] | NF | ((res >> 8) & CF) | ((m_a ^ v ^ res) & HF)
		| (((m_a ^ v) & (m_a ^ res) & 0x80) >> 5)));
	m_a = u8(res);
}

void Z80::cp8(uint8_t v)
{
	uint32_t res = m_a - v;
	set_f(u8((SZ[res & 0xFF] & (SF | ZF)) | (v & XYF) | NF | ((res >> 8) & CF)
		| ((m_a ^ v ^ res) & HF) | (((m_a ^ v) & (m_a ^ res) & 0x80) >> 5)));
}

uint8_t Z80::inc8(uint8_t v)
{
	uint8_t res = u8(v + 1);
	set_f(u8((m_f & CF) | SZHV_INC[res]));
	return res;
}

uint8_t Z80::dec8(uint8_t v)
{
	uint8_t res = u8(v - 1);
	set_f(u8((m_f & CF) | SZHV_DEC[res]));
	return res;
}

void Z80::daa()
{
	uint32_t a = m_a;
	uint32_t adj = 0;
	if ((m_f & HF) || (m_a & 0x0F) > 9) {
		adj = 6;
	}
	if ((m_f & CF) || m_a > 0x99) {
		adj |= 0x60;
	}
	a = (m_f & NF) ? (a - adj) : (a + adj);
	set_f(u8((m_f & (NF | CF)) | (m_a > 0x99 ? CF : 0) | ((m_a ^ a) & HF) | SZP[a & 0xFF]));
	m_a = u8(a);
}

/* 16-bit arithmetic on HL, IX or IY. X and Y come from the high byte;
 * WZ holds the first operand plus one. */

void Z80::add16(uint16_t v)
{
	uint32_t src = xy();
	uint32_t res = src + v;
	m_wz = u16(src + 1);
	set_f(u8((m_f & (SF | ZF | PF)) | (((src ^ res ^ v) >> 8) & HF) | ((res >> 16) & CF)
		| ((res >> 8) & XYF)));
	set_xy(u16(res));
}

void Z80::adc16(uint16_t v)
{
	uint32_t src = hl();
	uint32_t res = src + v + (m_f & CF);
	m_wz = u16(src + 1);
	set_f(u8((((src ^ res ^ v) >> 8) & HF) | ((res >> 16) & CF) | ((res >> 8) & (SF | XYF))
		| ((res & 0xFFFF) ? 0 : ZF) | (((v ^ src ^ 0x8000) & (v ^ res) & 0x8000) >> 13)));
	set_hl(u16(res));
}

void Z80::sbc16(uint16_t v)
{
	uint32_t src = hl();
	uint32_t res = src - v - (m_f & CF);
	m_wz = u16(src + 1);
	set_f(u8((((src ^ res ^ v) >> 8) & HF) | NF | ((res >> 16) & CF) | ((res >> 8) & (SF | XYF))
		| ((res & 0xFFFF) ? 0 : ZF) | (((v ^ src) & (src ^ res) & 0x8000) >> 13)));
	set_hl(u16(res));
}

/* Rotates and shifts: RLC, RRC, RL, RR, SLA, SRA, SLL, SRL. */

uint8_t Z80::rot(uint8_t op, uint8_t v)
{
	uint32_t c = m_f & CF;
	uint32_t res;
	uint32_t carry;
	switch (op) {
	case 0: res = (v << 1) | (v >> 7); carry = v >> 7; break;
	case 1: res = (v >> 1) | (v << 7); carry = v & 1; break;
	case 2: res = (v << 1) | c; carry = v >> 7; break;
	case 3: res = (v >> 1) | (c << 7); carry = v & 1; break;
	case 4: res = v << 1; carry = v >> 7; break;
	case 5: res = (v >> 1) | (v & 0x80); carry = v & 1; break;
	case 6: res = (v << 1) | 1; carry = v >> 7; break;
	default: res = v >> 1; carry = v & 1; break;
	}
	res &= 0xFF;
	set_f(u8(SZP[res] | carry));
	return u8(res);
}

/* RLCA, RRCA, RLA, RRA keep S, Z and P/V. */
void Z80::rot_a(uint8_t op)
{
	uint8_t keep = u8(m_f & (SF | ZF | PF));
	uint8_t res = rot(op, m_a);
	set_f(u8(keep | (m_f & CF) | (res & XYF)));
	m_a = res;
}

/* BIT: Z and P/V say the bit is clear, S says bit 7 is set. X and Y
 * come from the register, or from the high address byte for memory. */
void Z80::bit(uint8_t n, uint8_t v, uint8_t xy_src)
{
	uint32_t hit = v & (1u << n);
	set_f(u8((m_f & CF) | HF | (xy_src & XYF) | (hit ? 0 : (ZF | PF)) | (hit & SF)));
}

void Z80::ld_a_ir(uint8_t v)
{
	m_a = v;
	set_f(u8((m_f & CF) | SZ[v] | (m_iff2 ? PF : 0)));
	m_after_ldair = true;
}

void Z80::rld()
{
	uint16_t addr = hl();
	uint8_t v = rd(addr);
	m_wz = u16(addr + 1);
	wr(addr, u8((v << 4) | (m_a & 0x0F)));
	m_a = u8((m_a & 0xF0) | (v >> 4));
	set_f(u8((m_f & CF) | SZP[m_a]));
}

void Z80::rrd()
{
	uint16_t addr = hl();
	uint8_t v = rd(addr);
	m_wz = u16(addr + 1);
	wr(addr, u8((v >> 4) | (m_a << 4)));
	m_a = u8((m_a & 0xF0) | (v & 0x0F));
	set_f(u8((m_f & CF) | SZP[m_a]));
}

/* Block transfers. LDI/LDD show bits 1 and 3 of A plus the byte moved
 * in Y and X; CPI/CPD do the same with the compare result minus H. */

void Z80::block_ld(bool up)
{
	uint8_t v = rd(hl());
	wr(de(), v);
	set_hl(u16(up ? hl() + 1 : hl() - 1));
	set_de(u16(up ? de() + 1 : de() - 1));
	set_bc(u16(bc() - 1));
	uint32_t n = m_a + v;
	set_f(u8((m_f & (SF | ZF | CF)) | (bc() ? PF : 0) | ((n << 4) & YF) | (n & XF)));
}

void Z80::block_cp(bool up)
{
	uint8_t v = rd(hl());
	uint32_t res = (m_a - v) & 0xFF;
	set_hl(u16(up ? hl() + 1 : hl() - 1));
	set_bc(u16(bc() - 1));
	m_wz = u16(up ? m_wz + 1 : m_wz - 1);
	uint32_t hf = (m_a ^ v ^ res) & HF;
	uint32_t n = res - (hf ? 1 : 0);
	set_f(u8((m_f & CF) | (SZ[res] & (SF | ZF)) | hf | NF | (bc() ? PF : 0)
		| ((n << 4) & YF) | (n & XF)));
}

/* Block I/O. N is bit 7 of the data; H and C carry out of data plus
 * C±1 (input) or data plus L (output); P/V is the parity of that sum's
 * low three bits with B. Returns the data byte for the repeat flags. */

uint8_t Z80::block_in(bool up)
{
	uint8_t v = in(bc());
	m_wz = u16(up ? bc() + 1 : bc() - 1);
	m_b--;
	wr(hl(), v);
	set_hl(u16(up ? hl() + 1 : hl() - 1));
	uint32_t t = ((up ? m_c + 1 : m_c - 1) & 0xFF) + v;
	set_f(u8(SZ[m_b] | ((v & 0x80) ? NF : 0) | ((t & 0x100) ? (HF | CF) : 0)
		| (SZP[(t & 7) ^ m_b] & PF)));
	return v;
}

uint8_t Z80::block_out(bool up)
{
	uint8_t v = rd(hl());
	m_b--;
	m_wz = u16(up ? bc() + 1 : bc() - 1);
	out(bc(), v);
	set_hl(u16(up ? hl() + 1 : hl() - 1));
	uint32_t t = m_l + v;
	set_f(u8(SZ[m_b] | ((v & 0x80) ? NF : 0) | ((t & 0x100) ? (HF | CF) : 0)
		| (SZP[(t & 7) ^ m_b] & PF)));
	return v;
}

/* INIR/INDR/OTIR/OTDR going round again: X and Y show the high byte of
 * the instruction address, and H and P/V are reworked from B and the
 * data byte (MAME z80.cpp, block_io_interrupted_flags). */
void Z80::block_io_repeat_flags(uint8_t data)
{
	uint8_t f = u8((m_f & ~XYF) | ((m_pc >> 8) & XYF));
	if (f & CF) {
		f &= u8(~HF);
		if (data & 0x80) {
			f ^= u8((SZP[(m_b - 1) & 7] ^ PF) & PF);
			if ((m_b & 0x0F) == 0x00) {
				f |= HF;
			}
		} else {
			f ^= u8((SZP[(m_b + 1) & 7] ^ PF) & PF);
			if ((m_b & 0x0F) == 0x0F) {
				f |= HF;
			}
		}
	} else {
		f ^= u8((SZP[m_b & 7] ^ PF) & PF);
	}
	set_f(f);
}

/* EX (SP),HL and its IX/IY forms. The stack is read low byte first and
 * written high byte first, as on the bus. */
void Z80::ex_sp()
{
	uint16_t v = pop();
	push(xy());
	set_xy(v);
	m_wz = v;
}

} /* namespace odin */
