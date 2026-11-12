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

#include "odin/machine/boards/hikaru/sound_board.hpp"

#include <utility>

#include "odin/common/clock_ratio.hpp"

namespace odin {

namespace {

/* Where the ARM7 has the registers */
constexpr uint32_t ARM_REGS = 0x800000;
constexpr uint32_t ARM_REGS_END = 0x808000;

/* The page of the board on the bus of the main processor */
constexpr uint32_t PAGE_REGS = 0x700000;
constexpr uint32_t PAGE_REGS_END = 0x708000;
constexpr uint32_t PAGE_RTC = 0x710000;
constexpr uint32_t PAGE_RTC_END = 0x710010;
constexpr uint32_t PAGE_RAM = 0x800000;
/* The pages of the wave memory the ARM7 watches */
constexpr uint32_t WATCH_PAGE_SIZE = 1u << Arm7::WATCH_PAGE;

static_assert(WaveMemory::PAGE_BITS == Arm7::WATCH_PAGE, "the chip and the ARM7 watch the same pages");

} /* namespace */

SoundBoard::SoundBoard(std::span<uint8_t> ram, uint32_t clock_hz, std::function<void(bool)> main_irq)
	: m_main_irq(std::move(main_irq)), m_clock_hz(clock_hz), m_sample_acc(0), m_silence_acc(0),
	  m_arm_held(true), m_arm_ahead(0), m_ram(ram.data()), m_arm(*this), m_watcher(m_arm), m_aica(*this, ram), m_source(nullptr)
{
	map_ram();
	/* The core looks at the registers again through peek(), which changes
	 * nothing there. */
	m_arm.set_turn_replay(true);
}

void
SoundBoard::reset()
{
	m_arm.settle();
	m_aica.reset();
}

void
SoundBoard::reset_pin()
{
	m_arm.settle();
	m_aica.reset(false);
}

void
SoundBoard::arm_reset(bool level)
{
	if (level) {
		m_arm_held = true;
	} else if (m_arm_held) {
		m_arm_held = false;
		m_arm_ahead = 0;
		m_arm.reset();
	}
}

void
SoundBoard::main_irq(bool level)
{
	if (m_main_irq)
		m_main_irq(level);
}

bool
SoundBoard::host_read(uint32_t offset, unsigned size, uint64_t &value)
{
	if (offset >= PAGE_RAM) {
		m_arm.ram_read(offset - PAGE_RAM, size);
		value = m_aica.read_ram(offset - PAGE_RAM, size);
	} else if (offset >= PAGE_REGS && offset < PAGE_REGS_END) {
		value = m_aica.read_reg(offset - PAGE_REGS, size > 4 ? 4 : size);
	} else if (offset >= PAGE_RTC && offset < PAGE_RTC_END) {
		value = m_aica.read_rtc(offset - PAGE_RTC);
	} else {
		return false;
	}
	return true;
}

SoundBoard::Written
SoundBoard::host_write(uint32_t offset, unsigned size, uint64_t value)
{
	if (offset >= PAGE_RAM) {
		m_arm.ram_write(offset - PAGE_RAM, size);
		m_aica.write_ram(offset - PAGE_RAM, size, value);
	} else if (offset >= PAGE_REGS && offset < PAGE_REGS_END) {
		/* A register may move the memory, the lines or the ARM7. */
		m_arm.settle();
		m_aica.write_reg(offset - PAGE_REGS, size > 4 ? 4 : size, static_cast<uint32_t>(value));
	} else if (offset >= PAGE_RTC && offset < PAGE_RTC_END) {
		m_aica.write_rtc(offset - PAGE_RTC, static_cast<uint32_t>(value));
		return m_aica.take_rtc_set() ? Written::CLOCK_SET : Written::DONE;
	} else {
		return Written::NOWHERE;
	}
	return Written::DONE;
}

std::span<const uint8_t>
SoundBoard::host_ram_read(uint32_t offset)
{
	if (offset < PAGE_RAM)
		return {};

	const uint32_t addr = offset - PAGE_RAM, size = WATCH_PAGE_SIZE - (addr & (WATCH_PAGE_SIZE - 1));

	m_arm.ram_read(addr, size);
	return std::span<const uint8_t>(m_ram + (addr & m_aica.ram_mask()), size);
}

std::span<uint8_t>
SoundBoard::host_ram_write(uint32_t offset)
{
	if (offset < PAGE_RAM)
		return {};

	const uint32_t addr = offset - PAGE_RAM, size = WATCH_PAGE_SIZE - (addr & (WATCH_PAGE_SIZE - 1));

	m_arm.ram_write(addr, size);
	return std::span<uint8_t>(m_ram + (addr & m_aica.ram_mask()), size);
}

void
SoundBoard::run(uint32_t cycles)
{
	const uint64_t samples = take_ticks(m_sample_acc, cycles, Aica::SAMPLE_RATE, m_clock_hz);

	for (uint64_t s = 0; s < samples; s++) {
		if (m_source) {
			const std::vector<int16_t> &in = m_source->m_samples;
			const size_t at = m_samples.size();

			if (at + 1 < in.size())
				m_aica.set_external(in[at], in[at + 1]);
			else
				m_aica.set_external(0, 0);
		}
		m_aica.watch({ &m_watcher, m_arm.watched_reads(), m_arm.watched_writes() });
		m_aica.tick(1);
		m_samples.push_back(m_aica.left());
		m_samples.push_back(m_aica.right());
		if (!m_arm_held) {
			const uint32_t slice = ARM_CYCLES_PER_SAMPLE - m_arm_ahead;

			map_ram();
			m_arm_ahead = m_arm.run(slice) - slice;
		}
	}
}

/* Its own count of the time, so that the sample periods of the chip fall
 * where they would without it. */
void
SoundBoard::silence(uint32_t cycles)
{
	const uint64_t samples = take_ticks(m_silence_acc, cycles, Aica::SAMPLE_RATE, m_clock_hz);

	m_samples.insert(m_samples.end(), 2 * samples, 0);
}

void
SoundBoard::save(State &out)
{
	m_arm.settle();
	out.sample_acc = m_sample_acc;
	out.arm_ahead = m_arm_ahead;
	m_arm.save(out.arm);
	m_aica.save(out.aica);
	out.arm_held = m_arm_held ? 1 : 0;
}

/* The chip first: it drives the reset pin of the ARM7, which the saved
 * core then replaces. */
void
SoundBoard::load(const State &in)
{
	m_aica.load(in.aica);
	m_sample_acc = in.sample_acc;
	m_arm_ahead = in.arm_ahead;
	m_arm_held = in.arm_held != 0;
	m_arm.load(in.arm);
}

uint32_t
SoundBoard::read(uint32_t addr, uint32_t size)
{
	if (addr < ARM_REGS)
		return static_cast<uint32_t>(m_aica.read_ram(addr, size));
	if (addr < ARM_REGS_END)
		return m_aica.read_reg(addr - ARM_REGS, size);
	return 0xFFFFFFFFu;
}

uint32_t
SoundBoard::peek(uint32_t addr, uint32_t size)
{
	if (addr < ARM_REGS)
		return static_cast<uint32_t>(m_aica.read_ram(addr, size));
	if (addr < ARM_REGS_END)
		return m_aica.peek_reg(addr - ARM_REGS, size);
	return 0xFFFFFFFFu;
}

void
SoundBoard::write(uint32_t addr, uint32_t size, uint32_t value)
{
	if (addr < ARM_REGS) {
		m_aica.write_ram(addr, size, value);
	} else if (addr < ARM_REGS_END) {
		m_aica.write_reg(addr - ARM_REGS, size, value);
		map_ram();
	}
}

/* The wave memory, as the ARM7 reads it without the bus: a register sets
 * how much of it is decoded. */
void
SoundBoard::map_ram()
{
	m_arm.set_ram({m_ram, ARM_REGS, m_aica.ram_mask()});
}

} /* namespace odin */
