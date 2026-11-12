/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/boards/atvtrack/atvtrack.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "odin/common/bytes.hpp"
#include "odin/common/env.hpp"
#include "odin/machine/boards/atvtrack/atvtrack_fpga.hpp"

namespace odin {

namespace {

constexpr uint32_t SHARED = 0x00000000;
constexpr uint32_t CONTROL = 0x00020000, DAC = 0x00020040;
constexpr uint32_t NAND_DATA = 0x14000000, NAND_COMMAND = 0x14100000, NAND_ADDRESS = 0x14200000;
constexpr uint32_t CACHE_RAM = 8u << 10, CACHE_RAM_AT = 0x1C000000;
constexpr uint32_t FLASH_SECTOR = 64u << 10;
/* Smashing Drive */
constexpr uint32_t SMASH_SHARED = 0x10000000, SMASH_CONTROL = 0x10000400, SMASH_DAC = 0x10000440;
constexpr uint32_t SMASH_ROM = 64u << 20, SMASH_PROGRAM = 0x14000000, SMASH_PROGRAM_SIZE = 4u << 20;
constexpr uint32_t SDRAM = 0x0C000000, SDRAM_AREA = 0x04000000;
constexpr uint32_t GPU_REGS = 0x14000000, GPU_REGS_SIZE = 0x8000, GPU_RAM = 0x18000000;
constexpr uint32_t MASTER_RAM = 8u << 20, SLAVE_RAM = 16u << 20, GPU_RAM_SIZE = 32u << 20;

/* The PMX1's Thread Kicker n, at 0xB0 + 4n */
constexpr uint32_t THREAD_KICKER = 0xB0;
/* The 3D core's Start Render, Object Base, Region Header Base and Texture
 * Control, PVR_IRQ its interrupt */
constexpr uint32_t START_RENDER = 0x14;
constexpr uint32_t PVR_IRQ = 1u << 7;

/* 198 MHz: the 33.000 MHz crystal of the photos of the Smashing Drive and
 * ATV Track boards, times 6. INFERRED: the TMU1 the games set for a screen
 * frame then beats at 59.3910 Hz, and the PMX1's DAC, on a crystal of its
 * own, at 59.3916 Hz: the peripheral clock is the crystal's, which only
 * clock modes 1 and 3 give, both with the CPU at 6 times (hardware manual,
 * table 10.3). Of the two, mode 3 (MD2-MD0 = 011) has the bus at twice the
 * crystal. INFERRED: the start-up of the three games (Smashing Drive's at
 * 0x8C05C524, the FPGA's boot program of the other two) refreshes their
 * 64 Mbit SDRAM (MCR AMX 4) every 776 bus clocks, 11.8 us at 66 MHz, under
 * the 15.6 us such chips need; at 33 MHz it would be 23.5 us. */
constexpr int32_t FRAME_CYCLES = 198'000'000 / 60;
constexpr unsigned SLICES = AtvTrack::SLICES_PER_60TH;
/* The PMX1's core clock, as the slave program sets its PLL (PMX1ProgramCorePLL
 * called with 100 MHz) */
constexpr int32_t CORE_CYCLES = 100'000'000 / 60;

} /* namespace */


NandBank::NandBank(std::array<std::vector<uint8_t>, CHIPS> chips)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		m_chips[c].data = std::move(chips[c]);
		m_chips[c].data.resize(SIZE, 0xFF);
	}
}

/* Reads continue through the page's spare bytes into the next page, from the
 * area the command chose (K9F2808U0B datasheet, sequential row read) */
uint8_t NandBank::read(Chip &chip)
{
	switch (chip.command) {
	case 0x00:
	case 0x01:
	case 0x50: {
		const uint8_t value = chip.data[chip.at];

		chip.at++;
		if (chip.at % PAGE == 0) {
			const uint32_t page = chip.at / PAGE % PAGES;

			chip.at = page * PAGE + (chip.command == 0x50 ? 512 : 0);
		}
		return value;
	}
	case 0x90:
		/* Samsung, K9F2808U0B */
		return chip.id++ == 0 ? 0xEC : 0x73;
	default:
		/* Status: ready, not protected */
		return 0xC0;
	}
}

uint32_t NandBank::read_data()
{
	uint32_t lanes = 0;

	for (unsigned c = 0; c < CHIPS; c++)
		lanes |= static_cast<uint32_t>(read(m_chips[c])) << (8 * c);
	return lanes;
}

void NandBank::set_block(unsigned chip, uint32_t number, std::span<const uint8_t> bytes)
{
	std::copy(bytes.begin(), bytes.end(), m_chips[chip].data.begin() + number * BLOCK);
	m_changed.insert({ chip, number });
}

void NandBank::write_data(uint32_t lanes)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		Chip &chip = m_chips[c];

		if (chip.command == 0x80 && chip.at < SIZE) {
			/* Programming only clears bits */
			chip.data[chip.at] &= static_cast<uint8_t>(lanes >> (8 * c));
			m_changed.insert({ c, chip.at / BLOCK });
			chip.at++;
		}
	}
}

void NandBank::write_command(uint32_t lanes)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		Chip &chip = m_chips[c];
		const uint8_t command = static_cast<uint8_t>(lanes >> (8 * c));

		switch (command) {
		case 0x00:
		case 0x01:
		case 0x50:
			chip.area = command == 0x00 ? 0 : command == 0x01 ? 1 : 2;
			chip.command = command;
			chip.step = 0;
			break;
		case 0x80:
		case 0x60:
		case 0x90:
			chip.command = command;
			chip.step = 0;
			chip.id = 0;
			break;
		case 0x10:
		case 0x70:
			chip.command = 0x70;
			break;
		case 0xD0:
			if (chip.command == 0x60) {
				const uint32_t block = chip.row / 32 % 1024;

				std::fill_n(chip.data.begin() + block * 32 * PAGE, 32 * PAGE, uint8_t{0xFF});
				m_changed.insert({ c, block });
			}
			chip.command = 0x70;
			break;
		default:
			chip.command = 0xFF;
			chip.area = 0;
			break;
		}
	}
}

