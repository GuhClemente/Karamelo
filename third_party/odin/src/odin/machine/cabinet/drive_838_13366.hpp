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

#ifndef ODIN_DRIVE_838_13366_HPP
#define ODIN_DRIVE_838_13366_HPP

#include "odin/machine/cabinet/drive_board.hpp"
#include "odin/machine/cpu/z80/z80.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* The drive board of NASCAR's cabinet, the 838-13366 running its program
 * (epr-20512, the board and program of Sega Rally 2): a Z80
 * at 8 MHz, its program below E000, 8 KB of RAM above, all its work in the
 * NMI that channel 1 of its CTC gives every 16000 clocks (500 Hz). Its
 * ports, as its program uses them: 10-13 the CTC; 20, 21 its DIP switches
 * (read); 24-27 a serial converter, written to start a channel then read
 * 8 times, bit 0 the next bit, the highest first: 25 the wheel, 00 left
 * to FF right; 28 the command of the game (read), 29 its reply; 2C eight
 * inputs, all 0 while the motor board is well; each NMI 2E then 2A, a
 * register of the motor board and its value: FE the torque, 80 none, 80
 * + n to the left, 80 - n to the right, FF the motor at rest, bit 3 of the
 * register at 0 a reset of the motor board; 2D its lamps; 1C-1F, 20-23
 * (its 7-segment digits), F0, F1 (its watchdog) written only. */
class Drive838_13366 : private Z80Bus {
public:
	static constexpr uint32_t CLOCK_HZ = 8000000;
	static constexpr size_t ROM_SIZE = 0x10000;
	static constexpr uint16_t RAM_BASE = 0xE000;
	static constexpr size_t RAM_SIZE = 0x2000;
	/* Its DIP switches, read 1 while off: DIP1 bits 7-6 off for its own
	 * program (on: its self-test, its demo), bits 4-2 the strength of the
	 * spring of the motor board when the game asks for it (B0, B1); DIP2
	 * what its digits show. All off, as the manual of NASCAR asks: "Set all
	 * the DIP switches (837-13844 and 838-13366) to OFF". */
	static constexpr uint8_t DIP1 = 0xFF, DIP2 = 0xFF;
	/* The torque register of the motor board; the most it is told each
	 * way is DriveBoard::MOST */
	static constexpr uint8_t TORQUE = 0xFE;

	/* What it keeps, plain data */
	struct State {
		Z80State cpu;
		std::array<uint8_t, RAM_SIZE> ram;
		uint64_t clocks;		/* since its reset */
		uint64_t next_nmi;		/* when channel 1 of the CTC next ends, 0: never */
		uint8_t ctc_control[2], ctc_constant[2];
		uint8_t ctc_loading;		/* bit n: channel n waits for its constant */
		uint8_t ctc_running;		/* bit n: channel n counts */
		uint8_t command, reply;
		uint8_t adc_value, adc_bits;
		uint8_t motor_register, motor_value, torque_value;
		uint8_t lamps;

		bool operator==(const State &) const = default;
	};

	/* `rom`: the 64 KB of epr-20512 */
	explicit Drive838_13366(std::span<const uint8_t> rom);

	void reset();
	/* Runs it up to that clock since its reset, the game giving it that
	 * command and the wheel standing there (00 left, 80 centre, FF right) */
	void run_until(uint64_t clock, uint8_t command, uint8_t wheel);
	uint64_t clocks() const { return m_state.clocks; }

	/* Its reply to the game, as it left it */
	uint8_t reply() const { return m_state.reply; }
	/* The torque it tells the motor board, -DriveBoard::MOST (all to the
	 * left) to DriveBoard::MOST (all to the right); 0 with the motor at
	 * rest */
	int torque() const;

	void save(State &out) const;
	void load(const State &in);

	/* For the tests: off, every turn of its main loop runs, none is
	 * counted without being run. The states must be the same. */
	void set_waits_counted(bool on);
	/* For the tests: the clocks counted so since it was made */
	uint64_t waits_counted() const { return m_wait_cycles; }

private:
	/* Z80Bus */
	uint8_t read8(uint16_t addr) override;
	void write8(uint16_t addr, uint8_t value) override;
	uint8_t in8(uint16_t port) override;
	void out8(uint16_t port, uint8_t value) override;

	void write_ctc(unsigned channel, uint8_t value);
	/* The clocks between two NMI as the CTC is set; 0: none come */
	uint64_t nmi_period() const;

	/* One instruction, or the turns that wait before `to` and one */
	void step(uint64_t to);
	void wait_mark();
	void wait_check();
	void wait_record();
	void bill(uint64_t to);

	std::array<uint8_t, ROM_SIZE> m_rom;
	State m_state;
	Z80 m_cpu;
	uint8_t m_wheel = 0x80;

	/* Between two NMI its program goes round its main loop (067A),
	 * reading only its RAM. A turn waits when it comes back where it
	 * started with the CPU as it was (R aside), without a port used, R
	 * read or the NMI taken, every byte it wrote back as it was (the
	 * returns of its calls on the stack): every further turn is the same
	 * until the NMI. */
	static constexpr unsigned MAX_TURN = 64;	/* instructions */
	static constexpr unsigned MAX_WRITTEN = 8;	/* bytes of RAM */
	/* The search: the CPU and the clocks at the mark, the instructions
	 * since, the bytes written with their value at the mark; `again`, a
	 * turn has come back and the next one is recorded to be proven */
	struct Wait {
		Z80State cpu;
		uint64_t clock;
		unsigned steps;
		bool touched;
		bool again;
		uint32_t refresh;
		unsigned written;
		std::array<uint16_t, MAX_WRITTEN> addr;
		std::array<uint8_t, MAX_WRITTEN> was;
	};
	/* The turn proven, before its instruction n: the CPU, the clocks and
	 * the M1 cycles from its start, the bytes it writes; the CPU before
	 * instruction `at` */
	struct Proof {
		bool on;
		unsigned length, at;
		uint64_t period;
		uint32_t refresh;
		std::array<uint64_t, MAX_TURN> offset;
		std::array<uint32_t, MAX_TURN> m1;
		std::array<Z80State, MAX_TURN> cpu;
		std::array<std::array<uint8_t, MAX_WRITTEN>, MAX_TURN> ram;
	};
	Wait m_wait{};
	Proof m_proof{};
	bool m_waits_counted = true;
	uint64_t m_wait_cycles = 0;
};

} /* namespace odin */

#endif /* ODIN_DRIVE_838_13366_HPP */
