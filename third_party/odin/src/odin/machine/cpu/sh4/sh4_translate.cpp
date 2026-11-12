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

/* SH-4: the host code of the kept blocks. Each instruction of a block is
 * done in place by a few steps of Sh4Host when it has some, else called;
 * the block ends as the instructions say. The reader (sh4_ops.cpp) stays
 * the reference: the two give the same, cycle for cycle. */

#include "odin/machine/cpu/sh4/sh4.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace odin {

namespace {

using Alu = Sh4Host::Alu;
using If = Sh4Host::If;
using Shift = Sh4Host::Shift;

/* The steps most simple instructions are made of, on the words of the
 * registers of the CPU named by their rank; `t` is the word of T. */
struct Words {
	Sh4Host &host;
	uint32_t t;

	/* to = from */
	void copy(uint32_t to, uint32_t from) const
	{
		host.load(from);
		host.store(to);
	}
	/* reg = reg <alu> other */
	void both(Alu alu, uint32_t reg, uint32_t other) const
	{
		host.load(reg);
		host.alu(alu, other);
		host.store(reg);
	}
	/* to = from <alu> value */
	void with_value(Alu alu, uint32_t to, uint32_t from, uint32_t value) const
	{
		host.load(from);
		host.alu_value(alu, value);
		host.store(to);
	}
	/* T = reg <alu> other <found>: Compare for a comparison, And for a test */
	void compare(Alu alu, If found, uint32_t reg, uint32_t other) const
	{
		host.load(reg);
		host.alu(alu, other);
		host.flag(found, t);
	}
	void compare_value(Alu alu, If found, uint32_t reg, uint32_t value) const
	{
		host.load(reg);
		host.alu_value(alu, value);
		host.flag(found, t);
	}
	/* to = 0 - from: every bit turned over, plus one */
	void negate(uint32_t to, uint32_t from) const
	{
		host.load(from);
		host.alu_value(Alu::Xor, 0xFFFFFFFF);
		host.alu_value(Alu::Add, 1);
		host.store(to);
	}
	/* to = the low `bits` of from, extended by their sign */
	void extend(uint32_t to, uint32_t from, uint32_t bits) const
	{
		host.load(from);
		host.shift(Shift::Left, 32 - bits);
		host.shift(Shift::RightSigned, 32 - bits);
		host.store(to);
	}
	void shift(Shift kind, uint32_t reg, uint32_t count) const
	{
		host.load(reg);
		host.shift(kind, count);
		host.store(reg);
	}
	/* The same by one bit, T taking the bit that leaves. */
	void shift_out(Shift kind, uint32_t reg) const
	{
		host.load(reg);
		if (kind == Shift::Left) {
			host.shift(Shift::Right, 31);
		} else {
			host.alu_value(Alu::And, 1);
		}
		host.store(t);
		shift(kind, reg, 1);
	}
	/* reg = reg - 1, T set when it reaches 0 (DT) */
	void decrement(uint32_t reg) const
	{
		host.load(reg);
		host.alu_value(Alu::Sub, 1);
		host.store(reg);
		host.flag(If::Equal, t);
	}
};

} /* namespace */

void Sh4::set_translate(bool on)
{
	if (on && !m_host) {
		const auto from_cpu = [this](const void *at) { return HostCode::place_in(this, at); };
		const Sh4Host::Memory memory{m_rd.data(), m_wr.data(), m_code_epoch.data(), PAGE_BITS, PHYSICAL_PAGES,
		                             from_cpu(&m_epoch), from_cpu(&m_touched)};

		const Sh4Host::Places places{from_cpu(&m_remaining), from_cpu(&m_event), from_cpu(&m_blk),
		                             from_cpu(&m_wait.regs.pc), from_cpu(&m_wait.countdown), m_near.data(),
		                             NEAR_SIZE - 1, sizeof(Block), offsetof(Block, pc), offsetof(Block, count),
		                             offsetof(Block, checked), offsetof(Block, host)};

		m_host = std::make_unique<Sh4Host>(m_r.r, memory, places);
	}
	m_translate = on && m_host->usable();
	drop_blocks();
}

/* The steps of host code that do what the instruction does, on the
 * registers of the CPU, which stay where they are: every step writes its
 * result to them. False, and nothing written, for an instruction that has
 * no such steps. */