/* One column byte, then two row bytes; an erase sends the row only */
void NandBank::write_address(uint32_t lanes)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		Chip &chip = m_chips[c];
		const uint32_t byte = (lanes >> (8 * c)) & 0xFF;
		const bool row_only = chip.command == 0x60;

		if (!row_only && chip.step == 0) {
			chip.column = byte;
			chip.row = 0;
		} else {
			const uint32_t shift = (chip.step - (row_only ? 0 : 1)) * 8;

			chip.row = (chip.row & ~(0xFFu << shift)) | (byte << shift);
		}
		chip.step++;
		/* Area C reads the 16 spare bytes */
		const uint32_t base = chip.area == 1 ? 256 : chip.area == 2 ? 512 : 0;
		const uint32_t column = chip.area == 2 ? chip.column % 16 : chip.column;

		chip.at = (chip.row % PAGES) * PAGE + base + column;
	}
}


AtvTrack::AtvTrack(std::array<std::vector<uint8_t>, NandBank::CHIPS> nand, std::span<const uint8_t> fpga_config)
	: m_boot(atvtrack_boot(fpga_config)), m_control_at(CONTROL), m_dac_at(DAC), m_shared(Sh4::PAGE_SIZE),
	  m_master_ram(MASTER_RAM), m_slave_ram(SLAVE_RAM), m_gpu_ram(GPU_RAM_SIZE), m_nand(std::move(nand)),
	  m_bus{ Bus(*this, MASTER), Bus(*this, SLAVE) },
	  m_pmx(m_gpu_ram),
	  m_core(m_gpu_ram, [this](uint32_t offset, uint32_t size) { gpu_written(offset, size); })
{
	if (m_boot.empty())
		std::fprintf(stderr, "ERROR: atvtrack: the FPGA configuration is too short for its boot program\n");
}

AtvTrack::AtvTrack(std::vector<uint8_t> rom, std::vector<uint8_t> program)
	: m_rom(std::move(rom)), m_program(std::move(program)), m_control_at(SMASH_CONTROL),
	  m_dac_at(SMASH_DAC), m_shared(Sh4::PAGE_SIZE), m_master_ram(MASTER_RAM), m_slave_ram(SLAVE_RAM),
	  m_gpu_ram(GPU_RAM_SIZE), m_nand({}), m_bus{ Bus(*this, MASTER), Bus(*this, SLAVE) },
	  m_pmx(m_gpu_ram),
	  m_core(m_gpu_ram, [this](uint32_t offset, uint32_t size) { gpu_written(offset, size); })
{
	m_rom.resize(SMASH_ROM, 0xFF);
	m_program.resize(SMASH_PROGRAM_SIZE, 0xFF);
}

std::vector<uint8_t> AtvTrack::rom_of(std::span<const uint8_t> a, std::span<const uint8_t> b, std::span<const uint8_t> c)
{
	std::vector<uint8_t> rom(SMASH_ROM, 0xFF);
	auto interleave = [&rom](std::span<const uint8_t> chip, size_t at) {
		for (size_t i = 0; i + 1 < chip.size() && at + i * 2 + 1 < rom.size(); i += 2) {
			rom[at + i * 2] = chip[i];
			rom[at + i * 2 + 1] = chip[i + 1];
		}
	};

	interleave(a, 0);
	interleave(b, 2);
	/* The third chip is copied as is */
	std::copy(c.begin(), c.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(c.size(), 1u << 25)),
	          rom.begin() + (1u << 25));
	return rom;
}

void AtvTrack::build()
{
	/* Debug: ODIN_WAV=<path>, the sound of the speakers (not the woofer)
	 * in a WAV file */
	if (const char *path = std::getenv("ODIN_WAV")) {
		m_wav = std::make_unique<WavFile>(path, DAC_RATE, 2);
		if (!m_wav->ok())
			std::fprintf(stderr, "ERROR: ODIN_WAV: cannot write '%s'\n", path);
	}
	/* The 3D core is HLE (Pmx1): it draws when the primary core starts the
	 * render. HEURISTIC: the render ends when the core next runs. The core
	 * polls for its end every few cycles and the host takes it in its
	 * interrupt, so the core must see it first, as on the board */
	m_core.set_3d([this](uint32_t index, uint32_t) {
		if (index != START_RENDER)
			return;
		m_pmx.draw(m_core.registers_3d());
		m_core.interrupt_next(PVR_IRQ);
	});
	m_core.set_interrupt([this](bool on) { m_sh[SLAVE]->set_irq(Sh4Source::IRQ14, on); });
	/* For the tests: ODIN_PMX1_TRANSLATE=0 has the core's blocks run from
	 * their decoded ops; the pictures are the same */
	m_core.set_translate(env_flag("ODIN_PMX1_TRANSLATE", true));
	m_pmx.set_pages([this](uint32_t from, uint32_t to, uint64_t since) { return gpu_kept(from, to, since); },
	                [this](uint32_t from, uint32_t to) { return gpu_keep(from, to); });
	m_gpu_written.assign(GPU_RAM_SIZE / Sh4::PAGE_SIZE, UINT64_MAX);
	for (std::vector<uint8_t> &ram : m_cache_ram)
		ram.resize(CACHE_RAM);
	/* Clock mode 3: the CPU at six times the 33 MHz crystal */
	for (unsigned which : { MASTER, SLAVE }) {
		m_iregs[which].resize(0x10000);
		m_sh[which] = std::make_unique<Sh4>(m_bus[which], std::span<uint8_t>(m_iregs[which]), which == MASTER,
		                                    true, 3);
		m_sh[which]->set_translate(true);
		m_sh[which]->set_clock_waits(true);
		/* The loops that only poll the plain memory or the shared page of
		 * Smashing Drive are counted without being run: those reads change
		 * nothing (master_read), and the pictures of the three games are
		 * the same with and without */
		m_sh[which]->set_still_turns(true);
		m_sh[which]->set_serial_at_once(true);
	}
}

void AtvTrack::map_pages()
{
	for (unsigned which : { MASTER, SLAVE }) {
		Sh4 &sh = *m_sh[which];
		std::vector<uint8_t> &ram = which == MASTER ? m_master_ram : m_slave_ram;
		const uint32_t ram_pages = static_cast<uint32_t>(ram.size() / Sh4::PAGE_SIZE);

		map_board(sh, which);
		/* Operand cache as RAM (CCR.ORA), at 7C000000 */
		for (uint32_t p = 0; p < CACHE_RAM / Sh4::PAGE_SIZE; p++) {
			uint8_t *page = m_cache_ram[which].data() + p * Sh4::PAGE_SIZE;

			sh.map_page(CACHE_RAM_AT / Sh4::PAGE_SIZE + p, page, page);
		}
		/* Map once: a host page mapped twice is not written in place.
		 * The bus serves the mirrors over the rest of area 3. */
		for (uint32_t p = 0; p < ram_pages; p++) {
			uint8_t *page = ram.data() + p * Sh4::PAGE_SIZE;

			sh.map_page(SDRAM / Sh4::PAGE_SIZE + p, page, page);
		}
		if (which == SLAVE)
			for (uint32_t p = 0; p < GPU_RAM_SIZE / Sh4::PAGE_SIZE; p++) {
				uint8_t *page = m_gpu_ram.data() + p * Sh4::PAGE_SIZE;

				sh.map_page(GPU_RAM / Sh4::PAGE_SIZE + p, page, page);
			}
	}
}

