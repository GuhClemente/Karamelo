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

#ifndef ODIN_MIE_HPP
#define ODIN_MIE_HPP

#include "odin/machine/cabinet/jvs.hpp"
#include "odin/machine/cpu/z80/z80.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace odin {

/* Sega 315-6146 "MIE": a Z80 with 2 KB of mask ROM, 2 KB + 32 KB of RAM,
 * eight GPIO ports, two 16550-style UARTs (the first drives the JVS
 * RS-485 link), a Maple slave block and a bus-request input through
 * which the host CPU reads and writes the Z80 memory and I/O spaces.
 *
 * Memory map and I/O ports: MAME src/mame/sega/mie.cpp (mie_map,
 * mie_port) and the Z80 code the Hikaru BOOTROM uploads. Host windows:
 * MAME src/mame/sega/hikaru.cpp localbus_map. */

/* Board wiring around the chip: what the GPIO pins see. Ports are
 * numbered A..H = 0..7. */
class MieHost {
public:
	virtual ~MieHost() = default;
	/* Level on the pins of a port configured as input. */
	virtual uint8_t gpio_read(uint8_t port) = 0;
	/* New level driven on a port configured as output. */
	virtual void gpio_write(uint8_t port, uint8_t value) = 0;
	/* What an I/O board adds around the chip; the main board has none
	 * of it. Channel `n` of the converter (port 0F). */
	virtual uint8_t analog_read(unsigned n)
	{
		(void)n;
		return 0;
	}
	/* The ports the chip does not decode: false when nothing answers. */
	virtual bool board_in8(uint8_t port, uint8_t &value)
	{
		(void)port;
		(void)value;
		return false;
	}
	virtual bool board_out8(uint8_t port, uint8_t value)
	{
		(void)port;
		(void)value;
		return false;
	}
	/* Port 91, when the board gives its pins: false for the sense of the
	 * JVS cable of UART 1, as the MIE of the main board reads it. */
	virtual bool sense_pins(uint8_t &value)
	{
		(void)value;
		return false;
	}
	/* Port 90 written: bit 0 the driver of UART 1, bit 1 the line that
	 * tells the board before that an address is set. */
	virtual void jvs_control_written(uint8_t value) { (void)value; }
};

/* The far end of UART 1 when it is a line byte by byte, an I/O board that
 * runs its own program, rather than a JvsHost. Ticks are those of the
 * line, 1/9.216 GHz, from the start of the current Mie::run(). */
class MieLine {
public:
	virtual ~MieLine() = default;
	/* A byte from UART 1: its start bit begins `ticks` into the run. */
	virtual void line_sent(uint8_t byte, uint64_t ticks) = 0;
};

/* What sits at the far end of the serial adapter block (ports 0x09-
 * 0x0E): a device on an RS-232 line. */
class MieSerialDevice {
public:
	virtual ~MieSerialDevice() = default;
	/* A byte the Z80 sent on the line. */
	virtual void receive(uint8_t byte) = 0;
	/* Bytes the device sends back, one at a time. */
	virtual bool has_byte() = 0;
	virtual uint8_t take_byte() = 0;
};

/* Times in ticks of 1/9.216 GHz (mie.cpp). */
struct MieUartState {
	uint8_t dll, dlm, ier, fcr, lcr, mcr, scr;
	std::array<uint8_t, 16> tx_fifo;
	uint8_t tx_count;
	uint32_t tx_ticks;
	uint8_t tx_byte;
	std::array<uint8_t, 512> rx_fifo;
	uint32_t rx_head, rx_count;
	std::array<uint8_t, JvsHost::BUFFER_SIZE> wire;
	uint32_t wire_pos, wire_count, wire_ticks;
	uint8_t thre_irq;	/* the transmitter asks for its interrupt */
};

struct MieSerialState {
	uint8_t data;		/* port 0x0A latch */
	uint8_t control;	/* last port 0x09 write */
	uint8_t reply_control;	/* port 0x0B */
	uint8_t reply_data;	/* port 0x0C */
	uint8_t status;		/* port 0x0D */
	uint8_t op;		/* exchange in flight */
	uint32_t busy_cycles;
};

struct MieState {
	Z80State cpu;
	std::array<uint8_t, 0x800> ram_low;
	std::array<uint8_t, 0x40> maple;
	std::array<uint8_t, 8> gpio_val;
	uint8_t gpio_dir;
	uint8_t irq_enable, irq_pending, maple_irqlevel;
	uint8_t jvs_control;
	uint8_t busrq;
	MieUartState uart;
	MieSerialState serial;
	uint8_t adc_channel;
	uint8_t external_irq;
};

