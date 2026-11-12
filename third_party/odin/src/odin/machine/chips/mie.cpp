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

#include "odin/machine/chips/mie.hpp"
#include "odin/common/repeats.hpp"

#include <algorithm>
#include <format>

namespace odin {

namespace {

/* I/O ports (MAME mie.cpp mie_port; UART registers from the uploaded
 * program, listing 0x8677 and 0x87CB-0x8806). */
enum : uint8_t {
	PORT_GPIO_A = 0x00,
	PORT_GPIO_H = 0x07,
	PORT_GPIO_DIR = 0x08,
	PORT_SERIAL_CONTROL = 0x09,
	PORT_SERIAL_DATA = 0x0A,
	PORT_SERIAL_REPLY_CONTROL = 0x0B,
	PORT_SERIAL_REPLY_DATA = 0x0C,
	PORT_SERIAL_STATUS = 0x0D,
	PORT_SERIAL_END = 0x0E,
	PORT_ADC = 0x0F,
	PORT_UART1 = 0x10,
	PORT_UART1_END = 0x17,
	PORT_UART2 = 0x20,
	PORT_UART2_END = 0x27,
	PORT_IRQ_ENABLE = 0x30,
	PORT_MAPLE_IRQLEVEL = 0x50,
	PORT_IRQ_PENDING = 0x70,
	PORT_JVS_CONTROL = 0x90,
	PORT_JVS_SENSE = 0x91
};

/* 16550 registers, offsets from the UART base. */
enum : uint8_t {
	UART_RBR_THR_DLL = 0,
	UART_IER_DLM = 1,
	UART_IIR_FCR = 2,
	UART_LCR = 3,
	UART_MCR = 4,
	UART_LSR = 5,
	UART_MSR = 6,
	UART_SCR = 7
};

enum : uint8_t {
	LCR_DLAB = 0x80,
	FCR_ENABLE = 0x01,
	FCR_CLEAR_RX = 0x02,
	FCR_CLEAR_TX = 0x04,
	LSR_DR = 0x01,
	LSR_THRE = 0x20,
	LSR_TEMT = 0x40,
	IER_RX = 0x01,
	IER_THRE = 0x02,
	IIR_NONE = 0x01,
	IIR_THRE = 0x02,
	IIR_RX = 0x04,
	IIR_FIFO = 0xC0
};

/* Bits of port 30: the levels the program lets through (UART 1 is level
 * 3), the input of the board (bit 7); bit 6 gates BUSAK. */
enum : uint8_t {
	IRQ_UART1 = 0x08,
	IRQ_EXTERNAL = 0x80,
	IRQ_SOURCES = 0xBF
};

/* Bits on the wire per character: start, 8 data, stop. */
constexpr uint32_t UART_BITS_PER_BYTE = 10;
/* Reference clock per bit, as in a 16550: 16 x the baud rate. */
constexpr uint32_t UART_CLOCKS_PER_BIT = 16;
/* The UART runs from the 14.7456 MHz crystal beside the chip (MAME
 * hikaru.cpp, board layout; the same on the 837-13741), the Z80 at
 * 16 MHz: with the divisor 8 the programs set (MIE program at 8677,
 * epr-21868 at 01A1) that is the 115200 bauds of JVS. Times on the line
 * are counted in ticks of 1/9.216 GHz, in which both clocks are whole. */
constexpr uint32_t TICKS_PER_UART_CLOCK = 625;
/* For an I/O board whose program was not timed (JvsHost::reply_cycles), the
 * time from the end of the request to the end of the first byte of the
 * reply, in characters. HEURISTIC: not measured. */
constexpr uint32_t JVS_TURNAROUND_BYTES = 2;

/* Serial adapter block, ports 0x09-0x0E (MAME mie.cpp: "315-5649-style
 * serial block", nothing more). Semantics from the Z80 code games
 * upload for their card reader (von4 extension 0x9000-0x932B): write
 * 0x0A latches a byte, write 0x09 starts an exchange, 0x0D reports it,
 * 0x0B and 0x0C hold the two reply bytes. */
enum : uint8_t {
	SERIAL_CTRL_SEND = 0x01,	/* byte on the line, echo as reply */
	SERIAL_CTRL_REQUEST = 0x80,	/* adapter request (0x80 and 0x81) */
	SERIAL_CTRL_LOOPBACK = 0x55,	/* both bytes come straight back */
	SERIAL_REQ_STATUS = 0x80,	/* reply bit 6: a line byte waits */
	SERIAL_REQ_TAKE = 0x81,		/* reply: that byte */
	SERIAL_STATUS_BUSY = 0x03,	/* exchange running */
	SERIAL_STATUS_LOOP_READY = 0x04,
	SERIAL_STATUS_READY = 0x08,
	SERIAL_RX_WAITING = 0x40
};

enum : uint8_t {
	SERIAL_OP_NONE = 0,
	SERIAL_OP_SEND,
	SERIAL_OP_REQUEST,
	SERIAL_OP_LOOPBACK
};

/* HEURISTIC: reply delays, not measured. The program waits ~4 300
 * cycles before polling a sent byte and gives up ~7 000 cycles after
 * the control write otherwise. */
constexpr uint32_t SERIAL_SEND_CYCLES = 640;
constexpr uint32_t SERIAL_REQUEST_CYCLES = 160;

/* A received byte into the FIFO, lost when it is full */
void
rx_push(MieUartState &u, uint8_t byte)
{
	if (u.rx_count < u.rx_fifo.size()) {
		u.rx_fifo[(u.rx_head + u.rx_count) % static_cast<uint32_t>(u.rx_fifo.size())] = byte;
		u.rx_count++;
	}
}

} /* namespace */

Mie::Mie(MieHost &host, std::span<const uint8_t> rom, std::span<uint8_t> ram)
	: m_host(host), m_cpu(*this), m_jvs(nullptr), m_rom(rom), m_external(rom.size() > ROM_SIZE),
	  m_adc_channel(0), m_external_irq(false), m_ram(ram),
	  /* Zeroed with the gaps between their fields, which the states copy */
	  m_uart{}, m_serial{}, m_serial_device(nullptr), m_wait{},
	  m_waits_counted(true), m_wait_cycles(0), m_log(nullptr), m_trace(nullptr),
	  m_trace_left(0), m_trace_start(0), m_trace_armed(false), m_busak_traced(false)
{
	m_port_logged.fill(false);
	m_mem_logged.fill(false);
	m_ram_low.fill(0);
	reset();
}

/* Reset values: MAME mie.cpp device_reset. */
void Mie::reset()
{
	m_maple.fill(0);
	m_gpio_val.fill(0);
	m_gpio_dir = 0xFF;
	m_irq_enable = 0;
	m_irq_pending = 0;
	m_maple_irqlevel = 0xFF;
	m_jvs_control = 0;
	m_busrq = false;
	m_adc_channel = 0;
	m_external_irq = false;
	uart_reset();
	serial_reset();
	m_proof.on = false;
	m_cpu.reset();
}

void Mie::set_trace(FILE *out, uint32_t max_instructions, uint16_t start_pc)
{
	m_trace = out;
	m_trace_left = out ? max_instructions : 0;
	m_trace_start = start_pc;
	m_trace_armed = m_trace_left != 0;
	m_cpu.set_trace(nullptr);
}

/* Host side of the two spaces. Port reads and writes are the Z80's
 * own, so the GPIO and UART see them the same way. */

uint8_t Mie::host_read_port(uint8_t port)
{
	const uint8_t v = in8(port);
	if (m_trace) {
		trace_host('i', port, v);
	}
	return v;
}

void Mie::host_write_port(uint8_t port, uint8_t value)
{
	if (m_trace) {
		trace_host('o', port, value);
	}
	out8(port, value);
}

uint8_t Mie::host_read_mem(uint16_t addr)
{
	const uint8_t v = read8(addr);
	if (m_trace) {
		trace_host('r', addr, v);
	}
	return v;
}

void Mie::host_write_mem(uint16_t addr, uint8_t value)
{
	if (m_trace) {
		trace_host('w', addr, value);
	}
	write8(addr, value);
}

/* Bus request pin; traced as port 0x100, 'q' request and 'k' grant. */

void Mie::set_busrq(bool level)
{
	if (m_trace) {
		trace_host('q', 0x100, level ? 1 : 0);
	}
	m_busrq = level;
}

bool Mie::host_busak_read()
{
	const bool ack = busak();
	/* The host polls this; one line per change keeps the trace small. */
	if (m_trace && ack != m_busak_traced) {
		m_busak_traced = ack;
		trace_host('k', 0x100, ack ? 1 : 0);
	}
	return ack;
}

void Mie::trace_host(char rw, uint32_t addr, uint8_t value) const
{
	std::fputs(std::format("host 0000 {} {:04x} 1 {:02x}\n", rw, addr, value).c_str(), m_trace);
}

/* Z80 memory space (MAME mie.cpp mie_map). */

uint8_t Mie::read8(uint16_t addr)
{
	if (addr >= RAM_BASE) {
		return m_ram[addr - RAM_BASE];
	}
	if (m_external) {
		return addr < m_rom.size() ? m_rom[addr] : 0xFF;
	}
	if (addr >= RAM_LOW_BASE) {
		return m_ram_low[addr - RAM_LOW_BASE];
	}
	if (addr >= MAPLE_BASE) {
		/* Control, length and transfer buffer, mirrored every 0x40. */
		const uint8_t off = addr & 0x3F;
		return off < 0x24 ? m_maple[off] : 0x00;
	}
	if (addr >= ROM_END) {
		return 0xFF;
	}
	return m_rom[addr];
}

void Mie::write8(uint16_t addr, uint8_t value)
{
	m_wait.touched = true;
	m_proof.on = false;
	if (addr >= RAM_BASE) {
		m_ram[addr - RAM_BASE] = value;
	} else if (m_external || addr < ROM_END) {
		log_once(m_mem_logged[1], 'w', "rom", addr, value);
	} else if (addr >= RAM_LOW_BASE) {
		m_ram_low[addr - RAM_LOW_BASE] = value;
	} else if (addr >= MAPLE_BASE) {
		/* No Maple host on Hikaru: the block is plain storage. */
		const uint8_t off = addr & 0x3F;
		if (off < 0x24) {
			m_maple[off] = value;
		}
	} else {
		log_once(m_mem_logged[0], 'w', "mem", addr, value);
	}
}

/* Z80 I/O space (MAME mie.cpp mie_port). */

uint8_t Mie::in8(uint16_t port)
{
	const uint8_t p = static_cast<uint8_t>(port);

	if (p == static_cast<unsigned>(PORT_UART1) + static_cast<unsigned>(UART_LSR)) {
		/* No effect: it changes only in uart_tick, at times known
		 * before (uart_end). */
		m_wait.lsr = true;
		return uart_read(UART_LSR);
	}
	m_wait.touched = true;
	if (p <= PORT_GPIO_H) {
		return gpio_read(p);
	}
	if (p >= PORT_UART1 && p <= PORT_UART1_END) {
		return uart_read(static_cast<uint8_t>(p - PORT_UART1));
	}
	if (p >= PORT_SERIAL_CONTROL && p <= PORT_SERIAL_END) {
		return serial_read(p);
	}
	switch (p) {
	case PORT_GPIO_DIR:
		return m_gpio_dir;
	case PORT_ADC:
		/* The channels one after the other from 0 (epr-21868 139B) */
		return m_host.analog_read(m_adc_channel++ & 7u);
	case PORT_IRQ_ENABLE:
		return m_irq_enable;
	case PORT_MAPLE_IRQLEVEL:
		return m_maple_irqlevel;
	case PORT_IRQ_PENDING:
		return m_irq_pending;
	case PORT_JVS_SENSE: {
		uint8_t pins;

		return m_host.sense_pins(pins) ? pins : jvs_sense();
	}
	default:
		break;
	}
	if (p >= PORT_UART2 && p <= PORT_UART2_END) {
		/* Second UART, nothing wired: transmitter idle, no data. */
		return (p - PORT_UART2) == UART_LSR ? (LSR_THRE | LSR_TEMT) : 0;
	}
	if (uint8_t value; m_host.board_in8(p, value)) {
		return value;
	}
	log_once(m_port_logged[p], 'i', "port", p, 0xFF);
	return 0xFF;
}

void Mie::out8(uint16_t port, uint8_t value)
{
	const uint8_t p = static_cast<uint8_t>(port);

	m_wait.touched = true;
	if (p <= PORT_GPIO_H) {
		gpio_write(p, value);
		return;
	}
	if (p >= PORT_UART1 && p <= PORT_UART1_END) {
		uart_write(static_cast<uint8_t>(p - PORT_UART1), value);
		return;
	}
	if (p >= PORT_SERIAL_CONTROL && p <= PORT_SERIAL_END) {
		serial_write(p, value);
		return;
	}
	switch (p) {
	case PORT_GPIO_DIR:
		gpio_dir_write(value);
		return;
	case PORT_ADC:
		m_adc_channel = 0;
		return;
	case PORT_IRQ_ENABLE:
		/* Bit 6 also gates BUSAK. The programs of the main board never
		 * let a source through (MIE ROM 0124, 0201, 027B; uploaded 8012,
		 * 8031: bit 6 only); a proof of a wait does not see them. */
		m_irq_enable = value;
		m_proof.on = false;
		return;
	case PORT_MAPLE_IRQLEVEL:
		m_maple_irqlevel = value;
		return;
	case PORT_IRQ_PENDING:
		m_irq_pending = value;
		return;
	case PORT_JVS_CONTROL:
		/* Bit 0 enables the RS-485 driver; the frame is complete when
		 * it drops (MAME mie.cpp jvs_control_w). */
		if ((m_jvs_control & 1) && !(value & 1)) {
			uart_commit_frame();
		}
		m_jvs_control = value;
		m_host.jvs_control_written(value);
		return;
	default:
		break;
	}
	if (p >= PORT_UART2 && p <= PORT_UART2_END) {
		return;
	}
	if (m_host.board_out8(p, value)) {
		return;
	}
	log_once(m_port_logged[256 + p], 'o', "port", p, value);
}

/* GPIO: direction bit set = input (MAME mie.cpp gpio_r); an output reads
 * back its latch. */

uint8_t Mie::gpio_read(uint8_t port)
{
	if (m_gpio_dir & (1u << port)) {
		return m_host.gpio_read(port);
	}
	return m_gpio_val[port];
}

void Mie::gpio_write(uint8_t port, uint8_t value)
{
	m_gpio_val[port] = value;
	if (!(m_gpio_dir & (1u << port))) {
		m_host.gpio_write(port, value);
	}
}

void Mie::gpio_dir_write(uint8_t value)
{
	/* Ports that just became outputs drive their latched value. */
	const uint8_t to_output = static_cast<uint8_t>((value ^ m_gpio_dir) & ~value);
	m_gpio_dir = value;
	for (uint8_t i = 0; i < 8; i++) {
		if (to_output & (1u << i)) {
			m_host.gpio_write(i, m_gpio_val[i]);
		}
	}
}

/* JVS sense input (MAME mie.cpp jvs_sense_r): bit 0 set when no board
 * is wired, bit 1 set when every board has its address. */
uint8_t Mie::jvs_sense() const
{
	const bool present = m_line || (m_jvs && m_jvs->presence_line());
	const bool addressed = m_line ? m_line_addressed : m_jvs && m_jvs->address_set_line();

	return static_cast<uint8_t>(0x8C | (addressed ? 2 : 0) | (present ? 0 : 1));
}

/* UART 1, the JVS link. Characters take their time on the wire; the
 * reply of the I/O board comes back the same way. */

void Mie::uart_reset()
{
	m_uart.dll = 0;
	m_uart.dlm = 0;
	m_uart.ier = 0;
	m_uart.fcr = 0;
	m_uart.lcr = 0;
	m_uart.mcr = 0;
	m_uart.scr = 0;
	m_uart.tx_fifo.fill(0);
	m_uart.tx_count = 0;
	m_uart.tx_ticks = 0;
	m_uart.tx_byte = 0;
	m_uart.rx_fifo.fill(0);
	m_uart.rx_head = 0;
	m_uart.rx_count = 0;
	m_uart.wire.fill(0);
	m_uart.wire_pos = 0;
	m_uart.wire_count = 0;
	m_uart.wire_ticks = 0;
	m_uart.thre_irq = 0;
}

/* Interrupts */

bool Mie::uart_irq() const
{
	return ((m_uart.ier & IER_RX) && m_uart.rx_count) || ((m_uart.ier & IER_THRE) && m_uart.thre_irq);
}

/* The levels 0 to 6 that ask and are enabled */
uint8_t Mie::levels() const
{
	return static_cast<uint8_t>(m_irq_enable & (m_irq_pending | (uart_irq() ? IRQ_UART1 : 0)) & 0x7F);
}

bool Mie::irq_line() const
{
	return levels() || ((m_irq_enable & IRQ_EXTERNAL) && m_external_irq);
}

uint8_t Mie::int_ack()
{
	const uint8_t asking = levels();

	for (unsigned level = 0; level < 7; level++) {
		if (asking & (1u << level)) {
			/* UART 1 asks as long as its cause holds */
			m_irq_pending = static_cast<uint8_t>(m_irq_pending & ~(1u << level));
			return static_cast<uint8_t>(0xF2 + 2 * level);
		}
	}
	if ((m_irq_enable & IRQ_EXTERNAL) && m_external_irq) {
		m_external_irq = false;
		return 0xFE;
	}
	return 0xFF;
}

/* A divisor above 21474 (under 6 bauds; no program sets one) is held
 * there, for the turnaround to fit in 32 bits. */
uint32_t Mie::uart_byte_ticks() const
{
	uint32_t divisor = static_cast<uint32_t>((m_uart.dlm << 8) | m_uart.dll);
	if (divisor == 0) {
		divisor = 1;
	}
	divisor = std::min<uint32_t>(divisor, 21474);
	return UART_BITS_PER_BYTE * UART_CLOCKS_PER_BIT * TICKS_PER_UART_CLOCK * divisor;
}

uint8_t Mie::uart_read(uint8_t reg)
{
	MieUartState &u = m_uart;
	switch (reg) {
	case UART_RBR_THR_DLL:
		if (u.lcr & LCR_DLAB) {
			return u.dll;
		}
		if (u.rx_count) {
			const uint8_t v = u.rx_fifo[u.rx_head];
			u.rx_head = (u.rx_head + 1) % static_cast<uint32_t>(u.rx_fifo.size());
			u.rx_count--;
			return v;
		}
		return 0;
	case UART_IER_DLM:
		return (u.lcr & LCR_DLAB) ? u.dlm : u.ier;
	case UART_IIR_FCR: {
		/* Received data first, then the transmitter, whose request
		 * this read takes (a 16550) */
		const uint8_t fifo = (u.fcr & FCR_ENABLE) ? IIR_FIFO : 0;

		if ((u.ier & IER_RX) && u.rx_count) {
			return static_cast<uint8_t>(IIR_RX | fifo);
		}
		if ((u.ier & IER_THRE) && u.thre_irq) {
			u.thre_irq = 0;
			return static_cast<uint8_t>(IIR_THRE | fifo);
		}
		return static_cast<uint8_t>(IIR_NONE | fifo);
	}
	case UART_LCR:
		return u.lcr;
	case UART_MCR:
		return u.mcr;
	case UART_LSR:
		return uart_lsr();
	case UART_MSR:
		return 0;
	case UART_SCR:
	default:
		return u.scr;
	}
}

/* Without the CPU, its bits only go from 0 to 1: a byte lands, the
 * transmitter empties. */
uint8_t Mie::uart_lsr() const
{
	const MieUartState &u = m_uart;
	uint8_t v = 0;

	if (u.rx_count) {
		v |= LSR_DR;
	}
	if (u.tx_count == 0) {
		v |= LSR_THRE;
		if (u.tx_ticks == 0) {
			v |= LSR_TEMT;
		}
	}
	return v;
}

void Mie::uart_write(uint8_t reg, uint8_t value)
{
	MieUartState &u = m_uart;
	switch (reg) {
	case UART_RBR_THR_DLL:
		if (u.lcr & LCR_DLAB) {
			u.dll = value;
		} else {
			u.thre_irq = 0;
			if (u.tx_count < u.tx_fifo.size()) {
				u.tx_fifo[u.tx_count++] = value;
			}
		}
		break;
	case UART_IER_DLM:
		if (u.lcr & LCR_DLAB) {
			u.dlm = value;
		} else {
			/* Enabled while it is empty, the transmitter asks at once */
			if ((value & IER_THRE) && !(u.ier & IER_THRE) && u.tx_count == 0) {
				u.thre_irq = 1;
			}
			u.ier = value;
		}
		break;
	case UART_IIR_FCR:
		u.fcr = value;
		if (value & FCR_CLEAR_RX) {
			u.rx_head = 0;
			u.rx_count = 0;
		}
		if (value & FCR_CLEAR_TX) {
			u.tx_count = 0;
		}
		break;
	case UART_LCR:
		u.lcr = value;
		break;
	case UART_MCR:
		u.mcr = value;
		break;
	case UART_LSR:
	case UART_MSR:
		break;
	case UART_SCR:
	default:
		u.scr = value;
		break;
	}
}

/* The driver was switched off: the frame is on the bus, the board
 * answers after its turnaround time. A timed board starts its reply after
 * the clocks its program takes, at the clock of the MIE (the 315-6146 of
 * the 837-13741 sits beside the same two crystals); its first byte lands a
 * character later. */
void Mie::uart_commit_frame()
{
	MieUartState &u = m_uart;

	if (!m_jvs) {
		u.wire_count = 0;
		u.wire_pos = 0;
		u.wire_ticks = 0;
		return;
	}
	m_jvs->commit_encoded();
	const std::span<const uint8_t> reply = m_jvs->reply();
	u.wire_count = static_cast<uint32_t>(reply.size());
	u.wire_pos = 0;
	for (uint32_t i = 0; i < u.wire_count; i++) {
		u.wire[i] = reply[i];
	}
	const uint32_t cycles = m_jvs->reply_cycles();

	if (!u.wire_count)
		u.wire_ticks = 0;
	else if (cycles)
		u.wire_ticks = cycles * Mie::TICKS_PER_CYCLE + uart_byte_ticks();
	else
		u.wire_ticks = uart_byte_ticks() * JVS_TURNAROUND_BYTES;
}

void Mie::uart_tick(uint32_t cycles)
{
	MieUartState &u = m_uart;
	const uint32_t byte_ticks = uart_byte_ticks();
	const uint64_t ticks = static_cast<uint64_t>(cycles) * Mie::TICKS_PER_CYCLE;

	/* Transmitter: shift register then FIFO. */
	uint64_t left = ticks;
	while (left) {
		if (u.tx_ticks) {
			const uint32_t step = u.tx_ticks < left ? u.tx_ticks : static_cast<uint32_t>(left);
			u.tx_ticks -= step;
			left -= step;
			if (u.tx_ticks == 0 && m_jvs) {
				m_jvs->push(u.tx_byte);
			}
		} else if (u.tx_count) {
			u.tx_byte = u.tx_fifo[0];
			for (uint8_t i = 1; i < u.tx_count; i++) {
				u.tx_fifo[i - 1] = u.tx_fifo[i];
			}
			u.tx_count--;
			u.tx_ticks = byte_ticks;
			if (u.tx_count == 0) {
				u.thre_irq = 1;
			}
			if (m_line)
				m_line->line_sent(u.tx_byte, m_run_ticks + (ticks - left));
		} else {
			break;
		}
	}

	/* Receiver: bytes land one character time apart. */
	left = ticks;
	while (left && u.wire_pos < u.wire_count) {
		const uint32_t step = u.wire_ticks < left ? u.wire_ticks : static_cast<uint32_t>(left);
		u.wire_ticks -= step;
		left -= step;
		if (u.wire_ticks == 0) {
			rx_push(u, u.wire[u.wire_pos]);
			u.wire_pos++;
			u.wire_ticks = byte_ticks;
		}
	}
}

/* Serial adapter block. An exchange starts on the write to 0x09, runs
 * for a while with the busy bits set, then leaves its two reply bytes
 * in 0x0B and 0x0C with a ready bit; reading 0x0C clears it. */

void Mie::serial_reset()
{
	m_serial.data = 0;
	m_serial.control = 0;
	m_serial.reply_control = 0;
	m_serial.reply_data = 0;
	m_serial.status = 0;
	m_serial.op = SERIAL_OP_NONE;
	m_serial.busy_cycles = 0;
}

uint8_t Mie::serial_read(uint8_t port)
{
	MieSerialState &s = m_serial;
	switch (port) {
	case PORT_SERIAL_REPLY_CONTROL:
		return s.reply_control;
	case PORT_SERIAL_REPLY_DATA: {
		const uint8_t v = s.reply_data;
		s.status = static_cast<uint8_t>(s.status & ~(SERIAL_STATUS_READY | SERIAL_STATUS_LOOP_READY));
		return v;
	}
	case PORT_SERIAL_STATUS:
		return s.status;
	default:
		break;
	}
	log_once(m_port_logged[port], 'i', "port", port, 0xFF);
	return 0xFF;
}

void Mie::serial_write(uint8_t port, uint8_t value)
{
	MieSerialState &s = m_serial;
	if (port == PORT_SERIAL_DATA) {
		s.data = value;
		return;
	}
	if (port != PORT_SERIAL_CONTROL) {
		/* The ROM writes zeros to 0x0D and 0x0E at power-up. */
		serial_reset();
		return;
	}
	s.control = value;
	s.status = static_cast<uint8_t>(s.status & ~(SERIAL_STATUS_READY | SERIAL_STATUS_LOOP_READY));
	switch (value) {
	case SERIAL_CTRL_SEND:
		s.op = SERIAL_OP_SEND;
		s.busy_cycles = SERIAL_SEND_CYCLES;
		break;
	case SERIAL_CTRL_REQUEST:
	case SERIAL_CTRL_REQUEST + 1:
		if (s.data != SERIAL_REQ_STATUS && s.data != SERIAL_REQ_TAKE) {
			log_once(m_port_logged[256 + port], 'o', "serial request", port, s.data);
			return;
		}
		s.op = SERIAL_OP_REQUEST;
		s.busy_cycles = SERIAL_REQUEST_CYCLES;
		break;
	case SERIAL_CTRL_LOOPBACK:
		s.op = SERIAL_OP_LOOPBACK;
		s.busy_cycles = SERIAL_REQUEST_CYCLES;
		break;
	default:
		log_once(m_port_logged[256 + port], 'o', "serial control", port, value);
		return;
	}
	s.status |= SERIAL_STATUS_BUSY;
}

void Mie::serial_complete()
{
	MieSerialState &s = m_serial;
	switch (s.op) {
	case SERIAL_OP_SEND:
		if (m_serial_device) {
			m_serial_device->receive(s.data);
		}
		s.reply_control = s.control;
		s.reply_data = s.data;
		s.status |= SERIAL_STATUS_READY;
		break;
	case SERIAL_OP_REQUEST: {
		const bool waiting = m_serial_device && m_serial_device->has_byte();
		s.reply_control = s.control;
		if (s.data == SERIAL_REQ_STATUS) {
			s.reply_data = waiting ? SERIAL_RX_WAITING : 0;
		} else {
			s.reply_data = waiting ? m_serial_device->take_byte() : 0;
		}
		s.status |= SERIAL_STATUS_READY;
		break;
	}
	case SERIAL_OP_LOOPBACK:
		s.reply_control = s.control;
		s.reply_data = s.data;
		s.status |= SERIAL_STATUS_LOOP_READY;
		break;
	default:
		break;
	}
	s.status = static_cast<uint8_t>(s.status & ~SERIAL_STATUS_BUSY);
	s.op = SERIAL_OP_NONE;
}

void Mie::serial_tick(uint32_t cycles)
{
	MieSerialState &s = m_serial;
	if (s.op == SERIAL_OP_NONE) {
		return;
	}
	if (s.busy_cycles > cycles) {
		s.busy_cycles -= cycles;
		return;
	}
	s.busy_cycles = 0;
	serial_complete();
}

void Mie::tick(uint32_t cycles)
{
	uart_tick(cycles);
	serial_tick(cycles);
	m_run_ticks += static_cast<uint64_t>(cycles) * Mie::TICKS_PER_CYCLE;
}

void Mie::line_received(uint8_t byte)
{
	rx_push(m_uart, byte);
}

/* Loops that only wait */

namespace {

/* What an instruction of a turn reads and writes, for those a waiting loop
 * is made of: registers by their number in the opcodes (A 7, bit 6 none),
 * flags as in F. An instruction not in this list is not known. */
struct Effect {
	bool known = false;
	uint8_t reads = 0, writes = 0;
	uint8_t flags_read = 0, flags_written = 0;
	int dec = -1;		/* DEC r */
	bool djnz = false;
	bool z_jump = false;	/* a jump on Z or on NZ */
};

constexpr uint8_t FLAG_S = 0x80, FLAG_Z = 0x40, FLAG_PV = 0x04, FLAG_C = 0x01;

Effect effect(uint8_t op, uint8_t next)
{
	const auto reg = [](unsigned r) { return static_cast<uint8_t>(1u << r); };
	constexpr uint8_t A = 0x80;
	Effect e;

	if (op == 0x00 || op == 0x18 || op == 0xC3) {
		e.known = true;
	} else if ((op & 0xC6) == 0x04 && ((op >> 3) & 7) != 6) {
		/* INC r, DEC r */
		e.known = true;
		e.reads = e.writes = reg((op >> 3) & 7);
		e.flags_written = 0xFE;
		if (op & 1) {
			e.dec = (op >> 3) & 7;
		}
	} else if (op == 0x10) {
		e.known = e.djnz = true;
		e.reads = e.writes = reg(0);
	} else if ((op & 0xE7) == 0x20 || (op & 0xC7) == 0xC2) {
		/* JR cc, JP cc */
		static constexpr uint8_t FLAG_OF[8] = { FLAG_Z, FLAG_Z, FLAG_C, FLAG_C, FLAG_PV, FLAG_PV, FLAG_S, FLAG_S };
		const unsigned cc = (op & 0xE7) == 0x20 ? (op >> 3) & 3 : (op >> 3) & 7;

		e.known = true;
		e.flags_read = FLAG_OF[cc];
		e.z_jump = cc < 2;
	} else if (op == 0xDB) {
		/* IN A,(n): A on the high half of the address */
		e.known = true;
		e.reads = e.writes = A;
	} else if (op == 0x3A) {
		e.known = true;
		e.writes = A;
	} else if (op >= 0x40 && op < 0x80 && op != 0x76 && ((op >> 3) & 7) != 6 && (op & 7) != 6) {
		e.known = true;
		e.reads = reg(op & 7);
		e.writes = reg((op >> 3) & 7);
	} else if ((op >= 0x80 && op < 0xC0 && (op & 7) != 6) || (op & 0xC7) == 0xC6) {
		/* ADD ADC SUB SBC AND XOR OR CP, of r or n */
		const unsigned alu = (op >> 3) & 7;

		e.known = true;
		e.reads = static_cast<uint8_t>(A | (op < 0xC0 ? reg(op & 7) : 0));
		e.writes = alu == 7 ? 0 : A;
		e.flags_read = alu == 1 || alu == 3 ? FLAG_C : 0;
		e.flags_written = 0xFF;
	} else if (op == 0xCB && next >= 0x40 && next < 0x80 && (next & 7) != 6) {
		/* BIT b,r */
		e.known = true;
		e.reads = reg(next & 7);
		e.flags_written = 0xFE;
	}
	return e;
}

uint8_t &reg_of(Z80State &s, unsigned r)
{
	switch (r) {
	case 0: return s.b;
	case 1: return s.c;
	case 2: return s.d;
	case 3: return s.e;
	case 4: return s.h;
	default: return s.l;
	}
}

} /* namespace */

/* The turn from the mark does the same for any value of register `reg` that it
 * does not bring to 0: it reads it only in one DEC of it (or a DJNZ of B), a
 * jump on Z right after, no other flag of that DEC read before it is written
 * again, all of them written again before the DEC comes round; and only
 * instructions known to do no more than Effect tells. 1 if so, 0 if so but the
 * mark is where the flags still hold what the DEC made of the count (then
 * `dec_pc`, a place for it), -1 if not. */
int Mie::counts_down(unsigned reg, uint16_t &dec_pc)
{
	const uint8_t bit = static_cast<uint8_t>(1u << reg);
	const unsigned steps = m_wait.steps;
	const auto pc_of = [this](unsigned n) { return n ? m_proof.cpu[n].pc : m_wait.regs.pc; };
	unsigned dec = steps;

	for (unsigned n = 0; n < steps && dec == steps; n++) {
		const Effect e = effect(read8(pc_of(n)), read8(static_cast<uint16_t>(pc_of(n) + 1)));

		if (e.dec == static_cast<int>(reg) || (e.djnz && reg == 0)) {
			dec = n;
		}
	}
	if (dec == steps) {
		return -1;
	}

	/* Round the loop from the DEC */
	uint8_t tainted = 0;
	unsigned clean = dec;
	bool z_next = false;

	for (unsigned k = 0; k < steps; k++) {
		const unsigned n = (dec + k) % steps;
		const Effect e = effect(read8(pc_of(n)), read8(static_cast<uint16_t>(pc_of(n) + 1)));

		if (!e.known) {
			return -1;
		}
		if (k == 0) {
			if (!e.djnz) {
				tainted = e.flags_written;
				z_next = true;
			}
		} else if (z_next) {
			if (!e.z_jump) {
				return -1;
			}
			z_next = false;
		} else if ((e.reads & bit) || (e.writes & bit) || (e.flags_read & tainted)) {
			return -1;
		} else if (tainted) {
			tainted &= static_cast<uint8_t>(~e.flags_written);
			clean = n;
		}
	}
	if (z_next || tainted) {
		return -1;
	}
	/* The mark is before instruction 0: the flags there come from the DEC
	 * if 0 is after it and not after the instruction that writes them
	 * again */
	const unsigned at = (steps - dec) % steps, upto = (clean + steps - dec) % steps;

	if (at != 0 && at <= upto) {
		dec_pc = pc_of(dec);
		return 0;
	}
	return 1;
}

void Mie::wait_mark(uint32_t done)
{
	m_cpu.save(m_wait.regs);
	m_wait.done = done;
	m_wait.countdown = m_wait.period;
	m_wait.touched = false;
	m_wait.steps = 0;
	m_wait.lsr = false;
	m_wait.lsr_at = uart_lsr();
	m_wait.mark_at = -1;
	m_cpu.forget_refresh_read();
}

/* The whole turns of `turn` cycles from `done` that end before the call
 * does, and before UART 1 can change what a turn that reads it reads */
uint32_t Mie::turns_before_end(uint32_t done, uint32_t cycles, uint32_t turn) const
{
	const uint32_t end = m_wait.lsr ? uart_end(done, cycles) : cycles;

	return end > done ? (end - done - 1) / turn : 0;
}

/* Back at the mark with the registers it had, R aside, and nothing written,
 * no port used and R not read since: only the memory was read, which nothing
 * changes before the end of the call, so every further turn is the same. The whole
 * turns that end before the call does are counted without being run: R gets
 * the instructions they fetch, the rest of the chip their cycles at once
 * (tick() of a sum is tick() of each part). Returns their cycles. A loop
 * that works is looked at less and less often. */
uint32_t Mie::wait_check(uint32_t done, uint32_t cycles)
{
	const uint32_t turn = done - m_wait.done;
	uint32_t counted = 0;
	Z80State now;

	if (turn == 0) {
		return 0;
	}
	if (m_wait.pass > 0) {
		m_wait.pass--;
		return 0;
	}
	m_cpu.save(now);
	const uint32_t refresh = (now.r - m_wait.regs.r) & 0x7Fu;
	now.r = static_cast<uint8_t>((now.r & 0x80) | (m_wait.regs.r & 0x7F));
	const bool clean = !m_wait.touched && !m_cpu.refresh_read() && !(m_wait.lsr && uart_lsr() != m_wait.lsr_at);
	int count = -1, counting = -1;
	uint16_t dec_pc = 0;

	for (unsigned r = 0; clean && r < 6 && !(now == m_wait.regs); r++) {
		Z80State other = now;

		reg_of(other, r) = reg_of(m_wait.regs, r);
		if (other == m_wait.regs) {
			count = static_cast<int>(r);
		}
	}
	if (clean && now == m_wait.regs) {
		if (m_wait.steps <= MAX_TURN) {
			m_proof.on = true;
			m_proof.length = m_wait.steps;
			m_proof.at = 0;
			m_proof.period = turn;
			m_proof.refresh = refresh;
			m_proof.lsr = m_wait.lsr;
			m_proof.lsr_at = m_wait.lsr_at;
			m_proof.cpu[0] = m_wait.regs;
			return bill(done, cycles);
		}
		const uint32_t turns = turns_before_end(done, cycles, turn);

		counted = turns * turn;
		tick(counted);
		m_cpu.add_refresh(turns * refresh);
		m_wait_cycles += counted;
	} else if (count >= 0 && m_wait.steps <= MAX_TURN &&
	           (counting = counts_down(static_cast<unsigned>(count), dec_pc)) == 1) {
		/* A loop that counts down, by one a turn: whole turns, while its
		 * DEC does not give 0 */
		const unsigned r = static_cast<unsigned>(count);
		const uint32_t turns = std::min<uint32_t>(static_cast<uint8_t>(reg_of(now, r) - 1),
		                                          turns_before_end(done, cycles, turn));
		Z80State regs;

		counted = turns * turn;
		m_cpu.save(regs);
		reg_of(regs, r) = static_cast<uint8_t>(reg_of(regs, r) - turns);
		m_cpu.load(regs);
		m_cpu.add_refresh(turns * refresh);
		tick(counted);
		m_wait_cycles += counted;
	} else if (counting != 0) {
		if (m_wait.busy < 6) {
			m_wait.busy++;
		}
		m_wait.pass = (1u << m_wait.busy) - 1;
	}
	wait_mark(done + counted);
	if (counting == 0) {
		m_wait.mark_at = dec_pc;
	}
	return counted;
}

/* A call that starts on a turn proven to wait: it still is if the CPU is
 * where the turn has it, R aside, and UART 1 as the turn read it (a byte
 * from the line, the host at its ports). A write to the memory would have
 * dropped it. */
uint32_t Mie::resume(uint32_t cycles)
{
	const Z80State &then = m_proof.cpu[m_proof.at];
	Z80State now;

	m_cpu.save(now);
	now.r = static_cast<uint8_t>((now.r & 0x80) | (then.r & 0x7F));
	if (!(now == then) || (m_proof.lsr && uart_lsr() != m_proof.lsr_at)) {
		m_proof.on = false;
		return 0;
	}
	return bill(0, cycles);
}

/* The CPU before an instruction of a turn proven to wait goes round the
 * same way to the end of the call. The instructions it would run before
 * that are counted, the CPU put in the state it would have after them, R
 * advanced by their M1 cycles; the one after is run and ends the call, as
 * when every turn is run. The clocks counted. */
uint32_t Mie::bill(uint32_t done, uint32_t cycles)
{
	const uint32_t end = m_proof.lsr ? uart_end(done, cycles) : cycles;

	if (done >= end) {
		if (end < cycles) {
			m_proof.on = false;
			wait_mark(done);
		}
		return 0;
	}

	const uint64_t from = m_proof.offset[m_proof.at];
	const uint64_t last = from + (end - done - 1);
	const uint64_t into = last % m_proof.period;
	unsigned to = m_proof.length - 1;

	while (m_proof.offset[to] > into) {
		to--;
	}

	const uint32_t billed = static_cast<uint32_t>(last - into + m_proof.offset[to] - from);

	if (billed) {
		const uint8_t r0 = m_proof.cpu[0].r;
		const uint32_t m1 = static_cast<uint32_t>((last - into) / m_proof.period) * m_proof.refresh +
		                    ((m_proof.cpu[to].r - r0) & 0x7Fu) - ((m_proof.cpu[m_proof.at].r - r0) & 0x7Fu);
		Z80State now, land = m_proof.cpu[to];

		m_cpu.save(now);
		land.r = static_cast<uint8_t>((now.r & 0x80) | ((now.r + m1) & 0x7F));
		m_cpu.load(land);
		m_proof.at = to;
		tick(billed);
		m_wait_cycles += billed;
	}
	/* UART 1 changes next: the search starts again there */
	if (end < cycles) {
		m_proof.on = false;
		wait_mark(done + billed);
	}
	return billed;
}

/* The end of a call for a turn that reads the state of UART 1: the
 * instructions billed start before it can change, at the end of the byte
 * being sent (TEMT, or THRE as the next one leaves the FIFO) or the
 * landing of a byte of the reply (DR). An instruction sees what the ticks
 * of those before it did. */
uint32_t Mie::uart_end(uint32_t done, uint32_t cycles) const
{
	const MieUartState &u = m_uart;
	uint64_t ticks = UINT64_MAX;

	if (u.tx_ticks) {
		ticks = u.tx_ticks;
	} else if (u.tx_count) {
		ticks = 0;
	}
	if (u.wire_pos < u.wire_count) {
		ticks = std::min<uint64_t>(ticks, u.wire_ticks);
	}
	if (ticks == UINT64_MAX) {
		return cycles;
	}
	return static_cast<uint32_t>(std::min<uint64_t>(cycles, done + ticks / Mie::TICKS_PER_CYCLE + 1));
}

uint32_t Mie::run(uint32_t cycles)
{
	const bool waits = m_waits_counted && !m_trace;
	uint32_t done = 0;

	m_run_ticks = 0;
	m_wait.period = 4;
	m_wait.pass = 0;
	m_wait.busy = 0;
	wait_mark(0);
	if (waits && m_proof.on && !busak()) {
		done += resume(cycles);
	}
	while (done < cycles) {
		if (busak()) {
			/* Bus released to the host: the CPU waits, the rest of
			 * the chip keeps going. */
			tick(cycles - done);
			return cycles;
		}
		/* A program that lets an interrupt through is run turn by
		 * turn (writing port 30 drops a proof) */
		if (waits && !m_proof.on && !(m_irq_enable & IRQ_SOURCES)) {
			if (m_cpu.pc() == m_wait.regs.pc) {
				done += wait_check(done, cycles);
			} else if (m_cpu.pc() == m_wait.mark_at) {
				wait_mark(done);
			} else if (--m_wait.countdown == 0) {
				if (m_wait.period < 1024) {
					m_wait.period *= 2;
				}
				wait_mark(done);
			}
			if (!m_proof.on && m_wait.steps && m_wait.steps < MAX_TURN) {
				m_proof.offset[m_wait.steps] = done - m_wait.done;
				m_cpu.save(m_proof.cpu[m_wait.steps]);
			}
		}
		if (m_trace_armed && m_cpu.pc() == m_trace_start) {
			m_trace_armed = false;
			m_cpu.set_trace(m_trace);
		}
		m_cpu.set_int((m_irq_enable & IRQ_SOURCES) && irq_line());
		const uint32_t c = m_cpu.step();
		if (m_trace_left && !m_trace_armed && --m_trace_left == 0) {
			m_cpu.set_trace(nullptr);
		}
		tick(c);
		done += c;
		if (m_proof.on) {
			m_proof.at = m_proof.at + 1 == m_proof.length ? 0 : m_proof.at + 1;
		} else {
			m_wait.steps++;
		}
	}
	return done;
}

/* Each kind of unknown access is told once */
void Mie::log_once(bool &said, char rw, const char *space, uint32_t addr, uint32_t value)
{
	if (!said) {
		said = true;
		log_unknown(rw, space, addr, value);
	}
}

void Mie::log_unknown(char rw, const char *space, uint32_t addr, uint32_t value)
{
	if (!m_log) {
		return;
	}
	static const char KIND[] = "unknown";
	const std::string line = std::format("{}: unknown {} {} {:04x} pc {:04x} value {:02x}\n",
	                                     m_external ? "i/o board" : "mie", space, rw, addr, m_cpu.pc(), value);

	repeats::put(m_log, KIND, line.c_str());
}

void Mie::save(MieState &out) const
{
	m_cpu.save(out.cpu);
	out.ram_low = m_ram_low;
	out.maple = m_maple;
	out.gpio_val = m_gpio_val;
	out.gpio_dir = m_gpio_dir;
	out.irq_enable = m_irq_enable;
	out.irq_pending = m_irq_pending;
	out.maple_irqlevel = m_maple_irqlevel;
	out.jvs_control = m_jvs_control;
	out.busrq = m_busrq ? 1 : 0;
	out.uart = m_uart;
	out.serial = m_serial;
	out.adc_channel = m_adc_channel;
	out.external_irq = m_external_irq ? 1 : 0;
}

void Mie::load(const MieState &in)
{
	m_proof.on = false;
	m_cpu.load(in.cpu);
	m_ram_low = in.ram_low;
	m_maple = in.maple;
	m_gpio_val = in.gpio_val;
	m_gpio_dir = in.gpio_dir;
	m_irq_enable = in.irq_enable;
	m_irq_pending = in.irq_pending;
	m_maple_irqlevel = in.maple_irqlevel;
	m_jvs_control = in.jvs_control;
	m_busrq = in.busrq != 0;
	m_uart = in.uart;
	m_serial = in.serial;
	m_adc_channel = in.adc_channel;
	m_external_irq = in.external_irq != 0;
}

} /* namespace odin */
