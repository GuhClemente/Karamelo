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

#include "odin/machine/chips/tmp90ph44.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "odin/common/stop_message.hpp"

namespace odin {

namespace {

/* Registers, from FFC0 (MAME tmp90844_regs) */
enum : uint8_t {
	P0 = 0x00, P1 = 0x02, P2 = 0x04, P3 = 0x06, P3CR = 0x07, P4 = 0x08, P5 = 0x0A, P6 = 0x0B, P7 = 0x0C, P67CR = 0x0D,
	WDMOD = 0x12, WDCR = 0x13, TREG0 = 0x14, T01MOD = 0x18, T23MOD = 0x19, TRUN = 0x1C,
	SCMOD = 0x26, SCCR = 0x27, SCBUF = 0x28, BRGCR = 0x29, IRFL = 0x2A, IRFH = 0x2B,
	ADMOD = 0x2F, ADREG0 = 0x30, INTEL = 0x34, INTEH = 0x35
};

constexpr uint16_t RAM_BASE = 0xFDC0;
constexpr uint16_t SFR_BASE = 0xFFC0;

[[noreturn]] void
unknown(const char *what, unsigned value)
{
	stop_fatal("TMP90PH44: %s %02X, which is not known", what, value);
}

} /* namespace */

Tmp90ph44::Tmp90ph44(std::span<const uint8_t> rom, Tmp90ph44Board &board)
	: m_cpu(*this), m_board(board)
{
	std::memcpy(m_rom.data(), rom.data(), std::min(rom.size(), m_rom.size()));
	m_cpu.set_fixed(ROM_SIZE);
	reset();
}

/* After the data book (TMP90C840, reset): the timers and the prescaler
 * stop, the watchdog runs with WDTE set and WDTP 00, every port is an
 * input, its latch at 0. */
void
Tmp90ph44::reset()
{
	m_sfr.fill(0);
	m_sfr[WDMOD] = 0x80;
	/* HEURISTIC: the latch of P3 at 1, as that of the TMP90C840 (3.5.4),
	 * the only one the data book gives; there P3 has no direction register.
	 * The program sets P3CR before P3 (00A4): at 0, P3 bit 2 would tell the
	 * MIE the board has its address for an instruction. */
	m_sfr[P3] = 0xFF;
	m_prescaler = 0;
	m_timer.fill(0);
	m_watchdog = 0;
	m_requests = 0;
	m_rx_full = false;
	m_rx_errors = 0;
	m_tx_left = -1;
	m_synced = m_states;
	m_next_event = next_event();
	m_proof.on = false;
	m_cpu.reset();
}

uint32_t
Tmp90ph44::run(uint32_t states)
{
	const uint64_t end = m_states + states;
	uint32_t done = 0;

	if (m_proof.on)
		done += static_cast<uint32_t>(resume(end));
	while (done < states) {
		const uint16_t from = m_cpu.pc();
		const uint32_t s = m_cpu.step();

		m_states += s;
		done += s;
		if (m_states >= m_next_event) {
			sync();
			spoil(Spoil::AGAIN);
		}
		if (m_trial.on)
			done += static_cast<uint32_t>(turn(end));
		else if (m_proof.on)
			m_proof.at = m_proof.at + 1 == m_proof.length ? 0 : m_proof.at + 1;
		else if (m_waits_counted && m_cpu.pc() <= from)
			jumped_back();
	}
	m_trial.on = false;
	sync();
	return done;
}

/* A place jumped back to (or stayed at: a jump to itself, HALT) a second
 * time in a while starts a turn on trial, unless a turn from there was
 * found not idle a short time ago. */
void
Tmp90ph44::jumped_back()
{
	const uint16_t pc = m_cpu.pc();
	const bool again = std::find(m_back.begin(), m_back.end(), pc) != m_back.end();

	std::copy_backward(m_back.begin(), m_back.end() - 1, m_back.end());
	m_back[0] = pc;
	if (!again)
		return;
	const Cool &cool = m_cool[pc & 15];
	if (cool.pc == pc && m_states < cool.until)
		return;
	m_trial.on = true;
	m_trial.spoil = Spoil::NONE;
	m_trial.pc = pc;
	m_trial.steps = 0;
	m_trial.start = m_states;
	m_proof.read = 0;
	m_cpu.save(m_proof.cpu[0]);
}

/* After each instruction of a turn on trial. Back at its start in the same
 * state, it is a fixed point: nothing it reads can change before the next
 * event of a device, so it would go round the same way until then. The
 * states billed. */
uint64_t
Tmp90ph44::turn(uint64_t end)
{
	if (m_trial.spoil == Spoil::NONE && ++m_trial.steps > MAX_TURN)
		m_trial.spoil = Spoil::NEVER;
	if (m_trial.spoil == Spoil::NONE && m_cpu.pc() != m_trial.pc) {
		if (m_trial.steps < MAX_TURN) {
			m_proof.offset[m_trial.steps] = static_cast<uint32_t>(m_states - m_trial.start);
			m_cpu.save(m_proof.cpu[m_trial.steps]);
		}
		return 0;
	}
	m_trial.on = false;

	Tlcs90State now;
	m_cpu.save(now);
	if (m_trial.spoil == Spoil::NONE && !(now == m_proof.cpu[0]))
		m_trial.spoil = Spoil::NEVER;
	if (m_trial.spoil == Spoil::NEVER)
		m_cool[m_trial.pc & 15] = { m_trial.pc, m_states + COOLDOWN };
	if (m_trial.spoil != Spoil::NONE)
		return 0;
	m_proof.on = true;
	m_proof.length = m_trial.steps;
	m_proof.at = 0;
	m_proof.period = m_states - m_trial.start;
	return bill(end);
}

/* A run() that starts on a turn proven idle: it still is if the CPU is
 * where the turn has it and the pins the turn read have not changed. */
uint64_t
Tmp90ph44::resume(uint64_t end)
{
	Tlcs90State now;

	m_cpu.save(now);
	if (!(now == m_proof.cpu[m_proof.at]))
		m_proof.on = false;
	for (unsigned n = 0; m_proof.on && n < m_proof.pins.size(); n++)
		if ((m_proof.read >> n & 1) && pin_now(n) != m_proof.pins[n])
			m_proof.on = false;
	return m_proof.on ? bill(end) : 0;
}

/* The CPU before an instruction of a turn proven idle goes round the same
 * way up to the next event of a device or the end of run(). The
 * instructions it would run before the last of these are billed, the CPU
 * put in the state it would have after them; the one after is run, and
 * reaches it, as one at a time. The states billed. */
uint64_t
Tmp90ph44::bill(uint64_t end)
{
	const uint64_t limit = std::min(m_next_event, end);

	if (m_states >= limit)
		return 0;

	const uint64_t from = m_proof.offset[m_proof.at];
	const uint64_t last = from + (limit - m_states - 1);
	const uint64_t into = last % m_proof.period;
	unsigned to = m_proof.length - 1;

	while (m_proof.offset[to] > into)
		to--;

	const uint64_t billed = last - into + m_proof.offset[to] - from;

	if (billed) {
		m_cpu.load(m_proof.cpu[to]);
		m_proof.at = to;
		m_states += billed;
		m_skipped += billed;
	}
	return billed;
}

/* Pin n as the board has it: a turn on trial keeps what it read. */
uint8_t
Tmp90ph44::pin(unsigned n)
{
	const uint8_t value = pin_now(n);

	if (m_trial.on) {
		m_proof.read = static_cast<uint16_t>(m_proof.read | 1u << n);
		m_proof.pins[n] = value;
	}
	return value;
}

void
Tmp90ph44::save(Tmp90ph44State &out) const
{
	m_cpu.save(out.cpu);
	out.ram = m_ram;
	out.sfr = m_sfr;
	out.timer = m_timer;
	out.states = m_states;
	out.prescaler = m_prescaler;
	out.watchdog = m_watchdog;
	out.requests = m_requests;
	out.tx_left = m_tx_left;
	out.tx = m_tx;
	out.rx = m_rx;
	out.rx_full = m_rx_full;
	out.rx_errors = m_rx_errors;
}

/* run() leaves the devices up to date: they are so at the time loaded. */
void
Tmp90ph44::load(const Tmp90ph44State &in)
{
	m_cpu.load(in.cpu);
	m_ram = in.ram;
	m_sfr = in.sfr;
	m_timer = in.timer;
	m_states = in.states;
	m_synced = in.states;
	m_prescaler = in.prescaler;
	m_watchdog = in.watchdog;
	m_requests = in.requests;
	m_tx_left = in.tx_left;
	m_tx = in.tx;
	m_rx = in.rx;
	m_rx_full = in.rx_full != 0;
	m_rx_errors = in.rx_errors;
	m_trial.on = false;
	m_proof.on = false;
	m_back.fill(0);
	m_cool.fill({});
	m_next_event = next_event();
}

/* After the data book (TMP90C840, 3.8.2): the byte goes from the shift
 * register into SCBUF with INTRX, unless SCBUF has not been read since the
 * last one: then the new byte is lost and OERR set. */
void
Tmp90ph44::serial_received(uint8_t byte)
{
	if (!(m_sfr[SCMOD] & 0x20))
		return;
	m_proof.on = false;
	if (m_rx_full) {
		m_rx_errors |= 0x10;
		return;
	}
	m_rx = byte;
	m_rx_full = true;
	request(VEC_RX);
}

uint8_t
Tmp90ph44::peek(uint16_t addr) const
{
	if (addr < ROM_SIZE)
		return m_rom[addr];
	if (addr >= SFR_BASE)
		return m_sfr[addr - SFR_BASE];
	if (addr >= RAM_BASE)
		return m_ram[addr - RAM_BASE];
	return 0xFF;
}

uint8_t
Tmp90ph44::read8(uint16_t addr)
{
	if (addr < ROM_SIZE)
		return m_rom[addr];
	if (addr >= SFR_BASE)
		return sfr_read(static_cast<uint8_t>(addr - SFR_BASE));
	if (addr >= RAM_BASE)
		return m_ram[addr - RAM_BASE];
	spoil(Spoil::NEVER);
	return m_board.bus_read(addr);
}

void
Tmp90ph44::write8(uint16_t addr, uint8_t value)
{
	if (addr < ROM_SIZE)
		return;
	if (addr >= SFR_BASE) {
		spoil(Spoil::NEVER);
		sfr_write(static_cast<uint8_t>(addr - SFR_BASE), value);
	} else if (addr >= RAM_BASE) {
		if (m_ram[addr - RAM_BASE] != value)
			spoil(Spoil::AGAIN);
		m_ram[addr - RAM_BASE] = value;
	} else {
		spoil(Spoil::NEVER);
		m_board.bus_write(addr, value);
	}
}

uint8_t
Tmp90ph44::sfr_read(uint8_t reg)
{
	switch (reg) {
	case P0:
	case P1:
	case P2:
	case P3:
	case P4: {
		/* A bit of the control register after the port set drives the
		 * pin (P3CR 9C, P3 bits 0 and 1 read as inputs at 1450); the
		 * program leaves P0, P1, P2 and P4 inputs, the switches and
		 * coins its tables name (3080-31C0). */
		const uint8_t out = m_sfr[reg + 1];

		return static_cast<uint8_t>((m_sfr[reg] & out) | (pin(reg / 2u) & ~out));
	}
	case P6:
	case P7: {
		/* Four bits each */
		const unsigned port = reg == P6 ? 6u : 7u;
		const uint8_t out = p67_driven(port);

		return static_cast<uint8_t>((m_sfr[reg] & out) | (pin(port) & ~out & 0x0F));
	}
	case P5:
		/* HEURISTIC: the latch. The program reads P5 only to change P56
		 * (1296, 12B0); P50-P53 are its analog inputs. */
		return m_sfr[P5];
	case ADREG0:
	case ADREG0 + 1:
	case ADREG0 + 2:
	case ADREG0 + 3:
		/* HEURISTIC: ADMOD F7 converts AN0 to AN3 over and over into
		 * FFF0-FFF3, and a read gives the pin as it is then. The program
		 * never starts a conversion nor waits for one, and reads the four
		 * at each tick (127E), 4.44 ms apart; a conversion of the
		 * TMP90C840 takes 190 states. */
		if (m_sfr[ADMOD] != 0xF7)
			unknown("A/D mode", m_sfr[ADMOD]);
		return pin(8u + reg - ADREG0);
	case SCCR: {
		/* OERR, bit 4, as serial_received sets it; no parity nor framing
		 * error on the line. The three are cleared when read. */
		const uint8_t value = static_cast<uint8_t>((m_sfr[SCCR] & 0xE3) | m_rx_errors);

		m_rx_errors = 0;
		return value;
	}
	case SCBUF:
		m_rx_full = false;
		return m_rx;
	case IRFH:
		/* The request flags of the vectors 0018 to 0050, bit n for the
		 * vector (n + 3) * 8. Bit 2, 0028, is polled with the interrupts
		 * shut while the delay before a reply runs (05DB, 14AB);
		 * HEURISTIC for the other bits, which the program does not read. */
		return static_cast<uint8_t>(m_requests >> 3);
	default:
		return m_sfr[reg];
	}
}

void
Tmp90ph44::sfr_write(uint8_t reg, uint8_t value)
{
	sync();
	switch (reg) {
	case WDCR:
		if (value == 0x4E)
			m_watchdog = 0;
		else if (value == 0xB1 && !(m_sfr[WDMOD] & 0x80))
			m_sfr[WDMOD] &= 0x7F;
		break;
	case TRUN: {
		const uint8_t stopped = m_sfr[TRUN] & static_cast<uint8_t>(~value);

		/* A timer stopped is cleared; so is the prescaler. */
		for (unsigned n = 0; n < 4; n++)
			if (stopped & (1u << n))
				m_timer[n] = 0;
		if (stopped & 0x20)
			m_prescaler = 0;
		m_sfr[TRUN] = value;
		break;
	}
	case SCBUF:
		if (m_tx_left >= 0)
			unknown("byte written while one is being sent:", value);
		m_tx = value;
		m_tx_left = BYTE_STATES;
		break;
	case IRFL:
		/* The flag of the vector value * 8 (1499, 14C1: 05 for 0028, 07
		 * for 0038) */
		m_requests &= ~(1u << (value & 31));
		break;
	default:
		m_sfr[reg] = value;
		if (reg < P5)
			tell_port(reg / 2u);
		else if (reg <= P7)
			tell_port(reg - P5 + 5u);
		else if (reg == P67CR) {
			tell_port(6);
			tell_port(7);
		}
		break;
	}
	m_next_event = next_event();
}

/* P0 to P4 drive the bits their control register sets; P56 and P57 are
 * outputs (data sheet TMP90PH44, table 2.2); P6 and P7 four bits each, as
 * P67CR says. */
void
Tmp90ph44::tell_port(unsigned port)
{
	if (port < 5)
		m_board.port_out(port, m_sfr[P0 + 2 * port], m_sfr[P0 + 2 * port + 1]);
	else if (port == 5)
		m_board.port_out(5, m_sfr[P5], 0xC0);
	else
		m_board.port_out(port, m_sfr[port == 6 ? P6 : P7] & 0x0F, p67_driven(port));
}

/* The pins of P6 or P7 the chip drives, as P67CR says */
uint8_t
Tmp90ph44::p67_driven(unsigned port) const
{
	return port == 6 ? m_sfr[P67CR] & 0x0F : static_cast<uint8_t>(m_sfr[P67CR] >> 4);
}

/* The states of a tick of timer n: T01MOD and T23MOD as MAME reads them
 * (bits 1-0 timer 0 or 2, bits 3-2 timer 1 or 3: 1 phiT1, 2 phiT16, 3
 * phiT256; bits 7-4 the 8-bit mode, 0). HEURISTIC: the layout of these
 * registers of the TMP90C844. */
uint32_t
Tmp90ph44::timer_period(unsigned n) const
{
	const uint8_t mod = m_sfr[n < 2 ? T01MOD : T23MOD];
	const unsigned clock = (mod >> ((n & 1) * 2)) & 3;

	if (mod & 0xF0)
		unknown("timer mode", mod);
	switch (clock) {
	case 1: return 4;
	case 2: return 64;
	case 3: return 1024;
	default: unknown("timer clock", mod);
	}
}

/* The ticks timer n has left to its match */
unsigned
Tmp90ph44::ticks_to_match(unsigned n) const
{
	return ((m_sfr[TREG0 + n] - m_timer[n] - 1) & 0xFF) + 1u;
}

uint32_t
Tmp90ph44::watchdog_period() const
{
	return 1u << (13 + 2 * ((m_sfr[WDMOD] >> 5) & 3));
}

/* The timers, the watchdog and the serial line brought to now: what they
 * did since the last time, as they would have done it after each
 * instruction. Nothing of theirs is seen by the CPU between their events
 * (the requests IRFH reads change only then), so the chip brings them up
 * to date only at an event, before a write to a register, and at the end
 * of run(). */
void
Tmp90ph44::sync()
{
	const uint64_t states = m_states - m_synced;

	m_synced = m_states;
	if (!states)
		return;

	/* The timers: one tick per period of their clock, counted from the
	 * start of the prescaler; cleared at their match */
	if ((m_sfr[TRUN] & 0x20) && (m_sfr[TRUN] & 0x0F)) {
		for (unsigned n = 0; n < 4; n++) {
			if (!(m_sfr[TRUN] & (1u << n)))
				continue;
			const uint32_t period = timer_period(n);
			uint64_t ticks = (m_prescaler + states) / period - m_prescaler / period;
			const unsigned left = ticks_to_match(n);

			if (ticks < left) {
				m_timer[n] = static_cast<uint8_t>(m_timer[n] + ticks);
				continue;
			}
			ticks -= left;
			m_timer[n] = static_cast<uint8_t>(ticks % (m_sfr[TREG0 + n] ? m_sfr[TREG0 + n] : 256u));
			/* HEURISTIC: timers 0 and 2, whose vectors the program
			 * leaves empty, ask for nothing. */
			if (n == 1)
				request(VEC_T1);
			else if (n == 3)
				request(VEC_T3);
		}
	}
	if (m_sfr[TRUN] & 0x20)
		m_prescaler += states;

	/* The watchdog: INTWD each 2^(14 + 2 WDTP) periods of fc */
	if (m_sfr[WDMOD] & 0x80) {
		m_watchdog += static_cast<uint32_t>(states);
		if (m_watchdog >= watchdog_period()) {
			m_watchdog -= watchdog_period();
			request(VEC_WD);
		}
	}

	/* The byte being sent */
	if (m_tx_left >= 0) {
		m_tx_left -= static_cast<int32_t>(states);
		if (m_tx_left < 0) {
			if (m_sfr[BRGCR] != 0x02 || (m_sfr[SCMOD] & 0x0F) != 0x09)
				unknown("serial mode", static_cast<unsigned>(m_sfr[BRGCR] << 4 | (m_sfr[SCMOD] & 0x0F)));
			m_board.serial_sent(m_tx);
			request(VEC_TX);
		}
	}
	m_next_event = next_event();
}

/* The first time a device asks for an interrupt: the overflow of the
 * watchdog, the match of timer 1 or 3, the end of the byte sent */
uint64_t
Tmp90ph44::next_event() const
{
	uint64_t next = UINT64_MAX;

	if (m_sfr[WDMOD] & 0x80)
		next = m_synced + (m_watchdog < watchdog_period() ? watchdog_period() - m_watchdog : 0);
	if (m_tx_left >= 0)
		next = std::min(next, m_synced + static_cast<uint64_t>(m_tx_left) + 1);
	if (m_sfr[TRUN] & 0x20)
		for (unsigned n : { 1u, 3u })
			if (m_sfr[TRUN] & (1u << n)) {
				const uint32_t period = timer_period(n);

				next = std::min(next, m_synced + (m_prescaler / period + ticks_to_match(n)) * period - m_prescaler);
			}
	return next;
}

/* In the order of their vectors, the watchdog first. HEURISTIC: INTWD
 * waits for the interrupts to be open, as the others do; on the TMP90C840
 * it does not (data book, table 3.3 (1)). The program does all its work
 * on a frame inside INTRX (0276: the commands, the poll of the delay at
 * 05DB, the start of the reply at 13BB) and shuts the interrupts only at
 * its start (0079), while the handler of INTWD opens them (019A). Taken
 * inside INTRX, INTWD would let the delay be taken and its flag cleared,
 * the poll then waiting for ever, and bytes be lost or sent twice
 * (measured with the program of the 315-6215: the board dead from its 9th
 * frame on, or 93 frames of 572 wrong with the flag kept; none wrong this
 * way).
 * HEURISTIC: INTEH bit 2 enables 0028 and bit 0 0038, as the program sets
 * and clears them (147C, 1499, 14B9, 14E3); the serial ones are taken as
 * enabled by the bits the program sets at its start and never clears
 * (INTEL 0C, INTEH 48), which of them for which not being known. */
uint16_t
Tmp90ph44::interrupt(bool maskable)
{
	if (!maskable || !m_requests)
		return 0;

	const uint8_t inteh = m_sfr[INTEH];
	const bool serial = (inteh & 0x48) == 0x48 && (m_sfr[INTEL] & 0x0C) == 0x0C;
	uint16_t vector = 0;

	if (requested(VEC_WD))
		vector = VEC_WD;
	else if (requested(VEC_T1) && (inteh & 0x04))
		vector = VEC_T1;
	else if (requested(VEC_T3) && (inteh & 0x01))
		vector = VEC_T3;
	else if (requested(VEC_RX) && serial)
		vector = VEC_RX;
	else if (requested(VEC_TX) && serial)
		vector = VEC_TX;
	if (vector) {
		m_requests &= ~(1u << (vector >> 3));
		spoil(Spoil::AGAIN);
	}
	return vector;
}

} /* namespace odin */
