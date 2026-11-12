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

#ifndef ODIN_Z80_HPP
#define ODIN_Z80_HPP

#include <cstdint>
#include <cstdio>

namespace odin {

/* Everything the Z80 keeps between two instructions. Plain data so that
 * a machine can save and compare it byte by byte. */
struct Z80State {
	uint8_t a, f, b, c, d, e, h, l;
	uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
	uint8_t ixh, ixl, iyh, iyl;
	uint16_t sp, pc, wz;
	uint8_t i, r;
	uint8_t iff1, iff2, im;
	uint8_t halted;
	uint8_t int_line, nmi_pending;
	uint8_t after_ei, after_ldair;
	uint8_t q, q_prev;

	bool operator==(const Z80State &) const = default;
};

/* What the CPU sees outside its pins. The machine implements it. */
class Z80Bus {
public:
	virtual ~Z80Bus() = default;
	virtual uint8_t read8(uint16_t addr) = 0;
	virtual void write8(uint16_t addr, uint8_t value) = 0;
	virtual uint8_t in8(uint16_t port) = 0;
	virtual void out8(uint16_t port, uint8_t value) = 0;
	/* Byte on the data bus during the INT acknowledge cycle: an opcode
	 * in IM 0, the low vector byte in IM 2. Floating bus by default. */
	virtual uint8_t int_ack() { return 0xFF; }
};

class Z80 {
public:
	explicit Z80(Z80Bus &bus);

	void reset();
	/* Services a pending interrupt, then runs one instruction. */
	uint32_t step();

	void set_int(bool level) { m_int_line = level; }
	/* R as that many more M1 cycles leave it, for a machine that counts
	 * instructions it does not run. */
	void add_refresh(uint32_t m1) { m_r = u8((m_r & 0x80) | ((m_r + m1) & 0x7F)); }
	/* Whether the program has read R since this was last forgotten:
	 * what it then does cannot be counted. */
	bool refresh_read() const { return m_r_read; }
	void forget_refresh_read() { m_r_read = false; }
	void set_nmi() { m_nmi_pending = true; }
	/* For the tests */
	bool halted() const { return m_halted; }

	void save(Z80State &out) const;
	void load(const Z80State &in);

	/* Debug: trace lines go to `out`; nullptr turns tracing off. */
	void set_trace(FILE *out) { m_trace = out; }

	uint16_t pc() const { return m_pc; }
	/* Registers read from outside the core (the drive board reads R) */
	uint16_t bc() const { return pair(m_b, m_c); }
	uint16_t de() const { return pair(m_d, m_e); }
	uint16_t hl() const { return pair(m_h, m_l); }
	uint8_t r() const { return m_r; }

private:
	static uint8_t u8(uint32_t v) { return static_cast<uint8_t>(v); }
	static uint16_t u16(uint32_t v) { return static_cast<uint16_t>(v); }
	static uint16_t pair(uint8_t hi, uint8_t lo) { return u16((hi << 8) | lo); }

	/* Bus access with the trace point. */
	uint8_t rd(uint16_t addr);
	void wr(uint16_t addr, uint8_t v);
	uint8_t in(uint16_t port);
	void out(uint16_t port, uint8_t v);
	void trace(char rw, uint16_t addr, uint8_t v) const;
	uint8_t fetch();
	uint8_t imm8();
	uint16_t imm16();
	uint16_t rd16(uint16_t addr);
	void wr16(uint16_t addr, uint16_t v);
	void push(uint16_t v);
	uint16_t pop();
	void incr_r() { add_refresh(1); }

	/* Register file views. */
	void set_bc(uint16_t v) { m_b = u8(v >> 8); m_c = u8(v); }
	void set_de(uint16_t v) { m_d = u8(v >> 8); m_e = u8(v); }
	void set_hl(uint16_t v) { m_h = u8(v >> 8); m_l = u8(v); }
	uint16_t xy() const { return pair(*m_xh, *m_xl); }
	void set_xy(uint16_t v) { *m_xh = u8(v >> 8); *m_xl = u8(v); }
	uint16_t hl_addr() const { return m_indexed ? m_ea : hl(); }
	uint8_t get_r(uint8_t idx);
	void set_r(uint8_t idx, uint8_t v);
	uint16_t get_rp(uint8_t idx) const;
	void set_rp(uint8_t idx, uint16_t v);
	bool cond(uint8_t cc) const;
	void set_f(uint8_t v) { m_f = v; m_q = 0x28; }

	/* Interrupts. */
	void take_nmi();
	void take_int();
	void leave_halt() { m_halted = false; }

	/* Arithmetic and logic. */
	void alu(uint8_t op, uint8_t v);
	void add8(uint8_t v, uint8_t carry);
	void sub8(uint8_t v, uint8_t carry);
	void cp8(uint8_t v);
	uint8_t inc8(uint8_t v);
	uint8_t dec8(uint8_t v);
	void daa();
	void add16(uint16_t v);
	void adc16(uint16_t v);
	void sbc16(uint16_t v);
	uint8_t rot(uint8_t op, uint8_t v);
	void rot_a(uint8_t op);
	void bit(uint8_t n, uint8_t v, uint8_t xy_src);
	void ld_a_ir(uint8_t v);
	void rld();
	void rrd();
	void block_ld(bool up);
	void block_cp(bool up);
	uint8_t block_in(bool up);
	uint8_t block_out(bool up);
	void block_io_repeat_flags(uint8_t data);
	void ex_sp();

	/* Decoders, one per opcode page. */
	void exec_main(uint8_t op);
	void exec_x0(uint8_t op);
	void exec_x0_z0(uint8_t y);
	void exec_x0_z2(uint8_t y);
	void exec_x0_z7(uint8_t y);
	void exec_x3(uint8_t op);
	void exec_x3_z1(uint8_t y);
	void exec_x3_z3(uint8_t y);
	void exec_x3_z5(uint8_t y);
	void exec_cb();
	void exec_ed();
	void exec_ed_x1(uint8_t op);
	void exec_ed_x1_z7(uint8_t y);
	void exec_ed_block(uint8_t y, uint8_t z);
	void exec_xy(uint8_t &xh, uint8_t &xl);
	void exec_xycb();
	void jr(bool taken);
	void jp_cond(bool taken);
	void call_cond(bool taken);
	void ret_cond(bool taken);

	Z80Bus &m_bus;
	FILE *m_trace;

	uint8_t m_a, m_f, m_b, m_c, m_d, m_e, m_h, m_l;
	uint8_t m_a2, m_f2, m_b2, m_c2, m_d2, m_e2, m_h2, m_l2;
	uint8_t m_ixh, m_ixl, m_iyh, m_iyl;
	uint16_t m_sp, m_pc, m_wz;
	uint8_t m_i, m_r;
	bool m_r_read;	/* see refresh_read() */
	bool m_iff1, m_iff2;
	uint8_t m_im;
	bool m_halted;
	bool m_int_line, m_nmi_pending;
	bool m_after_ei, m_after_ldair;
	/* 0x28 when the last instruction wrote F through the ALU, else 0.
	 * SCF and CCF read it for their X and Y bits. */
	uint8_t m_q, m_q_prev;

	/* Per-instruction scratch. */
	uint8_t *m_xh, *m_xl;
	bool m_indexed;
	uint16_t m_ea;
	uint16_t m_op_pc;
	uint32_t m_cycles;
};

} /* namespace odin */

#endif /* ODIN_Z80_HPP */
