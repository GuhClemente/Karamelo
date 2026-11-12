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

#include "odin/machine/cabinet/drive_838_13366.hpp"

#include <algorithm>

namespace odin {

Drive838_13366::Drive838_13366(std::span<const uint8_t> rom) : m_rom{}, m_state{}, m_cpu(*this)
{
	std::fill(m_rom.begin(), m_rom.end(), 0xFF);
	std::copy_n(rom.begin(), std::min(rom.size(), m_rom.size()), m_rom.begin());
	reset();
}

void
Drive838_13366::reset()
{
	m_state = State{};
	m_state.reply = m_state.command = 0xFF;
	m_state.motor_register = m_state.motor_value = 0xFF;
	m_state.torque_value = 0x80;
	m_state.lamps = 0xFF;
	m_cpu.reset();
	m_proof.on = false;
	wait_mark();
}

void
Drive838_13366::set_waits_counted(bool on)
{
	m_waits_counted = on;
	m_proof.on = false;
	wait_mark();
}

/* The CTC of the board as its program sets it (01F7): channel 0 a timer
 * (prescaler 16, constant FA), channel 1 counting its ends (constant 4),
 * the end of channel 1 on the NMI; 16000 clocks */
uint64_t
Drive838_13366::nmi_period() const
{
	const State &s = m_state;

	if ((s.ctc_running & 3) != 3 || (s.ctc_control[0] & 0x40) || !(s.ctc_control[1] & 0x40))
		return 0;

	const uint64_t prescaler = (s.ctc_control[0] & 0x20) ? 256 : 16;
	const uint64_t first = s.ctc_constant[0] ? s.ctc_constant[0] : 256;
	const uint64_t second = s.ctc_constant[1] ? s.ctc_constant[1] : 256;

	return prescaler * first * second;
}

/* A control word (bit 0), or the constant it said would follow (bit 2);
 * a reset (bit 1) stops the channel until then */
void
Drive838_13366::write_ctc(unsigned channel, uint8_t value)
{
	State &s = m_state;
	const uint8_t bit = static_cast<uint8_t>(1u << channel);

	if (s.ctc_loading & bit) {
		s.ctc_constant[channel] = value;
		s.ctc_loading = static_cast<uint8_t>(s.ctc_loading & ~bit);
		s.ctc_running = static_cast<uint8_t>(s.ctc_running | bit);
	} else if (value & 1) {
		s.ctc_control[channel] = value;
		if (value & 2)
			s.ctc_running = static_cast<uint8_t>(s.ctc_running & ~bit);
		if (value & 4)
			s.ctc_loading = static_cast<uint8_t>(s.ctc_loading | bit);
	} else {
		return;	/* the vector of the interrupts: none here */
	}

	const uint64_t period = nmi_period();

	s.next_nmi = period ? s.clocks + period : 0;
}

void
Drive838_13366::run_until(uint64_t clock, uint8_t command, uint8_t wheel)
{
	State &s = m_state;

	s.command = command;
	m_wheel = wheel;
	/* The end of channel 1 looked at after each instruction: the program
	 * may set the CTC on the way */
	while (s.clocks < clock) {
		step(s.next_nmi ? std::min(clock, s.next_nmi) : clock);
		if (s.next_nmi && s.clocks >= s.next_nmi) {
			m_cpu.set_nmi();
			s.next_nmi += nmi_period();
			m_proof.on = false;
			m_wait.touched = true;
		}
	}
}

/* Whole instructions until `to` is reached, as Z80::run does. On a turn
 * proven to wait, the instructions that start before the one that reaches
 * `to` are counted without being run: the CPU put as it is before that
 * one, R advanced by their M1 cycles, the bytes the turn writes as they
 * are there; that one is run. */
void
Drive838_13366::step(uint64_t to)
{
	State &s = m_state;

	if (m_waits_counted && m_proof.on) {
		bill(to);
		s.clocks += m_cpu.step();
		m_proof.at = m_proof.at + 1 == m_proof.length ? 0 : m_proof.at + 1;
		return;
	}
	if (m_waits_counted && m_wait.again && m_wait.steps < MAX_TURN)
		wait_record();
	s.clocks += m_cpu.step();
	if (!m_waits_counted)
		return;
	m_wait.steps++;
	if (m_cpu.pc() == m_wait.cpu.pc)
		wait_check();
	else if (m_wait.steps > MAX_TURN)
		wait_mark();
}

void
Drive838_13366::wait_mark()
{
	m_cpu.save(m_wait.cpu);
	m_wait.clock = m_state.clocks;
	m_wait.steps = 0;
	m_wait.touched = false;
	m_wait.again = false;
	m_wait.written = 0;
	m_cpu.forget_refresh_read();
}

/* Before instruction n of the turn being proven */
void
Drive838_13366::wait_record()
{
	const unsigned n = m_wait.steps;
	Z80State &cpu = m_proof.cpu[n];

	m_cpu.save(cpu);
	m_proof.offset[n] = m_state.clocks - m_wait.clock;
	m_proof.m1[n] = n ? m_proof.m1[n - 1] + ((cpu.r - m_proof.cpu[n - 1].r) & 0x7Fu) : 0;
	for (unsigned i = 0; i < m_wait.written; i++)
		m_proof.ram[n][i] = m_state.ram[m_wait.addr[i] - RAM_BASE];
}

/* Back at the mark: a turn that waits is recorded once more, then
 * proven if that turn is the same */
void
Drive838_13366::wait_check()
{
	const State &s = m_state;
	Z80State now;

	m_cpu.save(now);

	const uint8_t r = now.r;

	now.r = static_cast<uint8_t>((r & 0x80) | (m_wait.cpu.r & 0x7F));

	bool waits = !m_wait.touched && !m_cpu.refresh_read() && m_wait.steps <= MAX_TURN && now == m_wait.cpu;

	for (unsigned i = 0; waits && i < m_wait.written; i++)
		waits = s.ram[m_wait.addr[i] - RAM_BASE] == m_wait.was[i];
	if (!waits) {
		wait_mark();
		return;
	}

	const uint64_t period = s.clocks - m_wait.clock;
	const uint32_t refresh = (r - m_wait.cpu.r) & 0x7Fu;

	if (!m_wait.again) {
		m_wait.again = true;
		m_proof.length = m_wait.steps;
		m_proof.period = period;
		m_wait.refresh = refresh;
		m_wait.cpu.r = r;
		m_wait.clock = s.clocks;
		m_wait.steps = 0;
		return;
	}

	const unsigned last = m_wait.steps - 1;
	const uint32_t m1 = m_proof.m1[last] + ((r - m_proof.cpu[last].r) & 0x7Fu);

	if (m_wait.steps != m_proof.length || period != m_proof.period || (m1 & 0x7F) != m_wait.refresh) {
		wait_mark();
		return;
	}
	m_proof.on = true;
	m_proof.at = 0;
	m_proof.refresh = m1;
	m_wait.again = false;
}

/* To the last start of an instruction of the turn before `to` */
void
Drive838_13366::bill(uint64_t to)
{
	State &s = m_state;
	Proof &p = m_proof;
	const uint64_t from = p.offset[p.at];
	const uint64_t last = from + (to - s.clocks - 1);
	const uint64_t into = last % p.period;
	unsigned n = p.length - 1;

	while (p.offset[n] > into)
		n--;

	const uint64_t billed = last - into + p.offset[n] - from;

	if (!billed)
		return;

	const uint64_t m1 = (last - into) / p.period * p.refresh + p.m1[n] - p.m1[p.at];
	Z80State land = p.cpu[n];
	const uint8_t r = m_cpu.r();

	land.r = static_cast<uint8_t>((r & 0x80) | ((r + m1) & 0x7F));
	m_cpu.load(land);
	for (unsigned i = 0; i < m_wait.written; i++)
		s.ram[m_wait.addr[i] - RAM_BASE] = p.ram[n][i];
	s.clocks += billed;
	p.at = n;
	m_wait_cycles += billed;
}

int
Drive838_13366::torque() const
{
	const int most = DriveBoard::MOST;

	return std::clamp(0x80 - static_cast<int>(m_state.torque_value), -most, most);
}

uint8_t
Drive838_13366::read8(uint16_t addr)
{
	return addr >= RAM_BASE ? m_state.ram[addr - RAM_BASE] : m_rom[addr];
}

/* While a turn is looked at, the bytes it writes and their value at
 * its mark (a turn proven writes the same ones) */
void
Drive838_13366::write8(uint16_t addr, uint8_t value)
{
	if (addr < RAM_BASE)
		return;

	uint8_t &byte = m_state.ram[addr - RAM_BASE];

	if (!m_proof.on && !m_wait.touched) {
		unsigned i = 0;

		while (i < m_wait.written && m_wait.addr[i] != addr)
			i++;
		if (i == m_wait.written) {
			if (m_wait.again || i == MAX_WRITTEN) {
				m_wait.touched = true;
			} else {
				m_wait.addr[i] = addr;
				m_wait.was[i] = byte;
				m_wait.written++;
			}
		}
	}
	byte = value;
}

uint8_t
Drive838_13366::in8(uint16_t port)
{
	State &s = m_state;

	m_proof.on = false;
	m_wait.touched = true;
	switch (port & 0xFF) {
	case 0x20:
		return DIP1;
	case 0x21:
		return DIP2;
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
		if (!s.adc_bits)
			return 0;
		s.adc_bits--;
		return (s.adc_value >> s.adc_bits) & 1;
	case 0x28:
		return s.command;
	case 0x2C:
		return 0x00;	/* the motor board well */
	default:
		return 0xFF;
	}
}

void
Drive838_13366::out8(uint16_t port, uint8_t value)
{
	State &s = m_state;

	m_proof.on = false;
	m_wait.touched = true;
	switch (port & 0xFF) {
	case 0x10:
	case 0x11:
		write_ctc(port & 1, value);
		return;
	/* The converter samples the channel written to: the wheel; the
	 * second axis (the seat of a deluxe cabinet) at its centre; nothing on
	 * the other two */
	case 0x24:
	case 0x27:
		s.adc_value = 0x00;
		s.adc_bits = 8;
		return;
	case 0x25:
		s.adc_value = m_wheel;
		s.adc_bits = 8;
		return;
	case 0x26:
		s.adc_value = 0x80;
		s.adc_bits = 8;
		return;
	case 0x29:
		s.reply = value;
		return;
	case 0x2A:
		s.motor_value = value;
		if (s.motor_register == TORQUE)
			s.torque_value = value;
		else if (s.motor_register == 0xFF || !(s.motor_register & 0x08))
			s.torque_value = 0x80;	/* at rest, or reset */
		return;
	case 0x2D:
		s.lamps = value;
		return;
	case 0x2E:
		s.motor_register = value;
		return;
	default:
		return;	/* the other ports of the CTC, the digits, the watchdog */
	}
}

void
Drive838_13366::save(State &out) const
{
	out = m_state;
	m_cpu.save(out.cpu);
}

void
Drive838_13366::load(const State &in)
{
	m_state = in;
	m_cpu.load(in.cpu);
	m_proof.on = false;
	wait_mark();
}

} /* namespace odin */