bool Sh4::translate_inline(Inline form, const Sh4Op &op)
{
	using Whole = Sh4Host::Whole;
	static_assert(offsetof(Regs, r) == 0);
	static_assert(offsetof(Regs, macl) == offsetof(Regs, mach) + 4);
	constexpr uint32_t PR = offsetof(Regs, pr) / 4, GBR = offsetof(Regs, gbr) / 4;
	constexpr uint32_t MACH = offsetof(Regs, mach) / 4, MACL = offsetof(Regs, macl) / 4;
	const Words w{*m_host, offsetof(Regs, t) / 4};
	const uint32_t n = op.n, m = op.m, imm = op.imm;
	const auto whole = [&](Whole what) { return w.host.whole(what, n, m, w.t, MACH); };

	switch (form) {
	case Inline::Mov: w.copy(n, m); return true;
	case Inline::MovI: w.host.store_value(n, imm); return true;
	case Inline::MovA: w.host.store_value(0, imm); return true;
	case Inline::MovT: w.copy(n, w.t); return true;
	case Inline::Add: w.both(Alu::Add, n, m); return true;
	case Inline::AddI: w.with_value(Alu::Add, n, n, imm); return true;
	case Inline::Sub: w.both(Alu::Sub, n, m); return true;
	case Inline::Neg: w.negate(n, m); return true;
	case Inline::And: w.both(Alu::And, n, m); return true;
	case Inline::AndI: w.with_value(Alu::And, 0, 0, imm); return true;
	case Inline::Or: w.both(Alu::Or, n, m); return true;
	case Inline::OrI: w.with_value(Alu::Or, 0, 0, imm); return true;
	case Inline::Xor: w.both(Alu::Xor, n, m); return true;
	case Inline::XorI: w.with_value(Alu::Xor, 0, 0, imm); return true;
	case Inline::Not: w.with_value(Alu::Xor, n, m, 0xFFFFFFFF); return true;
	case Inline::Tst: w.compare(Alu::And, If::Equal, n, m); return true;
	case Inline::TstI: w.compare_value(Alu::And, If::Equal, 0, imm); return true;
	case Inline::CmpEq: w.compare(Alu::Compare, If::Equal, n, m); return true;
	case Inline::CmpIm: w.compare_value(Alu::Compare, If::Equal, 0, imm); return true;
	case Inline::CmpHs: w.compare(Alu::Compare, If::AtLeast, n, m); return true;
	case Inline::CmpGe: w.compare(Alu::Compare, If::AtLeastSigned, n, m); return true;
	case Inline::CmpHi: w.compare(Alu::Compare, If::Above, n, m); return true;
	case Inline::CmpGt: w.compare(Alu::Compare, If::AboveSigned, n, m); return true;
	case Inline::CmpPz: w.compare_value(Alu::Compare, If::AtLeastSigned, n, 0); return true;
	case Inline::CmpPl: w.compare_value(Alu::Compare, If::AboveSigned, n, 0); return true;
	case Inline::ExtSB: w.extend(n, m, 8); return true;
	case Inline::ExtSW: w.extend(n, m, 16); return true;
	case Inline::ExtUB: w.with_value(Alu::And, n, m, 0xFF); return true;
	case Inline::ExtUW: w.with_value(Alu::And, n, m, 0xFFFF); return true;
	case Inline::ShLL: w.shift_out(Shift::Left, n); return true;
	case Inline::ShLR: w.shift_out(Shift::Right, n); return true;
	case Inline::ShAR: w.shift_out(Shift::RightSigned, n); return true;
	case Inline::ShLL2: w.shift(Shift::Left, n, 2); return true;
	case Inline::ShLL8: w.shift(Shift::Left, n, 8); return true;
	case Inline::ShLL16: w.shift(Shift::Left, n, 16); return true;
	case Inline::ShLR2: w.shift(Shift::Right, n, 2); return true;
	case Inline::ShLR8: w.shift(Shift::Right, n, 8); return true;
	case Inline::ShLR16: w.shift(Shift::Right, n, 16); return true;
	case Inline::Dt: w.decrement(n); return true;
	case Inline::ClrT: w.host.store_value(w.t, 0); return true;
	case Inline::SetT: w.host.store_value(w.t, 1); return true;
	case Inline::Nop: return true;
	case Inline::LdsMacH: w.copy(MACH, n); return true;
	case Inline::LdsMacL: w.copy(MACL, n); return true;
	case Inline::LdsPr: w.copy(PR, n); return true;
	case Inline::LdcGbr: w.copy(GBR, n); return true;
	case Inline::StsMacH: w.copy(n, MACH); return true;
	case Inline::StsMacL: w.copy(n, MACL); return true;
	case Inline::StsPr: w.copy(n, PR); return true;
	case Inline::StcGbr: w.copy(n, GBR); return true;
	case Inline::ClrMac: w.host.store_value(MACH, 0); w.host.store_value(MACL, 0); return true;
	case Inline::SwapB: return whole(Whole::SwapB);
	case Inline::SwapW: return whole(Whole::SwapW);
	case Inline::Xtrct: return whole(Whole::Xtrct);
	case Inline::AddC: return whole(Whole::AddC);
	case Inline::SubC: return whole(Whole::SubC);
	case Inline::NegC: return whole(Whole::NegC);
	case Inline::MulL: return whole(Whole::MulL);
	case Inline::MulSW: return whole(Whole::MulSW);
	case Inline::MulUW: return whole(Whole::MulUW);
	case Inline::DMulS: return whole(Whole::DMulS);
	case Inline::DMulU: return whole(Whole::DMulU);
	case Inline::RotL: return whole(Whole::RotL);
	case Inline::RotR: return whole(Whole::RotR);
	case Inline::RotCL: return whole(Whole::RotCL);
	case Inline::RotCR: return whole(Whole::RotCR);
	case Inline::ShAd: return whole(Whole::ShAd);
	case Inline::ShLd: return whole(Whole::ShLd);
	default: return false;
	}
}

