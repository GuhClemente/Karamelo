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

/* ARM7 decoders. Bits 27-25 of the opcode pick the class; group 0 and
 * 1 are shared between data processing, multiplies, swaps, halfword
 * transfers and the PSR moves and are split again on the low bits.
 * Cycle sums come from the ARM7TDMI data sheet, chapter 10. */

#include "odin/machine/cpu/arm7/arm7.hpp"

#include <array>
#include <bit>
#include <utility>

namespace odin {

void Arm7::exec(uint32_t op)
{
	switch ((op >> 25) & 7) {
	case 0:
		exec_group0(op);
		break;
	case 1:
		exec_group1(op);
		break;
	case 2:
		single_transfer(op);
		break;
	case 3:
		if (op & 0x10) {
			undefined(op);
		} else {
			single_transfer(op);
		}
		break;
	case 4:
		block_transfer(op);
		break;
	case 5:
		branch(op);
		break;
	case 6:
		/* LDC and STC: no coprocessor on this bus. */
		undefined(op);
		break;
	default:
		if (op & (1u << 24)) {
			take_swi();
		} else {
			/* CDP, MRC and MCR. */
			undefined(op);
		}
		break;
	}
}

/* Register forms: data processing, MUL/MLA, long multiplies, SWP,
 * halfword and signed transfers, BX, MRS and MSR. */
void Arm7::exec_group0(uint32_t op)
{
	if ((op & 0x90) == 0x90) {
		if (op & 0x60) {
			halfword_transfer(op);
		} else if ((op & 0x0FC00000) == 0) {
			multiply(op);
		} else if ((op & 0x0F800000) == 0x00800000) {
			multiply_long(op);
		} else if ((op & 0x0FB00FF0) == 0x01000090) {
			swap(op);
		} else {
			undefined(op);
		}
		return;
	}
	if ((op & 0x0FFFFFF0) == 0x012FFF10) {
		branch_exchange(op);
		return;
	}
	if ((op & 0x01900000) == 0x01000000) {
		/* TST, TEQ, CMP, CMN without S: the PSR moves live here. */
		if ((op & 0x0FBF0FFF) == 0x010F0000) {
			status_to_reg(op);
		} else if ((op & 0x0FB0FFF0) == 0x0120F000) {
			reg_to_status(op, get_r(op & 0xF));
		} else {
			undefined(op);
		}
		return;
	}
	data_processing(op);
}

/* Immediate forms: data processing and MSR. */
void Arm7::exec_group1(uint32_t op)
{
	if ((op & 0x01900000) == 0x01000000) {
		if ((op & 0x0FB0F000) == 0x0320F000) {
			reg_to_status(op, ror(op & 0xFF, ((op >> 8) & 0xF) * 2));
		} else {
			undefined(op);
		}
		return;
	}
	data_processing(op);
}

/* Data processing (data sheet 4.5). With a register-specified shift r15
 * reads 12 bytes ahead and the instruction costs one more cycle; a
 * result written to r15 flushes the pipeline (+2) and, with S, moves
 * the SPSR into the CPSR. */
void Arm7::data_processing(uint32_t op)
{
	uint32_t opcode = (op >> 21) & 0xF;
	bool s = (op & (1u << 20)) != 0;
	uint32_t rn_idx = (op >> 16) & 0xF;
	uint32_t rd = (op >> 12) & 0xF;
	bool by_register = (op & (1u << 25)) == 0 && (op & 0x10) != 0;
	uint32_t carry;
	uint32_t op2 = shifter(op, s, carry);
	uint32_t rn = get_r(rn_idx);
	if (rn_idx == 15 && by_register) {
		rn += 4;
	}
	if (by_register) {
		m_cycles += 1;
	}
	uint32_t c_in = (m_cpsr & FLAG_C) ? 1 : 0;
	uint32_t res;
	bool logical = false;
	switch (opcode) {
	case 0x0: case 0x8: res = rn & op2; logical = true; break;
	case 0x1: case 0x9: res = rn ^ op2; logical = true; break;
	case 0x2: case 0xA: res = s ? sub_flags(rn, op2, 1) : rn - op2; break;
	case 0x3: res = s ? sub_flags(op2, rn, 1) : op2 - rn; break;
	case 0x4: case 0xB: res = s ? add_flags(rn, op2, 0) : rn + op2; break;
	case 0x5: res = s ? add_flags(rn, op2, c_in) : rn + op2 + c_in; break;
	case 0x6: res = s ? sub_flags(rn, op2, c_in) : rn - op2 - (1 - c_in); break;
	case 0x7: res = s ? sub_flags(op2, rn, c_in) : op2 - rn - (1 - c_in); break;
	case 0xC: res = rn | op2; logical = true; break;
	case 0xD: res = op2; logical = true; break;
	case 0xE: res = rn & ~op2; logical = true; break;
	default: res = ~op2; logical = true; break;
	}
	if (s && logical) {
		set_nz(res);
		set_flag(FLAG_C, carry != 0);
	}
	if (rd == 15 && s) {
		/* MOVS pc, lr and friends: exception return. Without an SPSR
		 * (User and System) the flags computed above stand. */
		if (const uint32_t *const saved = spsr_slot(mode())) {
			set_cpsr_full(*saved);
		}
	}
	if ((opcode & 0xC) == 0x8) {
		m_cycles += 1;
		return;
	}
	if (rd == 15) {
		set_pc(res);
		m_cycles += 3;
	} else {
		m_r[rd] = res;
		m_cycles += 1;
	}
}

/* Multiplier array cycles: 1 to 4 depending on how many leading bytes
 * of Rs are all zero or all one (data sheet 4.7.2). */
namespace {

uint32_t multiplier_cycles(uint32_t rs)
{
	if ((rs & 0xFFFFFF00u) == 0 || (rs & 0xFFFFFF00u) == 0xFFFFFF00u) {
		return 1;
	}
	if ((rs & 0xFFFF0000u) == 0 || (rs & 0xFFFF0000u) == 0xFFFF0000u) {
		return 2;
	}
	if ((rs & 0xFF000000u) == 0 || (rs & 0xFF000000u) == 0xFF000000u) {
		return 3;
	}
	return 4;
}

} /* namespace */

/* MUL and MLA: N and Z from the low word, C left alone (the data sheet
 * calls it meaningless). 1S + mI, MLA one more. */
void Arm7::multiply(uint32_t op)
{
	uint32_t rd = (op >> 16) & 0xF;
	uint32_t rn = (op >> 12) & 0xF;
	uint32_t rs = get_r((op >> 8) & 0xF);
	uint32_t rm = get_r(op & 0xF);
	uint32_t res = rm * rs;
	m_cycles += 1 + multiplier_cycles(rs);
	if (op & (1u << 21)) {
		res += get_r(rn);
		m_cycles += 1;
	}
	if (op & (1u << 20)) {
		set_nz(res);
	}
	if (rd != 15) {
		m_r[rd] = res;
	}
}

/* UMULL, UMLAL, SMULL, SMLAL. N and Z from the 64-bit result, C and V
 * left alone. 1S + (m+1)I, accumulate one more. */
void Arm7::multiply_long(uint32_t op)
{
	uint32_t rd_hi = (op >> 16) & 0xF;
	uint32_t rd_lo = (op >> 12) & 0xF;
	uint32_t rs = get_r((op >> 8) & 0xF);
	uint32_t rm = get_r(op & 0xF);
	bool is_signed = (op & (1u << 22)) != 0;
	uint64_t res;
	if (is_signed) {
		int64_t prod = static_cast<int64_t>(static_cast<int32_t>(rm)) * static_cast<int64_t>(static_cast<int32_t>(rs));
		res = static_cast<uint64_t>(prod);
	} else {
		res = static_cast<uint64_t>(rm) * rs;
	}
	m_cycles += 2 + multiplier_cycles(rs);
	if (op & (1u << 21)) {
		res += (static_cast<uint64_t>(get_r(rd_hi)) << 32) | get_r(rd_lo);
		m_cycles += 1;
	}
	if (op & (1u << 20)) {
		m_cpsr = (m_cpsr & ~(FLAG_N | FLAG_Z)) | ((res >> 63) != 0 ? FLAG_N : 0) | (res == 0 ? FLAG_Z : 0);
	}
	if (rd_hi != 15) {
		m_r[rd_hi] = static_cast<uint32_t>(res >> 32);
	}
	if (rd_lo != 15) {
		m_r[rd_lo] = static_cast<uint32_t>(res);
	}
}

/* SWP and SWPB: read then write with the bus locked. 1S + 2N + 1I. */
void Arm7::swap(uint32_t op)
{
	uint32_t addr = get_r((op >> 16) & 0xF);
	uint32_t rd = (op >> 12) & 0xF;
	uint32_t src = get_r(op & 0xF);
	uint32_t v;
	if (op & (1u << 22)) {
		v = rd8(addr);
		wr8(addr, u8(src));
	} else {
		v = ror(rd32(addr), (addr & 3) * 8);
		wr32(addr, src);
	}
	set_reg(rd, v);
	m_cycles += 4;
}

/* Offset of LDR/STR: 12-bit immediate, or Rm shifted by an immediate;
 * RRX takes C in on top (ARM DDI 0100I, A5.2.4). */
uint32_t Arm7::single_transfer_offset(uint32_t op)
{
	if ((op & (1u << 25)) == 0) {
		return op & 0xFFF;
	}
	uint32_t carry = (m_cpsr & FLAG_C) ? 1 : 0;
	return shift_by((op >> 5) & 3, get_r(op & 0xF), (op >> 7) & 0x1F, false, carry);
}

/* A load into r15 flushes the pipeline: 2 more cycles. */
void Arm7::load_pc(uint32_t v)
{
	set_pc(v);
	m_cycles += 2;
}

/* LDR, LDRB, STR, STRB (data sheet 4.9). Pre-indexed with W or
 * post-indexed updates the base; the writeback lands before the loaded
 * value, so with Rd = Rn the load wins. A word load from an unaligned
 * address rotates the word so that the addressed byte ends up low.
 * STR of r15 stores the instruction address plus 12. Loads cost
 * 1S + 1N + 1I, stores 2N. */
void Arm7::single_transfer(uint32_t op)
{
	bool pre = (op & (1u << 24)) != 0;
	bool up = (op & (1u << 23)) != 0;
	bool byte = (op & (1u << 22)) != 0;
	bool writeback = (op & (1u << 21)) != 0;
	bool load = (op & (1u << 20)) != 0;
	uint32_t rn = (op >> 16) & 0xF;
	uint32_t rd = (op >> 12) & 0xF;
	uint32_t offset = single_transfer_offset(op);
	uint32_t base = get_r(rn);
	uint32_t indexed = up ? base + offset : base - offset;
	uint32_t addr = pre ? indexed : base;
	bool update_base = (!pre || writeback) && rn != 15;
	if (load) {
		uint32_t v = byte ? rd8(addr) : ror(rd32(addr), (addr & 3) * 8);
		if (update_base) {
			m_r[rn] = indexed;
		}
		if (rd == 15) {
			load_pc(v);
		} else {
			m_r[rd] = v;
		}
		m_cycles += 3;
	} else {
		uint32_t v = rd == 15 ? m_op_pc + 12 : m_r[rd];
		if (byte) {
			wr8(addr, u8(v));
		} else {
			wr32(addr, v);
		}
		if (update_base) {
			m_r[rn] = indexed;
		}
		m_cycles += 2;
	}
}

/* LDRH, STRH, LDRSB, LDRSH (data sheet 4.10). Same addressing as LDR
 * with an 8-bit immediate or an unshifted register. An odd address on
 * a halfword load behaves like the ARM7: the halfword is rotated by 8
 * (LDRH) or the sign-extended value is shifted right by 8 (LDRSH). */
void Arm7::halfword_transfer(uint32_t op)
{
	bool pre = (op & (1u << 24)) != 0;
	bool up = (op & (1u << 23)) != 0;
	bool immediate = (op & (1u << 22)) != 0;
	bool writeback = (op & (1u << 21)) != 0;
	bool load = (op & (1u << 20)) != 0;
	uint32_t rn = (op >> 16) & 0xF;
	uint32_t rd = (op >> 12) & 0xF;
	uint32_t kind = (op >> 5) & 3;
	uint32_t offset = immediate ? (((op >> 4) & 0xF0) | (op & 0xF)) : get_r(op & 0xF);
	uint32_t base = get_r(rn);
	uint32_t indexed = up ? base + offset : base - offset;
	uint32_t addr = pre ? indexed : base;
	bool update_base = (!pre || writeback) && rn != 15;
	if (load) {
		uint32_t v;
		switch (kind) {
		case 1:
			v = rd16(addr);
			if (addr & 1) {
				v = ror(v, 8);
			}
			break;
		case 2:
			v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(rd8(addr))));
			break;
		default:
			v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(rd16(addr))));
			if (addr & 1) {
				v = static_cast<uint32_t>(static_cast<int32_t>(v) >> 8);
			}
			break;
		}
		if (update_base) {
			m_r[rn] = indexed;
		}
		if (rd == 15) {
			load_pc(v);
		} else {
			m_r[rd] = v;
		}
		m_cycles += 3;
		return;
	}
	if (kind != 1) {
		/* LDRD and STRD are ARMv5. */
		undefined(op);
		return;
	}
	uint32_t v = rd == 15 ? m_op_pc + 12 : m_r[rd];
	wr16(addr, u16(v));
	if (update_base) {
		m_r[rn] = indexed;
	}
	m_cycles += 2;
}

