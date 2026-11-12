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

/* ARM7 core: state, bus helpers, register banks, exceptions, the
 * barrel shifter and the flag arithmetic shared by the decoders in
 * arm7_ops.cpp. Semantics follow the ARM7TDMI data sheet (ARM DDI 0029)
 * and the ARM Architecture Reference Manual for ARMv4. Cycle counts are
 * the data sheet's S/N/I sums with every bus cycle worth one clock; no
 * wait states are modelled, the bus owner adds them. */

#include "odin/machine/cpu/arm7/arm7.hpp"
#include "odin/common/bytes.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <unordered_map>

namespace odin {

namespace {

/* For each condition, a bit for each value of NZCV (CPSR bits 31 to 28)
 * it passes with */
constexpr std::array<uint16_t, 16> COND_PASS = [] {
	std::array<uint16_t, 16> pass{};
	for (uint32_t cc = 0; cc < 16; cc++) {
		for (uint32_t f = 0; f < 16; f++) {
			const bool n = f & 8, z = f & 4, c = f & 2, v = f & 1;
			const bool yes = cc == 0x0 ? z : cc == 0x1 ? !z : cc == 0x2 ? c : cc == 0x3 ? !c : cc == 0x4 ? n :
			                 cc == 0x5 ? !n : cc == 0x6 ? v : cc == 0x7 ? !v : cc == 0x8 ? c && !z :
			                 cc == 0x9 ? !c || z : cc == 0xA ? n == v : cc == 0xB ? n != v :
			                 cc == 0xC ? !z && n == v : cc == 0xD ? z || n != v : cc == 0xE;
			if (yes) {
				pass[cc] = static_cast<uint16_t>(pass[cc] | (1u << f));
			}
		}
	}
	return pass;
}();

} /* namespace */

Arm7::Arm7(Arm7Bus &bus)
	: m_bus(bus), m_ram{nullptr, 0, 0}, m_trace(nullptr), m_failed(0), m_turn_mark(0), m_decoded_on(true),
	  m_translate(false), m_host_stale(false), m_host_stale_block(0), m_turn(Turn::Off), m_turn_limit(TURN_MAX),
	  m_turn_counter(TURN_NO_COUNTER), m_turn_delta(0), m_turn_laps(0), m_turn_kept(false),
	  m_turn_trail_full(false), m_turn_one_mode(false), m_turn_try(0), m_turn_misses(0), m_turn_origin(0),
	  m_turn_left(false), m_turn_resumable(false), m_turn_left_at(0), m_turn_left_state{}, m_turn_recorded(0),
	  m_turn_counted(0)
{
	m_irq_line = m_fiq_line = false;
	reset();
}

/* RESET: SVC mode, IRQ and FIQ masked, ARM state, PC at the reset vector.
 * The other registers are undefined on silicon; they are zeroed so that
 * runs stay deterministic. */
void Arm7::reset()
{
	for (uint32_t &v : m_r) {
		v = 0;
	}
	for (uint32_t &v : m_usr) {
		v = 0;
	}
	for (uint32_t &v : m_fiq) {
		v = 0;
	}
	m_irq[0] = m_irq[1] = 0;
	m_svc[0] = m_svc[1] = 0;
	m_abt[0] = m_abt[1] = 0;
	m_und[0] = m_und[1] = 0;
	for (uint32_t &v : m_spsr) {
		v = 0;
	}
	m_cpsr = FLAG_I | FLAG_F | MODE_SVC;
	m_pc = 0;
	m_op_pc = 0;
	m_cycles = 0;
	m_instructions = 0;
	if (m_turn != Turn::Off) {
		m_turn = Turn::Idle;
	}
	m_turn_wait = m_turn_backoff = 0;
	m_turn_limit = TURN_MAX;
	m_turn_kept = false;
}

uint32_t Arm7::run(uint32_t cycles)
{
	uint32_t done = 0;
	if (m_turn == Turn::Replay) {
		if (!turn_reads_hold()) {
			settle();
		}
	} else if (m_turn == Turn::Idle && !m_turn_kept) {
		/* A turn of the program, not of an interrupt handler. */
		bool taken = interrupt_pending();
		if (m_turn_wait > 0) {
			m_turn_wait--;
		} else if (!taken && mode() != MODE_FIQ && mode() != MODE_IRQ) {
			turn_begin();
		}
	}
	while (done < cycles) {
		if (m_turn == Turn::Replay) {
			done += turn_replay(cycles - done);
		} else if (m_turn == Turn::Record) {
			uint64_t before = m_instructions;
			uint32_t spent = step();
			done += spent;
			turn_step(spent, static_cast<uint32_t>(m_instructions - before));
		} else {
			/* The program may come back to the turn that was
			 * counted, for some time. */
			if (m_turn_kept && m_turn_left && m_pc == m_turn_left_state.pc && turn_resume()) {
				continue;
			}
			if (m_turn_kept) {
				/* At its start as the program, not as the
				 * handler of an interrupt that returns
				 * there. */
				if (m_pc == m_turn_start.pc && m_cpsr == m_turn_start.cpsr &&
				    m_irq_line == (m_turn_start.irq_line != 0) && m_fiq_line == (m_turn_start.fiq_line != 0)) {
					if (turn_registers_same(m_turn_start) && turn_rejoin()) {
						continue;
					}
					m_turn_misses++;
				}
				if (--m_turn_try == 0 || m_turn_misses == TURN_ROUND) {
					m_turn_kept = false;
				}
			}
			if (m_decoded_on && !m_trace) {
				done = m_translate ? run_translated(done + step_decoded(), cycles)
				                   : run_decoded(done + step_decoded(), cycles);
			} else {
				done += step();
			}
		}
	}
	return done;
}

/* The loop above and step_decoded(), instruction for instruction, for as
 * long as neither has more to do: back to the loop when the cycles are
 * spent, an interrupt is to be taken, the code is outside the memory or
 * the turn kept is to be looked at. */
uint32_t Arm7::run_decoded(uint32_t done, uint32_t cycles)
{
	uint32_t at_page = DECODED_PAGES;
	Decoded *page = nullptr;

	while (done < cycles) {
		if (interrupt_pending()) {
			break;
		}
		const uint32_t addr = m_pc & ~3u;
		const uint32_t at = addr & m_ram.mask;
		/* in_decoded_memory() written out: the loop keeps its registers
		 * across the instruction this way */
		if (addr >= m_ram.size || at >= (DECODED_PAGES << DECODED_PAGE)) {
			break;
		}
		if (m_turn_kept) {
			if (turn_watched()) {
				break;
			}
			turn_away(1);
		}
		if ((at >> DECODED_PAGE) != at_page) {
			at_page = at >> DECODED_PAGE;
			page = m_decoded[at_page].get();
			if (!page) {
				page = decoded_page(at_page);
			}
		}

		const uint32_t op = le32(m_ram.base + at);
		const Run does = refreshed(page[word_in_page(at)], op);

		m_cycles = 0;
		m_op_pc = m_pc;
		m_pc += 4;
		if ((COND_PASS[op >> 28] >> (m_cpsr >> 28)) & 1) {
			m_instructions++;
			does(*this, op);
		} else {
			m_cycles += 1;
		}
		done += m_cycles;
	}
	return done;
}

/* Turns counted without being run. A turn starts at any instruction and
 * ends when the program is back there with every register as it was. It
 * can be counted from then on if it took no exception, wrote nothing
 * outside the memory and left the memory as it found it: with the same
 * memory, the same answers of the bus and the same interrupt lines, the
 * next turn does the same again.
 *
 * A turn may also leave one word of the memory changed, if that word is a
 * count of the turns: what the turn loads from it only goes back to it,
 * with a constant added, and nothing else the turn does looks at it (see
 * turn_counts). The next turn then does the same again with the word that
 * much further, and the turns counted are owed to the word: settle() gives
 * them.
 *
 * A few turns may make one: a program that goes through eight states, one
 * at each turn, is back where it was after eight. The recording goes on
 * past a turn that leaves the memory changed, for some turns.
 *
 * A turn that was counted is kept when the program leaves it, as it does
 * for an interrupt. It is counted again from where the program left it if
 * the program is back there with every register, every byte the turn reads
 * or writes and the answers of the bus as they were (see turn_resume). It
 * is not run again to be recorded if the program comes back to its start
 * with every register as it was, everything it read still there and what it
 * leaves in the memory already there (see turn_rejoin): it is counted from
 * there. A program that stays away, or is at that start a few times without
 * being in the turn, has another turn recorded. */

void Arm7::set_turn_replay(bool on)
{
	settle();
	m_turn_kept = false;
	m_turn = on ? Turn::Idle : Turn::Off;
	m_turn_wait = m_turn_backoff = 0;
	m_turn_limit = TURN_MAX;
}

void Arm7::turn_begin()
{
	if (m_trace || m_ram.size > ((m_turn_watched.size() * 8) << WATCH_PAGE)) {
		return;
	}
	save(m_turn_start);
	m_turn_count = 0;
	m_turn_rounds = 0;
	m_turn_cycles[0] = 0;
	m_turn_passed[0] = 0;
	m_turn_changed.fill(0);
	m_turn_watched.fill(0);
	m_turn_num_reads = 0;
	m_turn_num_stores = 0;
	m_turn_bad = false;
	m_turn_counter = TURN_NO_COUNTER;
	m_turn_laps = 0;
	m_turn_kept = false;
	m_turn_trail.clear();
	m_turn_trail_full = false;
	m_turn_one_mode = true;
	m_turn_recorded++;
	m_turn = Turn::Record;
}

void Arm7::turn_abort()
{
	/* Once is the turn that follows an interrupt; more is a program
	 * that does not wait. */
	m_turn = Turn::Idle;
	m_turn_wait = m_turn_backoff;
	m_turn_backoff = m_turn_backoff ? std::min(m_turn_backoff * 2, TURN_BACKOFF) : 1;
	if (m_turn_backoff == TURN_BACKOFF) {
		m_turn_limit = TURN_MAX;
	}
}

/* Something outside is about to change. */
void Arm7::turn_break()
{
	if (m_turn == Turn::Replay) {
		settle();
	} else if (m_turn == Turn::Record) {
		m_turn = Turn::Idle;
		m_turn_wait = 0;
	}
}

void Arm7::turn_step(uint32_t cycles, uint32_t passed)
{
	uint32_t n = ++m_turn_count;
	m_turn_cycles[n] = m_turn_cycles[n - 1] + cycles;
	m_turn_passed[n] = u16(m_turn_passed[n - 1] + passed);
	if (m_turn_bad) {
		turn_abort();
		return;
	}
	/* Another mode, or a transfer of the User registers from one. */
	const TurnOp &last = m_turn_ops[n - 1];
	if (mode() != (m_turn_start.cpsr & 0x1F) || (last.ran && (last.op & 0x0E400000) == 0x08400000)) {
		m_turn_one_mode = false;
	}
	if (m_pc == m_turn_start.pc && m_cpsr == m_turn_start.cpsr) {
		Arm7State now;
		save(now);
		if (now == m_turn_start) {
			/* The bytes left changed must be those of one word. */
			bool same = turn_reads_hold();
			uint32_t counter = TURN_NO_COUNTER;
			for (uint32_t i = 0; same && i < m_turn_num_stores; i++) {
				const uint32_t offset = m_turn_stores[i].offset;
				if (m_ram.base[offset] != m_turn_stores[i].old) {
					same = counter == TURN_NO_COUNTER || counter == (offset & ~3u);
					counter = offset & ~3u;
				}
			}
			if (same && counter != TURN_NO_COUNTER) {
				same = turn_counts(counter);
			}
			if (same) {
				m_turn = Turn::Replay;
				m_turn_kept = turn_keep();
				m_turn_at = 0;
				m_turn_origin = 0;
				m_turn_backoff = 0;
				/* The next turn should not be much longer. */
				m_turn_limit = std::min(n * 2, TURN_MAX);
				return;
			}
			/* A few turns may make one: the memory is as it was
			 * after the last of them only. */
			if (++m_turn_rounds == TURN_ROUND) {
				turn_abort();
				return;
			}
		}
	}
	if (n == m_turn_limit) {
		turn_abort();
	}
}

bool Arm7::turn_reads_hold()
{
	for (uint32_t i = 0; i < m_turn_num_reads; i++) {
		const TurnRead &r = m_turn_reads[i];
		if (m_bus.peek(r.addr, r.size) != r.value) {
			return false;
		}
	}
	return true;
}

/* Whether the turn just recorded only counts in the word at `counter`: the
 * instructions are gone through again, with a mark on each register that
 * holds the word plus a constant. Such a register may be moved, have a
 * constant added and be stored back to the word; any other use of it, or of
 * flags it set, is the turn looking at its count, and the turn is not
 * counted. At the end no register may still hold it, and what the turn
 * added to the word is kept. */
bool Arm7::turn_counts(uint32_t counter)
{
	uint32_t marked = 0;	/* a bit for each register */
	const auto is = [&marked](uint32_t r) { return ((marked >> r) & 1) != 0; };
	const auto clear = [&marked](uint32_t r) { marked &= ~(1u << r); };
	const auto touches = [counter](uint32_t lo, uint32_t hi) { return hi != 0 && lo < counter + 4 && hi > counter; };
	const auto is_word = [counter](uint32_t lo, uint32_t hi) { return lo == counter && hi == counter + 4; };

	for (uint32_t i = 0; i < m_turn_count; i++) {
		const TurnOp &o = m_turn_ops[i];
		const uint32_t op = o.op;
		const uint32_t rn = (op >> 16) & 15, rd = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;

		if (!o.ran) {
			continue;
		}
		if ((op & 0x0E000000) == 0x0A000000) {
			/* B, BL */
			if (op & 0x01000000) {
				clear(14);
			}
		} else if ((op & 0x0FBF0FFF) == 0x010F0000) {
			/* MRS */
			clear(rd);
		} else if ((op & 0x0FB0FFF0) == 0x0120F000) {
			/* MSR from a register */
			if (is(rm)) {
				return false;
			}
		} else if ((op & 0x0FB0F000) == 0x0320F000) {
			/* MSR from a constant */
		} else if ((op & 0x0C000000) == 0x00000000 && (op & 0x02000090) != 0x00000090) {
			/* Data processing; what has bits 4 and 7 set without a
			 * constant is a multiply, a swap or a short transfer. */
			const uint32_t code = (op >> 21) & 15;
			const bool constant = (op & 0x02000000) != 0, flags = (op & 0x00100000) != 0;
			const bool test = code >= 8 && code <= 11, one = code == 13 || code == 15;
			const bool by_register = !constant && (op & 0x10) != 0;

			if (test && !flags) {
				return false;	/* not a data processing instruction */
			}
			const bool from = (!one && is(rn)) || (!constant && is(rm)) || (by_register && is(rs));
			if (!from) {
				if (!test) {
					clear(rd);
				}
			} else if (!flags && rd != 15 && constant && (code == 2 || code == 4) && is(rn)) {
				/* SUB, ADD of a constant */
				marked |= 1u << rd;
			} else if (!flags && rd != 15 && code == 13 && (op & 0x00000FF0) == 0) {
				/* MOV of the register as it is */
				marked |= 1u << rd;
			} else {
				return false;
			}
		} else if ((op & 0x0C000000) == 0x04000000) {
			/* LDR, STR, LDRB, STRB */
			const bool load = (op & 0x00100000) != 0, byte = (op & 0x00400000) != 0;

			if (is(rn) || ((op & 0x02000000) && is(rm))) {
				return false;
			}
			if (load) {
				if (touches(o.load_lo, o.load_hi)) {
					if (byte || rd == 15 || !is_word(o.load_lo, o.load_hi)) {
						return false;
					}
					marked |= 1u << rd;
				} else {
					clear(rd);
				}
			} else if (is(rd)) {
				if (byte || !is_word(o.store_lo, o.store_hi)) {
					return false;
				}
			} else if (touches(o.store_lo, o.store_hi)) {
				return false;	/* the word set to something else */
			}
		} else if ((op & 0x0E000000) == 0x08000000) {
			/* LDM, STM */
			const bool load = (op & 0x00100000) != 0;

			if (is(rn) || (op & 0x00400000) || touches(o.load_lo, o.load_hi) || touches(o.store_lo, o.store_hi)) {
				return false;
			}
			for (uint32_t r = 0; r < 16; r++) {
				if ((op >> r) & 1) {
					if (load) {
						clear(r);
					} else if (is(r)) {
						return false;
					}
				}
			}
		} else {
			/* A multiply, a swap, a short transfer, BX, SWI and the
			 * rest: with nothing marked, and away from the word,
			 * they cannot look at it. What they set is not known
			 * here: only the registers they could set are named. */
			if (marked || touches(o.load_lo, o.load_hi) || touches(o.store_lo, o.store_hi)) {
				return false;
			}
		}
	}
	if (marked) {
		return false;
	}

	const uint32_t after = le32(m_ram.base + counter);
	uint32_t before = 0;

	for (uint32_t b = 0; b < 4; b++) {
		uint8_t old = m_ram.base[counter + b];
		for (uint32_t k = 0; k < m_turn_num_stores; k++) {
			if (m_turn_stores[k].offset == counter + b) {
				old = m_turn_stores[k].old;
			}
		}
		before |= static_cast<uint32_t>(old) << (8 * b);
	}
	m_turn_counter = counter;
	m_turn_delta = after - before;
	return true;
}

/* Stops where run() would: at the first instruction boundary at or past
 * the budget. */
uint32_t Arm7::turn_replay(uint32_t budget)
{
	uint32_t done = 0;
	while (done < budget) {
		uint32_t base = m_turn_cycles[m_turn_at];
		auto first = m_turn_cycles.begin() + m_turn_at + 1;
		auto last = m_turn_cycles.begin() + m_turn_count + 1;
		auto it = std::lower_bound(first, last, base + (budget - done));
		uint32_t to = it == last ? m_turn_count : static_cast<uint32_t>(it - m_turn_cycles.begin());
		done += m_turn_cycles[to] - base;
		m_instructions += static_cast<uint32_t>(m_turn_passed[to] - m_turn_passed[m_turn_at]);
		if (to == m_turn_count) {
			m_turn_laps++;
			m_turn_counted++;
		}
		m_turn_at = to == m_turn_count ? 0 : to;
	}
	return done;
}

void Arm7::settle()
{
	if (m_turn == Turn::Record) {
		m_turn = Turn::Idle;
		m_turn_wait = 0;
	}
	if (m_turn != Turn::Replay) {
		return;
	}
	/* The count of the turns gets those that were only counted. */
	if (m_turn_counter != TURN_NO_COUNTER && m_turn_laps) {
		uint8_t *p = m_ram.base + m_turn_counter;
		uint32_t count = le32(p);
		count += m_turn_laps * m_turn_delta;
		put_le32(p, count);
	}
	m_turn_laps = 0;
	/* The bus answers what it did when the turn was recorded: it may
	 * have changed since the part to run was counted. */
	uint64_t instructions = m_instructions;
	uint32_t count = (m_turn_at + m_turn_count - m_turn_origin) % m_turn_count;
	m_turn = Turn::Settle;
	m_turn_next_read = 0;
	while (m_turn_next_read < m_turn_num_reads && m_turn_reads[m_turn_next_read].index < m_turn_origin) {
		m_turn_next_read++;
	}
	/* The instructions in the memory run as run_decoded() runs them,
	 * through the same reads as step() */
	for (uint32_t i = 0; i < count; i++) {
		const uint32_t addr = m_pc & ~3u;

		if (m_decoded_on && in_decoded_memory(addr)) {
			step_decoded();
		} else {
			step();
		}
	}
	m_instructions = instructions;
	m_turn = Turn::Idle;
	m_turn_wait = 0;
	m_turn_try = TURN_AWAY;
	m_turn_misses = 0;
	turn_leave();
}

/* What the bus answered when the turn was recorded, from the origin of the
 * turns counted on, past its end to its start */
uint32_t Arm7::turn_settle_read()
{
	if (m_turn_next_read == m_turn_num_reads) {
		m_turn_next_read = 0;
	}
	return m_turn_reads[m_turn_next_read++].value;
}

/* Where the program leaves the turn kept. A turn that counts in a word is
 * only taken again at its start: there, no register holds the count. So is
 * a turn the program once did not come back to as it left it: what made it
 * leave may do so each time. Every byte the turn reads or writes, from its
 * trail, the first time. */
void Arm7::turn_leave()
{
	m_turn_left = m_turn_kept && m_turn_resumable && m_turn_counter == TURN_NO_COUNTER;
	if (!m_turn_left) {
		return;
	}
	if (m_turn_footprint.empty()) {
		std::vector<uint32_t> bytes;
		for (const TurnAccess &a : m_turn_trail) {
			for (uint32_t i = 0; i < a.size; i++) {
				bytes.push_back((a.offset + i) & m_ram.mask);
			}
		}
		std::sort(bytes.begin(), bytes.end());
		bytes.erase(std::unique(bytes.begin(), bytes.end()), bytes.end());
		for (uint32_t b : bytes) {
			if (!m_turn_footprint.empty() && m_turn_footprint.back().offset + m_turn_footprint.back().size == b) {
				m_turn_footprint.back().size++;
			} else {
				m_turn_footprint.push_back({b, 1});
			}
		}
		m_turn_left_bytes.resize(bytes.size());
	}
	m_turn_left_at = m_turn_at;
	save(m_turn_left_state);
	uint8_t *to = m_turn_left_bytes.data();
	for (const TurnSpan &s : m_turn_footprint) {
		std::memcpy(to, m_ram.base + s.offset, s.size);
		to += s.size;
	}
}

void Arm7::turn_trail(uint32_t offset, uint32_t value, uint32_t size, bool store)
{
	if (m_turn_trail.size() == TURN_TRAIL) {
		m_turn_trail_full = true;
	} else {
		m_turn_trail.push_back({offset, value, u8(size), store});
	}
}

/* What the turn just recorded took from the memory and what it leaves
 * there, from the trail of its loads and stores: a load counts if the turn
 * had not stored there before, a store by the last value it puts at each
 * byte. The word the turn counts in is left out: it is owed its turns.
 * False if the trail does not tell, the turn is then not kept. */
bool Arm7::turn_keep()
{
	std::unordered_map<uint32_t, uint8_t> stored;
	const auto counts = [this](uint32_t offset) {
		return m_turn_counter != TURN_NO_COUNTER && offset - m_turn_counter < 4;
	};

	m_turn_inputs.clear();
	m_turn_outputs.clear();
	m_turn_footprint.clear();
	m_turn_resumable = true;
	if (m_turn_trail_full) {
		return false;
	}
	for (const TurnAccess &a : m_turn_trail) {
		uint32_t own = 0, counted = 0;

		for (uint32_t i = 0; i < a.size; i++) {
			const uint32_t offset = (a.offset + i) & m_ram.mask;

			counted += counts(offset);
			if (a.store) {
				stored[offset] = u8(a.value >> (8 * i));
			} else {
				own += static_cast<uint32_t>(stored.count(offset));
			}
		}
		if (a.store || counted == a.size) {
			continue;
		}
		if (counted != 0 || (own != 0 && own != a.size)) {
			return false;
		}
		if (own == 0) {
			m_turn_inputs.push_back(a);
		}
	}
	for (const auto &[offset, value] : stored) {
		if (!counts(offset)) {
			m_turn_outputs.push_back({offset, value});
		}
	}
	return true;
}

/* The program is at the start of the turn kept, with the registers the turn
 * started with (see turn_registers_same). With the same instructions and
 * data where the turn reads them, the same answers of the bus and, in the
 * memory, what the turn leaves there, the turn would run as it was recorded
 * and change nothing: it is counted from here. */
bool Arm7::turn_rejoin()
{
	for (const TurnAccess &a : m_turn_inputs) {
		uint32_t value = 0;

		for (uint32_t i = 0; i < a.size; i++) {
			value |= static_cast<uint32_t>(m_ram.base[(a.offset + i) & m_ram.mask]) << (8 * i);
		}
		if (value != a.value) {
			return false;
		}
	}
	for (const TurnByte &b : m_turn_outputs) {
		if (m_ram.base[b.offset] != b.value) {
			return false;
		}
	}
	if (!turn_reads_hold()) {
		return false;
	}
	m_turn = Turn::Replay;
	m_turn_at = 0;
	m_turn_origin = 0;
	return true;
}

/* The program is at the instruction where it left the turn kept. With the
 * same registers and lines, every byte the turn reads or writes as it was
 * there and the same answers of the bus, it goes on as the turn was
 * recorded from there: the turn is counted from there. It is looked at
 * once, the first time the program is there in its mode with the lines as
 * they were: then it is back as it left the turn, or does something else. */
bool Arm7::turn_resume()
{
	if (mode() != (m_turn_left_state.cpsr & 0x1F) || m_irq_line != (m_turn_left_state.irq_line != 0) ||
	    m_fiq_line != (m_turn_left_state.fiq_line != 0)) {
		return false;
	}
	m_turn_left = false;
	m_turn_resumable = false;
	if (!turn_registers_same(m_turn_left_state)) {
		return false;
	}
	const uint8_t *was = m_turn_left_bytes.data();
	for (const TurnSpan &s : m_turn_footprint) {
		if (std::memcmp(m_ram.base + s.offset, was, s.size) != 0) {
			return false;
		}
		was += s.size;
	}
	if (!turn_reads_hold()) {
		return false;
	}
	m_turn_resumable = true;
	m_turn = Turn::Replay;
	m_turn_at = m_turn_origin = m_turn_left_at;
	return true;
}

bool Arm7::turn_page(const std::array<uint8_t, 256> &pages, uint32_t addr) const
{
	uint32_t page = (addr & m_ram.mask) >> WATCH_PAGE;
	return (pages[page >> 3] >> (page & 7)) & 1;
}

/* The instruction itself, which step() then notes with its data. */
void Arm7::turn_note_fetch(uint32_t addr, uint32_t op)
{
	if (addr < m_ram.size) {
		uint32_t page = (addr & m_ram.mask) >> WATCH_PAGE;
		m_turn_watched[page >> 3] |= u8(1u << (page & 7));
		turn_trail(addr & m_ram.mask, op, 4, false);
	} else if (m_turn_num_reads < TURN_READS) {
		m_turn_reads[m_turn_num_reads++] = {m_turn_count, addr, 4, op};
	} else {
		m_turn_bad = true;
	}
}

void Arm7::turn_note_read(uint32_t addr, uint32_t size, uint32_t value)
{
	/* Where in the memory, for turn_counts(); the bus is elsewhere. */
	const uint32_t at = addr < m_ram.size ? addr & m_ram.mask : addr;
	TurnOp &o = m_turn_ops[m_turn_count];
	o.load_lo = o.load_hi ? std::min(o.load_lo, at) : at;
	o.load_hi = std::max(o.load_hi, at + size);
	if (addr < m_ram.size) {
		uint32_t page = (addr & m_ram.mask) >> WATCH_PAGE;
		m_turn_watched[page >> 3] |= u8(1u << (page & 7));
		turn_trail(addr & m_ram.mask, value, size, false);
	} else if (m_turn_num_reads < TURN_READS) {
		m_turn_reads[m_turn_num_reads++] = {m_turn_count, addr, size, value};
	} else {
		m_turn_bad = true;
	}
}

/* Before the store: the bytes it changes are kept as they were. */
void Arm7::turn_note_store(uint32_t addr, uint32_t size, uint32_t value)
{
	if (addr >= m_ram.size) {
		m_turn_bad = true;
		return;
	}
	/* What another writes there, the next turn would write back */
	for (uint32_t at : { addr, addr + size - 1 }) {
		const uint32_t page = (at & m_ram.mask) >> WATCH_PAGE;
		m_turn_watched[page >> 3] |= u8(1u << (page & 7));
	}
	TurnOp &o = m_turn_ops[m_turn_count];
	o.store_lo = o.store_hi ? std::min(o.store_lo, addr & m_ram.mask) : addr & m_ram.mask;
	o.store_hi = std::max(o.store_hi, (addr & m_ram.mask) + size);
	turn_trail(addr & m_ram.mask, value, size, true);
	for (uint32_t i = 0; i < size; i++) {
		uint32_t offset = (addr + i) & m_ram.mask;
		if (m_ram.base[offset] == u8(value >> (8 * i))) {
			continue;
		}
		bool known = false;
		for (uint32_t k = 0; !known && k < m_turn_num_stores; k++) {
			known = m_turn_stores[k].offset == offset;
		}
		if (known) {
			continue;
		}
		if (m_turn_num_stores == TURN_STORES) {
			m_turn_bad = true;
			return;
		}
		m_turn_stores[m_turn_num_stores++] = {offset, m_ram.base[offset]};
		uint32_t page = offset >> WATCH_PAGE;
		m_turn_changed[page >> 3] |= u8(1u << (page & 7));
	}
}

/* The machine reads the memory: a turn that is only counted has not put
 * there what it changes for a while. */
void Arm7::ram_read(uint32_t addr, uint32_t size)
{
	if (m_turn != Turn::Replay || size == 0) {
		return;
	}
	if (turn_page(m_turn_changed, addr) || turn_page(m_turn_changed, addr + size - 1)) {
		settle();
	}
}

void Arm7::ram_write(uint32_t addr, uint32_t size)
{
	if (m_turn == Turn::Record) {
		turn_break();
	}
	if (m_turn != Turn::Replay || size == 0) {
		return;
	}
	if (turn_page(m_turn_watched, addr) || turn_page(m_turn_watched, addr + size - 1)) {
		settle();
	}
}

void Arm7::set_ram(const Arm7Ram &ram)
{
	if (ram.base != m_ram.base || ram.size != m_ram.size || ram.mask != m_ram.mask) {
		turn_break();
		m_turn_kept = false;
		m_ram = ram;
		drop_blocks();
	}
}

void Arm7::set_irq(bool level)
{
	if (level != m_irq_line) {
		turn_break();
		m_irq_line = level;
	}
}

void Arm7::set_fiq(bool level)
{
	if (level != m_fiq_line) {
		turn_break();
		m_fiq_line = level;
	}
}

void Arm7::set_trace(FILE *out)
{
	turn_break();
	m_turn_kept = false;
	m_trace = out;
}

/* FIQ beats IRQ; both are level sensitive and sampled between two
 * instructions, which is where the pipeline lets them in. */
uint32_t Arm7::poll_interrupts()
{
	m_cycles = 0;
	if (m_fiq_line && (m_cpsr & FLAG_F) == 0) {
		take_fiq();
	} else if (m_irq_line && (m_cpsr & FLAG_I) == 0) {
		take_irq();
	}
	return m_cycles;
}

uint32_t Arm7::step()
{
	m_cycles = poll_interrupts();
	m_op_pc = m_pc;
	uint32_t op = fetch();
	m_pc += 4;
	const bool ran = cond(op >> 28);
	if (m_turn == Turn::Record) {
		m_turn_ops[m_turn_count] = {op, 0, 0, 0, 0, ran};
	}
	if (ran) {
		m_instructions++;
		exec(op);
	} else {
		/* A failed condition still costs the fetch: 1S. */
		m_cycles += 1;
	}
	return m_cycles;
}

uint32_t Arm7::step_decoded()
{
	m_cycles = poll_interrupts();
	m_op_pc = m_pc;

	const uint32_t addr = m_pc & ~3u;
	const uint32_t at = addr & m_ram.mask;
	uint32_t op;
	Run does;

	if (in_decoded_memory(addr)) {
		Decoded *page = m_decoded[at >> DECODED_PAGE].get();

		if (!page) {
			page = decoded_page(at >> DECODED_PAGE);
		}
		op = le32(m_ram.base + at);
		does = refreshed(page[word_in_page(at)], op);
	} else {
		op = m_bus.read32(addr);
		does = decode(op);
	}
	m_pc += 4;
	if ((COND_PASS[op >> 28] >> (m_cpsr >> 28)) & 1) {
		m_instructions++;
		does(*this, op);
	} else {
		m_cycles += 1;
	}
	return m_cycles;
}

uint16_t Arm7::condition_pass(uint32_t cc)
{
	return COND_PASS[cc & 15];
}

Arm7::Decoded *Arm7::decoded_page(uint32_t page)
{
	m_decoded[page] = std::make_unique<Decoded[]>(DECODED_WORDS);
	return m_decoded[page].get();
}

namespace {

/* The slot of a banked register in a saved state: the map of bank_slot(),
 * an illegal mode taking the user set without a note */
uint32_t *state_slot(Arm7State &s, uint32_t mode, uint32_t idx)
{
	if (idx < 13) {
		return mode == Arm7::MODE_FIQ ? &s.fiq[idx - 8] : &s.usr[idx - 8];
	}
	switch (mode) {
	case Arm7::MODE_FIQ: return &s.fiq[idx - 8];
	case Arm7::MODE_IRQ: return &s.irq[idx - 13];
	case Arm7::MODE_SVC: return &s.svc[idx - 13];
	case Arm7::MODE_ABT: return &s.abt[idx - 13];
	case Arm7::MODE_UND: return &s.und[idx - 13];
	default: return &s.usr[idx - 8];
	}
}

} /* namespace */

void Arm7::save(Arm7State &out) const
{
	for (uint32_t i = 0; i < 8; i++) {
		out.r[i] = m_r[i];
	}
	out.pc = m_pc;
	out.cpsr = m_cpsr;
	for (uint32_t i = 0; i < 7; i++) {
		out.usr[i] = m_usr[i];
		out.fiq[i] = m_fiq[i];
	}
	for (uint32_t i = 0; i < 2; i++) {
		out.irq[i] = m_irq[i];
		out.svc[i] = m_svc[i];
		out.abt[i] = m_abt[i];
		out.und[i] = m_und[i];
	}
	for (uint32_t i = 0; i < 5; i++) {
		out.spsr[i] = m_spsr[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		*state_slot(out, mode(), i) = m_r[i];
	}
	out.irq_line = m_irq_line;
	out.fiq_line = m_fiq_line;
}

void Arm7::load(const Arm7State &in)
{
	Arm7State s = in;
	for (uint32_t i = 0; i < 8; i++) {
		m_r[i] = s.r[i];
	}
	m_pc = s.pc;
	m_cpsr = s.cpsr;
	for (uint32_t i = 0; i < 7; i++) {
		m_usr[i] = s.usr[i];
		m_fiq[i] = s.fiq[i];
	}
	for (uint32_t i = 0; i < 2; i++) {
		m_irq[i] = s.irq[i];
		m_svc[i] = s.svc[i];
		m_abt[i] = s.abt[i];
		m_und[i] = s.und[i];
	}
	for (uint32_t i = 0; i < 5; i++) {
		m_spsr[i] = s.spsr[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		m_r[i] = *state_slot(s, mode(), i);
	}
	m_r[15] = 0;
	m_irq_line = s.irq_line != 0;
	m_fiq_line = s.fiq_line != 0;
	if (m_turn != Turn::Off) {
		m_turn = Turn::Idle;
	}
	m_turn_wait = m_turn_backoff = 0;
	m_turn_limit = TURN_MAX;
	m_turn_kept = false;
}

/* Whether the registers are those of `as`, the turn kept being there: every
 * one, or those of its mode when the turn stays in it. It does not see the
 * others then, which an interrupt taken meanwhile has changed. */
bool Arm7::turn_registers_same(const Arm7State &as) const
{
	Arm7State now, start = as;
	const uint32_t rank = spsr_index(mode());

	save(now);
	if (!m_turn_one_mode) {
		return now == start;
	}
	bool same = now.pc == start.pc && now.cpsr == start.cpsr && now.irq_line == start.irq_line &&
	            now.fiq_line == start.fiq_line && (rank == SPSRS || now.spsr[rank] == start.spsr[rank]);
	for (uint32_t i = 0; i < 8; i++) {
		same = same && now.r[i] == start.r[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		same = same && *state_slot(now, mode(), i) == *state_slot(start, mode(), i);
	}
	return same;
}

/* Bus access. The trace point is one pointer test when off. Word and
 * halfword addresses are aligned here: the ARM7 drops A[1:0] and A[0]
 * on the bus and rotates the loaded data instead (data sheet 4.9.3). */

void Arm7::trace(char rw, uint32_t addr, uint32_t size, uint32_t v) const
{
	std::fputs(std::format("arm7 {:08x} {} {:08x} {} {:0{}x}\n", m_op_pc, rw, addr, size, v, size * 2).c_str(), m_trace);
}

void Arm7::trace_note(const char *what, uint32_t v) const
{
	if (m_trace) {
		std::fputs(std::format("arm7 {:08x} # {} {:08x}\n", m_op_pc, what, v).c_str(), m_trace);
	}
}

uint32_t Arm7::fetch()
{
	uint32_t addr = m_pc & ~3u;
	uint32_t op;
	if (addr < m_ram.size) {
		const uint8_t *p = m_ram.base + (addr & m_ram.mask);
		op = le32(p);
	} else if (m_turn == Turn::Settle) {
		op = turn_settle_read();
	} else {
		op = m_bus.read32(addr);
	}
	if (m_turn == Turn::Record) {
		turn_note_fetch(addr, op);
	}
	if (m_trace) {
		trace('f', addr, 4, op);
	}
	return op;
}

uint8_t Arm7::rd8(uint32_t addr)
{
	uint8_t v;
	if (addr < m_ram.size) {
		v = m_ram.base[addr & m_ram.mask];
	} else if (m_turn == Turn::Settle) {
		v = u8(turn_settle_read());
	} else {
		v = m_bus.read8(addr);
	}
	if (m_turn == Turn::Record) {
		turn_note_read(addr, 1, v);
	}
	if (m_trace) {
		trace('r', addr, 1, v);
	}
	return v;
}

uint16_t Arm7::rd16(uint32_t addr)
{
	addr &= ~1u;
	uint16_t v;
	if (addr < m_ram.size) {
		const uint8_t *p = m_ram.base + (addr & m_ram.mask);
		v = le16(p);
	} else if (m_turn == Turn::Settle) {
		v = u16(turn_settle_read());
	} else {
		v = m_bus.read16(addr);
	}
	if (m_turn == Turn::Record) {
		turn_note_read(addr, 2, v);
	}
	if (m_trace) {
		trace('r', addr, 2, v);
	}
	return v;
}

uint32_t Arm7::rd32(uint32_t addr)
{
	addr &= ~3u;
	uint32_t v;
	if (addr < m_ram.size) {
		const uint8_t *p = m_ram.base + (addr & m_ram.mask);
		v = le32(p);
	} else if (m_turn == Turn::Settle) {
		v = turn_settle_read();
	} else {
		v = m_bus.read32(addr);
	}
	if (m_turn == Turn::Record) {
		turn_note_read(addr, 4, v);
	}
	if (m_trace) {
		trace('r', addr, 4, v);
	}
	return v;
}

void Arm7::wr8(uint32_t addr, uint8_t v)
{
	if (m_trace) {
		trace('w', addr, 1, v);
	}
	if (m_turn == Turn::Record) {
		turn_note_store(addr, 1, v);
	}
	if (addr < m_ram.size) {
		m_ram.base[addr & m_ram.mask] = v;
	} else {
		m_bus.write8(addr, v);
	}
}

void Arm7::wr16(uint32_t addr, uint16_t v)
{
	addr &= ~1u;
	if (m_trace) {
		trace('w', addr, 2, v);
	}
	if (m_turn == Turn::Record) {
		turn_note_store(addr, 2, v);
	}
	if (addr < m_ram.size) {
		put_le16(m_ram.base + (addr & m_ram.mask), v);
	} else {
		m_bus.write16(addr, v);
	}
}

void Arm7::wr32(uint32_t addr, uint32_t v)
{
	addr &= ~3u;
	if (m_trace) {
		trace('w', addr, 4, v);
	}
	if (m_turn == Turn::Record) {
		turn_note_store(addr, 4, v);
	}
	if (addr < m_ram.size) {
		put_le32(m_ram.base + (addr & m_ram.mask), v);
	} else {
		m_bus.write32(addr, v);
	}
}

/* Register banks. r0-r7 and r15 are shared; FIQ has its own r8-r14,
 * the other exception modes their own r13-r14. The active set lives in
 * m_r and is swapped on every mode change. */

uint32_t *Arm7::bank_slot(uint32_t mode_bits, uint32_t idx)
{
	if (idx < 13) {
		return mode_bits == MODE_FIQ ? &m_fiq[idx - 8] : &m_usr[idx - 8];
	}
	switch (mode_bits) {
	case MODE_FIQ: return &m_fiq[idx - 8];
	case MODE_IRQ: return &m_irq[idx - 13];
	case MODE_SVC: return &m_svc[idx - 13];
	case MODE_ABT: return &m_abt[idx - 13];
	case MODE_UND: return &m_und[idx - 13];
	case MODE_USR:
	case MODE_SYS:
		return &m_usr[idx - 8];
	default:
		/* An illegal mode has no bank; the user set stands in. */
		trace_note("illegal mode", mode_bits);
		return &m_usr[idx - 8];
	}
}

uint32_t Arm7::spsr_index(uint32_t mode_bits)
{
	switch (mode_bits) {
	case MODE_FIQ: return 0;
	case MODE_IRQ: return 1;
	case MODE_SVC: return 2;
	case MODE_ABT: return 3;
	case MODE_UND: return 4;
	default: return SPSRS;
	}
}

uint32_t *Arm7::spsr_slot(uint32_t mode_bits)
{
	const uint32_t index = spsr_index(mode_bits);

	return index < SPSRS ? &m_spsr[index] : nullptr;
}

/* User and System have no SPSR; reading it gives the CPSR. */
uint32_t Arm7::spsr() const
{
	const uint32_t index = spsr_index(mode());

	return index < SPSRS ? m_spsr[index] : m_cpsr;
}

void Arm7::switch_mode(uint32_t new_mode)
{
	uint32_t old_mode = mode();
	if (old_mode == new_mode) {
		return;
	}
	for (uint32_t i = 8; i < 15; i++) {
		*bank_slot(old_mode, i) = m_r[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		m_r[i] = *bank_slot(new_mode, i);
	}
	m_cpsr = (m_cpsr & ~0x1Fu) | new_mode;
}

/* Whole CPSR write: SPSR restore or MSR with the control field. The T
 * bit is refused because Thumb is not implemented. */
void Arm7::set_cpsr_full(uint32_t v)
{
	if (v & FLAG_T) {
		trace_note("thumb state refused", v);
		v &= ~FLAG_T;
	}
	switch_mode(v & 0x1F);
	m_cpsr = v;
}

/* User bank view for LDM/STM with the S bit. */

uint32_t Arm7::get_user_r(uint32_t idx)
{
	if (idx < 8 || idx == 15) {
		return get_r(idx);
	}
	uint32_t m = mode();
	if (m == MODE_USR || m == MODE_SYS) {
		return m_r[idx];
	}
	if (m == MODE_FIQ || idx >= 13) {
		return m_usr[idx - 8];
	}
	return m_r[idx];
}

void Arm7::set_user_r(uint32_t idx, uint32_t v)
{
	if (idx == 15) {
		set_pc(v);
		return;
	}
	uint32_t m = mode();
	if (idx < 8 || m == MODE_USR || m == MODE_SYS) {
		m_r[idx] = v;
	} else if (m == MODE_FIQ || idx >= 13) {
		m_usr[idx - 8] = v;
	} else {
		m_r[idx] = v;
	}
}

void Arm7::set_reg(uint32_t idx, uint32_t v)
{
	if ((idx & 15) == 15) {
		set_pc(v);
	} else {
		m_r[idx & 15] = v;
	}
}

/* ARM state only: the two low bits of a new PC are dropped. */
void Arm7::set_pc(uint32_t v)
{
	m_pc = v & ~3u;
}

/* EQ NE CS CC MI PL VS VC HI LS GE LT GT LE AL NV. NV never executes on
 * an ARMv4 core (ARM ARM A3.2.1). */
bool Arm7::cond(uint32_t cc) const
{
	return (COND_PASS[cc & 15] >> (m_cpsr >> 28)) & 1;
}

/* Exceptions (data sheet 3.8). Entry: SPSR_new = CPSR, mode switch,
 * r14_new = return link, I set (F too for FIQ and reset), PC = vector.
 * The link values assume the handler returns with SUBS pc, lr, #4 for
 * IRQ and FIQ and MOVS pc, lr for SWI and undefined. Entry costs 2S + 1N. */

void Arm7::take_exception(uint32_t vector, uint32_t new_mode, uint32_t lr, bool mask_fiq)
{
	uint32_t old_cpsr = m_cpsr;
	if (m_turn == Turn::Record) {
		m_turn_bad = true;
	}
	switch_mode(new_mode);
	*spsr_slot(new_mode) = old_cpsr;
	m_r[14] = lr;
	m_cpsr |= FLAG_I;
	if (mask_fiq) {
		m_cpsr |= FLAG_F;
	}
	m_cpsr &= ~FLAG_T;
	m_pc = vector;
	m_cycles += 3;
}

void Arm7::take_fiq()
{
	take_exception(0x1C, MODE_FIQ, m_pc + 4, true);
}

void Arm7::take_irq()
{
	take_exception(0x18, MODE_IRQ, m_pc + 4, false);
}

void Arm7::take_swi()
{
	take_exception(0x08, MODE_SVC, m_pc, false);
}

void Arm7::take_undefined()
{
	take_exception(0x04, MODE_UND, m_pc, false);
}

/* Barrel shifter (data sheet 4.5.2). Returns the second operand and the
 * shifter carry out, which is the old C when nothing moves. With a
 * register shift amount r15 reads 12 bytes ahead instead of 8. */

uint32_t Arm7::shifter(uint32_t op, bool want_carry, uint32_t &carry_out)
{
	carry_out = (m_cpsr & FLAG_C) ? 1 : 0;
	if (op & (1u << 25)) {
		uint32_t rot = ((op >> 8) & 0xF) * 2;
		uint32_t v = ror(op & 0xFF, rot);
		if (want_carry && rot != 0) {
			carry_out = v >> 31;
		}
		return v;
	}
	uint32_t type = (op >> 5) & 3;
	bool by_register = (op & 0x10) != 0;
	uint32_t rm = op & 0xF;
	uint32_t v = get_r(rm);
	uint32_t amount;
	if (by_register) {
		if (rm == 15) {
			v += 4;
		}
		amount = get_r((op >> 8) & 0xF) & 0xFF;
	} else {
		amount = (op >> 7) & 0x1F;
	}
	return shift_by(type, v, amount, by_register, carry_out);
}

/* LSL, LSR, ASR, ROR. An immediate amount of 0 means LSL #0 (no shift),
 * LSR #32, ASR #32 and RRX respectively; a register amount of 0 leaves
 * the value and the carry alone. `carry_out` holds the old C on entry. */
uint32_t Arm7::shift_by(uint32_t type, uint32_t v, uint32_t amount, bool by_register, uint32_t &carry_out)
{
	if (by_register && amount == 0) {
		return v;
	}
	switch (type) {
	case 0:
		if (amount == 0) {
			return v;
		}
		if (amount < 32) {
			carry_out = bit(v, 32 - amount);
			return v << amount;
		}
		carry_out = amount == 32 ? (v & 1) : 0;
		return 0;
	case 1:
		if (amount == 0) {
			amount = 32;
		}
		if (amount < 32) {
			carry_out = bit(v, amount - 1);
			return v >> amount;
		}
		carry_out = amount == 32 ? (v >> 31) : 0;
		return 0;
	case 2:
		if (amount == 0 || amount > 32) {
			amount = 32;
		}
		if (amount < 32) {
			carry_out = bit(v, amount - 1);
			return static_cast<uint32_t>(static_cast<int32_t>(v) >> amount);
		}
		carry_out = v >> 31;
		return (v & 0x80000000u) ? 0xFFFFFFFFu : 0;
	default:
		if (amount == 0) {
			uint32_t old_c = carry_out;
			carry_out = v & 1;
			return (v >> 1) | (old_c << 31);
		}
		amount &= 31;
		if (amount == 0) {
			carry_out = v >> 31;
			return v;
		}
		carry_out = bit(v, amount - 1);
		return ror(v, amount);
	}
}

/* Flags. Logical results set N and Z and take C from the shifter;
 * arithmetic results set all four. */

void Arm7::set_nz(uint32_t res)
{
	m_cpsr = (m_cpsr & ~(FLAG_N | FLAG_Z)) | (res & FLAG_N) | (res == 0 ? FLAG_Z : 0);
}

uint32_t Arm7::add_flags(uint32_t a, uint32_t b, uint32_t carry_in)
{
	uint64_t full = static_cast<uint64_t>(a) + b + carry_in;
	uint32_t res = static_cast<uint32_t>(full);
	set_nz(res);
	set_flag(FLAG_C, (full >> 32) != 0);
	set_flag(FLAG_V, (((a ^ res) & (b ^ res)) >> 31) != 0);
	return res;
}

/* a - b - !carry_in, as a + ~b + carry_in: C is "no borrow". */
uint32_t Arm7::sub_flags(uint32_t a, uint32_t b, uint32_t carry_in)
{
	uint64_t full = static_cast<uint64_t>(a) + static_cast<uint32_t>(~b) + carry_in;
	uint32_t res = static_cast<uint32_t>(full);
	set_nz(res);
	set_flag(FLAG_C, (full >> 32) != 0);
	set_flag(FLAG_V, (((a ^ b) & (a ^ res)) >> 31) != 0);
	return res;
}

} /* namespace odin */