void AtvTrack::reset()
{
	std::fill(m_shared.begin(), m_shared.end(), uint8_t{0});
	std::copy(m_boot.begin(), m_boot.end(), m_shared.begin());
	std::fill(m_master_ram.begin(), m_master_ram.end(), uint8_t{0});
	std::fill(m_slave_ram.begin(), m_slave_ram.end(), uint8_t{0});
	std::fill(m_gpu_ram.begin(), m_gpu_ram.end(), uint8_t{0});
	for (std::vector<uint8_t> &ram : m_cache_ram)
		std::fill(ram.begin(), ram.end(), uint8_t{0});
	m_control = 0;
	m_port_a = 0;
	m_slave_enabled = m_slave_running = false;
	m_core.reset();
	/* The clock of the texture reads goes on: the 3D core's kept textures
	 * were read at times before now, and a page known from now on is newer */
	std::fill(m_gpu_written.begin(), m_gpu_written.end(), UINT64_MAX);
	m_dac.fill(0);
	m_dac_time = m_dac_played = m_dac_half = 0;
	m_dac_asking = false;
	m_adc_step = m_adc_command = 0;
	m_adc_clock = false;
	m_ball_clock = false;
	m_ball_edge = 0;
	m_flash_step = 0;
	m_frame = 0;
	map_pages();
	m_sh[MASTER]->reset(true);
	m_sh[SLAVE]->reset(true);
	m_sh[SLAVE]->set_run(Sh4Run::Stopped);
}

/* AMD flash commands: AA at 5555, 55 at AAAA, then A0 at 5555 and a byte to
 * program, or 80 at 5555, AA, 55 and 30 at a sector to erase. They finish at
 * once. */
void AtvTrack::flash_write(uint32_t offset, uint8_t byte)
{
	const uint32_t low = offset & 0xFFFF;
	const uint32_t step = m_flash_step;

	m_flash_step = 0;
	if (byte == 0xF0)
		return;
	switch (step) {
	case 0:
	case 4:
		if (low == 0x5555 && byte == 0xAA)
			m_flash_step = step + 1;
		break;
	case 1:
	case 5:
		if (low == 0xAAAA && byte == 0x55)
			m_flash_step = step + 1;
		break;
	case 2:
		if (low == 0x5555 && byte == 0xA0)
			m_flash_step = 3;
		else if (low == 0x5555 && byte == 0x80)
			m_flash_step = 4;
		break;
	case 3:
		/* Programming only clears bits */
		m_program[offset] &= byte;
		m_flash_changed.insert(offset / FLASH_SECTOR);
		break;
	case 6:
		if (byte == 0x30) {
			std::fill_n(m_program.begin() + offset / FLASH_SECTOR * FLASH_SECTOR, FLASH_SECTOR, uint8_t{0xFF});
			m_flash_changed.insert(offset / FLASH_SECTOR);
		}
		break;
	default:
		break;
	}
}

void AtvTrack::run_frame()
{
	m_sound.clear();
	m_woofer.clear();
	if (m_motion_platform)
		move_platform();
	/* A frame of the screen: up to the next start of frame of the DAC feed
	 * (59.39 a second in the three games); 1/60 s before it shows one, and
	 * when it shows none for a quarter more */
	const uint32_t shown = m_core.dac_frames();
	uint32_t slice = 0;

	do {
		m_sh[MASTER]->run(FRAME_CYCLES / SLICES);
		serve_link();
		if (m_slave_running)
			m_sh[SLAVE]->run(FRAME_CYCLES / SLICES);
		m_core.run(CORE_CYCLES / SLICES);
		m_core.run_dac(60 * SLICES);
		play_dac(SLICES);
		slice++;
	} while (shown ? m_core.dac_frames() == shown && slice < SLICES * 5 / 4 : slice < SLICES);
	m_frame_slices = slice;
	m_frame++;
	if (m_wav)
		m_wav->write(m_sound);
}

/* The DAC plays 8-byte samples, DAC_CLOCK / DAC_DIVIDER a second, from two
 * 32-byte halves, requesting the next with IRL 5 until the program writes
 * to the half it was given.
 * The sound tests of the three games play their woofer on the second
 * channel, their right speaker on the third and their left on the fourth;
 * none plays the first alone. */
void AtvTrack::play_dac(unsigned slices_per_frame)
{
	m_dac_time += DAC_CLOCK;
	while (m_dac_time >= DAC_DIVIDER * 60u * slices_per_frame) {
		m_dac_time -= DAC_DIVIDER * 60u * slices_per_frame;
		int16_t sample[4];

		std::memcpy(sample, m_dac.data() + m_dac_half * 32 + m_dac_played * 8, 8);
		m_sound.push_back(sample[3]);
		m_sound.push_back(sample[2]);
		m_woofer.push_back(sample[1]);
		m_woofer.push_back(sample[0]);
		if (++m_dac_played == 4) {
			m_dac_played = 0;
			m_dac_half ^= 1;
			m_dac_asking = true;
			m_sh[MASTER]->set_irq(Sh4Source::IRQ5, true);
		}
	}
}

/* Bit 2 of the first control register holds the slave in reset unless the
 * system port allows it to run */
void AtvTrack::control_write(uint32_t value)
{
	/* Bits 15-14 the mode of the link's port: 0 master, 1 slave, 3 idle.
	 * HEURISTIC: a new mode starts the port anew, the words that came in
	 * before it dropped. Without that, two Odins started together stayed
	 * on "Waiting remote ID" in 3 runs of 8, one of them having two of the
	 * other's records waiting (INFERRED: one from before its own mode);
	 * with it, 6 runs of 6 linked */
	if (m_link && ((value ^ m_control) >> 14 & 3))
		m_link->flush();
	m_control = value;
	const bool run = (value & 4) && m_slave_enabled;

	if (run && !m_slave_running) {
		m_sh[SLAVE]->reset(true);
		m_sh[SLAVE]->set_run(Sh4Run::Running);
	} else if (!run && m_slave_running) {
		m_sh[SLAVE]->set_run(Sh4Run::Stopped);
	}
	m_slave_running = run;
}

