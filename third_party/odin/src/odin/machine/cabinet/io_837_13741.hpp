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

#ifndef ODIN_IO_837_13741_HPP
#define ODIN_IO_837_13741_HPP

#include "odin/machine/chips/d71054.hpp"
#include "odin/machine/core/io_line.hpp"
#include "odin/machine/cabinet/jvs.hpp"
#include "odin/machine/chips/mie.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* The 837-13741, the I/O board of NASCAR, running its program (epr-21868,
 * the name its JVS TEST page shows in the manuals) on the JVS line from
 * UART 1 of the MIE: a 315-6146 like the MIE (Mie), its
 * EPROM below 8000, 32 KB of SRAM above, the D71054 at ports A0-A3. As
 * the program reads and drives them (found by running it):
 * port 0 bits 6 and 7 TEST and TILT, 4 and 5 the coin switches, 2 and 3
 * the return pins of the coin meters (wired: 0), 0 and 1 DIP switches
 * (off: 1); port 1 the first eight switches of player 1 in JVS order,
 * port 6 bits 0-3 its next four; port 2 and port 6 bits 4-7 those of
 * player 2; closed at 0. Outputs 1-6 on port 3 bits 2-7, 7-14 on port 4,
 * 15-22 on port 5 (the drive board), on at 0. The converter gives the
 * eight analog channels. Port 91: no board after it, DIP switches off
 * (configuration 0, the manual of NASCAR). Port 90 bit 1 tells the MIE
 * that the board has its address.
 *
 * Its time is that of the line: ticks of 1/9.216 GHz from its reset, a
 * clock of its Z80 576 of them. HEURISTIC: its 14.7456 MHz crystal and the
 * one of the UART of the MIE are taken as the same (as on the 837-13551). */
class Io837_13741 : public IoLineBoard, private MieHost {
public:
	/* HEURISTIC: a coin closes the switch of its slot 50 ms, then leaves
	 * it open 50 ms before the next (as on the 837-13551) */
	static constexpr uint64_t COIN_CYCLES = 800000;
	/* A character on the line: 10 bits at 115200 bauds (divisor 8 from
	 * 14.7456 MHz, both programs), in ticks */
	static constexpr uint64_t CHAR_TICKS = 800000;

	/* What it keeps, plain data */
	struct State {
		MieState chip;
		std::array<uint8_t, Mie::RAM_SIZE> ram;
		D71054::State timer;
		std::array<IoLineEvent, IoLineWays::MOST> to_chip, to_mie;
		uint32_t to_chip_count, to_mie_count;
		uint64_t cycles;
		uint8_t addressed;
		uint8_t coin_phase[2];	/* IoLineCoins */
		uint64_t coin_until[2];
		uint32_t coins[2];
	};

	/* `eprom`: the 64 KB of epr-21868 */
	static constexpr uint32_t EPROM_SIZE = 0x10000;	/* epr-21868 */

	Io837_13741(std::span<const uint8_t> eprom, JvsInputs &inputs);

	/* IoLineBoard */
	void reset() override;
	void inc_coin(uint8_t slot) override { m_coins.waiting[slot & 1]++; }
	void run_until(uint64_t tick) override;
	uint64_t now() const override { return m_cycles * Mie::TICKS_PER_CYCLE; }
	void set_run_start(uint64_t tick) override { m_run_start = tick; }
	uint64_t next_event() const override { return m_ways.next_event(); }
	void deliver(Mie &mie, uint64_t tick) override { m_ways.deliver(mie, tick); }
	std::span<const uint8_t> outputs() const override;
	/* MieLine: a byte of the MIE */
	void line_sent(uint8_t byte, uint64_t ticks) override;

	void save(State &out) const;
	void load(const State &in, Mie &mie);
	void set_log(FILE *out) { m_chip.set_log(out); }
	Mie &chip() { return m_chip; }

private:
	/* UART 1 of its chip, toward the MIE */
	class ChipLine : public MieLine {
	public:
		explicit ChipLine(Io837_13741 &board) : m_board(board) {}
		void line_sent(uint8_t byte, uint64_t ticks) override { m_board.chip_sent(byte, ticks); }

	private:
		Io837_13741 &m_board;
	};

	/* MieHost */
	uint8_t gpio_read(uint8_t port) override;
	void gpio_write(uint8_t, uint8_t) override {}
	uint8_t analog_read(unsigned n) override { return m_analog[n & 7]; }
	bool board_out8(uint8_t port, uint8_t value) override;
	bool sense_pins(uint8_t &value) override;
	void jvs_control_written(uint8_t value) override;

	void read_pins();
	void run_chip(uint64_t cycles);
	uint64_t chip_now() const { return m_chip_start + m_chip.run_ticks(); }
	void chip_sent(uint8_t byte, uint64_t ticks);

	JvsInputs &m_inputs;
	std::array<uint8_t, Mie::RAM_SIZE> m_ram{};
	Mie m_chip;
	ChipLine m_chip_line;
	D71054 m_timer;
	IoLineWays m_ways;
	uint64_t m_cycles = 0;
	uint64_t m_chip_start = 0;	/* the tick the current run of the chip started at */
	uint64_t m_run_start = 0;
	bool m_addressed = false;
	std::array<uint8_t, 8> m_pins{};
	uint8_t m_analog[8] = {};
	mutable std::array<uint8_t, 4> m_outputs{};	/* outputs(): from the latches */
	IoLineCoins m_coins;
};

} /* namespace odin */

#endif /* ODIN_IO_837_13741_HPP */