/* LDM and STM (data sheet 4.11). Registers go up from the lowest
 * address whatever the addressing mode. Rules from the data sheet:
 * an empty list transfers r15 and moves the base by 0x40; LDM with the
 * base in the list keeps the loaded value; STM with the base in the
 * list stores the old value only when the base is the first register;
 * S without r15 uses the User bank, S with r15 restores the CPSR from
 * the SPSR; STM of r15 stores the instruction address plus 12.
 * LDM costs nS + 1N + 1I (+2 for r15), STM (n-1)S + 2N. */
void Arm7::block_transfer(uint32_t op)
{
	bool pre = (op & (1u << 24)) != 0;
	bool up = (op & (1u << 23)) != 0;
	bool s = (op & (1u << 22)) != 0;
	bool writeback = (op & (1u << 21)) != 0;
	bool load = (op & (1u << 20)) != 0;
	uint32_t rn = (op >> 16) & 0xF;
	uint32_t list = op & 0xFFFF;
	uint32_t n = static_cast<uint32_t>(std::popcount(list));
	uint32_t span = n * 4;
	if (n == 0) {
		list = 0x8000;
		n = 1;
		span = 0x40;
	}
	uint32_t base = get_r(rn);
	uint32_t start = up ? (pre ? base + 4 : base) : (pre ? base - span : base - span + 4);
	uint32_t final = up ? base + span : base - span;
	bool user_bank = s && !(load && (list & 0x8000));
	uint32_t addr = start & ~3u;
	if (load) {
		bool pc_loaded = false;
		uint32_t new_pc = 0;
		for (uint32_t i = 0; i < 16; i++) {
			if ((list & (1u << i)) == 0) {
				continue;
			}
			uint32_t v = rd32(addr);
			addr += 4;
			if (i == 15) {
				pc_loaded = true;
				new_pc = v;
			} else if (user_bank) {
				set_user_r(i, v);
			} else {
				m_r[i] = v;
			}
		}
		if (writeback && rn != 15 && (list & (1u << rn)) == 0) {
			m_r[rn] = final;
		}
		if (pc_loaded) {
			const uint32_t *const saved = s ? spsr_slot(mode()) : nullptr;

			if (saved) {
				set_cpsr_full(*saved);
			}
			load_pc(new_pc);
		}
		m_cycles += n + 2;
		return;
	}
	uint32_t first = static_cast<uint32_t>(std::countr_zero(list));
	for (uint32_t i = 0; i < 16; i++) {
		if ((list & (1u << i)) == 0) {
			continue;
		}
		uint32_t v;
		if (i == 15) {
			v = m_op_pc + 12;
		} else if (i == rn && writeback && i != first) {
			v = final;
		} else {
			v = user_bank ? get_user_r(i) : m_r[i];
		}
		wr32(addr, v);
		addr += 4;
	}
	if (writeback && rn != 15) {
		m_r[rn] = final;
	}
	m_cycles += n + 1;
}