/* SDRAM mirrored further into area 3: the boot program sizes it this way */
bool AtvTrack::mirror(std::vector<uint8_t> &ram, uint32_t addr, uint32_t size, uint64_t *read, uint64_t value)
{
	if (addr < SDRAM || addr >= SDRAM + SDRAM_AREA)
		return false;
	uint8_t *const at = ram.data() + (addr - SDRAM) % ram.size();

	if (read) {
		*read = 0;
		std::memcpy(read, at, size);
	} else {
		std::memcpy(at, &value, size);
	}
	return true;
}

bool AtvTrack::master_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	if (mirror(m_master_ram, addr, size, &value, 0))
		return true;
	if (addr >= m_control_at && addr < m_control_at + 8) {
		/* HEURISTIC: every status reads all ones */
		value = size == 8 ? ~uint64_t{0} : 0xFFFFFFFF;
		return true;
	}
	return board_read(addr, size, value);
}

bool AtvTrack::master_write(uint32_t addr, uint32_t size, uint64_t value)
{
	if (mirror(m_master_ram, addr, size, nullptr, value))
		return true;
	if (addr >= m_control_at && addr < m_control_at + 8) {
		if (addr == m_control_at)
			control_write(static_cast<uint32_t>(value));
		else if (m_link)
			m_link->send(static_cast<uint16_t>(value));
		else
			tell_once(true, MASTER, addr, size, value, "control +4: a word on the link cable, unplugged");
		return true;
	}
	if (addr >= m_dac_at && addr < m_dac_at + 0x40) {
		const uint32_t at = addr - m_dac_at;

		std::memcpy(m_dac.data() + at, &value, std::min<uint32_t>(size, 0x40 - at));
		if (m_dac_asking) {
			m_dac_asking = false;
			m_sh[MASTER]->set_irq(Sh4Source::IRQ5, false);
		}
		return true;
	}
	return board_write(addr, size, value);
}

/* The core and the TA wrote the frame buffer: the slave SH-4 compares its
 * code there again, and the textures there are read again */
void AtvTrack::gpu_written(uint32_t offset, uint32_t size)
{
	m_sh[SLAVE]->written(GPU_RAM + offset, size);
	for (uint32_t page = offset / Sh4::PAGE_SIZE; page <= (offset + size - 1) / Sh4::PAGE_SIZE; page++)
		m_gpu_written[page] = m_gpu_time;
}

/* The writes to the pages of a texture, for the 3D core: each page keeps
 * the time it was last known written. The core and the TA say so
 * (gpu_written); a write of the slave SH-4 ends the watch gpu_keep() set
 * on the page, which is seen and set again here. */
bool AtvTrack::gpu_kept(uint32_t from, uint32_t to, uint64_t since)
{
	bool kept = true;

	for (uint32_t page = from / Sh4::PAGE_SIZE; from < to && page <= (to - 1) / Sh4::PAGE_SIZE; page++) {
		if (!m_sh[SLAVE]->watched(GPU_RAM / Sh4::PAGE_SIZE + page)) {
			m_gpu_written[page] = m_gpu_time;
			m_sh[SLAVE]->watch(GPU_RAM / Sh4::PAGE_SIZE + page);
		}
		kept = kept && m_gpu_written[page] < since;
	}
	return kept;
}

/* The pages of a texture about to be read, watched; returns the time of
 * the read, after every write known */
uint64_t AtvTrack::gpu_keep(uint32_t from, uint32_t to)
{
	for (uint32_t page = from / Sh4::PAGE_SIZE; from < to && page <= (to - 1) / Sh4::PAGE_SIZE; page++)
		if (!m_sh[SLAVE]->watched(GPU_RAM / Sh4::PAGE_SIZE + page)) {
			m_gpu_written[page] = m_gpu_time;
			m_sh[SLAVE]->watch(GPU_RAM / Sh4::PAGE_SIZE + page);
		}
	return ++m_gpu_time;
}

bool AtvTrack::slave_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	if (mirror(m_slave_ram, addr, size, &value, 0))
		return true;
	if (addr >= GPU_REGS && addr < GPU_REGS + GPU_REGS_SIZE && Pmx1Core::host_register(addr - GPU_REGS)) {
		value = m_core.host_read(addr - GPU_REGS);
		return true;
	}
	return false;
}

bool AtvTrack::slave_write(uint32_t addr, uint32_t size, uint64_t value)
{
	if (mirror(m_slave_ram, addr, size, nullptr, value))
		return true;
	if (addr >= GPU_REGS && addr < GPU_REGS + GPU_REGS_SIZE) {
		const uint32_t data = static_cast<uint32_t>(value);

		if (Pmx1Core::host_register(addr - GPU_REGS)) {
			m_core.host_write(addr - GPU_REGS, data);
			return true;
		}
		if (addr - GPU_REGS >= THREAD_KICKER && addr - GPU_REGS < THREAD_KICKER + 4 * Pmx1Core::THREADS) {
			m_core.kick((addr - GPU_REGS - THREAD_KICKER) / 4, data);
			return true;
		}
		return false;
	}
	return false;
}

/* With -v, an access nothing answers, or that does nothing here, is told
 * the first time, with where its SH-4 is: a program that makes one makes
 * it again at every frame */
void AtvTrack::tell_once(bool is_write, unsigned which, uint32_t addr, uint32_t size, uint64_t value,
                         const char *what)
{
	const uint64_t access = addr | static_cast<uint64_t>(size) << 32 | static_cast<uint64_t>(is_write) << 40 |
	                        static_cast<uint64_t>(which) << 41;
	const char cpu = which == MASTER ? 'M' : 'S';
	char line[160];

	if (!m_verbose || !m_told.insert(access).second)
		return;
	if (is_write)
		std::snprintf(line, sizeof(line), "ERROR: %c @%08X @%08X : %s: W%u @%08X = %" PRIX64 " (told once)\n",
		              cpu, m_sh[which]->pc(), m_sh[which]->pr(), what, 8 * size, addr, value);
	else
		std::snprintf(line, sizeof(line), "ERROR: %c @%08X @%08X : %s: R%u @%08X (told once)\n",
		              cpu, m_sh[which]->pc(), m_sh[which]->pr(), what, 8 * size, addr);
	std::fputs(line, stderr);
}

