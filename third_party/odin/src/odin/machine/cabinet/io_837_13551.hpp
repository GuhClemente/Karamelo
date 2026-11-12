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

#ifndef ODIN_IO_837_13551_HPP
#define ODIN_IO_837_13551_HPP

#include "odin/machine/core/io_line.hpp"
#include "odin/machine/cabinet/jvs.hpp"
#include "odin/machine/chips/mie.hpp"
#include "odin/machine/chips/tmp90ph44.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* A pin of the TMP90PH44 and what drives it: the bit `bit` of the system
 * byte (word 0) or of the word of player 1 or 2 (word 1, 2), JVS order. */
struct Io837_13551Pin {
	uint8_t port, mask;
	uint8_t word, bit;
};

/* The 837-13551 itself: the TMP90PH44 running 315-6215 on the JVS line
 * from UART 1 of the MIE. Its switches are those the tables
 * of the program name, closed at 0 (PINS); a coin closes the switch of
 * its slot; the return pins of the coin meters read 1, the meters wired;
 * AN0-AN3 are channels 1-4 while P56 is 1, 5-8 while it is 0 (127E,
 * 0A46); P6 bits 1-3 drive outputs 1-3 and P7 bits 1-3 outputs 4-6, on at
 * 0 (12BA); P3 bit 4 at 0 drives the line, P3 bits 0-1 read 3 (no board
 * after it), P3 bit 2 at 0 tells the MIE the board has its address
 * (1450, 1471).
 *
 * Its time is that of the line: ticks of 1/9.216 GHz from the reset, a
 * state of the chip 1250 of them. HEURISTIC: its 14.7456 MHz crystal and
 * the one of the UART of the MIE are taken as the same. */
class Io837_13551 : public Tmp90ph44Board, public IoLineBoard {
public:
	static constexpr uint64_t TICKS_PER_STATE = 1250;
	/* HEURISTIC: a coin closes the switch of its slot 50 ms, then leaves
	 * it open 50 ms before the next; the program counts any closing of 3
	 * to 1000 of its ticks, 13 ms to 4.4 s (06F9). */
	static constexpr uint32_t COIN_STATES = 368640;
	/* The bits of a player word, JVS order (jvs.hpp) */
	enum : uint8_t {
		START = 15, SERVICE = 14, UP = 13, DOWN = 12, LEFT = 11, RIGHT = 10,
		PUSH1 = 9, PUSH2 = 8, PUSH3 = 7, PUSH4 = 6, PUSH5 = 5, PUSH6 = 4, PUSH7 = 3
	};
	/* Player 1 on P2 and P1, player 2 on P0 and P4, TEST and TILT on P1
	 * and P4: the tables 307C-3177, the bits of the reply from 12F8 (the
	 * system byte bit 7 FF3C, bit 6 FF3D) */
	static constexpr std::array<Io837_13551Pin, 28> PINS = { {
		{ 2, 0x01, 1, START }, { 2, 0x02, 1, RIGHT }, { 2, 0x04, 1, LEFT }, { 2, 0x08, 1, UP },
		{ 2, 0x10, 1, DOWN }, { 2, 0x20, 1, PUSH1 }, { 2, 0x40, 1, PUSH2 }, { 2, 0x80, 1, PUSH3 },
		{ 1, 0x01, 1, PUSH4 }, { 1, 0x02, 1, PUSH5 }, { 1, 0x04, 1, PUSH6 }, { 1, 0x08, 1, PUSH7 },
		{ 1, 0x10, 1, SERVICE }, { 1, 0x20, 0, 7 },
		{ 0, 0x01, 2, START }, { 0, 0x02, 2, RIGHT }, { 0, 0x04, 2, LEFT }, { 0, 0x08, 2, UP },
		{ 0, 0x10, 2, DOWN }, { 0, 0x20, 2, PUSH1 }, { 0, 0x40, 2, PUSH2 }, { 0, 0x80, 2, PUSH3 },
		{ 4, 0x01, 2, PUSH4 }, { 4, 0x02, 2, PUSH5 }, { 4, 0x04, 2, PUSH6 }, { 4, 0x08, 2, PUSH7 },
		{ 4, 0x10, 2, SERVICE }, { 4, 0x20, 0, 6 },
	} };
	/* 3178 and 318B: the coin switches of slots 1 and 2 */
	static constexpr uint8_t COIN_PORT[2] = { 1, 4 }, COIN_MASK = 0x40;
	/* 319E and 31B3: the return pins of the coin meters */
	static constexpr uint8_t METER_MASK = 0x80;

	/* What it keeps, plain data: the chip, the bytes on their way to it,
	 * what it has for the MIE, its pins, its coins. */
	struct State {
		Tmp90ph44State chip;
		std::array<IoLineEvent, IoLineWays::MOST> to_chip, to_mie;
		uint32_t to_chip_count, to_mie_count;
		std::array<uint8_t, 8> latch, driven;
		uint8_t addressed;	/* as the chip drives P3 bit 2 */
		uint8_t coin_phase[2];	/* IoLineCoins */
		uint64_t coin_until[2];
		uint32_t coins[2];	/* waiting */
	};

	Io837_13551(std::span<const uint8_t> prom, JvsInputs &inputs);

	/* IoLineBoard */
	void reset() override;
	void inc_coin(uint8_t slot) override { m_coins.waiting[slot & 1]++; }
	void run_until(uint64_t tick) override;
	uint64_t now() const override { return m_chip.states() * TICKS_PER_STATE; }
	void set_run_start(uint64_t tick) override { m_run_start = tick; }
	uint64_t next_event() const override { return m_ways.next_event(); }
	void deliver(Mie &mie, uint64_t tick) override { m_ways.deliver(mie, tick); }
	std::span<const uint8_t> outputs() const override { return m_outputs; }
	Tmp90ph44 &chip() { return m_chip; }

	void save(State &out) const;
	void load(const State &in, Mie &mie);

	/* MieLine */
	void line_sent(uint8_t byte, uint64_t ticks) override;

	/* Tmp90ph44Board */
	uint8_t bus_read(uint16_t) override { return 0xFF; }
	void bus_write(uint16_t, uint8_t) override {}
	uint8_t port_pins(unsigned port) override { return m_pins[port & 7]; }
	void port_out(unsigned port, uint8_t latch, uint8_t driven) override;
	uint8_t analog_pin(unsigned n) override;
	void serial_sent(uint8_t byte) override;

private:
	void read_pins();

	JvsInputs &m_inputs;
	Tmp90ph44 m_chip;
	IoLineWays m_ways;
	std::array<uint8_t, 8> m_latch{}, m_driven{}, m_pins{};
	std::array<uint8_t, 4> m_outputs{};
	uint8_t m_analog[8] = {};
	bool m_addressed = false;
	IoLineCoins m_coins;
	uint64_t m_run_start = 0;
};

} /* namespace odin */

#endif /* ODIN_IO_837_13551_HPP */