/* B and BL: 24-bit signed word offset from the instruction address
 * plus 8. 2S + 1N. */
void Arm7::branch(uint32_t op)
{
	const uint32_t offset = branch_offset(op);

	if (op & (1u << 24)) {
		m_r[14] = m_op_pc + 4;
	}
	m_pc = m_op_pc + 8 + offset;
	m_cycles += 3;
}

/* BX: bit 0 of the target would select Thumb state, which this core
 * does not have; the branch is taken in ARM state and reported. */
void Arm7::branch_exchange(uint32_t op)
{
	uint32_t target = get_r(op & 0xF);
	if (target & 1) {
		trace_note("thumb entry refused", target);
	}
	set_pc(target);
	m_cycles += 3;
}

/* MRS: CPSR or SPSR to a register. 1S. */
void Arm7::status_to_reg(uint32_t op)
{
	uint32_t rd = (op >> 12) & 0xF;
	uint32_t v = (op & (1u << 22)) ? spsr() : m_cpsr;
	set_reg(rd, v);
	m_cycles += 1;
}

/* MSR with the field mask of bits 19-16 (f s x c). User mode can only
 * touch the flags; User and System have no SPSR, so the write is
 * dropped there. 1S. */
void Arm7::reg_to_status(uint32_t op, uint32_t value)
{
	uint32_t fields = (op >> 16) & 0xF;
	uint32_t mask = 0;
	if (fields & 1) {
		mask |= 0x000000FF;
	}
	if (fields & 2) {
		mask |= 0x0000FF00;
	}
	if (fields & 4) {
		mask |= 0x00FF0000;
	}
	if (fields & 8) {
		mask |= 0xF0000000;
	}
	m_cycles += 1;
	if (op & (1u << 22)) {
		uint32_t *slot = spsr_slot(mode());
		if (slot != nullptr) {
			*slot = (*slot & ~mask) | (value & mask);
		}
		return;
	}
	if (mode() == MODE_USR) {
		mask &= 0xF0000000;
	}
	set_cpsr_full((m_cpsr & ~mask) | (value & mask));
}