void AtvTrack::Bus::unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value)
{
	m_board.tell_once(is_write, m_which, addr, size, value, "unhandled");
}

bool AtvTrack::Bus::read(uint32_t addr, uint32_t size, uint64_t &value)
{
	return m_which == MASTER ? m_board.master_read(addr, size, value) : m_board.slave_read(addr, size, value);
}

bool AtvTrack::Bus::write(uint32_t addr, uint32_t size, uint64_t value)
{
	return m_which == MASTER ? m_board.master_write(addr, size, value) : m_board.slave_write(addr, size, value);
}

/* Master port A: the FPGA multiplexes the cabinet I/O, selected by bits
 * 15-11. Inputs are active low; bit 2 says the FPGA is configured. */
uint16_t AtvTrack::Bus::port_a_read()
{
	/* The group selected low by bits 14-11 drives its switches on bits
	 * 10-3, low when pressed */
	uint16_t pins = 0xFFFF;

	if (m_which == MASTER) {
		for (unsigned group = 0; group < 4; group++)
			if (!(m_board.m_port_a & (0x800u << group)))
				pins = static_cast<uint16_t>(pins & ~(m_board.m_switches[group] << 3));
		/* ADC data out comes with group 0, on bit 9 */
		if (!(m_board.m_port_a & 0x800) && !m_board.adc_out())
			pins = static_cast<uint16_t>(pins & ~0x200u);
		if (m_board.m_balls && (m_board.m_port_a >> 11) == 0x0E)
			for (unsigned ball = 0; ball < 2; ball++) {
				const uint16_t line = static_cast<uint16_t>(0x200u << ball);

				pins = static_cast<uint16_t>(m_board.ball_out(ball) ? pins | line : pins & ~line);
			}
	}
	return pins;
}

void AtvTrack::Bus::port_a_write(uint16_t pins)
{
	if (m_which != MASTER)
		return;
	m_board.m_port_a = pins;
	if (m_board.m_balls)
		m_board.ball_pins(pins);
	/* Bits 15-11 high select the ADC and the DAC */
	if ((pins & 0xF800) == 0xF800) {
		m_board.adc_pins(pins);
		m_board.motor_pins(pins, m_board.m_sh[MASTER]->port_a_outputs());
	}
	/* System control: bit 8 lets the slave run */
	if ((pins & 0xF000) == 0x7000 && (pins & 0x0100))
		m_board.m_slave_enabled = true;
	/* The system outputs take only the pins the SH-4 drives: the others,
	 * inputs for a while, are not the program's. And only with bit 1 low,
	 * as for the motors: Gaelco Football writes 0x7802 with pins 3-10
	 * driven low each frame as it reads its balls (its 0x8C0A5900), which
	 * would put out its START lamps at once. INFERRED: bit 1 high writes
	 * nothing */
	const uint16_t driven = m_board.m_sh[MASTER]->port_a_outputs();

	if ((driven & 0xF800) == 0xF800 && (pins >> 11) == 0x0F && !(pins & 0x02))
		m_board.m_system = static_cast<uint16_t>((m_board.m_system & ~driven) | (pins & driven));
}

/* ADC (ADC0834 or similar), bits 15-11 high: bit 6 chip select (low), 5
 * clock, 3 data in. Rising edges: start bit, single-ended, two channel bits
 * LSB first, one settle edge, then 8 sample bits MSB first. */
void AtvTrack::adc_pins(uint16_t pins)
{
	const bool selected = !(pins & 0x40), clock = pins & 0x20, data = pins & 0x08;

	if (!selected) {
		m_adc_step = 0;
		m_adc_command = 0;
	} else if (clock && !m_adc_clock) {
		if (m_adc_step < 4)
			m_adc_command |= static_cast<uint32_t>(data) << m_adc_step;
		else if (m_adc_step == 4)
			m_adc_sample = m_analog[(m_adc_command >> 2) & 3];
		m_adc_step++;
	}
	m_adc_clock = clock;
}

/* Motor DAC, bits 15-11 high and bit 6 high: a TLC5620's framing (INFERRED
 * chip). Eleven bits, the first one first, each taken as bit 5 falls, from
 * bit 4: two of channel, one of range, eight of value; bit 7 rising loads
 * the word. Bits 8-10: the motors' run line and the platform's directions
 * (ATV Track's program at 0x8C064618). Only the pins the SH-4 drives, and
 * only with bit 1 low: the program writes this group so, and leaves it
 * with bit 1 high (0xF802), at times with the data of group 7800 still on
 * bits 3-10 (its 0x8C0733C8). INFERRED: bit 1 high writes nothing. */
void AtvTrack::motor_pins(uint16_t pins, uint16_t driven)
{
	if (pins & 0x02)
		return;
	const bool clock = pins & 0x20, load = pins & 0x80;

	if ((driven & 0x70) == 0x70 && (pins & 0x40) && !clock && m_motor_clock)
		m_motor_shift = static_cast<uint16_t>((m_motor_shift << 1 | ((pins >> 4) & 1)) & 0x7FF);
	if ((driven & 0x80) && load && !m_motor_load)
		m_motors[(m_motor_shift >> 9) & 3] = static_cast<uint8_t>(m_motor_shift);
	if (driven & 0x20)
		m_motor_clock = clock;
	if (driven & 0x80)
		m_motor_load = load;
	m_motor_lines = static_cast<uint16_t>((m_motor_lines & ~driven & 0x700) | (pins & driven & 0x700));
}

/* The motion platform of ATV Track, as its program drives it (its
 * 0x8C064618): motor channel 2 the left side, 3 the right, how fast; bits 9 and 10 their ways, set to
 * go down; bit 8 lets them run. A positive command raises a side, the only
 * way the program's loop holds; ADC channel 2 reads 128 less the left
 * side, channel 3 128 and the right. A side moves as the program's own
 * model of it says, at the 60 runs a second it counts on: its speed
 * follows the command within 2.5 s to full speed, and at full speed it
 * goes 0.44 count a frame, the speed its braking stops on the target; it
 * stops at the ends of its travel, -64 and 64. HEURISTIC: no manual gives
 * the platform's speed nor its inverters' ramp. */
