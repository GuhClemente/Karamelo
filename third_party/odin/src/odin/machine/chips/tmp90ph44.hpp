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

#ifndef ODIN_TMP90PH44_HPP
#define ODIN_TMP90PH44_HPP

#include <array>
#include <cstdint>
#include <span>

#include "odin/machine/cpu/tlcs90/tlcs90.hpp"

namespace odin {

/* What the board around the chip gives it. The pins and the analog inputs
 * stay as they are for the whole of a run(): the chip bills the turns of a
 * loop that reads them without running them (Tmp90ph44::run), and reads
 * them again at the start of the next. */
class Tmp90ph44Board {
public:
	virtual ~Tmp90ph44Board() = default;
	/* The external bus, 4000 to FDBF */
	virtual uint8_t bus_read(uint16_t addr) = 0;
	virtual void bus_write(uint16_t addr, uint8_t value) = 0;
	/* The pins of port 0 to 8 */
	virtual uint8_t port_pins(unsigned port) = 0;
	/* The program has written port 0 to 7 or its control register: its
	 * latch and the bits that drive their pins. Until then, after the
	 * reset pin, no pin is driven. */
	virtual void port_out(unsigned port, uint8_t latch, uint8_t driven) = 0;
	/* Analog input AN0 to AN3 converted: 00 at AGND, FF at VREF */
	virtual uint8_t analog_pin(unsigned n) = 0;
	/* A byte has left on the serial line, its stop bit ended. */
	virtual void serial_sent(uint8_t byte) = 0;
};

/* What the chip keeps from one run() to the next, plain data. The trials
 * of loops are not kept: they change what is run, never what it does. */
struct Tmp90ph44State {
	Tlcs90State cpu;
	std::array<uint8_t, 0x200> ram;
	std::array<uint8_t, 0x40> sfr;
	std::array<uint8_t, 4> timer;
	uint64_t states, prescaler;
	uint32_t watchdog, requests;
	int32_t tx_left;
	uint8_t tx, rx, rx_full, rx_errors;
};

/* The Toshiba TMP90PH44: the TMP90C844 with 16 KB of PROM at 0000 and
 * 512 bytes of RAM at FDC0 (Toshiba data sheet TMP90PH44), around a
 * TLCS-90 CPU. No data sheet of the TMP90C844 was found: the addresses of
 * its registers are those MAME gives (tmp90844_regs); a block works as the
 * same block of the TMP90C840 in the 1988 Toshiba data book where the
 * register is the same, and as the program 315-6215 uses it otherwise.
 * A register value nothing tells the meaning of stops the
 * emulator.
 *
 *  - Prescaler: fc/4 into 9 stages; the 8-bit timers count phiT1 (fc/8),
 *    phiT16 (fc/128) or phiT256 (fc/2048). Timer n counts while TRUN bit n
 *    and the prescaler bit 5 are set, is cleared when it matches TREGn
 *    and then asks for its interrupt.
 *  - Watchdog: 20 stages on fc/2; INTWD every 2^(14 + 2 * WDTP) periods
 *    of fc; 4E in WDCR clears it, B1 with WDTE clear stops it.
 *  - Interrupts: a flag per vector, cleared when taken or when FFEA is
 *    written with the vector / 8.
 *  - Serial channel: 8-bit UART at the rate of the JVS line; a byte
 *    received while SCBUF holds one not read is lost (OERR).
 *  - A/D converter: AN0 to AN3 (P50-P53) into FFF0-FFF3.
 *
 * Times are in states, two periods of fc. */
class Tmp90ph44 : private Tlcs90Bus {
public:
	static constexpr size_t ROM_SIZE = 0x4000;
	/* States a byte takes on the line: 10 bits at fc / 128 a bit */
	static constexpr uint32_t BYTE_STATES = 640;
	/* States from the start of a byte on the line to its INTRX: the
	 * middle of its last data bit, where the UART samples it */
	static constexpr uint32_t RX_STATES = 544;

	Tmp90ph44(std::span<const uint8_t> rom, Tmp90ph44Board &board);

	/* The reset pin */
	void reset();
	/* Runs instructions and devices for at least `states`; the states it
	 * ran. A loop proven idle is billed without being run: a turn that
	 * comes back to its start in the same state, having changed nothing,
	 * read nothing that changes and taken no interrupt, would do the same
	 * up to the next event of a device. The proof holds into
	 * the next run() if the CPU is where the turn has it and the pins the
	 * turn read are the same. */
	uint32_t run(uint32_t states);
	/* The data bits of a byte on the serial line have been sampled:
	 * RX_STATES after its start. */
	void serial_received(uint8_t byte);

	/* Between two run() calls */
	void save(Tmp90ph44State &out) const;
	void load(const Tmp90ph44State &in);

