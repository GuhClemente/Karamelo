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

#ifndef ODIN_TLCS90_HPP
#define ODIN_TLCS90_HPP

#include <cstdint>
#include <vector>

namespace odin {

/* The CPU of the Toshiba TLCS-90 family (TMP90C840, TMP90C844 and their
 * PROM versions), after the 1988 Toshiba data book "TLCS-48/90", chapter
 * TMP90C840 (instructions, flags, interrupts), checked against the core of
 * MAME. The on-chip devices are the bus: the chip around the CPU decides
 * which interrupt it has. Times are counted in states, two periods of the
 * clock each. */

/* Everything the CPU keeps between two instructions, plain data. */
struct Tlcs90State {
	uint8_t a, f, b, c, d, e, h, l;
	uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
	uint16_t ix, iy, sp, pc;
	uint8_t halted, ei_pending;

	bool operator==(const Tlcs90State &) const = default;
};

class Tlcs90Bus {
public:
	virtual ~Tlcs90Bus() = default;
	virtual uint8_t read8(uint16_t addr) = 0;
	virtual void write8(uint16_t addr, uint8_t value) = 0;
	/* The vector of an interrupt the chip has for the CPU, 0 for none:
	 * a non-maskable one at any time, a maskable one only when
	 * `maskable` (IFF set). Giving it is its acknowledge. */
	virtual uint16_t interrupt(bool maskable) = 0;
};

class Tlcs90 {
public:
	/* Flags, F from bit 7 down */
	enum : uint8_t {
		FLAG_S = 0x80, FLAG_Z = 0x40, FLAG_I = 0x20, FLAG_H = 0x10,
		FLAG_X = 0x08, FLAG_V = 0x04, FLAG_N = 0x02, FLAG_C = 0x01
	};
	/* The entry of an interrupt, from its acceptance to its vector */
	static constexpr uint32_t INTERRUPT_STATES = 20;

	explicit Tlcs90(Tlcs90Bus &bus) : m_bus(bus) { reset(); }

	/* PC 0, IFF 0, not halted; the other registers are left (the data
	 * book: "CPU registers remain unchanged"). */
	void reset();
	/* Takes an interrupt the bus has, or runs one instruction, or idles
	 * two states when halted. The states it took. */
	uint32_t step();

	void save(Tlcs90State &out) const;
	void load(const Tlcs90State &in);
	/* The memory below `end` never changes (a ROM): its instructions are
	 * decoded once, the address of a memory operand at a register taken
	 * again each time. */
	void set_fixed(uint16_t end) { m_decoded.assign(end, {}); }

	uint16_t pc() const { return m_pc; }
	/* For the tests */
	uint16_t sp() const { return m_sp; }
	uint8_t a() const { return m_r[A]; }
	uint8_t f() const { return m_f; }
	uint16_t bc() const { return r16(BC); }
	uint16_t de() const { return r16(DE); }
	uint16_t hl() const { return r16(HL); }
	bool halted() const { return m_halted; }

private:
	/* Register numbers as the opcodes give them */
	enum : uint8_t { B, C, D, E, H, L, A };
	enum : uint8_t { BC, DE, HL, NO_PAIR, IX, IY, SP, AF, AF2 };

	enum class Op : uint8_t;
	struct Operand {
		enum class Kind : uint8_t { NONE, R8, R16, IMM, MEM } kind = Kind::NONE;
		uint8_t reg = 0;
		uint16_t value = 0;	/* immediate, address, bit or condition */
		/* A memory operand, or its address as an immediate (LDA, JP,
		 * CALL), at a register pair plus disp, at HL + A, or fixed */
		enum : uint8_t { HL_PLUS_A = 0xFE, FIXED = 0xFF };
		uint8_t base = FIXED;
		int8_t disp = 0;
	};
	struct Instruction {
		Op op;
		bool wide = false;	/* 16 bits */
		Operand x, y;		/* destination, source */
		uint32_t states = 0, states_not_taken = 0;

		/* The operation and its states, the same when not taken unless said */
		void set(Op o, uint32_t taken, uint32_t not_taken = 0)
		{
			op = o;
			states = taken;
			states_not_taken = not_taken ? not_taken : taken;
		}
	};

	/* The operation of the low three bits of an opcode: ADD to CP, RLC to SRL */
	static Op alu_op(uint8_t n);
	static Op shift_op(uint8_t n);
	static Operand reg8(uint8_t n) { return { Operand::Kind::R8, n, 0 }; }
	static Operand reg16(uint8_t n) { return { Operand::Kind::R16, n, 0 }; }
	static Operand imm(uint16_t v) { return { Operand::Kind::IMM, 0, v }; }
	static Operand mem(uint16_t addr) { return { Operand::Kind::MEM, 0, addr }; }
	/* The address of a memory operand, as an immediate */
	static Operand itself(Operand m)
	{
		m.kind = Operand::Kind::IMM;
		return m;
	}

	uint8_t fetch() { return m_bus.read8(m_pc++); }
	uint16_t fetch16();
	uint8_t rd(uint16_t addr) const { return m_bus.read8(addr); }
	void wr(uint16_t addr, uint8_t v) { m_bus.write8(addr, v); }
	uint16_t rd16(uint16_t addr) const;
	void wr16(uint16_t addr, uint16_t v);
	void push(uint16_t v);
	uint16_t pop();

	uint16_t r16(uint8_t n) const;
	void set_r16(uint8_t n, uint16_t v);
	uint8_t get8(const Operand &o) const;
	void put8(const Operand &o, uint8_t v);
	uint16_t get16(const Operand &o) const;
	void put16(const Operand &o, uint16_t v);
	bool test(uint8_t cc) const;

	/* The addressing modes of the prefixes E0 to FE */
	Operand mem_of(uint8_t prefix, uint8_t displacement);
	Operand based(uint8_t base, uint8_t displacement) const;
	uint16_t address_of(const Operand &o) const;
	void decode(Instruction &ins);
	void decode_prefixed(Instruction &ins, uint8_t prefix);
	uint32_t execute(const Instruction &ins);
	void interrupt(uint16_t vector);

	void alu8(Op op, const Operand &x, uint8_t v);
	void alu16(Op op, const Operand &x, uint16_t v, bool flags_of_add);
	uint8_t shift(Op op, uint8_t v, bool on_a);
	void flags_inc_dec8(uint8_t v, bool up);
	void daa();

	Tlcs90Bus &m_bus;
	uint8_t m_r[7] = {};	/* B C D E H L A */
	uint8_t m_f = 0;
	uint8_t m_alt[8] = {};	/* B' C' D' E' H' L' A' F' */
	uint16_t m_ix = 0, m_iy = 0, m_sp = 0, m_pc = 0;
	bool m_halted = false, m_ei_pending = false;
	struct Decoded {
		Instruction ins{};
		uint8_t length = 0;	/* 0: not decoded yet */
	};
	std::vector<Decoded> m_decoded;	/* by address, below set_fixed's end */
};

} /* namespace odin */

#endif /* ODIN_TLCS90_HPP */