void AtvTrack::move_platform()
{
	static constexpr float TOP_SPEED = 0.44f, RAMP = 1.0f / 150.0f;
	const unsigned lines = m_motor_lines >> 8;

	for (unsigned side = 0; side < 2; side++) {
		const bool down = (lines >> (1 + side)) & 1;
		const float want = lines & 1 ? (down ? -1.0f : 1.0f) * static_cast<float>(m_motors[2 + side]) / 255.0f : 0.0f;
		PlatformSide &s = m_platform[side];

		s.speed += std::clamp(want - s.speed, -RAMP, RAMP);
		s.at = std::clamp(s.at + s.speed * TOP_SPEED, -64.0f, 64.0f);
	}
	m_analog[2] = static_cast<uint8_t>(std::clamp(std::lround(128.0f - m_platform[0].at), 0L, 255L));
	m_analog[3] = static_cast<uint8_t>(std::clamp(std::lround(128.0f + m_platform[1].at), 0L, 255L));
}

/* ATV Track's settings (0x8C05E41C, its loader 0x8C05E478): records of
 * 0x288 bytes in its files BK1 and BK2, two to a page (at 0 and 0x400 of
 * its 0x840 bytes), each with the sum of its 0x142 halfwords at 0x284 and
 * its complement at 0x286. The game loads the first good one from page 31
 * down, BK2 before BK1, the second half of a page before the first; the
 * setting goes in that one, its sums made again. Its files are found as
 * its file system finds them (0x8C0705C8): the root, in page 31 or 30 of
 * block 0, names the two blocks of NFL_FS_FAT, each a copy of the
 * directory (0x8C0704C0). Byte i of a block of the four chips is on chip
 * i % 4 */
bool AtvTrack::set_nand_setting(uint32_t byte, unsigned shift, unsigned bits, uint8_t value,
                                std::optional<uint8_t> only_from)
{
	using Blocks = std::array<uint32_t, NandBank::CHIPS>;
	constexpr uint32_t BLOCKS = NandBank::PAGES / 32, PAGE = NandBank::CHIPS * NandBank::PAGE;
	constexpr uint32_t SECOND = 0x400, SPARE = 0x800, ROOT = 0x220;
	constexpr uint32_t DIRECTORY = 0x800, TABLE = 0x840, TABLE_SIZE = 0x2000, ENTRY = 0x20;
	constexpr uint32_t FILE_SIZE = 0x10648, SUM = 0x284;

	if (byte >= SUM || bits == 0 || shift + bits > 8 || !m_nand.present())
		return false;

	const auto u8 = [this](const Blocks &in, uint32_t i) -> uint32_t {
		return m_nand.block(i % NandBank::CHIPS, in[i % NandBank::CHIPS])[i / NandBank::CHIPS];
	};
	const auto u16 = [&u8](const Blocks &in, uint32_t i) { return u8(in, i) | u8(in, i + 1) << 8; };
	const auto u32 = [&u16](const Blocks &in, uint32_t i) { return u16(in, i) | u16(in, i + 2) << 16; };
	const auto words = [&u32](const Blocks &in, uint32_t from, uint32_t size) {
		uint32_t total = 0;

		for (uint32_t i = 0; i < size; i += 4)
			total += u32(in, from + i);
		return total;
	};
	/* Four halfwords: a block on each chip */
	const auto blocks = [&u16](const Blocks &in, uint32_t i, Blocks &out) {
		for (unsigned chip = 0; chip < NandBank::CHIPS; chip++)
			if ((out[chip] = u16(in, i + 2 * chip)) >= BLOCKS)
				return false;
		return true;
	};

	/* The root (0x8C07038C): the 0x220 bytes at 0x400 of the page, else at
	 * 0, when their mark (not ~0) and sum in its spare bytes are good */
	const Blocks zero{};
	const auto root = [&]() -> std::optional<uint32_t> {
		for (uint32_t page = 31; page >= 30; page--)
			for (const uint32_t half : { 1u, 0u }) {
				const uint32_t at = page * PAGE + half * SECOND, spare = page * PAGE + SPARE + 8 * half;

				if (u32(zero, spare) != ~0u && u32(zero, spare + 4) == words(zero, at, ROOT))
					return at;
			}
		return std::nullopt;
	}();
	Blocks fat[2];

	if (!root || u16(zero, *root + 0x10) != 0x100 || !blocks(zero, *root, fat[0]) || !blocks(zero, *root + 8, fat[1]))
		return false;

	/* The directory: the first copy if newer and not marked bad, else the
	 * second if not; the other one if its sum is wrong. After it, its sum,
	 * its count and its mark (~0) */
	const auto count = [&](unsigned k) { return static_cast<int32_t>(u32(fat[k], DIRECTORY + 4)); };
	const auto unmarked = [&](unsigned k) { return u32(fat[k], DIRECTORY + 8) == ~0u; };
	const auto good = [&](unsigned k) {
		return words(fat[k], 0, DIRECTORY) + words(fat[k], TABLE, TABLE_SIZE) == u32(fat[k], DIRECTORY);
	};
	unsigned copy = count(1) < count(0) && unmarked(0) ? 0 : 1;

	if (!unmarked(copy) || (!good(copy) && !good(copy ^= 1)))
		return false;

	/* A file (0x8C0708C2): the first entry of its name not free (size ~0),
	 * its blocks, its size and its name at 0, 8 and 0xC; one the loader
	 * takes has 0x10648 bytes */
	const auto open = [&](const char *name, Blocks &out) {
		for (uint32_t entry = 0; entry < DIRECTORY; entry += ENTRY) {
			bool same = u32(fat[copy], entry + 8) != ~0u;

			for (uint32_t i = 0; same && i <= std::strlen(name); i++)
				same = u8(fat[copy], entry + 0xC + i) == static_cast<uint8_t>(name[i]);
			if (same)
				return u32(fat[copy], entry + 8) == FILE_SIZE && blocks(fat[copy], entry, out);
		}
		return false;
	};
	Blocks file[2];
	const bool present[] = { open("BK1", file[0]), open("BK2", file[1]) };

	for (uint32_t page = 32; page-- > 0;)
		for (const uint32_t half : { 1u, 0u })
			for (const unsigned k : { 1u, 0u }) {
				const uint32_t at = page * PAGE + half * SECOND;

				if (!present[k])
					continue;

				uint32_t total = 0;

				for (uint32_t i = 0; i < SUM; i += 2)
					total += u16(file[k], at + i);

				const uint32_t sum = u16(file[k], at + SUM);

				if (sum != (~u16(file[k], at + SUM + 2) & 0xFFFF) || (total & 0xFFFF) != sum)
					continue;

				std::array<std::vector<uint8_t>, NandBank::CHIPS> bytes;

				for (unsigned chip = 0; chip < NandBank::CHIPS; chip++) {
					const std::span<const uint8_t> old = m_nand.block(chip, file[k][chip]);

					bytes[chip].assign(old.begin(), old.end());
				}

				const auto put = [&bytes](uint32_t i, uint32_t v) {
					bytes[i % NandBank::CHIPS][i / NandBank::CHIPS] = static_cast<uint8_t>(v);
				};
				const uint32_t mask = ((1u << bits) - 1) << shift, before = u8(file[k], at + byte);
				const uint32_t after = (before & ~mask) | ((static_cast<uint32_t>(value) << shift) & mask);
				const uint32_t made = (sum + ((after - before) << (8 * (byte & 1)))) & 0xFFFF;

				if (after == before || (only_from && ((before & mask) >> shift) != *only_from))
					return true;	/* already so, or not from that value: the block left as it is */
				put(at + byte, after);
				put(at + SUM, made & 0xFF);
				put(at + SUM + 1, made >> 8);
				put(at + SUM + 2, ~made & 0xFF);
				put(at + SUM + 3, (~made >> 8) & 0xFF);
				for (unsigned chip = 0; chip < NandBank::CHIPS; chip++)
					m_nand.set_block(chip, file[k][chip], bytes[chip]);
				return true;
			}
	return false;
}

