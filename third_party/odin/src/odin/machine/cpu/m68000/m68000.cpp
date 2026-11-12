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

#include "odin/machine/cpu/m68000/m68000.hpp"

namespace odin {

M68000::M68000(M68000Bus &bus)
	: m_bus(bus), m_d{}, m_a{}, m_other_sp(0), m_pc(0), m_au(0), m_sr(SR_S | SR_I), m_ir(0), m_irc(0), m_ird(0),
	  m_ipl(0), m_nmi_edge(false), m_stopped(false), m_halted(false), m_cycles(0), m_run_end(0)
{
}

/* Bus cycles. A word or long access at an odd address faults before the
 * cycle starts; after a fault, none starts. */

void
M68000::fault(uint32_t addr, bool write, bool program)
{
	m_faulting = true;
	m_fault = { addr, write, program };
	m_at_fault = { m_d, m_a, m_other_sp, m_pc, m_au, m_sr, m_ir, m_irc, m_ird, m_nmi_edge, m_stopped, m_halted, m_cycles };
}

/* Whether the instruction met a fault: the registers as they were at it */
bool
M68000::faulted()
{
	if (!m_faulting)
		return false;

	const Kept &k = m_at_fault;

	m_faulting = false;
	m_d = k.d;
	m_a = k.a;
	m_other_sp = k.other_sp;
	m_pc = k.pc;
	m_au = k.au;
	m_sr = k.sr;
	m_ir = k.ir;
	m_irc = k.irc;
	m_ird = k.ird;
	m_nmi_edge = k.nmi_edge;
	m_stopped = k.stopped;
	m_halted = k.halted;
	m_cycles = k.cycles;
	return true;
}

uint16_t
M68000::read_word(uint32_t addr)
{
	if (m_faulting)
		return 0;
	if (addr & 1) {
		fault(addr, false, false);
		return 0;
	}
	m_cycles += 4;
	return m_bus.read16(addr & 0xFFFFFF, data_fc());
}

uint16_t
M68000::fetch_word(uint32_t addr)
{
	if (m_faulting)
		return 0;
	if (addr & 1) {
		fault(addr, false, true);
		return 0;
	}
	m_cycles += 4;
	return m_bus.read16(addr & 0xFFFFFF, program_fc());
}

uint8_t
M68000::read_byte(uint32_t addr)
{
	if (m_faulting)
		return 0;
	m_cycles += 4;
	return m_bus.read8(addr & 0xFFFFFF, data_fc());
}

uint32_t
M68000::read_long(uint32_t addr)
{
	const uint32_t hi = read_word(addr);
	return (hi << 16) | read_word(addr + 2);
}

void
M68000::write_word(uint32_t addr, uint16_t v)
{
	if (m_faulting)
		return;
	if (addr & 1) {
		fault(addr, true, false);
		return;
	}
	m_cycles += 4;
	m_bus.write16(addr & 0xFFFFFF, v, data_fc());
}

void
M68000::write_byte(uint32_t addr, uint8_t v)
{
	if (m_faulting)
		return;
	m_cycles += 4;
	m_bus.write8(addr & 0xFFFFFF, v, data_fc());
}

void
M68000::write_long(uint32_t addr, uint32_t v)
{
	write_word(addr, static_cast<uint16_t>(v >> 16));
	write_word(addr + 2, static_cast<uint16_t>(v));
}

/* Read-modify-write instructions write the low word first: an odd
 * address faults on that word. */
void
M68000::write_long_low_first(uint32_t addr, uint32_t v)
{
	write_word(addr + 2, static_cast<uint16_t>(v));
	write_word(addr, static_cast<uint16_t>(v >> 16));
}

uint32_t
M68000::read(Size s, uint32_t addr)
{
	switch (s) {
	case BYTE:
		return read_byte(addr);
	case WORD:
		return read_word(addr);
	default:
		return read_long(addr);
	}
}

void
M68000::write(Size s, uint32_t addr, uint32_t v)
{
	switch (s) {
	case BYTE:
		write_byte(addr, static_cast<uint8_t>(v));
		break;
	case WORD:
		write_word(addr, static_cast<uint16_t>(v));
		break;
	default:
		write_long(addr, v);
		break;
	}
}

void
M68000::write_rmw(Size s, uint32_t addr, uint32_t v)
{
	if (s == LONG)
		write_long_low_first(addr, v);
	else
		write(s, addr, v);
}

/* The prefetch queue: IR holds the instruction, IRC the word after it.
 * Each extension word taken from IRC fetches the next one. */

uint16_t
M68000::next_word()
{
	const uint16_t v = m_irc;
	m_irc = fetch_word(m_au);
	m_au += 2;
	return v;
}

uint32_t
M68000::next_long()
{
	const uint32_t hi = next_word();
	return (hi << 16) | next_word();
}

/* The end of an instruction: IRC moves to IR and the next word comes in. */
void
M68000::prefetch()
{
	m_ir = m_irc;
	m_irc = fetch_word(m_au);
	m_au += 2;
	m_pc = m_au - 4;
}

/* A change of flow: both words of the queue come from the new address. */
void
M68000::jump(uint32_t addr)
{
	m_au = addr;
	m_ir = fetch_word(m_au);
	m_irc = fetch_word(m_au + 2);
	m_au += 4;
	m_pc = addr;
}

void
M68000::push_long(uint32_t v)
{
	m_a[7] -= 4;
	write_long(m_a[7], v);
}

uint32_t
M68000::pop_long()
{
	const uint32_t v = read_long(m_a[7]);
	m_a[7] += 4;
	return v;
}

/* Status register and flags */

void
M68000::set_sr(uint16_t v)
{
	v &= SR_MASK;
	if ((v ^ m_sr) & SR_S) {
		const uint32_t sp = m_a[7];
		m_a[7] = m_other_sp;
		m_other_sp = sp;
	}
	m_sr = v;
}

bool
M68000::cond(unsigned cc) const
{
	const bool c = m_sr & SR_C, v = m_sr & SR_V, z = m_sr & SR_Z, n = m_sr & SR_N;

	switch (cc & 15) {
	case 0: return true;
	case 1: return false;
	case 2: return !c && !z;	/* HI */
	case 3: return c || z;		/* LS */
	case 4: return !c;		/* CC */
	case 5: return c;		/* CS */
	case 6: return !z;		/* NE */
	case 7: return z;		/* EQ */
	case 8: return !v;		/* VC */
	case 9: return v;		/* VS */
	case 10: return !n;		/* PL */
	case 11: return n;		/* MI */
	case 12: return n == v;		/* GE */
	case 13: return n != v;		/* LT */
	case 14: return !z && n == v;	/* GT */
	default: return z || n != v;	/* LE */
	}
}

/* N and Z from the result, V and C cleared, X kept */
void
M68000::flags_nz(Size s, uint32_t r)
{
	uint16_t f = m_sr & static_cast<uint16_t>(~(SR_N | SR_Z | SR_V | SR_C));
	if ((r & mask(s)) == 0)
		f |= SR_Z;
	if (r & msb(s))
		f |= SR_N;
	m_sr = f;
}

/* r = a + b, X and C alike */
void
M68000::flags_add(Size s, uint32_t a, uint32_t b, uint32_t r)
{
	const uint32_t m = msb(s);
	uint16_t f = m_sr & static_cast<uint16_t>(~(SR_X | SR_N | SR_Z | SR_V | SR_C));
	if ((r & mask(s)) == 0)
		f |= SR_Z;
	if (r & m)
		f |= SR_N;
	if ((~(a ^ b) & (a ^ r)) & m)
		f |= SR_V;
	if (((a & b) | (~r & (a | b))) & m)
		f |= SR_C | SR_X;
	m_sr = f;
}

/* r = a - b, X and C alike */
void
M68000::flags_sub(Size s, uint32_t a, uint32_t b, uint32_t r)
{
	const uint32_t m = msb(s);
	uint16_t f = m_sr & static_cast<uint16_t>(~(SR_X | SR_N | SR_Z | SR_V | SR_C));
	if ((r & mask(s)) == 0)
		f |= SR_Z;
	if (r & m)
		f |= SR_N;
	if (((a ^ b) & (a ^ r)) & m)
		f |= SR_V;
	if (((~a & b) | (r & (~a | b))) & m)
		f |= SR_C | SR_X;
	m_sr = f;
}

/* Operands */

/* d8(An,Xn) and d8(PC,Xn): the index word */
uint32_t
M68000::index(uint16_t ext) const
{
	const unsigned n = (ext >> 12) & 7;
	uint32_t x = (ext & 0x8000) ? m_a[n] : m_d[n];
	if (!(ext & 0x0800))
		x = static_cast<uint32_t>(static_cast<int16_t>(x));
	return x + static_cast<uint32_t>(static_cast<int8_t>(ext));
}

/* Reads the extension words of an operand and works out where it is.
 * (An)+ and -(An) change An here. */
M68000::Ea
M68000::ea(unsigned mode, unsigned reg, Size s)
{
	Ea e{Ea::MEM, static_cast<uint8_t>(reg), 0, 0};
	const uint32_t step = step_of(s, reg);

	switch (mode) {
	case 0:
		e.kind = Ea::DREG;
		break;
	case 1:
		e.kind = Ea::AREG;
		break;
	case 2:
		e.addr = m_a[reg];
		break;
	case 3:
		e.addr = m_a[reg];
		m_a[reg] += step;
		break;
	case 4:
		idle(2);
		m_a[reg] -= step;
		e.addr = m_a[reg];
		break;
	case 5:
		e.addr = m_a[reg] + static_cast<uint32_t>(static_cast<int16_t>(next_word()));
		break;
	case 6:
		idle(2);
		e.addr = m_a[reg] + index(next_word());
		break;
	default:
		switch (reg) {
		case 0:
			e.addr = static_cast<uint32_t>(static_cast<int16_t>(next_word()));
			break;
		case 1:
			e.addr = next_long();
			break;
		case 2:
			e.addr = m_au - 2;
			e.addr += static_cast<uint32_t>(static_cast<int16_t>(next_word()));
			break;
		case 3:
			idle(2);
			e.addr = m_au - 2;
			e.addr += index(next_word());
			break;
		default:
			e.kind = Ea::IMM;
			e.imm = s == LONG ? next_long() : (next_word() & mask(s));
			break;
		}
		break;
	}
	return e;
}

uint32_t
M68000::get(const Ea &e, Size s)
{
	switch (e.kind) {
	case Ea::DREG:
		return m_d[e.reg] & mask(s);
	case Ea::AREG:
		return m_a[e.reg] & mask(s);
	case Ea::MEM:
		return read(s, e.addr);
	default:
		return e.imm;
	}
}

/* LEA and PEA: the address of a control operand */
uint32_t
M68000::control_address(unsigned mode, unsigned reg)
{
	if (mode == 6 || (mode == 7 && reg == 3)) {
		const uint32_t base = mode == 6 ? m_a[reg] : m_au - 2;
		idle(2);
		const uint32_t addr = base + index(next_word());
		idle(2);
		return addr;
	}
	return ea(mode, reg, LONG).addr;
}

/* Exceptions */

/* The frame of groups 1 and 2 on the supervisor stack: the low word of the
 * return PC first, then SR, then its high word */
void
M68000::push_frame(uint16_t sr, uint32_t return_pc)
{
	m_a[7] -= 6;
	write_word(m_a[7] + 4, static_cast<uint16_t>(return_pc));
	write_word(m_a[7], sr);
	write_word(m_a[7] + 2, static_cast<uint16_t>(return_pc >> 16));
}

/* That frame taken back by RTE and RTR: its SR, and the PC it returns to */
uint32_t
M68000::pop_frame(uint16_t &sr)
{
	const uint32_t hi = read_word(m_a[7] + 2);

	sr = read_word(m_a[7]);

	const uint32_t lo = read_word(m_a[7] + 4);

	m_a[7] += 6;
	return (hi << 16) | lo;
}

/* The new PC from the vector, and the queue filled from there */
void
M68000::enter_vector(uint32_t vector)
{
	const uint32_t target = read_long(vector * 4);

	m_au = target;
	m_ir = fetch_word(m_au);
	idle(2);
	m_irc = fetch_word(m_au + 2);
	m_au += 4;
	m_pc = target;
}

/* Groups 1 and 2: the return PC and SR go on the supervisor stack, then
 * the new PC comes from the vector. */
void
M68000::exception(unsigned vector, uint32_t return_pc)
{
	const uint16_t old = m_sr;

	set_sr(static_cast<uint16_t>((m_sr | SR_S) & ~SR_T));
	push_frame(old, return_pc);
	enter_vector(vector);
}

/* Group 0: the access that faulted, the instruction and the PC of the
 * prefetch go on the supervisor stack. A fault while doing so halts. */
void
M68000::address_error(AddressError e)
{
	const uint16_t old = m_sr;
	const unsigned fc = e.program ? program_fc() : data_fc();
	const uint16_t status = static_cast<uint16_t>((m_ird & 0xFFE0) | (e.write ? 0 : 0x10) | (e.program ? 0x08 : 0) | fc);
	const uint32_t pc = m_au - 4;

	idle(4);
	set_sr(static_cast<uint16_t>((m_sr | SR_S) & ~SR_T));
	m_a[7] -= 14;
	write_word(m_a[7] + 12, static_cast<uint16_t>(pc));
	write_word(m_a[7] + 8, old);
	write_word(m_a[7] + 10, static_cast<uint16_t>(pc >> 16));
	write_word(m_a[7] + 6, m_ird);
	write_word(m_a[7] + 4, static_cast<uint16_t>(e.addr));
	write_word(m_a[7], status);
	write_word(m_a[7] + 2, static_cast<uint16_t>(e.addr >> 16));
	enter_vector(3);
	if (faulted())
		m_halted = true;
}

/* Interrupt acknowledge, then as a group 1 exception. HEURISTIC: the
 * autovector cycle waits for the E clock; 4 clocks more than a vectored
 * one, which gives the 44 clocks of the manual. */
void
M68000::interrupt(unsigned level)
{
	const uint16_t old = m_sr;
	const uint32_t return_pc = m_pc;

	m_stopped = false;
	idle(6);
	int vector = m_bus.int_ack(level);
	m_cycles += 4;
	if (vector < 0) {
		vector = static_cast<int>(24 + level);
		idle(4);
	}
	set_sr(static_cast<uint16_t>(((m_sr | SR_S) & ~(SR_T | SR_I)) | (level << 8)));
	push_frame(old, return_pc);
	enter_vector(static_cast<uint32_t>(vector));
}

/* DIVU and DIVS by 0: the flags, then the trap of vector 5 */
void
M68000::divide_by_zero()
{
	m_sr = static_cast<uint16_t>((m_sr & ~(SR_N | SR_V | SR_C)) | SR_Z);
	idle(8);
	exception(5, m_au - 2);
}

/* Privilege violation and illegal instructions: the PC of the instruction */
void
M68000::privilege()
{
	idle(4);
	exception(8, m_pc);
}

void
M68000::illegal(unsigned vector)
{
	idle(4);
	exception(vector, m_pc);
}

/* Control */

void
M68000::reset()
{
	m_stopped = false;
	m_halted = false;
	m_nmi_edge = false;
	m_sr = SR_S | SR_I;
	m_a[7] = read_long(0);
	jump(read_long(4));
	if (faulted())
		m_halted = true;
}

void
M68000::set_ipl(unsigned level)
{
	level &= 7;
	if (level == 7 && m_ipl != 7)
		m_nmi_edge = true;
	m_ipl = static_cast<uint8_t>(level);
}

uint32_t
M68000::step()
{
	const uint64_t start = m_cycles;

	if (m_halted) {
		m_cycles += 4;
		return 4;
	}
	const unsigned mask_level = (m_sr & SR_I) >> 8;

	if (m_nmi_edge) {
		m_nmi_edge = false;
		interrupt(7);
	} else if (m_ipl > mask_level) {
		interrupt(m_ipl);
	} else if (m_stopped) {
		m_cycles += 4;
	} else {
		const bool trace = (m_sr & SR_T) != 0;
		m_ird = m_ir;
		execute();
		if (trace && !m_stopped && !m_faulting) {
			idle(4);
			exception(9, m_pc);
		}
	}
	if (faulted())
		address_error(m_fault);
	return static_cast<uint32_t>(m_cycles - start);
}

uint32_t
M68000::run(uint32_t cycles)
{
	const uint64_t start = m_cycles;

	m_run_end = start + cycles;
	while (m_cycles < m_run_end)
		step();
	return static_cast<uint32_t>(m_cycles - start);
}

void
M68000::save(M68000State &out) const
{
	out.d = m_d;
	out.a = m_a;
	out.other_sp = m_other_sp;
	out.pc = m_pc;
	out.sr = m_sr;
	out.ir = m_ir;
	out.irc = m_irc;
	out.ipl = m_ipl;
	out.nmi_edge = m_nmi_edge;
	out.stopped = m_stopped;
	out.halted = m_halted;
}

void
M68000::load(const M68000State &in)
{
	m_d = in.d;
	m_a = in.a;
	m_other_sp = in.other_sp;
	m_pc = in.pc;
	m_au = in.pc + 4;
	m_sr = static_cast<uint16_t>(in.sr & SR_MASK);
	m_ir = in.ir;
	m_irc = in.irc;
	m_ipl = static_cast<uint8_t>(in.ipl & 7);
	m_nmi_edge = in.nmi_edge != 0;
	m_stopped = in.stopped != 0;
	m_halted = in.halted != 0;
}

} /* namespace odin */
