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

#include "odin/machine/chips/eeprom_93c46.hpp"

#include <algorithm>

namespace odin {

namespace {

/* Opcodes, the two bits after the start bit. The 00 group is told apart
 * by the top two address bits. */
enum : uint8_t {
	OP_EXTENDED = 0,
	OP_WRITE = 1,
	OP_READ = 2,
	OP_ERASE = 3
};

/* Write cycle time. Data sheets give 5 to 10 ms maximum; 4 ms is a
 * typical figure and keeps the READY/BUSY poll observable. */
constexpr uint32_t WRITE_CYCLE_US = 4000;

} /* namespace */

Eeprom93c46::Eeprom93c46(uint32_t clock_hz)
	: m_write_cycles(static_cast<uint32_t>((static_cast<uint64_t>(clock_hz) * WRITE_CYCLE_US) / 1000000u))
{
	m_data.fill(0xFF);
	reset();
}

/* Power-up state: write disabled, no command in progress. The contents
 * survive a reset. */
void Eeprom93c46::reset()
{
	m_busy_cycles = 0;
	m_state = State::IDLE;
	m_cs = m_sk = m_di = m_org = false;
	m_write_enabled = false;
	m_bits = 0;
	m_shift = 0;
	m_opcode = 0;
	m_address = 0;
	m_out_shift = 0;
	m_do = true;
}

void Eeprom93c46::load(std::span<const uint8_t> in)
{
	const size_t n = std::min(in.size(), static_cast<size_t>(SIZE));
	std::copy_n(in.begin(), n, m_data.begin());
}

void Eeprom93c46::save(Eeprom93c46State &out) const
{
	out.data = m_data;
	out.busy_cycles = m_busy_cycles;
	out.out_shift = m_out_shift;
	out.shift = m_shift;
	out.address = m_address;
	out.state = static_cast<uint8_t>(m_state);
	out.cs = m_cs;
	out.sk = m_sk;
	out.di = m_di;
	out.org = m_org;
	out.write_enabled = m_write_enabled;
	out.bits = m_bits;
	out.opcode = m_opcode;
	out.dout = m_do;
}

void Eeprom93c46::load(const Eeprom93c46State &in)
{
	m_data = in.data;
	m_busy_cycles = in.busy_cycles;
	m_out_shift = in.out_shift;
	m_shift = in.shift;
	m_address = in.address;
	m_state = static_cast<State>(in.state);
	m_cs = in.cs != 0;
	m_sk = in.sk != 0;
	m_di = in.di != 0;
	m_org = in.org != 0;
	m_write_enabled = in.write_enabled != 0;
	m_bits = in.bits;
	m_opcode = in.opcode;
	m_do = in.dout != 0;
}

void Eeprom93c46::set_pins(bool cs, bool sk, bool di, bool org)
{
	m_di = di;
	if (!m_cs && cs) {
		on_cs_rise();
	} else if (m_cs && !cs) {
		on_cs_fall();
	}
	m_cs = cs;
	/* ORG is sampled while the chip is idle. */
	if (m_state == State::IDLE || m_state == State::START) {
		m_org = org;
	}
	if (!m_sk && sk && m_cs) {
		on_sk_rise();
	}
	m_sk = sk;
}

/* DO is high impedance (pulled up) outside a read. With CS high and no
 * command in progress it reports READY/BUSY after a write. */
bool Eeprom93c46::do_read() const
{
	if (m_state == State::READING) {
		return m_do;
	}
	if (m_state == State::START) {
		return m_busy_cycles == 0;
	}
	return true;
}

void Eeprom93c46::tick(uint32_t cycles)
{
	if (m_busy_cycles > cycles) {
		m_busy_cycles -= cycles;
	} else {
		m_busy_cycles = 0;
	}
}

void Eeprom93c46::on_cs_rise()
{
	m_state = State::START;
	m_bits = 0;
	m_shift = 0;
}

void Eeprom93c46::on_cs_fall()
{
	/* Programming starts when CS drops after the instruction. */
	if (m_state == State::DONE) {
		start_write_cycle();
	}
	m_state = State::IDLE;
	m_do = true;
}

void Eeprom93c46::on_sk_rise()
{
	switch (m_state) {
	case State::START:
		/* The start bit is a one on DI; clocks before it are ignored.
		 * A busy chip ignores instructions. */
		if (m_di && m_busy_cycles == 0) {
			m_state = State::COMMAND;
			m_bits = 0;
			m_shift = 0;
		}
		break;
	case State::COMMAND:
		m_shift = static_cast<uint16_t>((m_shift << 1) | (m_di ? 1 : 0));
		m_bits++;
		if (m_bits == 2 + address_bits()) {
			execute();
		}
		break;
	case State::READING:
		/* Data bits follow the dummy zero, most significant first; a
		 * sequential read continues into the next cell. */
		if ((m_bits % data_bits()) == 0) {
			const uint16_t index = static_cast<uint16_t>(m_address + m_bits / data_bits());
			m_out_shift = static_cast<uint32_t>(cell_read(index)) << (32 - data_bits());
		} else {
			m_out_shift = (m_out_shift << 1) | 1u;
		}
		m_do = (m_out_shift & 0x80000000u) != 0;
		m_bits++;
		break;
	case State::WRITING:
		m_shift = static_cast<uint16_t>((m_shift << 1) | (m_di ? 1 : 0));
		m_bits++;
		if (m_bits == data_bits()) {
			execute_write();
		}
		break;
	case State::IDLE:
	case State::DONE:
		break;
	}
}

/* Opcode and address are in; decide what the instruction does. */
void Eeprom93c46::execute()
{
	const uint16_t addr_mask = static_cast<uint16_t>((1u << address_bits()) - 1);
	m_opcode = static_cast<uint8_t>((m_shift >> address_bits()) & 3);
	m_address = static_cast<uint16_t>(m_shift & addr_mask);
	m_bits = 0;
	m_shift = 0;

	switch (m_opcode) {
	case OP_READ:
		m_out_shift = 0;
		m_do = false;
		m_state = State::READING;
		break;
	case OP_WRITE:
		m_state = State::WRITING;
		break;
	case OP_ERASE:
		if (m_write_enabled) {
			cell_write(m_address, 0xFFFF);
			m_state = State::DONE;
		} else {
			m_state = State::IDLE;
		}
		break;
	case OP_EXTENDED:
	default:
		/* Top two address bits: 11 EWEN, 00 EWDS, 10 ERAL, 01 WRAL. */
		switch (m_address >> (address_bits() - 2)) {
		case 3:
			m_write_enabled = true;
			m_state = State::IDLE;
			break;
		case 0:
			m_write_enabled = false;
			m_state = State::IDLE;
			break;
		case 2:
			if (m_write_enabled) {
				m_data.fill(0xFF);
				m_state = State::DONE;
			} else {
				m_state = State::IDLE;
			}
			break;
		case 1:
		default:
			m_state = State::WRITING;
			break;
		}
		break;
	}
}

/* Data bits are in: WRITE one cell or WRAL all of them. The cycle itself
 * starts when CS drops. */
void Eeprom93c46::execute_write()
{
	if (m_write_enabled) {
		if (m_opcode == OP_WRITE) {
			cell_write(m_address, m_shift);
		} else {
			const uint16_t cells = static_cast<uint16_t>(SIZE / (data_bits() / 8));
			for (uint16_t i = 0; i < cells; i++) {
				cell_write(i, m_shift);
			}
		}
		m_state = State::DONE;
	} else {
		m_state = State::IDLE;
	}
}

uint16_t Eeprom93c46::cell_read(uint16_t index) const
{
	if (m_org) {
		const uint32_t byte = (index & 0x3F) * 2u;
		return static_cast<uint16_t>((m_data[byte] << 8) | m_data[byte + 1]);
	}
	return m_data[index & 0x7F];
}

void Eeprom93c46::cell_write(uint16_t index, uint16_t value)
{
	if (m_org) {
		const uint32_t byte = (index & 0x3F) * 2u;
		m_data[byte] = static_cast<uint8_t>(value >> 8);
		m_data[byte + 1] = static_cast<uint8_t>(value);
	} else {
		m_data[index & 0x7F] = static_cast<uint8_t>(value);
	}
}

void Eeprom93c46::start_write_cycle()
{
	m_busy_cycles = m_write_cycles;
}

} /* namespace odin */
