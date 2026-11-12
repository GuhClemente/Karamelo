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

#ifndef ODIN_M68000_HPP
#define ODIN_M68000_HPP

#include <array>
#include <cstdint>

namespace odin {

/* Everything the 68000 keeps between two instructions. Plain data so that
 * a machine can save and compare it byte by byte. `pc` is the address of
 * the word in `ir`; `irc` holds the word after it (the prefetch). */
struct M68000State {
	std::array<uint32_t, 8> d;
	std::array<uint32_t, 8> a;	/* a[7]: the stack pointer in use */
	uint32_t other_sp;		/* USP in supervisor mode, SSP in user mode */
	uint32_t pc;
	uint16_t sr;
	uint16_t ir, irc;
	uint8_t ipl;			/* level on the interrupt pins */
	uint8_t nmi_edge;		/* level 7 seen rising, not yet taken */
	uint8_t stopped, halted;

	bool operator==(const M68000State &) const = default;
};

/* What the CPU sees outside its pins. Words are at even addresses; the
 * address has 24 bits. `fc` is the function code of the cycle (1 user
 * data, 2 user program, 5 supervisor data, 6 supervisor program). */
class M68000Bus {
public:
	virtual ~M68000Bus() = default;
	virtual uint16_t read16(uint32_t addr, unsigned fc) = 0;
	virtual uint8_t read8(uint32_t addr, unsigned fc) = 0;
	virtual void write16(uint32_t addr, uint16_t value, unsigned fc) = 0;
	virtual void write8(uint32_t addr, uint8_t value, unsigned fc) = 0;
	/* Interrupt acknowledge at `level`: a vector number, or -1 when the
	 * device answers with VPA (autovector). */
	virtual int int_ack(unsigned level)
	{
		(void)level;
		return -1;
	}
	/* The RESET instruction: the reset line of the devices pulses. */
	virtual void reset_devices() {}
};

/* Motorola 68000: instructions, exceptions and bus cycles as the M68000
 * User's Manual gives them, in the order of the SingleStepTests 680x0
 * suite; every bus cycle takes 4 clocks (no wait state). */
class M68000 {
public:
	explicit M68000(M68000Bus &bus);

	/* The reset exception: SSP and PC from the first two long words. */
	void reset();
	/* Runs whole instructions until at least `cycles` clocks have
	 * elapsed. Returns the clocks actually spent. */
	uint32_t run(uint32_t cycles);
	/* Takes a pending interrupt or runs one instruction (a stopped or
	 * halted CPU waits 4 clocks). Returns its clocks. */
	uint32_t step();

	/* Level on the IPL pins, 0 to 7. Level 7 is taken on its rising
	 * edge whatever the mask. */
	void set_ipl(unsigned level);

	void save(M68000State &out) const;
	void load(const M68000State &in);

	/* Clocks a machine bills without running them: turns of a wait it
	 * proved the program goes round, nothing changing. */
	void skip(uint64_t clocks) { m_cycles += clocks; }
	/* Whether the next step takes an interrupt */
	bool interrupt_pending() const { return m_nmi_edge || m_ipl > ((m_sr & SR_I) >> 8); }
	/* For a machine that sets an event in the middle of a run (a write of
	 * its program starts a timer): the run ends on the first instruction
	 * that starts at or after `cycles` (as cycles() counts them). */
	void end_run_at(uint64_t cycles)
	{
		if (cycles < m_run_end)
			m_run_end = cycles;
	}

	uint64_t cycles() const { return m_cycles; }
	uint32_t pc() const { return m_pc; }
	/* For the tests */
	uint16_t sr() const { return m_sr; }
	uint32_t d(unsigned n) const { return m_d[n & 7]; }
	uint32_t a(unsigned n) const { return m_a[n & 7]; }
	bool stopped() const { return m_stopped; }

private:
	enum : uint16_t {
		SR_C = 0x0001, SR_V = 0x0002, SR_Z = 0x0004, SR_N = 0x0008, SR_X = 0x0010,
		SR_I = 0x0700, SR_S = 0x2000, SR_T = 0x8000,
		SR_MASK = 0xA71F
	};
	enum Size : uint8_t { BYTE, WORD, LONG };

	/* A word or long access at an odd address: the instruction stops
	 * and the address error exception runs instead. The access does
	 * nothing, nor any after it; the registers go back to what they were
	 * at it (Kept) when the instruction returns. */
	struct AddressError {
		uint32_t addr;
		bool write;
		bool program;
	};
	struct Kept {
		std::array<uint32_t, 8> d, a;
		uint32_t other_sp, pc, au;
		uint16_t sr, ir, irc, ird;
		bool nmi_edge, stopped, halted;
		uint64_t cycles;
	};
	void fault(uint32_t addr, bool write, bool program);
	bool faulted();

	/* An operand once its extension words are read: in a register, in
	 * memory at `addr`, or the immediate `imm`. */
	struct Ea {
		enum Kind : uint8_t { DREG, AREG, MEM, IMM } kind;
		uint8_t reg;
		uint32_t addr;
		uint32_t imm;
	};