/* Undefined instruction trap: UND mode, r14 = next instruction,
 * vector 0x04. Coprocessor instructions land here too. */
void Arm7::undefined(uint32_t op)
{
	trace_note("undefined", op);
	take_undefined();
}

/* Instructions decoded once (Arm7::step_decoded): a function per form, the
 * choices exec() and the functions above make at each instruction made by
 * templates instead. Each does what those functions do, step for step;
 * data processing that writes r15, the moves of the status registers and the
 * rare forms run exec() itself (decode). */
struct Arm7Ops {
	/* The second operand of data processing, and the offset of LDR / STR */
	enum : uint32_t { SHIFT_LSL, SHIFT_LSR, SHIFT_ASR, SHIFT_ROR, SHIFT_REG, IMMEDIATE };
	/* single_transfer: its immediate offset, the last of its 5 forms */
	static constexpr uint32_t TRANSFER_IMMEDIATE = 4;

	/* shift_by() of an amount in the instruction (0 to 31) */
	template <uint32_t TYPE>
	static uint32_t shift_imm(uint32_t v, uint32_t amount, uint32_t &carry_out)
	{
		if constexpr (TYPE == SHIFT_LSL) {
			if (amount == 0) {
				return v;
			}
			carry_out = Arm7::bit(v, 32 - amount);
			return v << amount;
		} else if constexpr (TYPE == SHIFT_LSR) {
			if (amount == 0) {
				carry_out = v >> 31;
				return 0;
			}
			carry_out = Arm7::bit(v, amount - 1);
			return v >> amount;
		} else if constexpr (TYPE == SHIFT_ASR) {
			if (amount == 0) {
				carry_out = v >> 31;
				return (v & 0x80000000u) ? 0xFFFFFFFFu : 0;
			}
			carry_out = Arm7::bit(v, amount - 1);
			return static_cast<uint32_t>(static_cast<int32_t>(v) >> amount);
		} else {
			if (amount == 0) {
				const uint32_t old_c = carry_out;
				carry_out = v & 1;
				return (v >> 1) | (old_c << 31);
			}
			carry_out = Arm7::bit(v, amount - 1);
			return Arm7::ror(v, amount);
		}
	}