/* An access to the memory, the instruction `i` of the block: in place when
 * the memory is plain, else the long way, a call of the instruction written
 * behind the block. False, and nothing written, for an instruction that is
 * not such an access, or a read relative to PC from memory that is not
 * plain: that one is called. */
bool Sh4::translate_access(Inline form, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways, bool pairs)
{
	constexpr uint32_t PR = offsetof(Regs, pr) / 4, GBR = offsetof(Regs, gbr) / 4, NONE = ~0u;
	constexpr uint32_t FR = offsetof(Regs, fr) / 4, XF = offsetof(Regs, xf) / 4, FPSCR = offsetof(Regs, fpscr) / 4;
	Sh4Host &host = *m_host;
	const uint32_t n = op.n, m = op.m;
	/* The address, the word `base` + `disp` + the word `index`; `step`
	 * added to `base` after a read into another word or after a write. A
	 * move of the FPU is written for the FPSCR.SZ the block is seen to
	 * have there, and goes the long way when it has the other. */
	struct Access {
		uint32_t size;
		uint32_t base, disp, index;
		uint32_t reg;
		int32_t step;
		bool fpu;
	};
	const auto address = [&](const Access &a, Sh4Host::Slow &slow) {
		if (a.fpu) {
			slow.jumps[slow.count++] = host.jump_if_bits(FPSCR, FPSCR_SZ, !pairs);
		}
		host.load(a.base);
		if (a.index != NONE) {
			host.alu(Alu::Add, a.index);
		}
		if (a.disp) {
			host.alu_value(Alu::Add, a.disp);
		}
	};
	const auto read = [&](const Access &a) {
		Sh4Host::Slow slow{};

		address(a, slow);
		host.read(a.size, slow);
		if (a.size == 8) {
			host.store_pair(a.reg);
		} else {
			host.store(a.reg);
		}
		if (a.step && a.reg != a.base) {
			host.load(a.base);
			host.alu_value(Alu::Add, static_cast<uint32_t>(a.step));
			host.store(a.base);
		}
		ways.push_back(LongWay{slow, i, host.join()});
	};
	const auto write = [&](const Access &a) {
		Sh4Host::Slow slow{};

		address(a, slow);
		host.write(a.size, a.reg, slow);
		if (a.step) {
			host.load(a.base);
			host.alu_value(Alu::Add, static_cast<uint32_t>(a.step));
			host.store(a.base);
		}
		ways.push_back(LongWay{slow, i, host.join()});
	};
	/* A read relative to PC: the page is known now, the value is read
	 * when the block runs. */
	const auto read_pc = [&](uint32_t size) {
		const uint8_t *page = m_rd[op.imm >> PAGE_BITS];

		if (!page || (op.imm & (size - 1))) {
			return false;
		}
		host.read_at(page + (op.imm & (PAGE_SIZE - 1)), size);
		host.store(n);
		return true;
	};
	const auto pre = [](uint32_t size) { return static_cast<uint32_t>(-static_cast<int32_t>(size)); };
	/* FRn, or with SZ the pair: DRn, or XDn for an odd n */
	const uint32_t size = pairs ? 8 : 4;
	const auto fpu = [pairs](uint32_t k) { return !pairs ? FR + k : (k & 1) ? XF + (k & 14) : FR + (k & 14); };

	switch (form) {
	case Inline::MovLL: read({4, m, 0, NONE, n, 0, false}); break;
	case Inline::MovWL: read({2, m, 0, NONE, n, 0, false}); break;
	case Inline::MovBL: read({1, m, 0, NONE, n, 0, false}); break;
	case Inline::MovLL4: read({4, m, op.imm, NONE, n, 0, false}); break;
	case Inline::MovWL4: read({2, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBL4: read({1, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovLL0: read({4, m, 0, 0, n, 0, false}); break;
	case Inline::MovWL0: read({2, m, 0, 0, n, 0, false}); break;
	case Inline::MovBL0: read({1, m, 0, 0, n, 0, false}); break;
	case Inline::MovLP: read({4, m, 0, NONE, n, 4, false}); break;
	case Inline::MovWP: read({2, m, 0, NONE, n, 2, false}); break;
	case Inline::MovBP: read({1, m, 0, NONE, n, 1, false}); break;
	case Inline::MovLLG: read({4, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovWLG: read({2, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBLG: read({1, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovLI: return read_pc(4);
	case Inline::MovWI: return read_pc(2);
	case Inline::LdsMPr: read({4, n, 0, NONE, PR, 4, false}); break;
	case Inline::MovLS: write({4, n, 0, NONE, m, 0, false}); break;
	case Inline::MovWS: write({2, n, 0, NONE, m, 0, false}); break;
	case Inline::MovBS: write({1, n, 0, NONE, m, 0, false}); break;
	case Inline::MovLS4: write({4, n, op.imm, NONE, m, 0, false}); break;
	case Inline::MovWS4: write({2, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBS4: write({1, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovLS0: write({4, n, 0, 0, m, 0, false}); break;
	case Inline::MovWS0: write({2, n, 0, 0, m, 0, false}); break;
	case Inline::MovBS0: write({1, n, 0, 0, m, 0, false}); break;
	case Inline::MovLM: write({4, n, pre(4), NONE, m, -4, false}); break;
	case Inline::MovWM: write({2, n, pre(2), NONE, m, -2, false}); break;
	case Inline::MovBM: write({1, n, pre(1), NONE, m, -1, false}); break;
	case Inline::MovLSG: write({4, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovWSG: write({2, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBSG: write({1, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::StsMPr: write({4, n, pre(4), NONE, PR, -4, false}); break;
	case Inline::MovCA: write({4, n, 0, NONE, 0, 0, false}); break;
	case Inline::FMovLoad: read({size, m, 0, NONE, fpu(n), 0, true}); break;
	case Inline::FMovStore: write({size, n, 0, NONE, fpu(m), 0, true}); break;
	case Inline::FMovIndexLoad: read({size, m, 0, 0, fpu(n), 0, true}); break;
	case Inline::FMovIndexStore: write({size, n, 0, 0, fpu(m), 0, true}); break;
	case Inline::FMovRestore: read({size, m, 0, NONE, fpu(n), static_cast<int32_t>(size), true}); break;
	case Inline::FMovSave: write({size, n, pre(size), NONE, fpu(m), -static_cast<int32_t>(size), true}); break;
	case Inline::FMov: {
		/* no access: only the guard and a copy */
		Sh4Host::Slow slow{};

		slow.jumps[slow.count++] = host.jump_if_bits(FPSCR, FPSCR_SZ, !pairs);
		if (pairs) {
			host.load_pair(fpu(m));
			host.store_pair(fpu(n));
		} else {
			host.load(fpu(m));
			host.store(fpu(n));
		}
		ways.push_back(LongWay{slow, i, host.join()});
		break;
	}
	default:
		return false;
	}
	return true;
}

/* The block at `pc` for host code, see link(). */
uintptr_t Sh4::linked(Sh4 &c, uint32_t pc)
{
	return c.link(pc);
}

/* The test of a conditional branch in place: a jump, bound later, to where
 * the branch goes. False, and nothing written, for any other instruction or
 * a branch whose delay slot holds what it may not: that one is called. */
bool Sh4::translate_condition(const Sh4Op &op, Sh4Host::Jump &taken)
{
	constexpr uint32_t T = offsetof(Regs, t) / 4;
	const Inline form = inlined(op.run);

	if ((form != Inline::Bt && form != Inline::Bf && form != Inline::BtS && form != Inline::BfS) ||
	    (op.flags & OP_BAD_SLOT)) {
		return false;
	}
	taken = m_host->jump_if_word(T, form == Inline::Bt || form == Inline::BtS);
	return true;
}

/* A branch that always goes, the instruction `i` of the block, in place: PC
 * takes where it goes, PR where it returns to; its delay slot and the end
 * that follows are written after it. False, and nothing written, when the
 * slot holds what it may not or the branch is another one: it is called. */
bool Sh4::translate_jump(const Block &b, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways)
{
	constexpr uint32_t PC = offsetof(Regs, pc) / 4, PR = offsetof(Regs, pr) / 4;
	Sh4Host &host = *m_host;
	const uint32_t here = b.pc + 2 * op.idx;

	if (op.flags & OP_BAD_SLOT) {
		return false;
	}
	switch (inlined(op.run)) {
	case Inline::Bra:
		host.store_value(PC, op.imm);
		break;
	case Inline::Bsr:
		host.store_value(PR, here + 4);
		host.store_value(PC, op.imm);
		break;
	case Inline::Rts:
		host.load(PR);
		host.store(PC);
		break;
	case Inline::Jsr:
		host.load(op.n);
		host.store(PC);
		host.store_value(PR, here + 4);
		break;
	case Inline::Jmp: {
		/* An odd address: the instruction itself stops the program. */
		Sh4Host::Slow slow{};

		host.load(op.n);
		slow.jumps[slow.count++] = host.jump_if_odd();
		host.store(PC);
		ways.push_back(LongWay{slow, i, host.join()});
		break;
	}
	case Inline::BraF:
		host.load(op.n);
		host.alu_value(Alu::Add, here + 4);
		host.store(PC);
		break;
	case Inline::BsrF:
		host.load(op.n);
		host.alu_value(Alu::Add, here + 4);
		host.store(PC);
		host.store_value(PR, here + 4);
		break;
	default:
		return false;
	}
	return true;
}

/* The arithmetic of the FPU in single precision, the instruction `i` of
 * the block: the long way, a call of the instruction, when FPSCR.PR is set.
 * The host computes as the instruction does (see Sh4OpsT). False, and
 * nothing written, for another instruction, or FMAC on a host that cannot
 * round a product and a sum once. */
bool Sh4::translate_float(Inline form, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways)
{
	using Float = Sh4Host::Float;
	constexpr uint32_t FR = offsetof(Regs, fr) / 4, FPUL = offsetof(Regs, fpul) / 4, FPSCR = offsetof(Regs, fpscr) / 4;
	constexpr uint32_t T = offsetof(Regs, t) / 4;
	Sh4Host &host = *m_host;
	const uint32_t n = FR + op.n, m = FR + op.m;
	Sh4Host::Slow slow{};

	switch (form) {
	case Inline::FIpr:
	case Inline::FTrv:
	case Inline::LdsFpul:
	case Inline::StsFpul:
		return translate_vector(form, op, i, ways);
	case Inline::FSts:
		host.load(FPUL);
		host.store(n);
		return true;
	case Inline::FLds:
		host.load(n);
		host.store(FPUL);
		return true;
	case Inline::FLt: case Inline::FTrc: case Inline::FNeg: case Inline::FAbs: case Inline::FLdi0:
	case Inline::FLdi1: case Inline::FAdd: case Inline::FSub: case Inline::FMul: case Inline::FCmpGt:
		break;
	case Inline::FMac:
		if (host.fused()) {
			break;
		}
		return false;
	default:
		return false;
	}
	slow.jumps[slow.count++] = host.jump_if_bits(FPSCR, FPSCR_PR, true);
	switch (form) {
	case Inline::FLt:
		host.float_from(0, FPUL);
		host.float_store(n, 0);
		break;
	case Inline::FTrc:
		host.float_to(n);
		host.store(FPUL);
		break;
	case Inline::FNeg:
		host.load(n);
		host.alu_value(Alu::Xor, 0x80000000);
		host.store(n);
		break;
	case Inline::FAbs:
		host.load(n);
		host.alu_value(Alu::And, 0x7FFFFFFF);
		host.store(n);
		break;
	case Inline::FLdi0:
		host.store_value(n, 0x00000000);
		break;
	case Inline::FLdi1:
		host.store_value(n, 0x3F800000);
		break;
	/* FRm first, as the reader takes it: that picks the NaN of two */
	case Inline::FAdd:
	case Inline::FMul:
		host.float_load(0, m);
		host.float_op(form == Inline::FAdd ? Float::Add : Float::Mul, 0, n);
		host.float_store(n, 0);
		break;
	case Inline::FSub:
		host.float_load(0, n);
		host.float_op(Float::Sub, 0, m);
		host.float_store(n, 0);
		break;
	case Inline::FCmpGt:
		host.float_load(0, n);
		host.float_compare(0, m);
		host.flag(If::Above, T);
		break;
	default:
		/* FMAC: FRn = FR0 * FRm + FRn */
		host.float_load(0, FR);
		host.float_load(1, n);
		host.float_load(2, m);
		host.float_fused();
		host.float_store(n, 0);
		break;
	}
	ways.push_back(LongWay{slow, i, host.join()});
	return true;
}

/* FIPR and FTRV where the host has vectors, LDS and STS of FPUL: the long
 * way, a call of the instruction, when SR.FD has the FPU disabled (or
 * FPSCR.PR is set: FIPR and FTRV do nothing then). False for FIPR and FTRV on
 * another host. */
bool Sh4::translate_vector(Inline form, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways)
{
	constexpr uint32_t FR = offsetof(Regs, fr) / 4, XF = offsetof(Regs, xf) / 4, SR = offsetof(Regs, sr) / 4;
	constexpr uint32_t FPUL = offsetof(Regs, fpul) / 4, FPSCR = offsetof(Regs, fpscr) / 4;
	Sh4Host &host = *m_host;
	Sh4Host::Slow slow{};
	const bool fpul = form == Inline::LdsFpul || form == Inline::StsFpul;

	if (!fpul && !host.vectors()) {
		return false;
	}
	slow.jumps[slow.count++] = host.jump_if_bits(SR, SR_FD, true);
	if (!fpul) {
		slow.jumps[slow.count++] = host.jump_if_bits(FPSCR, FPSCR_PR, true);
	}
	if (form == Inline::LdsFpul) {
		host.load(op.n);
		host.store(FPUL);
	} else if (form == Inline::StsFpul) {
		host.load(FPUL);
		host.store(op.n);
	} else if (form == Inline::FIpr) {
		host.float_ipr(FR + op.n, FR + op.m);
	} else {
		host.float_trv(FR + op.n, XF);
	}
	ways.push_back(LongWay{slow, i, host.join()});
	return true;
}

/* The instruction `i` of a block as host code: its steps in place when it
 * has some, and true; else a call, and false: what the call returns is for
 * the block to look at. */
bool Sh4::translate_op(const Sh4Op *ops, uint32_t i, std::vector<LongWay> &ways, bool pairs)
{
	const Inline form = inlined(ops[i].run);

	if (translate_inline(form, ops[i]) || translate_access(form, ops[i], i, ways, pairs) ||
	    translate_float(form, ops[i], i, ways)) {
		return true;
	}
	m_host->call(called(ops[i].run), i);
	return false;
}

/* FPSCR.SZ where each instruction of the block is, from what it holds at
 * the start: FSCHG turns it over. Any other change (LDS to FPSCR) is left
 * to the guard of each move. */
void Sh4::pairs_through(const Block &b, std::array<bool, BLOCK_MAX + 3> &pairs) const
{
	pairs[0] = (m_r.fpscr & FPSCR_SZ) != 0;
	for (uint32_t i = 0; i < b.count; i++) {
		pairs[i + 1] = pairs[i] != (inlined(b.ops[i].run) == Inline::FSChg);
	}
}

/* The fast way of an end that leaves, before its call. */
void Sh4::leave_fast(const Sh4Op &end)
{
	if (end.run == op_leave) {
		m_host->leave_fast(offsetof(Regs, pc) / 4, static_cast<int32_t>(end.imm));
	}
}

/* Writes the host code of a kept block: its instructions in turn, each in
 * place or called, the block left as soon as a call says so. A conditional
 * branch that goes says where to; one with a delay slot has the copy of its
 * slot and the end that follows it behind the block, as a jump has its own
 * in line. */
void Sh4::translate(Block &b)
{
	struct Taken {
		Sh4Host::Jump jump;
		uint32_t op;
		bool in_place;	/* the branch was not called: what it does is left to do */
	};
	constexpr uint32_t PC = offsetof(Regs, pc) / 4;
	std::array<Taken, BLOCK_MAX + 1> taken;
	uint32_t num_taken = 0;
	const Sh4Op *ops = b.ops;
	Sh4Host &host = *m_host;
	std::vector<LongWay> &ways = m_long_ways;
	std::array<bool, BLOCK_MAX + 3> pairs;
	bool jumped = false;

	ways.clear();
	pairs_through(b, pairs);
	b.host = host.begin(ops);
	for (uint32_t i = 0; i < b.count && !jumped; i++) {
		if (ops[i].flags & (OP_BRANCH | OP_DELAYED)) {
			Sh4Host::Jump jump;

			if (translate_condition(ops[i], jump)) {
				taken[num_taken++] = Taken{jump, i, true};
			} else {
				host.call(called(ops[i].run), i);
				taken[num_taken++] = Taken{host.jump_if_told(), i, false};
			}
		} else if (ops[i].flags & OP_JUMP) {
			if (!translate_jump(b, ops[i], i, ways)) {
				host.call(called(ops[i].run), i);
			}
			translate_op(ops, i + 1, ways, pairs[i + 1]);
			leave_fast(ops[i + 2]);
			host.call(called(ops[i + 2].run), i + 2);
			host.go_where_told();
			jumped = true;
		} else if (!translate_op(ops, i, ways, pairs[i])) {
			host.loop_if_told();
		}
	}
	/* The end of a block cut for its length, see end_next. */
	if (!jumped) {
		host.store_value(PC, ops[b.count].imm);
		host.link_fast(ops[b.count].imm);
		host.call_link(linked, ops[b.count].imm);
		host.go_where_told();
	}
	/* A branch that goes, see branch() and jump(). */
	for (uint32_t k = 0; k < num_taken; k++) {
		const uint32_t i = taken[k].op;

		host.bind(taken[k].jump);
		if (taken[k].in_place) {
			host.store_value(PC, ops[i].imm);
		}
		if (ops[i].flags & OP_DELAYED) {
			const uint32_t slot = i + ops[i].m;

			translate_op(ops, slot, ways, pairs[i + 1]);
			leave_fast(ops[slot + 1]);
			host.call(called(ops[slot + 1].run), slot + 1);
		} else if (taken[k].in_place) {
			host.add_remaining(static_cast<int32_t>(b.count - i - 1));
			host.link_fast(ops[i].imm);
			host.call_link(linked, ops[i].imm);
		}
		host.go_where_told();
	}
	/* The long ways: the instruction called, the block left if it says
	 * so, but in a delay slot, where the end that follows sees to it. */
	for (const LongWay &way : ways) {
		host.bind(way.slow);
		host.call(called(ops[way.op].run), way.op);
		if (!(ops[way.op].flags & OP_SLOT)) {
			host.loop_if_told();
		}
		host.jump_back(way.back);
	}
}

} /* namespace odin */