class Mie : public Z80Bus {
public:
	/* MAME hikaru.cpp: MIE(config, m_mie, 16000000, ...). */
	static constexpr uint32_t CLOCK_HZ = 16000000;
	static constexpr uint32_t ROM_SIZE = 0x800;
	/* Z80 0x8000-0xFFFF, the half the host sees through its window. */
	static constexpr uint32_t RAM_SIZE = 0x8000;
	/* A Z80 clock in ticks of the line, 1/9.216 GHz (mie.cpp) */
	static constexpr uint32_t TICKS_PER_CYCLE = 576;

	/* `rom` is the 2 KB 315-6146 mask ROM; `ram` the 32 KB shared half,
	 * owned by the caller. A larger `rom` is the EPROM of an I/O board,
	 * which the chip runs from: 0000-7FFF is that EPROM. */
	Mie(MieHost &host, std::span<const uint8_t> rom, std::span<uint8_t> ram);

	void reset();

	/* Z80 side. */
	uint8_t read8(uint16_t addr) override;
	void write8(uint16_t addr, uint8_t value) override;
	uint8_t in8(uint16_t port) override;
	void out8(uint16_t port, uint8_t value) override;
	/* The interrupt: levels 0-6 at vectors F2 + 2 n, the lowest first
	 * (MAME mie.cpp irq_callback), UART 1 being level 3; then the input
	 * of the board, enabled by bit 7 of port 30, at vector FE. HEURISTIC:
	 * that input and its vector, from epr-21868 (its timer, port 30 =
	 * 88 at 02B5, routine of vector FE at 0828). */
	uint8_t int_ack() override;
	/* A pulse on that input: kept until taken. */
	void raise_external() { m_external_irq = true; }
	/* Ticks of the line from the start of the current run() */
	uint64_t run_ticks() const { return m_run_ticks; }

	/* Host side: the same two spaces, one byte at a time
	 * (hikaru.cpp localbus_map 0x00800000 and 0x00820000). */
	uint8_t host_read_port(uint8_t port);
	void host_write_port(uint8_t port, uint8_t value);
	uint8_t host_read_mem(uint16_t addr);
	void host_write_mem(uint16_t addr, uint8_t value);

	/* BUSRQ pin. The bus is granted only while the Z80 program allows
	 * it with bit 6 of port 0x30 (MAME mie.h busak_r). While granted,
	 * the CPU is stopped. */
	void set_busrq(bool level);
	bool busak() const { return m_busrq && (m_irq_enable & 0x40); }
	/* busak() with a trace line. */
	bool host_busak_read();

	/* Runs the chip for at least `cycles` Z80 clocks: it ends the
	 * instruction it is in. The clocks it ran. */
	uint32_t run(uint32_t cycles);
	/* A program that waits goes round a loop that only reads the memory,
	 * or the state of UART 1, and leaves every register as it was, R
	 * aside, which it does not read. Nothing changes what it reads before
	 * the chip is run again, or UART 1 changes: the turns up to there are
	 * counted without being run, and still are in the calls after while
	 * the memory is not written. Off, every turn is run; the two must give
	 * the same. */
	void set_waits_counted(bool on) { m_waits_counted = on; }
	/* For the tests: the Z80 clocks counted that way so far */
	uint64_t waits_counted() const { return m_wait_cycles; }

	/* A GPIO port from the board: whether the chip drives its pins, and
	 * with what. */
	bool gpio_is_output(uint8_t port) const { return !(m_gpio_dir & (1u << port)); }
	uint8_t gpio_latch(uint8_t port) const { return m_gpio_val[port]; }

	/* The JVS cable on UART 1, which the board owns. Without one the sense
	 * input tells that no I/O board is wired. */
	void attach_jvs(JvsHost *cable) { m_jvs = cable; }
	/* Or a line, byte by byte: the board gives UART 1 what the line
	 * brings, at its time, and sets the sense input. */
	void attach_line(MieLine *line) { m_line = line; }
	void line_received(uint8_t byte);
	void set_line_addressed(bool addressed) { m_line_addressed = addressed; }
	/* For the tests */
	const Z80 &cpu() const { return m_cpu; }
	/* Device on the serial adapter line; nullptr leaves the line open. */
	void set_serial_device(MieSerialDevice *dev) { m_serial_device = dev; }

	void save(MieState &out) const;
	void load(const MieState &in);