	/* For the tests: the CPU, a byte of the memory as the CPU sees it (the
	 * registers as written) */
	Tlcs90 &cpu() { return m_cpu; }
	uint8_t peek(uint16_t addr) const;
	uint64_t states() const { return m_states; }
	/* For the tests: the states run() billed without running them */
	uint64_t skipped() const { return m_skipped; }
	/* Off (ODIN_TURNS=0): no loop is tried, every turn runs. The states
	 * must be the same. */
	void set_waits_counted(bool on)
	{
		m_waits_counted = on;
		m_trial.on = false;
		m_proof.on = false;
	}

private:
	enum : uint16_t { VEC_WD = 0x20, VEC_T1 = 0x28, VEC_T3 = 0x38, VEC_RX = 0x60, VEC_TX = 0x68 };

	uint8_t read8(uint16_t addr) override;
	void write8(uint16_t addr, uint8_t value) override;
	uint16_t interrupt(bool maskable) override;

	uint8_t sfr_read(uint8_t reg);
	void sfr_write(uint8_t reg, uint8_t value);
	void tell_port(unsigned port);
	uint8_t p67_driven(unsigned port) const;
	void sync();
	uint64_t next_event() const;
	uint32_t timer_period(unsigned n) const;
	unsigned ticks_to_match(unsigned n) const;
	uint32_t watchdog_period() const;
	void request(uint16_t vector) { m_requests |= 1u << (vector >> 3); }
	bool requested(uint16_t vector) const { return (m_requests & (1u << (vector >> 3))) != 0; }

	/* What spoils the turn on trial: something that may not come again
	 * (an interrupt, an event, a byte of memory changed), or something no
	 * idle loop does (MAX_TURN instructions, a register written, the
	 * external bus, a state not the same). The registers that change when
	 * read (SCBUF, SCCR) do so the first time only. */
	enum class Spoil : uint8_t { NONE, AGAIN, NEVER };
	static constexpr unsigned MAX_TURN = 64;
	/* How long a loop that cannot be idle is not tried again */
	static constexpr uint64_t COOLDOWN = 1u << 15;
	void jumped_back();
	uint64_t turn(uint64_t end);
	uint64_t resume(uint64_t end);
	uint64_t bill(uint64_t end);
	uint8_t pin(unsigned n);
	uint8_t pin_now(unsigned n) { return n < 8 ? m_board.port_pins(n) : m_board.analog_pin(n - 8); }
	void spoil(Spoil how)
	{
		m_proof.on = false;
		if (m_trial.on && how > m_trial.spoil)
			m_trial.spoil = how;
	}

	Tlcs90 m_cpu;
	Tmp90ph44Board &m_board;
	std::array<uint8_t, ROM_SIZE> m_rom;
	std::array<uint8_t, 0x200> m_ram{};
	std::array<uint8_t, 0x40> m_sfr{};	/* FFC0-FFFF as written */
	uint64_t m_states = 0;
	uint64_t m_synced = 0;		/* the devices are up to date at this time */
	uint64_t m_next_event = 0;	/* the first time one asks for an interrupt */
	uint64_t m_prescaler = 0;	/* states since the prescaler runs */
	std::array<uint8_t, 4> m_timer{};
	uint32_t m_watchdog = 0;	/* states since cleared */
	uint32_t m_requests = 0;	/* a bit per vector / 8 */
	uint8_t m_rx = 0;
	bool m_rx_full = false;		/* SCBUF not read since its byte came */
	uint8_t m_rx_errors = 0;	/* SCCR bits 4-2 */
	int32_t m_tx_left = -1;		/* states until the byte is sent */
	uint8_t m_tx = 0;

	/* A turn of a loop on trial, from `pc` */
	struct Trial {
		bool on = false;
		Spoil spoil = Spoil::NONE;
		uint16_t pc = 0;
		unsigned steps = 0;
		uint64_t start = 0;
	} m_trial;
	/* What the turn on trial ran, kept once it is proven idle: before its
	 * instruction n, the CPU and the states from its start. Pin n is port
	 * n, 8 + n AN n. */
	struct Proof {
		bool on = false;
		unsigned length = 0;
		unsigned at = 0;		/* the instruction the CPU is before */
		uint64_t period = 0;		/* the states of a turn */
		uint16_t read = 0;		/* bit n: pin n read, as `pins` has it */
		std::array<uint8_t, 12> pins{};
		std::array<uint32_t, MAX_TURN> offset{};
		std::array<Tlcs90State, MAX_TURN> cpu{};
	} m_proof;
	std::array<uint16_t, 4> m_back{};	/* the last places jumped back to */
	bool m_waits_counted = true;
	struct Cool {
		uint16_t pc;
		uint64_t until;
	};
	std::array<Cool, 16> m_cool{};
	uint64_t m_skipped = 0;
};

} /* namespace odin */

#endif /* ODIN_TMP90PH44_HPP */
