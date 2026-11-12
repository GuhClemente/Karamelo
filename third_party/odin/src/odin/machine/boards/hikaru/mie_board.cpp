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

#include "odin/machine/boards/hikaru/mie_board.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "odin/common/clock_ratio.hpp"
#include "odin/machine/cabinet/io_837_13551_imitation.hpp"
#include "odin/machine/cabinet/io_837_13741_imitation.hpp"

namespace odin {

namespace {

/* The ports of the MIE wired on the board: its interrupt requests, port D
 * (the resets of the main board), the pins of the 93C46, the switches of
 * the board (Test, Service, the fans) */
enum : uint8_t { PORT_IRQ = 1, PORT_RESETS = 3, PORT_EEPROM = 5, PORT_SWITCHES = 6 };

/* The window of the board on the bus of the main processor */
constexpr uint32_t PAGE_MEM = 0x20000;
constexpr uint32_t PAGE_END = 0x40000;

std::array<uint8_t, Mie::ROM_SIZE>
copy_rom(std::span<const uint8_t> rom)
{
	std::array<uint8_t, Mie::ROM_SIZE> out;

	std::memcpy(out.data(), rom.data(), out.size());
	return out;
}

} /* namespace */

bool
MieBoard::knows_io_board(const std::string &name)
{
	return name == "837-13551" || name == "837-13741";
}

MieBoard::MieBoard(std::span<const uint8_t> rom, std::span<uint8_t> ram, uint32_t clock_hz,
                   std::function<void(Inputs &)> read_inputs, const std::string &io_board,
                   std::span<const uint8_t> io_rom)
	: m_read_inputs(std::move(read_inputs)), m_clock_hz(clock_hz), m_cycle_acc(0), m_mie_ahead(0), m_z80_time(0),
	  m_rom(copy_rom(rom)), m_inputs{},
	  m_eeprom(Mie::CLOCK_HZ), m_mie(*this, m_rom, ram), m_log(nullptr),
	  m_irq_lines(0)
{
	if (io_board == "837-13741")
		m_io = std::make_unique<Io837_13741Imitation>(*this);
	else
		m_io = std::make_unique<Io837_13551Imitation>(*this);

	const char *const io = std::getenv("ODIN_IO");
	const bool imitation = io && !std::strcmp(io, "imitation");

	if (io_board == "837-13551" && io_rom.size() == Tmp90ph44::ROM_SIZE && !imitation) {
		m_line_13551 = std::make_unique<Io837_13551>(io_rom, *this);
		m_line = m_line_13551.get();
	} else if (io_board == "837-13741" && io_rom.size() == Io837_13741::EPROM_SIZE && !imitation) {
		m_line_13741 = std::make_unique<Io837_13741>(io_rom, *this);
		m_line = m_line_13741.get();
	}
	if (m_line) {
		m_mie.attach_line(m_line);
	} else {
		m_jvs.add_device(m_io.get());
		m_mie.attach_jvs(&m_jvs);
	}
}

uint8_t
MieBoard::gpio_read(uint8_t port)
{
	switch (port) {
	case PORT_SWITCHES: {
		/* Fan sensors (4-6) are not populated, read high. Bit 7 is
		 * the GPU JTAG chain output, idle high. The game treats bit 3
		 * as Test and bit 2 as Service (BOOTROM credit and test-menu
		 * code); MAME labels them the other way round. */
		poll_inputs();
		uint8_t v = 0xF2;
		if (m_eeprom.do_read())
			v |= 0x01;
		if (!m_inputs.mainboard_service)
			v |= 0x04;
		if (!m_inputs.mainboard_test)
			v |= 0x08;
		return v;
	}
	case PORT_IRQ:
		return irq_pending();
	default:
		/* Unpopulated switches and unused inputs read high. */
		return 0xFF;
	}
}

void
MieBoard::gpio_write(uint8_t port, uint8_t value)
{
	if (port == PORT_EEPROM)
		m_eeprom.set_pins((value & 0x08) != 0, (value & 0x01) != 0,
		                  (value & 0x02) != 0, (value & 0x04) != 0);
	if (port == PORT_RESETS && m_port_d)
		m_port_d(value);
}

uint8_t
MieBoard::system()
{
	poll_inputs();
	return m_inputs.jvs_system;
}

uint16_t
MieBoard::player(uint8_t index)
{
	return index < 2 ? m_inputs.jvs_player[index] : 0;
}

uint16_t
MieBoard::analog(uint8_t channel)
{
	return channel < 8 ? m_inputs.jvs_analog[channel] : 0x8000;
}

void
MieBoard::reset()
{
	m_cycle_acc = 0;
	m_mie_ahead = 0;
	m_z80_time = 0;
	m_busrq_by = 0;
	m_eeprom.reset();
	m_io->reset();
	m_jvs.reset();
	m_mie.reset();
	if (m_line) {
		m_line->reset();
		m_mie.set_line_addressed(false);
	}
	if (m_card)
		m_card->reset();
}

void
MieBoard::run(uint32_t cycles)
{
	const uint64_t z80_cycles = take_ticks(m_cycle_acc, cycles, Mie::CLOCK_HZ, m_clock_hz);

	if (z80_cycles) {
		const uint32_t share = static_cast<uint32_t>(z80_cycles);

		/* The MIE ends its last instruction: what it ran past its
		 * share comes off the next one. */
		if (m_line)
			run_line(share);
		else if (share > m_mie_ahead)
			m_mie_ahead = m_mie.run(share - m_mie_ahead) - (share - m_mie_ahead);
		else
			m_mie_ahead -= share;
		m_z80_time += share;
		m_eeprom.tick(share);
	}
}

/* The chip of the I/O board a piece ahead of the MIE, then the MIE up to
 * where the chip is, stopped where the board has something for it: a byte
 * or the sense line, which it then sees after the instruction that holds
 * the time they come, as UART 1 shows them. */
void
MieBoard::run_line(uint32_t share)
{
	constexpr uint64_t TICKS = Mie::TICKS_PER_CYCLE;
	const uint64_t target = m_z80_time + share;
	uint64_t mie = m_z80_time + m_mie_ahead;

	while (mie < target) {
		if (m_line->now() < target * TICKS)
			m_line->run_until((mie + m_line_piece) * TICKS);

		const uint64_t end = std::min<uint64_t>(target, m_line->now() / TICKS);

		while (mie < end) {
			const uint64_t event = m_line->next_event();
			const uint64_t to = event == UINT64_MAX ? end : std::min(end, (event + TICKS - 1) / TICKS);

			if (to > mie) {
				m_line->set_run_start(mie * TICKS);
				mie += m_mie.run(static_cast<uint32_t>(to - mie));
			}
			m_line->deliver(m_mie, mie * TICKS);
		}
	}
	m_mie_ahead = static_cast<uint32_t>(mie - target);
}

/* Byte-wide registers and memory on even addresses (hikaru.cpp
 * localbus_map, umask16 0x00ff). The odd byte of the port page reads 0xFF:
 * the BOOTROM sign-extends the 16-bit read of port A and derives the Test
 * and Service bits from it; not measured on hardware. */
bool
MieBoard::host_read(uint32_t offset, unsigned size, uint32_t &value)
{
	if (offset >= PAGE_END || (size != 1 && size != 2))
		return false;
	const bool odd = (offset & 1) != 0;
	const uint32_t index = (offset & 0x3FFFF) >> 1;
	uint32_t v;
	if (offset < PAGE_MEM) {
		v = odd ? 0xFF : m_mie.host_read_port(static_cast<uint8_t>(index & 0xFF));
		if (size == 2)
			v |= 0xFF00;
	} else {
		v = odd ? 0x00 : m_mie.host_read_mem(static_cast<uint16_t>(index & 0xFFFF));
	}
	value = v;
	return true;
}

bool
MieBoard::host_write(uint32_t offset, unsigned size, uint32_t value)
{
	if (offset >= PAGE_END || (size != 1 && size != 2))
		return false;
	if (offset & 1)
		return true;
	const uint32_t index = (offset & 0x3FFFF) >> 1;
	const uint8_t byte = static_cast<uint8_t>(value);
	if (offset < PAGE_MEM)
		m_mie.host_write_port(static_cast<uint8_t>(index & 0xFF), byte);
	else
		m_mie.host_write_mem(static_cast<uint16_t>(index & 0xFFFF), byte);
	return true;
}

/* MAME hikaru.cpp:896: (!busak << 8) | 0x600. */
uint32_t
MieBoard::busrq_read()
{
	return (m_mie.host_busak_read() ? 0u : 0x100u) | 0x600u;
}

void
MieBoard::busrq_write(unsigned which, uint32_t value)
{
	const uint8_t bit = static_cast<uint8_t>(1u << (which & 1));

	m_busrq_by = static_cast<uint8_t>((value & 0x100) ? m_busrq_by | bit : m_busrq_by & ~bit);
	m_mie.set_busrq(m_busrq_by != 0);
}

uint8_t
MieBoard::port_d() const
{
	return m_mie.gpio_is_output(3) ? m_mie.gpio_latch(3) : 0xFF;
}

uint8_t
MieBoard::irq_pending() const
{
	const uint8_t own = m_mie.gpio_is_output(5) ? (m_mie.gpio_latch(5) >> 3) & 0x0E : 0;
	return static_cast<uint8_t>(m_irq_lines | own);
}

bool
MieBoard::irq_out() const
{
	return m_mie.gpio_is_output(2) && (irq_pending() & m_mie.gpio_latch(2)) != 0;
}

void
MieBoard::set_waits_counted(bool on)
{
	m_mie.set_waits_counted(on);
	if (m_line_13551)
		m_line_13551->chip().set_waits_counted(on);
	if (m_line_13741)
		m_line_13741->chip().set_waits_counted(on);
}

void
MieBoard::set_log(FILE *out)
{
	m_log = out;
	m_mie.set_log(out);
	if (m_line_13741)
		m_line_13741->set_log(out);
	if (m_card)
		m_card->set_log(out);
}

bool
MieBoard::attach_card(const char *card_file, FILE *trace)
{
	m_card = std::make_unique<CardReader>(card_file ? std::string(card_file) : std::string());
	m_card->set_log(m_log);
	m_card->set_trace(trace);
	m_mie.set_serial_device(m_card.get());
	return m_card->where() == CardReader::Where::IN_HAND;
}

void
MieBoard::save(State &out)
{
	out.cycle_acc = m_cycle_acc;
	out.mie_ahead = m_mie_ahead;
	out.z80_time = m_z80_time;
	out.has_line = line_kind();
	if (m_line_13551)
		m_line_13551->save(out.line);
	if (m_line_13741)
		m_line_13741->save(out.line_13741);
	m_eeprom.save(out.eeprom);
	m_io->save(out.io);
	m_jvs.save(out.jvs);
	m_mie.save(out.mie);
	out.has_card = m_card ? 1 : 0;
	if (m_card)
		m_card->save(out.card);
	out.busrq_by = m_busrq_by;
}

bool
MieBoard::load(const State &in)
{
	if ((in.has_card != 0) != (m_card != nullptr) || in.has_line != line_kind())
		return false;
	m_cycle_acc = in.cycle_acc;
	m_mie_ahead = in.mie_ahead;
	m_z80_time = in.z80_time;
	m_busrq_by = in.busrq_by;
	m_eeprom.load(in.eeprom);
	m_io->load(in.io);
	m_jvs.load(in.jvs);
	m_mie.load(in.mie);
	if (m_line_13551)
		m_line_13551->load(in.line, m_mie);
	if (m_line_13741)
		m_line_13741->load(in.line_13741, m_mie);
	if (m_card)
		m_card->load(in.card);
	return true;
}

void
MieBoard::poll_inputs()
{
	if (!m_read_inputs)
		return;
	m_read_inputs(m_inputs);
	for (uint8_t slot = 0; slot < 2; slot++) {
		if (m_inputs.jvs_coin[slot] && m_line)
			m_line->inc_coin(slot);
		else if (m_inputs.jvs_coin[slot])
			m_io->inc_coin(slot);
	}
}

} /* namespace odin */