	/* data_processing() with Rd other than r15 */
	template <uint32_t OPCODE, bool S, uint32_t FORM>
	static void data_processing(Arm7 &a, uint32_t op)
	{
		constexpr bool logical = OPCODE == 0x0 || OPCODE == 0x1 || OPCODE == 0x8 || OPCODE == 0x9 || OPCODE >= 0xC;
		const uint32_t rn_idx = (op >> 16) & 0xF;
		[[maybe_unused]] const uint32_t rd = (op >> 12) & 0xF;
		[[maybe_unused]] uint32_t carry = (a.m_cpsr & Arm7::FLAG_C) ? 1 : 0;
		uint32_t op2;

		if constexpr (FORM == IMMEDIATE) {
			const uint32_t rot = ((op >> 8) & 0xF) * 2;

			op2 = Arm7::ror(op & 0xFF, rot);
			if (S && rot != 0) {
				carry = op2 >> 31;
			}
		} else if constexpr (FORM == SHIFT_REG) {
			op2 = a.shifter(op, S, carry);
		} else {
			op2 = shift_imm<FORM>(a.get_r(op & 0xF), (op >> 7) & 0x1F, carry);
		}
		uint32_t rn = a.get_r(rn_idx);
		if constexpr (FORM == SHIFT_REG) {
			if (rn_idx == 15) {
				rn += 4;
			}
			a.m_cycles += 1;
		}
		[[maybe_unused]] const uint32_t c_in = (a.m_cpsr & Arm7::FLAG_C) ? 1 : 0;
		uint32_t res;
		if constexpr (OPCODE == 0x0 || OPCODE == 0x8) {
			res = rn & op2;
		} else if constexpr (OPCODE == 0x1 || OPCODE == 0x9) {
			res = rn ^ op2;
		} else if constexpr (OPCODE == 0x2 || OPCODE == 0xA) {
			res = S ? a.sub_flags(rn, op2, 1) : rn - op2;
		} else if constexpr (OPCODE == 0x3) {
			res = S ? a.sub_flags(op2, rn, 1) : op2 - rn;
		} else if constexpr (OPCODE == 0x4 || OPCODE == 0xB) {
			res = S ? a.add_flags(rn, op2, 0) : rn + op2;
		} else if constexpr (OPCODE == 0x5) {
			res = S ? a.add_flags(rn, op2, c_in) : rn + op2 + c_in;
		} else if constexpr (OPCODE == 0x6) {
			res = S ? a.sub_flags(rn, op2, c_in) : rn - op2 - (1 - c_in);
		} else if constexpr (OPCODE == 0x7) {
			res = S ? a.sub_flags(op2, rn, c_in) : op2 - rn - (1 - c_in);
		} else if constexpr (OPCODE == 0xC) {
			res = rn | op2;
		} else if constexpr (OPCODE == 0xD) {
			res = op2;
		} else if constexpr (OPCODE == 0xE) {
			res = rn & ~op2;
		} else {
			res = ~op2;
		}
		if constexpr (S && logical) {
			a.set_nz(res);
			a.set_flag(Arm7::FLAG_C, carry != 0);
		}
		if constexpr ((OPCODE & 0xC) == 0x8) {
			a.m_cycles += 1;
		} else {
			a.m_r[rd] = res;
			a.m_cycles += 1;
		}
	}