	/* Bus cycles: 4 clocks each. */
	uint16_t read_word(uint32_t addr);
	uint16_t fetch_word(uint32_t addr);
	uint8_t read_byte(uint32_t addr);
	uint32_t read_long(uint32_t addr);
	void write_word(uint32_t addr, uint16_t v);
	void write_byte(uint32_t addr, uint8_t v);
	void write_long(uint32_t addr, uint32_t v);
	void write_long_low_first(uint32_t addr, uint32_t v);
	uint32_t read(Size s, uint32_t addr);
	void write(Size s, uint32_t addr, uint32_t v);
	void write_rmw(Size s, uint32_t addr, uint32_t v);
	void idle(unsigned clocks) { m_cycles += clocks; }
	unsigned data_fc() const { return (m_sr & SR_S) ? 5u : 1u; }
	unsigned program_fc() const { return (m_sr & SR_S) ? 6u : 2u; }

	/* The prefetch queue. */
	uint16_t next_word();
	uint32_t next_long();
	void prefetch();
	void jump(uint32_t addr);
	void refill() { jump(m_au - 2); }
	void push_long(uint32_t v);
	uint32_t pop_long();

	/* Operands. */
	Ea ea(unsigned mode, unsigned reg, Size s);
	uint32_t get(const Ea &e, Size s);
	uint32_t index(uint16_t ext) const;
	uint32_t control_address(unsigned mode, unsigned reg);

	/* Status register and flags. */
	void set_sr(uint16_t v);
	void set_ccr(uint32_t v) { m_sr = static_cast<uint16_t>((m_sr & 0xFF00) | (v & 0x1F)); }
	bool supervisor() const { return (m_sr & SR_S) != 0; }
	bool cond(unsigned cc) const;
	void flags_nz(Size s, uint32_t r);
	void flags_add(Size s, uint32_t a, uint32_t b, uint32_t r);
	void flags_sub(Size s, uint32_t a, uint32_t b, uint32_t r);

	/* Exceptions. */
	void push_frame(uint16_t sr, uint32_t return_pc);
	uint32_t pop_frame(uint16_t &sr);
	void enter_vector(uint32_t vector);
	void divide_by_zero();
	void exception(unsigned vector, uint32_t return_pc);
	void address_error(AddressError e);
	void interrupt(unsigned level);
	void privilege();
	void illegal(unsigned vector = 4);

	/* Instructions, one group per top nibble of the opcode. */
	void execute();
	void op_0000();
	void op_move(Size s);
	void op_0100();
	void op_0101();
	void op_0110();
	void op_0111();
	void op_1000();
	void op_add_sub(bool add);
	void op_1011();
	void op_1100();
	void op_1110();

	void op_bit(unsigned kind, bool dynamic);
	void op_immediate();
	void op_to_sr_ccr(unsigned kind);
	void op_movep();
	void op_movem(bool to_registers);
	void op_misc_4e();
	void op_shift_memory();
	void op_shift_register();
	void op_divu();
	void op_divs();
	void op_mul(bool is_signed);
	void op_abcd_sbcd(bool add);
	void op_nbcd();
	void op_addx_subx(bool add);
	void op_cmpm();
	void op_chk();
	void op_exg();

	uint32_t alu(unsigned kind, Size s, uint32_t dst, uint32_t src);
	void alu_to_ea(unsigned kind, Size s, unsigned mode, unsigned reg, unsigned dreg);
	void alu_to_dn(unsigned kind, Size s, unsigned mode, unsigned reg, unsigned dreg);
	uint32_t bcd(bool add, uint32_t dst, uint32_t src);

	static uint32_t mask(Size s) { return s == BYTE ? 0xFFu : s == WORD ? 0xFFFFu : 0xFFFFFFFFu; }
	static uint32_t msb(Size s) { return s == BYTE ? 0x80u : s == WORD ? 0x8000u : 0x80000000u; }
	static uint32_t sext(Size s, uint32_t v)
	{
		return s == BYTE ? static_cast<uint32_t>(static_cast<int8_t>(v))
		       : s == WORD ? static_cast<uint32_t>(static_cast<int16_t>(v)) : v;
	}
	/* How far (An)+ and -(An) move An: A7 by 2 for a byte, to stay even */
	static uint32_t step_of(Size s, unsigned reg) { return s == BYTE ? (reg == 7 ? 2u : 1u) : s == WORD ? 2u : 4u; }
	static Size size_bits_to(unsigned bits) { return bits == 0 ? BYTE : bits == 1 ? WORD : LONG; }
	void set_d(unsigned n, Size s, uint32_t v)
	{
		const uint32_t m = mask(s);
		m_d[n & 7] = (m_d[n & 7] & ~m) | (v & m);
	}

	M68000Bus &m_bus;

	std::array<uint32_t, 8> m_d;
	std::array<uint32_t, 8> m_a;
	uint32_t m_other_sp;
	uint32_t m_pc;		/* address of the word in m_ir */
	uint32_t m_au;		/* address of the next word to prefetch */
	uint16_t m_sr;
	uint16_t m_ir, m_irc;
	uint16_t m_ird;		/* the instruction being run: IR may already
				 * hold the next one */
	uint8_t m_ipl;
	bool m_nmi_edge;
	bool m_stopped, m_halted;
	/* The address error the instruction met, and the CPU at it */
	bool m_faulting = false;
	AddressError m_fault{};
	Kept m_at_fault{};
	uint64_t m_cycles;
	uint64_t m_run_end;	/* where the run in progress stops */
};

} /* namespace odin */

#endif
