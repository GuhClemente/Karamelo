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

#include "odin/machine/cabinet/io_837_13551.hpp"

#include <algorithm>

namespace odin {

namespace {

/* From the start bit of a byte of the MIE to its INTRX in the chip */
constexpr uint64_t RX_TICKS = Tmp90ph44::RX_STATES * Io837_13551::TICKS_PER_STATE;

} /* namespace */

Io837_13551::Io837_13551(std::span<const uint8_t> prom, JvsInputs &inputs)
	: m_inputs(inputs), m_chip(prom, *this)
{
	reset();
}

void
Io837_13551::reset()
{
	m_ways.to_chip_count = m_ways.to_mie_count = 0;
	m_latch.fill(0);
	m_driven.fill(0);
	m_outputs.fill(0);
	m_addressed = false;
	m_coins = {};
	m_chip.reset();
	read_pins();
}

/* The switches as the cabinet has them, the coins as they go through,
 * the analog channels, for the run to come: the chip reads them as fixed
 * while it runs (Tmp90ph44Board). */
void
Io837_13551::read_pins()
{
	const uint16_t words[3] = { m_inputs.system(), m_inputs.player(0), m_inputs.player(1) };

	m_pins.fill(0xFF);
	m_pins[6] = m_pins[7] = 0x0F;
	for (const Io837_13551Pin &pin : PINS)
		if (words[pin.word] & (1u << pin.bit))
			m_pins[pin.port] = static_cast<uint8_t>(m_pins[pin.port] & ~pin.mask);

	for (unsigned slot = 0; slot < 2; slot++)
		if (m_coins.closed(slot, m_chip.states(), COIN_STATES))
			m_pins[COIN_PORT[slot]] = static_cast<uint8_t>(m_pins[COIN_PORT[slot]] & ~COIN_MASK);
	for (uint8_t channel = 0; channel < 8; channel++)
		m_analog[channel] = static_cast<uint8_t>(m_inputs.analog(channel) >> 8);
}

void
Io837_13551::run_until(uint64_t tick)
{
	read_pins();
	while (now() < tick) {
		if (m_ways.next_byte() <= now()) {
			m_chip.serial_received(m_ways.take_byte());
			continue;
		}
		const uint64_t to = std::min(tick, m_ways.next_byte());

		m_chip.run(static_cast<uint32_t>((to - now() + TICKS_PER_STATE - 1) / TICKS_PER_STATE));
	}
}

/* The chip runs at most 64 us ahead of the MIE: a byte of the MIE is due
 * to it RX_TICKS (73.8 us) after it starts, later than where the chip is,
 * past those 64 us by an instruction at most. */
void
Io837_13551::line_sent(uint8_t byte, uint64_t ticks)
{
	m_ways.byte_to_chip("837-13551", m_run_start + ticks + RX_TICKS, now(), byte);
}

void
Io837_13551::port_out(unsigned port, uint8_t latch, uint8_t driven)
{
	m_latch[port] = latch;
	m_driven[port] = driven;
	if (port == 3) {
		const bool addressed = (driven & 0x04) && !(latch & 0x04);

		if (addressed != m_addressed) {
			m_addressed = addressed;
			m_ways.for_mie("837-13551", now(), IoLineEvent::SENSE, addressed);
		}
	} else if (port == 6 || port == 7) {
		uint8_t on = 0;

		for (unsigned n = 0; n < 6; n++) {
			const unsigned p = n < 3 ? 6 : 7;
			const uint8_t bit = static_cast<uint8_t>(2u << (n % 3));

			if ((m_driven[p] & bit) && !(m_latch[p] & bit))
				on = static_cast<uint8_t>(on | 0x80 >> n);
		}
		m_outputs[0] = on;
	}
}

uint8_t
Io837_13551::analog_pin(unsigned n)
{
	return m_analog[(m_latch[5] & 0x40) ? n : n + 4];
}

/* A byte reaches the MIE when the board drives the line */
void
Io837_13551::serial_sent(uint8_t byte)
{
	if ((m_driven[3] & 0x10) && !(m_latch[3] & 0x10))
		m_ways.for_mie("837-13551", now(), IoLineEvent::BYTE, byte);
}

void
Io837_13551::save(State &out) const
{
	m_chip.save(out.chip);
	out.to_chip = m_ways.to_chip;
	out.to_mie = m_ways.to_mie;
	out.to_chip_count = m_ways.to_chip_count;
	out.to_mie_count = m_ways.to_mie_count;
	out.latch = m_latch;
	out.driven = m_driven;
	out.addressed = m_addressed;
	for (unsigned slot = 0; slot < 2; slot++) {
		out.coin_phase[slot] = m_coins.phase[slot];
		out.coin_until[slot] = m_coins.until[slot];
		out.coins[slot] = m_coins.waiting[slot];
	}
}

/* The MIE told of the sense line as it had it: what the board has not
 * given it yet is in to_mie. */
void
Io837_13551::load(const State &in, Mie &mie)
{
	m_chip.load(in.chip);
	m_ways.to_chip = in.to_chip;
	m_ways.to_mie = in.to_mie;
	m_ways.to_chip_count = in.to_chip_count;
	m_ways.to_mie_count = in.to_mie_count;
	m_latch = in.latch;
	m_driven = in.driven;
	m_addressed = in.addressed != 0;
	for (unsigned slot = 0; slot < 2; slot++) {
		m_coins.phase[slot] = in.coin_phase[slot];
		m_coins.until[slot] = in.coin_until[slot];
		m_coins.waiting[slot] = in.coins[slot];
	}
	mie.set_line_addressed(m_ways.told(m_addressed));
	port_out(6, m_latch[6], m_driven[6]);
	read_pins();
}

} /* namespace odin */