	/* single_transfer() */
	template <bool PRE, bool UP, bool BYTE, bool WRITEBACK, bool LOAD, uint32_t FORM>
	static void single_transfer(Arm7 &a, uint32_t op)
	{
		const uint32_t rn = (op >> 16) & 0xF;
		const uint32_t rd = (op >> 12) & 0xF;
		uint32_t offset;

		if constexpr (FORM == IMMEDIATE) {
			offset = op & 0xFFF;
		} else {
			uint32_t carry = (a.m_cpsr & Arm7::FLAG_C) ? 1 : 0;
			offset = shift_imm<FORM>(a.get_r(op & 0xF), (op >> 7) & 0x1F, carry);
		}
		const uint32_t base = a.get_r(rn);
		const uint32_t indexed = UP ? base + offset : base - offset;
		const uint32_t addr = PRE ? indexed : base;
		const bool update_base = (!PRE || WRITEBACK) && rn != 15;
		if constexpr (LOAD) {
			uint32_t v;
			if constexpr (BYTE) {
				v = a.rd8(addr);
			} else {
				v = Arm7::ror(a.rd32(addr), (addr & 3) * 8);
			}
			if (update_base) {
				a.m_r[rn] = indexed;
			}
			if (rd == 15) {
				a.load_pc(v);
			} else {
				a.m_r[rd] = v;
			}
			a.m_cycles += 3;
		} else {
			const uint32_t v = rd == 15 ? a.m_op_pc + 12 : a.m_r[rd];
			if constexpr (BYTE) {
				a.wr8(addr, Arm7::u8(v));
			} else {
				a.wr32(addr, v);
			}
			if (update_base) {
				a.m_r[rn] = indexed;
			}
			a.m_cycles += 2;
		}
	}

