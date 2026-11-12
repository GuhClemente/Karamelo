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

#ifndef ODIN_IO_LINE_HPP
#define ODIN_IO_LINE_HPP

#include "odin/machine/chips/mie.hpp"
#include "odin/common/stop_message.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* What goes on the line, to the chip of the board or to the MIE: a byte,
 * or the sense line that tells the MIE the board has its address; at its
 * tick */
struct IoLineEvent {
	enum : uint8_t { BYTE, SENSE };

	uint64_t tick;
	uint8_t kind;
	uint8_t value;
};

/* The two ways of the line as a board keeps them: the bytes of the MIE on
 * their way to its chip, and what it has for the MIE, each in the order of
 * their ticks. More than the line holds stops Odin. */
struct IoLineWays {
	static constexpr size_t MOST = 16;

	std::array<IoLineEvent, MOST> to_chip{}, to_mie{};
	uint32_t to_chip_count = 0, to_mie_count = 0;

	/* A byte of the MIE due to the chip at `due`, the chip being at `now` */
	void byte_to_chip(const char *board, uint64_t due, uint64_t now, uint8_t byte)
	{
		if (due < now)
			stop_fatal("%s: a byte of the MIE due where the chip has been: the chip too far ahead", board);
		if (to_chip_count == to_chip.size())
			stop_fatal("%s: more bytes from the MIE than the line holds", board);
		to_chip[to_chip_count++] = { due, IoLineEvent::BYTE, byte };
	}
	/* The tick of the next byte for the chip, UINT64_MAX for none */
	uint64_t next_byte() const { return to_chip_count ? to_chip[0].tick : UINT64_MAX; }
	/* That byte, taken */
	uint8_t take_byte()
	{
		const uint8_t byte = to_chip[0].value;

		std::copy(to_chip.begin() + 1, to_chip.begin() + to_chip_count, to_chip.begin());
		to_chip_count--;
		return byte;
	}
	/* Something for the MIE at `tick`, in the order of the ticks: a byte
	 * comes a character after its start bit, a change of the sense line at
	 * once */
	void for_mie(const char *board, uint64_t tick, uint8_t kind, uint8_t value)
	{
		if (to_mie_count == to_mie.size())
			stop_fatal("%s: more for the MIE than the line holds", board);
		uint32_t n = to_mie_count++;

		for (; n > 0 && to_mie[n - 1].tick > tick; n--)
			to_mie[n] = to_mie[n - 1];
		to_mie[n] = { tick, kind, value };
	}
	uint64_t next_event() const { return to_mie_count ? to_mie[0].tick : UINT64_MAX; }
	/* Gives the MIE all it has up to `tick` */
	void deliver(Mie &mie, uint64_t tick)
	{
		uint32_t n = 0;

		for (; n < to_mie_count && to_mie[n].tick <= tick; n++) {
			if (to_mie[n].kind == IoLineEvent::BYTE)
				mie.line_received(to_mie[n].value);
			else
				mie.set_line_addressed(to_mie[n].value != 0);
		}
		std::copy(to_mie.begin() + n, to_mie.begin() + to_mie_count, to_mie.begin());
		to_mie_count -= n;
	}
	/* The sense line as the MIE has it, the board driving it `addressed`:
	 * as before the first change it has not been given yet */
	bool told(bool addressed) const
	{
		for (uint32_t n = 0; n < to_mie_count; n++)
			if (to_mie[n].kind == IoLineEvent::SENSE)
				return to_mie[n].value == 0;
		return addressed;
	}
};

/* The coin switch of each slot as a board sees it: a coin closes it for
 * `length` of the clock of the board, then leaves it open as long before
 * the next */
struct IoLineCoins {
	uint8_t phase[2] = {};	/* 0 idle, 1 closed, 2 open */
	uint64_t until[2] = {};
	uint32_t waiting[2] = {};

	/* Whether the switch of the slot is closed at `at` */
	bool closed(unsigned slot, uint64_t at, uint64_t length)
	{
		if (phase[slot] && at >= until[slot]) {
			phase[slot] = phase[slot] == 1 ? 2 : 0;
			until[slot] = at + length;
		}
		if (!phase[slot] && waiting[slot]) {
			waiting[slot]--;
			phase[slot] = 1;
			until[slot] = at + length;
		}
		return phase[slot] == 1;
	}
};

/* An I/O board that runs its own program at the far end of UART 1 of the
 * MIE (MieBoard::run_line). Its time is that of the line: ticks of
 * 1/9.216 GHz from its reset. */
class IoLineBoard : public MieLine {
public:
	virtual void reset() = 0;
	/* A coin through slot 0 or 1 */
	virtual void inc_coin(uint8_t slot) = 0;
	/* Runs the board up to `tick`, giving it the bytes of the MIE at
	 * their time: then the first event for the MIE before it is known. */
	virtual void run_until(uint64_t tick) = 0;
	/* Where the board is, in ticks */
	virtual uint64_t now() const = 0;
	/* The tick the next Mie::run of the MIE starts at, for line_sent */
	virtual void set_run_start(uint64_t tick) = 0;
	/* The tick of the first event for the MIE, UINT64_MAX for none */
	virtual uint64_t next_event() const = 0;
	/* Gives the MIE the events up to `tick` */
	virtual void deliver(Mie &mie, uint64_t tick) = 0;
	/* The outputs as JVS numbers them: output 1 is bit 7 of the first byte */
	virtual std::span<const uint8_t> outputs() const = 0;
};

} /* namespace odin */

#endif /* ODIN_IO_LINE_HPP */
