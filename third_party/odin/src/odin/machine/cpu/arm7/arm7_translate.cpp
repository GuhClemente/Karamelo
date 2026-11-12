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

/* The blocks of the ARM7 as host code: what a block holds, where it is
 * found, and when they are all forgotten. Arm7Host writes the steps. */

#include "odin/machine/cpu/arm7/arm7.hpp"
#include "odin/common/bytes.hpp"

#include <bit>

namespace odin {

namespace {

/* The host code reads the pages of m_blocks as plain pointers */
static_assert(sizeof(std::unique_ptr<uint32_t[]>) == sizeof(uint32_t *));

/* An instruction after which the program never goes on to the next one
 * when it runs: B, BL, BX, SWI, a load of PC alone or in a list. The
 * block stops there; any other change of PC is seen after the
 * instruction. */
bool always_leaves(uint32_t op)
{
	if ((op >> 28) != 0xE) {
		return false;
	}
	return (op & 0x0E000000) == 0x0A000000 || (op & 0x0FFFFFF0) == 0x012FFF10 || (op & 0x0F000000) == 0x0F000000 ||
	       ((op & 0x0E108000) == 0x08108000) || ((op & 0x0C10F000) == 0x0410F000);
}

} /* namespace */

void Arm7::set_translate(bool on)
{
	if (on && !m_host) {
		const auto from_cpu = [this](const void *at) { return HostCode::place_in(this, at); };
		const Arm7Host::Places places{from_cpu(&m_r[0]), from_cpu(&m_pc), from_cpu(&m_op_pc), from_cpu(&m_cpsr),
		                              from_cpu(&m_cycles), from_cpu(&m_instructions), from_cpu(&m_irq_line),
		                              from_cpu(&m_fiq_line), from_cpu(&m_ram.base), from_cpu(&m_host_stale),
		                              from_cpu(&m_host_stale_block), from_cpu(&m_turn_kept), from_cpu(&m_failed),
		                              from_cpu(&m_ram.size), from_cpu(&m_ram.mask), from_cpu(&m_blocks),
		                              DECODED_PAGES << DECODED_PAGE};

		m_host = std::make_unique<Arm7Host>(places, &Arm7::linked);
	}
	m_translate = on && m_host->usable();
	drop_blocks();
}

void Arm7::drop_blocks()
{
	if (!m_host) {
		return;
	}
	m_host->clear();
	m_unlinked.clear();
	for (std::unique_ptr<uint32_t[]> &page : m_blocks) {
		page.reset();
	}
	for (std::unique_ptr<uint8_t[]> &page : m_block_words) {
		page.reset();
	}
}

/* run_decoded() with the blocks of the memory run as host code where they
 * may, which does the same, instruction for instruction. With a turn kept,
 * run() looks at it where run_decoded() would, at its start and where the
 * program left it: the host code then takes no direct link, and linked()
 * counts the instructions away from the turn as run_decoded() does, those
 * whose condition failed too, and comes back here at these places; a block
 * with one of them past its first instruction runs from what was decoded.
 * An instruction whose word is not the one its block was written for runs
 * from what was decoded, and its block is written again the next time. */
uint32_t Arm7::run_translated(uint32_t done, uint32_t cycles)
{
	bool stale = false;

	while (done < cycles) {
		if (interrupt_pending()) {
			break;
		}
		const uint32_t addr = m_pc & ~3u;
		const uint32_t at = addr & m_ram.mask;
		if (!in_decoded_memory(addr)) {
			break;
		}
		if (m_turn_kept && turn_watched()) {
			return done;
		}
		if (!stale) {
			if (const uint32_t place = host_block(addr, at)) {
				if (m_turn_kept && turn_inside(addr, at)) {
					return run_decoded(done, cycles);
				}
				m_turn_mark = m_instructions + m_failed;

				m_host_stale = false;
				done = m_host->run(*this, place, done, cycles);
				if (m_host_stale) {
					drop_block(m_host_stale_block);
				}
				stale = m_host_stale;
				if (m_turn_kept) {
					turn_away(m_instructions + m_failed - m_turn_mark);
				}
				continue;
			}
		}
		stale = false;
		if (m_turn_kept) {
			turn_away(1);
		}
		done += step_decoded();
	}
	return done;
}

/* The block at `addr` is written again the next time it runs. */
void Arm7::drop_block(uint32_t addr)
{
	const uint32_t at = addr & m_ram.mask;

	if (uint32_t *page = m_blocks[at >> DECODED_PAGE].get()) {
		page[word_in_page(at)] = 0;
	}
}

/* The place of the block that starts at `addr`, `at` in the memory,
 * written now if there is none; 0 when it cannot be. */
uint32_t Arm7::host_block(uint32_t addr, uint32_t at)
{
	if (m_pc & 3) {
		return 0;
	}
	std::unique_ptr<uint32_t[]> &page = m_blocks[at >> DECODED_PAGE];
	if (page && page[word_in_page(at)]) {
		return page[word_in_page(at)];
	}
	if (!m_host->room()) {
		drop_blocks();
	}
	if (!page) {
		page = std::make_unique<uint32_t[]>(DECODED_WORDS);
		m_block_words[at >> DECODED_PAGE] = std::make_unique<uint8_t[]>(DECODED_WORDS);
	}

	uint32_t words = 0;
	const uint32_t place = translate(addr, words);

	m_block_words[at >> DECODED_PAGE][word_in_page(at)] = static_cast<uint8_t>(words);
	page[word_in_page(at)] = place;
	/* The branches to it written before, its own among them, go
	 * straight to it now, as those written after it do. */
	if (const auto waiting = m_unlinked.find(addr); waiting != m_unlinked.end()) {
		for (const uint32_t branch : waiting->second) {
			m_host->link_to(branch, place);
		}
		m_unlinked.erase(waiting);
	}
	return place;
}

/* A place run() looks at for the turn kept, inside the block at `addr`
 * (`at` in the memory) past its first instruction: run_decoded() sees the
 * program reach it there. */
bool Arm7::turn_inside(uint32_t addr, uint32_t at) const
{
	const uint8_t *const words = m_block_words[at >> DECODED_PAGE].get();
	const uint32_t end = addr + 4u * (words ? words[word_in_page(at)] : 0u);
	const auto inside = [addr, end](uint32_t pc) { return pc > addr && pc < end; };

	return inside(m_turn_start.pc) || (m_turn_left && inside(m_turn_left_state.pc));
}

/* The place of the block written for `addr`, 0 for none */
uint32_t Arm7::block_at(uint32_t addr) const
{
	const uint32_t at = addr & m_ram.mask;

	if ((addr & 3) || !in_decoded_memory(addr)) {
		return 0;
	}
	const uint32_t *page = m_blocks[at >> DECODED_PAGE].get();
	return page ? page[word_in_page(at)] : 0;
}

/* The instructions from `addr` on, up to one after which the program
 * never goes on, the end of the page or BLOCK_MAX of them. Data
 * processing, LDR, STR, LDM, STM, B and BL are done in place, the others
 * called. */
uint32_t Arm7::translate(uint32_t addr, uint32_t &words)
{
	Arm7Host &host = *m_host;
	const uint32_t place = host.begin(addr);
	uint32_t pc = addr;
	bool leaves = false;

	for (uint32_t i = 0; i < BLOCK_MAX; i++) {
		const uint32_t offset = pc & m_ram.mask;

		if (i > 0 && ((offset & ((1u << DECODED_PAGE) - 1)) == 0 || pc >= m_ram.size)) {
			break;
		}
		const uint8_t *p = m_ram.base + offset;
		const uint32_t op = le32(p);
		const Run does = decode(op);
		const uint16_t pass = condition_pass(op >> 28);
		const bool conditional = pass != 0xFFFF;

		host.check(offset, pc, op);
		const HostCode::Jump skip = conditional ? host.unless(op >> 28, pass) : HostCode::Jump{0};
		switch (form_of(op, does)) {
		case Form::Data:
			data_in_place(pc, op);
			host.spent(pc, 1);
			break;
		case Form::Transfer:
			if (!transfer_in_place(pc, op, does)) {
				host.call(pc, op, does);
			}
			break;
		case Form::Block:
			if (!block_in_place(pc, op, does)) {
				host.call(pc, op, does);
			}
			break;
		case Form::Branch: {
			const uint32_t target = pc + 8 + branch_offset(op);
			if (const uint32_t at = host.branch(pc, target, (op & (1u << 24)) != 0, block_at(target))) {
				m_unlinked[target].push_back(at);
			}
			break;
		}
		default:
			host.call(pc, op, does);
			break;
		}
		if (conditional) {
			host.failed(skip, pc);
		}
		pc += 4;
		if (always_leaves(op)) {
			leaves = true;
			break;
		}
	}
	words = (pc - addr) / 4;
	/* A block that falls through goes on to the next as a branch does */
	if (const uint32_t at = host.end(pc, leaves ? 0 : block_at(pc)); at && !leaves) {
		m_unlinked[pc].push_back(at);
	}
	return place;
}

/* Arm7Ops::data_processing() for this word, Rd other than PC and the
 * second operand a value or a register shifted by a value: operand 2 in
 * B, Rn in A, the result in A, the carry of the shifter in E. */
void Arm7::data_in_place(uint32_t pc, uint32_t op)
{
	using Reg = Arm7Host::Reg;
	using Alu = Arm7Host::Alu;
	using Shift = Arm7Host::Shift;
	Arm7Host &host = *m_host;
	const uint32_t opcode = (op >> 21) & 0xF;
	const bool s = (op >> 20) & 1;
	const bool logical = opcode == 0x0 || opcode == 0x1 || opcode == 0x8 || opcode == 0x9 || opcode >= 0xC;
	const bool carry_wanted = s && logical;
	const uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
	/* The carry of the shifter, for S of a logical operation: as C was, in
	 * E, or a value. carry_out() keeps the carry of the host after a step
	 * that set it. */
	enum { KEPT, IN_E, ZERO, ONE } carry = KEPT;
	const auto carry_out = [&] {
		if (carry_wanted) {
			host.keep_carry(Reg::E);
			carry = IN_E;
		}
	};
	/* LSR #32 and ASR #32: the top bit goes out */
	const auto top_out = [&] {
		if (carry_wanted) {
			host.copy(Reg::E, Reg::B);
			host.shift(Shift::Right, Reg::E, 31);
			carry = IN_E;
		}
	};

	if (op & (1u << 25)) {
		const uint32_t rot = ((op >> 8) & 0xF) * 2;
		const uint32_t value = ror(op & 0xFF, rot);

		host.load_value(Reg::B, value);
		if (carry_wanted && rot != 0) {
			carry = (value >> 31) ? ONE : ZERO;
		}
	} else {
		const uint32_t rm = op & 0xF, amount = (op >> 7) & 0x1F;

		if (rm == 15) {
			host.load_value(Reg::B, pc + 8);
		} else {
			host.load(Reg::B, rm);
		}
		switch ((op >> 5) & 3) {
		case 0:
			if (amount) {
				host.shift(Shift::Left, Reg::B, amount);
				carry_out();
			}
			break;
		case 1:
			if (amount) {
				host.shift(Shift::Right, Reg::B, amount);
				carry_out();
			} else {
				top_out();
				host.load_value(Reg::B, 0);
			}
			break;
		case 2:
			if (amount) {
				host.shift(Shift::RightSigned, Reg::B, amount);
				carry_out();
			} else {
				top_out();
				host.shift(Shift::RightSigned, Reg::B, 31);
			}
			break;
		default:
			if (amount) {
				host.shift(Shift::Rotate, Reg::B, amount);
			} else {
				/* RRX: C in at the top, bit 0 out */
				host.carry_from_c(false);
				host.rotate_through_carry(Reg::B);
			}
			carry_out();
			break;
		}
	}

	const auto rn_to = [&](Reg reg) {
		if (rn == 15) {
			host.load_value(reg, pc + 8);
		} else {
			host.load(reg, rn);
		}
	};
	switch (opcode) {
	case 0x0:
	case 0x8:
		rn_to(Reg::A);
		host.alu(Alu::And, Reg::A, Reg::B);
		break;
	case 0x1:
	case 0x9:
		rn_to(Reg::A);
		host.alu(Alu::Xor, Reg::A, Reg::B);
		break;
	case 0x2:
	case 0xA:
		rn_to(Reg::A);
		host.alu(Alu::Sub, Reg::A, Reg::B);
		break;
	case 0x3:
		rn_to(Reg::D);
		host.copy(Reg::A, Reg::B);
		host.alu(Alu::Sub, Reg::A, Reg::D);
		break;
	case 0x4:
	case 0xB:
		rn_to(Reg::A);
		host.alu(Alu::Add, Reg::A, Reg::B);
		break;
	case 0x5:
		rn_to(Reg::A);
		host.carry_from_c(false);
		host.alu(Alu::Adc, Reg::A, Reg::B);
		break;
	case 0x6:
		rn_to(Reg::A);
		host.carry_from_c(true);
		host.alu(Alu::Sbb, Reg::A, Reg::B);
		break;
	case 0x7:
		rn_to(Reg::D);
		host.copy(Reg::A, Reg::B);
		host.carry_from_c(true);
		host.alu(Alu::Sbb, Reg::A, Reg::D);
		break;
	case 0xC:
		rn_to(Reg::A);
		host.alu(Alu::Or, Reg::A, Reg::B);
		break;
	case 0xD:
		host.copy(Reg::A, Reg::B);
		break;
	case 0xE:
		rn_to(Reg::A);
		host.invert(Reg::B);
		host.alu(Alu::And, Reg::A, Reg::B);
		break;
	default:
		host.copy(Reg::A, Reg::B);
		host.invert(Reg::A);
		break;
	}
	if (s) {
		if (!logical) {
			host.flags_arithmetic(Reg::A, opcode == 0x2 || opcode == 0x3 || opcode == 0x6 || opcode == 0x7 ||
			                                  opcode == 0xA);
		} else if (carry == IN_E) {
			const Reg e = Reg::E;
			host.flags_logical(Reg::A, &e);
		} else if (carry == ZERO || carry == ONE) {
			host.flags_logical_value(Reg::A, carry == ONE);
		} else {
			host.flags_logical(Reg::A, nullptr);
		}
	}
	if ((opcode & 0xC) != 0x8) {
		host.store(rd, Reg::A);
	}
}

/* Arm7Ops::single_transfer() for this word: the address in A, the offset
 * in B, the indexed address in D, the place in the memory in E. Where the
 * address is in the memory of the ARM7 it is read or written in place;
 * elsewhere, nothing done yet, the instruction is called. False, nothing
 * written, for a load of PC. */
bool Arm7::transfer_in_place(uint32_t pc, uint32_t op, Run does)
{
	using Reg = Arm7Host::Reg;
	using Alu = Arm7Host::Alu;
	using Shift = Arm7Host::Shift;
	Arm7Host &host = *m_host;
	const bool pre = (op >> 24) & 1, up = (op >> 23) & 1, one_byte = (op >> 22) & 1;
	const bool writeback = (op >> 21) & 1, load = (op >> 20) & 1;
	const uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;

	if ((load && rd == 15) || (m_ram.size & 3)) {
		return false;
	}
	if (rn == 15) {
		host.load_value(Reg::A, pc + 8);
	} else {
		host.load(Reg::A, rn);
	}
	if (!(op & (1u << 25))) {
		host.load_value(Reg::B, op & 0xFFF);
	} else {
		const uint32_t rm = op & 0xF, amount = (op >> 7) & 0x1F;

		if (rm == 15) {
			host.load_value(Reg::B, pc + 8);
		} else {
			host.load(Reg::B, rm);
		}
		switch ((op >> 5) & 3) {
		case 0:
			if (amount) {
				host.shift(Shift::Left, Reg::B, amount);
			}
			break;
		case 1:
			if (amount) {
				host.shift(Shift::Right, Reg::B, amount);
			} else {
				host.load_value(Reg::B, 0);
			}
			break;
		case 2:
			host.shift(Shift::RightSigned, Reg::B, amount ? amount : 31);
			break;
		default:
			if (amount) {
				host.shift(Shift::Rotate, Reg::B, amount);
			} else {
				/* RRX: C in at the top */
				host.carry_from_c(false);
				host.rotate_through_carry(Reg::B);
			}
			break;
		}
	}
	host.copy(Reg::D, Reg::A);
	host.alu(up ? Alu::Add : Alu::Sub, Reg::D, Reg::B);
	if (pre) {
		host.copy(Reg::A, Reg::D);
	}
	const bool update_base = (!pre || writeback) && rn != 15;

	const HostCode::Jump outside = host.unless_below(Reg::A, m_ram.size);
	host.copy(Reg::E, Reg::A);
	host.mask(Reg::E, one_byte ? m_ram.mask : m_ram.mask & ~3u);
	if (load) {
		if (one_byte) {
			host.ram_load(Reg::A, true);
		} else {
			/* A word at an address not aligned comes rotated */
			host.copy(Reg::B, Reg::A);
			host.mask(Reg::B, 3);
			host.shift(Shift::Left, Reg::B, 3);
			host.ram_load(Reg::A, false);
			host.rotate_by_b(Reg::A);
		}
		if (update_base) {
			host.store(rn, Reg::D);
		}
		host.store(rd, Reg::A);
		host.spent(pc, 3);
	} else {
		if (rd == 15) {
			host.load_value(Reg::B, pc + 12);
		} else {
			host.load(Reg::B, rd);
		}
		host.ram_store(Reg::B, one_byte);
		if (update_base) {
			host.store(rn, Reg::D);
		}
		host.spent(pc, 2);
	}
	const HostCode::Jump on = host.skip_ahead();
	host.bind(outside);
	host.call(pc, op, does);
	host.bind(on);
	return true;
}

/* Arm7::block_transfer() for this word in place, its words all in the memory
 * of the ARM7, else it is called: LDM and STM without S, the base not PC
 * and not in the list, the list not empty. A PC loaded, as a return does,
 * goes on to its block. False, nothing written, for any other. */
bool Arm7::block_in_place(uint32_t pc, uint32_t op, Run does)
{
	using Reg = Arm7Host::Reg;
	Arm7Host &host = *m_host;
	const bool pre = (op >> 24) & 1, up = (op >> 23) & 1, s = (op >> 22) & 1;
	const bool writeback = (op >> 21) & 1, load = (op >> 20) & 1;
	const uint32_t rn = (op >> 16) & 0xF, list = op & 0xFFFF;
	const uint32_t span = 4 * static_cast<uint32_t>(std::popcount(list));

	const bool loads_pc = load && (list & 0x8000);

	if (s || rn == 15 || list == 0 || (list & (1u << rn)) || (m_ram.size & 3) || span > m_ram.size) {
		return false;
	}
	/* A = the first address, D = the base after the transfer */
	host.load(Reg::A, rn);
	host.copy(Reg::D, Reg::A);
	host.add_value(Reg::A, up ? (pre ? 4 : 0) : (pre ? 0u - span : 4 - span));
	host.add_value(Reg::D, up ? span : 0u - span);
	host.mask(Reg::A, ~3u);

	const HostCode::Jump outside = host.unless_below(Reg::A, m_ram.size - span + 1);
	host.copy(Reg::E, Reg::A);
	host.mask(Reg::E, m_ram.mask & ~3u);
	for (uint32_t i = 0; i < 16; i++) {
		if (!(list & (1u << i))) {
			continue;
		}
		if (load) {
			host.ram_load(Reg::B, false);
			if (i == 15) {
				/* As load_pc() */
				host.mask(Reg::B, ~3u);
				host.store_pc(Reg::B);
			} else {
				host.store(i, Reg::B);
			}
		} else {
			if (i == 15) {
				host.load_value(Reg::B, pc + 12);
			} else {
				host.load(Reg::B, i);
			}
			host.ram_store(Reg::B, false);
		}
		host.add_value(Reg::E, 4);
	}
	if (writeback) {
		host.store(rn, Reg::D);
	}
	if (loads_pc) {
		host.went(span / 4 + 4);
	} else {
		host.spent(pc, span / 4 + (load ? 2 : 1));
	}
	const HostCode::Jump on = host.skip_ahead();
	host.bind(outside);
	host.call(pc, op, does);
	host.bind(on);
	return true;
}

/* For host code that went elsewhere: the block at PC, if it is written. */
uintptr_t Arm7::linked(Arm7 &c)
{
	const uint32_t place = c.block_at(c.m_pc);

	if (!place) {
		return 0;
	}
	/* A turn kept: the instructions of the blocks before counted, then
	 * back to run_translated() where it looks */
	if (c.m_turn_kept) {
		const uint64_t now = c.m_instructions + c.m_failed;

		c.turn_away(now - c.m_turn_mark);
		c.m_turn_mark = now;
		if (c.m_turn_kept && (c.turn_watched() || c.turn_inside(c.m_pc, c.m_pc & c.m_ram.mask))) {
			return 0;
		}
	}
	return c.m_host->address(place);
}

} /* namespace odin */