	/* branch() */
	template <bool LINK>
	static void branch(Arm7 &a, uint32_t op)
	{
		const uint32_t offset = Arm7::branch_offset(op);

		if constexpr (LINK) {
			a.m_r[14] = a.m_op_pc + 4;
		}
		a.m_pc = a.m_op_pc + 8 + offset;
		a.m_cycles += 3;
	}

	static void exec(Arm7 &a, uint32_t op) { a.exec(op); }
	static void halfword_transfer(Arm7 &a, uint32_t op) { a.halfword_transfer(op); }
	static void multiply(Arm7 &a, uint32_t op) { a.multiply(op); }
	static void multiply_long(Arm7 &a, uint32_t op) { a.multiply_long(op); }
	static void swap(Arm7 &a, uint32_t op) { a.swap(op); }
	static void branch_exchange(Arm7 &a, uint32_t op) { a.branch_exchange(op); }
	static void block_transfer(Arm7 &a, uint32_t op) { a.block_transfer(op); }

	/* data_processing<opcode, S, form> at (opcode * 2 + S) * 6 + form */
	template <uint32_t... I>
	static constexpr std::array<Arm7::Run, sizeof...(I)> data_table(std::integer_sequence<uint32_t, I...>)
	{
		return { &data_processing<I / 12, ((I / 6) & 1) != 0, I % 6>... };
	}
	/* single_transfer<P, U, B, W, L, form> at bits 24 to 20 * 5 + form,
	 * the form 4 being the immediate offset */
	template <uint32_t... I>
	static constexpr std::array<Arm7::Run, sizeof...(I)> transfer_table(std::integer_sequence<uint32_t, I...>)
	{
		return { &single_transfer<((I / 5) & 16) != 0, ((I / 5) & 8) != 0, ((I / 5) & 4) != 0, ((I / 5) & 2) != 0,
		                          ((I / 5) & 1) != 0, I % 5 == TRANSFER_IMMEDIATE ? IMMEDIATE : I % 5>... };
	}
	static const std::array<Arm7::Run, 192> DATA;
	static const std::array<Arm7::Run, 160> TRANSFER;
};