/* Smashing Drive's settings (settings_save_flash 0x8C057654, its loader
 * 0x8C0573C4): slots of 0x200 bytes, 128 in each of the sectors at
 * 0x3E0000 and 0x3F0000 of the program flash; the game takes the first,
 * from slot 127 down and sector 0x3F0000 first at each, whose word 0x1E4
 * is the complement of word 0x1E8, and word 0x1E4 is the sum of the words
 * before it */
bool AtvTrack::set_flash_setting(uint32_t byte, unsigned shift, unsigned bits, uint8_t value)
{
	constexpr uint32_t SECTORS[2] = { 0x3F0000, 0x3E0000 }, SLOT = 0x200, SUM = 0x1E4, SLOTS = 128;

	if (byte >= SUM || bits == 0 || shift + bits > 8 || m_program.size() < SECTORS[0] + FLASH_SECTOR)
		return false;
	for (uint32_t slot = SLOTS; slot-- > 0;)
		for (const uint32_t sector : SECTORS) {
			uint8_t *const at = m_program.data() + sector + slot * SLOT;

			if (le32(at + SUM) != ~le32(at + SUM + 4))
				continue;

			const uint32_t mask = ((1u << bits) - 1) << shift;
			const uint32_t set = (static_cast<uint32_t>(value) << shift) & mask;
			const auto after = static_cast<uint8_t>((at[byte] & ~mask) | set);
			uint32_t sum = 0;

			if (after == at[byte])
				return true;	/* already so: the sector left as it is */
			at[byte] = after;
			for (uint32_t word = 0; word < SUM; word += 4)
				sum += le32(at + word);
			put_le32(at + SUM, sum);
			put_le32(at + SUM + 4, ~sum);
			m_flash_changed.insert(sector / FLASH_SECTOR);
			return true;
		}
	return false;
}

/* The DMA of the link (DREQ0, DREQ1 of the master): channel 1 takes the
 * words to send as fast as it gives them, unplugged as well (HEURISTIC:
 * the FPGA's queue never fills); channel 0 gets each word that came in, in
 * the low half of a unit, while it takes some */
void AtvTrack::serve_link()
{
	Sh4 &sh = *m_sh[MASTER];

	while (const std::optional<uint32_t> unit = sh.dreq_from_memory(1))
		if (m_link)
			m_link->send(static_cast<uint16_t>(*unit));
	/* Idle, the port takes nothing in (HEURISTIC) */
	if (m_link && (m_control >> 14 & 3) == 3)
		m_link->flush();
	while (m_link && sh.dreq_ready(0)) {
		const std::optional<uint16_t> word = m_link->take();

		if (!word)
			break;
		sh.dreq_to_memory(0, *word);
	}
}

bool AtvTrack::set_link(const NetLinkSettings &link)
{
	m_link.reset();
	m_link_failed = false;
	if (link.board != NetLinkSettings::Board::RING)
		return true;
	m_link = std::make_unique<GaelcoLink>(m_name, link);
	if (m_link->ok())
		return true;
	m_link.reset();
	m_link_failed = true;
	return false;
}

GaelcoLink::GaelcoLink(const std::string &game, const NetLinkSettings &link)
{
	std::string host;
	uint16_t next_port;

	if (!link.next_cabinet(host, next_port)) {
		std::fprintf(stderr, "ERROR: link: the other cabinet '%s' is not an address or address:port\n",
		             link.next.c_str());
		return;
	}
	m_cable = std::make_unique<NetCable>(*this, game, link.port, host, next_port);
	if (!m_cable->start()) {
		m_cable.reset();
		std::fprintf(stderr, "ERROR: link: port %u cannot be opened\n", link.port);
		return;
	}
	std::printf("link: linked to the cabinet at %s:%u\n", host.c_str(), next_port);
}

GaelcoLink::~GaelcoLink()
{
	if (m_cable)
		m_cable->stop();
}

void GaelcoLink::send(uint16_t word)
{
	if (m_cable)
		m_cable->send({ { static_cast<uint8_t>(word), static_cast<uint8_t>(word >> 8) }, 0, 0 });
}

std::optional<uint16_t> GaelcoLink::take()
{
	const std::lock_guard<std::mutex> hold(m_inbox_lock);

	if (m_inbox.empty())
		return std::nullopt;

	const uint16_t word = m_inbox.front();

	m_inbox.pop_front();
	return word;
}

void GaelcoLink::flush()
{
	const std::lock_guard<std::mutex> hold(m_inbox_lock);

	m_inbox.clear();
}