	/* Unknown accesses are logged once each to `out`. */
	void set_log(FILE *out) { m_log = out; }
	/* Z80 bus trace for `max_instructions` instructions once the PC
	 * reaches `start_pc`; host accesses are traced as "host" for as long
	 * as `out` is set. */
	void set_trace(FILE *out, uint32_t max_instructions, uint16_t start_pc);

private:
	enum : uint16_t {
		ROM_END = 0x0800,
		MAPLE_BASE = 0x7000,
		RAM_LOW_BASE = 0x7800,
		RAM_BASE = 0x8000
	};

	bool uart_irq() const;
	uint8_t levels() const;
	bool irq_line() const;
	uint8_t gpio_read(uint8_t port);
	void gpio_write(uint8_t port, uint8_t value);
	void gpio_dir_write(uint8_t value);
	uint8_t uart_read(uint8_t reg);
	void uart_write(uint8_t reg, uint8_t value);
	void uart_reset();
	void uart_tick(uint32_t cycles);
	uint32_t uart_byte_ticks() const;
	void uart_commit_frame();
	uint8_t serial_read(uint8_t port);
	void serial_write(uint8_t port, uint8_t value);
	void serial_reset();
	void serial_tick(uint32_t cycles);
	void serial_complete();
	uint8_t jvs_sense() const;
	void tick(uint32_t cycles);
	void wait_mark(uint32_t done);
	uint32_t wait_check(uint32_t done, uint32_t cycles);
	uint32_t resume(uint32_t cycles);
	uint32_t bill(uint32_t done, uint32_t cycles);
	uint32_t uart_end(uint32_t done, uint32_t cycles) const;
	uint32_t turns_before_end(uint32_t done, uint32_t cycles, uint32_t turn) const;
	uint8_t uart_lsr() const;
	int counts_down(unsigned reg, uint16_t &dec_pc);
	void log_once(bool &said, char rw, const char *space, uint32_t addr, uint32_t value);
	void log_unknown(char rw, const char *space, uint32_t addr, uint32_t value);
	void trace_host(char rw, uint32_t addr, uint8_t value) const;

	MieHost &m_host;
	Z80 m_cpu;
	JvsHost *m_jvs;
	std::span<const uint8_t> m_rom;
	bool m_external;	/* runs from the EPROM of an I/O board */
	uint8_t m_adc_channel;
	bool m_external_irq;
	std::span<uint8_t> m_ram;
	std::array<uint8_t, 0x800> m_ram_low;
	std::array<uint8_t, 0x40> m_maple;
	std::array<uint8_t, 8> m_gpio_val;
	uint8_t m_gpio_dir;
	uint8_t m_irq_enable;
	uint8_t m_irq_pending;
	uint8_t m_maple_irqlevel;
	uint8_t m_jvs_control;
	bool m_busrq;
	MieUartState m_uart;
	MieSerialState m_serial;
	MieSerialDevice *m_serial_device;
	MieLine *m_line = nullptr;
	bool m_line_addressed = false;
	uint64_t m_run_ticks = 0;	/* from the start of the current run() */

	/* The search for a loop that waits, in a call of run(): the
	 * registers at a mark and the cycles run before it, whether the
	 * program has written or used a port since, the instructions to the
	 * next mark, and the returns to the mark left to be let through. */
	struct Wait {
		Z80State regs;
		uint32_t done;
		bool touched;
		uint32_t countdown, period;
		uint32_t pass, busy;
		uint32_t steps;		/* instructions since the mark */
		bool lsr;		/* the state of UART 1 read since */
		uint8_t lsr_at;		/* that state at the mark */
		int mark_at;		/* the next mark there, -1 none */
	};
	Wait m_wait;
	/* What the search ran since its mark, kept once the turn is proven
	 * to wait: before its instruction n, the CPU and the clocks from the
	 * mark. A turn of more instructions is counted by whole turns, in its
	 * call only. */
	static constexpr unsigned MAX_TURN = 64;
	struct Proof {
		bool on = false;
		unsigned length = 0;
		unsigned at = 0;		/* the instruction the CPU is before */
		uint32_t period = 0;		/* the clocks of a turn */
		uint32_t refresh = 0;		/* its M1 cycles */
		bool lsr = false;		/* it reads the state of UART 1 */
		uint8_t lsr_at = 0;		/* that state */
		std::array<uint32_t, MAX_TURN> offset{};
		std::array<Z80State, MAX_TURN> cpu{};
	} m_proof;
	bool m_waits_counted;
	uint64_t m_wait_cycles;

	FILE *m_log;
	FILE *m_trace;
	uint32_t m_trace_left;
	uint16_t m_trace_start;
	bool m_trace_armed;
	bool m_busak_traced;
	std::array<bool, 512> m_port_logged;
	std::array<bool, 2> m_mem_logged;
};

} /* namespace odin */

#endif /* ODIN_MIE_HPP */