const std::array<Arm7::Run, 192> Arm7Ops::DATA = Arm7Ops::data_table(std::make_integer_sequence<uint32_t, 192>{});
const std::array<Arm7::Run, 160> Arm7Ops::TRANSFER = Arm7Ops::transfer_table(std::make_integer_sequence<uint32_t, 160>{});

namespace {

uint32_t data_index(uint32_t op, uint32_t form)
{
	return (((op >> 21) & 0xF) * 2 + ((op >> 20) & 1)) * 6 + form;
}

uint32_t transfer_index(uint32_t op, uint32_t form)
{
	return ((op >> 20) & 0x1F) * 5 + form;
}

} /* namespace */

/* Host code does in place what these functions do, and calls the others:
 * the function decode() gave is compared with them. */
Arm7::Form Arm7::form_of(uint32_t op, Run run)
{
	switch ((op >> 25) & 7) {
	case 0:
		return run == Arm7Ops::DATA[data_index(op, (op & 0x10) ? Arm7Ops::SHIFT_REG : (op >> 5) & 3)] &&
		               !(op & 0x10)
		       ? Form::Data : Form::Other;
	case 1:
		return run == Arm7Ops::DATA[data_index(op, Arm7Ops::IMMEDIATE)] ? Form::Data : Form::Other;
	case 2:
		return run == Arm7Ops::TRANSFER[transfer_index(op, Arm7Ops::TRANSFER_IMMEDIATE)] ? Form::Transfer
		                                                                                 : Form::Other;
	case 3:
		return !(op & 0x10) && run == Arm7Ops::TRANSFER[transfer_index(op, (op >> 5) & 3)] ? Form::Transfer
		                                                                                 : Form::Other;
	case 4:
		return run == &Arm7Ops::block_transfer ? Form::Block : Form::Other;
	case 5:
		return run == &Arm7Ops::branch<true> || run == &Arm7Ops::branch<false> ? Form::Branch : Form::Other;
	default:
		return Form::Other;
	}
}

/* The choices of exec(), exec_group0() and exec_group1(), in their order. */
Arm7::Run Arm7::decode(uint32_t op)
{
	const bool to_pc = ((op >> 12) & 0xF) == 15;

	switch ((op >> 25) & 7) {
	case 0:
		if ((op & 0x90) == 0x90) {
			if (op & 0x60) {
				return &Arm7Ops::halfword_transfer;
			}
			if ((op & 0x0FC00000) == 0) {
				return &Arm7Ops::multiply;
			}
			if ((op & 0x0F800000) == 0x00800000) {
				return &Arm7Ops::multiply_long;
			}
			if ((op & 0x0FB00FF0) == 0x01000090) {
				return &Arm7Ops::swap;
			}
			return &Arm7Ops::exec;
		}
		if ((op & 0x0FFFFFF0) == 0x012FFF10) {
			return &Arm7Ops::branch_exchange;
		}
		if ((op & 0x01900000) == 0x01000000 || to_pc) {
			return &Arm7Ops::exec;
		}
		return Arm7Ops::DATA[data_index(op, (op & 0x10) ? Arm7Ops::SHIFT_REG : (op >> 5) & 3)];
	case 1:
		if ((op & 0x01900000) == 0x01000000 || to_pc) {
			return &Arm7Ops::exec;
		}
		return Arm7Ops::DATA[data_index(op, Arm7Ops::IMMEDIATE)];
	case 2:
		return Arm7Ops::TRANSFER[transfer_index(op, Arm7Ops::TRANSFER_IMMEDIATE)];
	case 3:
		return (op & 0x10) ? &Arm7Ops::exec : Arm7Ops::TRANSFER[transfer_index(op, (op >> 5) & 3)];
	case 4:
		return &Arm7Ops::block_transfer;
	case 5:
		return (op & (1u << 24)) ? &Arm7Ops::branch<true> : &Arm7Ops::branch<false>;
	default:
		return &Arm7Ops::exec;
	}
}

} /* namespace odin */