void GaelcoLink::arrive(std::vector<uint8_t> data, uint64_t, uint64_t)
{
	const std::lock_guard<std::mutex> hold(m_inbox_lock);

	for (size_t i = 0; i + 1 < data.size(); i += 2)
		m_inbox.push_back(static_cast<uint16_t>(data[i] | data[i + 1] << 8));
}

/* Ball sensors, read once a frame by 0x8C0A59A0: clock on bit 10 of group 0F
 * with bit 1 low, data on group 0E bits 9 and 10. HEURISTIC: the chips are
 * unknown. After the first rising edge low means ready; after the falling
 * edge high means a kick, taken that frame only (0x8C02F570) and as a button
 * press in a match (0x8C029F1E). Then 24 edges give three bytes LSB first,
 * and two end it: 28 in all. */
void AtvTrack::ball_pins(uint16_t pins)
{
	const unsigned group = pins >> 11;

	if (group == 0x0D || group == 0x0B || group == 0x07) {
		m_ball_edge = 0;
	} else if (group == 0x0F && !(pins & 2) && static_cast<bool>(pins & 0x400) != m_ball_clock) {
		m_ball_clock = pins & 0x400;
		m_ball_edge++;
	}
}

bool AtvTrack::ball_out(unsigned ball) const
{
	if (m_ball_edge == 2)
		return m_ball_kicked[ball];
	if (m_ball_edge < 3 || m_ball_edge >= 27)
		return m_ball_edge != 1;

	const unsigned bit = m_ball_edge - 3;

	return (m_ball[ball][bit / 8] >> (bit & 7)) & 1;
}

bool AtvTrack::adc_out() const
{
	if (m_adc_step < 5 || m_adc_step > 12)
		return true;
	return (m_adc_sample >> (12 - m_adc_step)) & 1;
}

Ref020419::Ref020419(std::array<std::vector<uint8_t>, NandBank::CHIPS> nand, std::span<const uint8_t> fpga_config)
	: AtvTrack(std::move(nand), fpga_config)
{
	build();
}

/* Both SH-4s share the page at 0. The master's pages of the NAND and of
 * the control registers answer without writing its memory (NandBank,
 * master_write; the DAC's interrupt it lowers through set_irq): its blocks
 * go on through them, as they read the game's data word by word */
void Ref020419::map_board(Sh4 &sh, unsigned which)
{
	sh.map_page(SHARED / Sh4::PAGE_SIZE, m_shared.data(), m_shared.data());
	sh.map_shared(SHARED / Sh4::PAGE_SIZE);
	if (which == MASTER)
		for (const uint32_t page : { CONTROL, NAND_DATA, NAND_COMMAND, NAND_ADDRESS })
			sh.map_quiet(page / Sh4::PAGE_SIZE);
}

bool Ref020419::board_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	if (addr >= NAND_DATA && addr < NAND_DATA + 8) {
		value = m_nand.read_data();
		if (size == 8)
			value |= static_cast<uint64_t>(m_nand.read_data()) << 32;
		return true;
	}
	return false;
}

bool Ref020419::board_write(uint32_t addr, uint32_t size, uint64_t value)
{
	if (addr >= NAND_DATA && addr < NAND_DATA + 8) {
		m_nand.write_data(static_cast<uint32_t>(value));
		if (size == 8)
			m_nand.write_data(static_cast<uint32_t>(value >> 32));
		return true;
	}
	if (addr >= NAND_COMMAND && addr < NAND_COMMAND + 8) {
		m_nand.write_command(static_cast<uint32_t>(value));
		return true;
	}
	if (addr >= NAND_ADDRESS && addr < NAND_ADDRESS + 8) {
		m_nand.write_address(static_cast<uint32_t>(value));
		return true;
	}
	return false;
}

Ref010131::Ref010131(std::vector<uint8_t> rom, std::vector<uint8_t> program)
	: AtvTrack(std::move(rom), std::move(program))
{
	build();
}

/* The master: the control registers on the page it shares with the slave,
 * the ROM at 0, the program. The slave: the shared page at 0, the ROM past
 * it, the program past the Neon 250 registers. */
void Ref010131::map_board(Sh4 &sh, unsigned which)
{
	if (which == MASTER) {
		sh.map_quiet(SMASH_SHARED / Sh4::PAGE_SIZE);
		for (uint32_t p = 0; p < SMASH_ROM / Sh4::PAGE_SIZE; p++)
			sh.map_page(p, m_rom.data() + p * Sh4::PAGE_SIZE, nullptr);
		for (uint32_t p = 0; p < SMASH_PROGRAM_SIZE / Sh4::PAGE_SIZE; p++)
			sh.map_page(SMASH_PROGRAM / Sh4::PAGE_SIZE + p, m_program.data() + p * Sh4::PAGE_SIZE,
			            nullptr);
		return;
	}
	sh.map_page(SHARED / Sh4::PAGE_SIZE, m_shared.data(), m_shared.data());
	sh.map_shared(SHARED / Sh4::PAGE_SIZE);
	for (uint32_t p = 1; p < SMASH_ROM / Sh4::PAGE_SIZE; p++)
		sh.map_page(p, m_rom.data() + p * Sh4::PAGE_SIZE, nullptr);
	for (uint32_t p = GPU_REGS_SIZE / Sh4::PAGE_SIZE; p < SMASH_PROGRAM_SIZE / Sh4::PAGE_SIZE; p++)
		sh.map_page(SMASH_PROGRAM / Sh4::PAGE_SIZE + p, m_program.data() + p * Sh4::PAGE_SIZE, nullptr);
}

bool Ref010131::board_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	if (addr >= SMASH_SHARED && addr + size <= SMASH_SHARED + 0x400) {
		value = 0;
		std::memcpy(&value, m_shared.data() + (addr - SMASH_SHARED), size);
		return true;
	}
	return false;
}

bool Ref010131::board_write(uint32_t addr, uint32_t size, uint64_t value)
{
	if (addr >= SMASH_SHARED && addr + size <= SMASH_SHARED + 0x400) {
		std::memcpy(m_shared.data() + (addr - SMASH_SHARED), &value, size);
		m_sh[SLAVE]->written(SHARED + (addr - SMASH_SHARED), size);
		return true;
	}
	if (addr >= SMASH_PROGRAM && addr < SMASH_PROGRAM + SMASH_PROGRAM_SIZE) {
		flash_write(addr - SMASH_PROGRAM, static_cast<uint8_t>(value));
		return true;
	}
	return false;
}

} /* namespace odin */
