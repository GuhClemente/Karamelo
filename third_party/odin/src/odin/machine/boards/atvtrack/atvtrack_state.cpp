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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "odin/machine/core/state_file.hpp"

/* Non-volatile data and save states. State order is part of the format
 * (StateFile::VERSION). */

namespace odin {

namespace {

constexpr char KEPT_MAGIC[8] = { 'O', 'D', 'I', 'N', 'A', 'T', 'V', '1' };
constexpr uint32_t KEPT_FLASH = 0xFFFFFFFF;
constexpr uint32_t FLASH_SECTOR = 64u << 10;

void
add_word(std::vector<uint8_t> &out, uint32_t word)
{
	const auto *const bytes = reinterpret_cast<const uint8_t *>(&word);

	out.insert(out.end(), bytes, bytes + sizeof(word));
}

bool
put_bytes(StateFile &state, std::span<const uint8_t> bytes)
{
	return state.put(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

bool
get_bytes(StateFile &state, std::span<uint8_t> bytes)
{
	return state.get(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

bool
put_cpu(StateFile &state, const Sh4 &sh)
{
	const auto block = std::make_unique<Sh4State>();

	std::memset(block.get(), 0, sizeof(Sh4State));
	sh.save(*block);
	return state.put(*block);
}

bool
get_cpu(StateFile &state, Sh4 &sh)
{
	const auto block = std::make_unique<Sh4State>();

	if (!state.get(*block))
		return false;
	sh.load(*block);
	return true;
}

} /* namespace */

/* Each block: the chip (KEPT_FLASH for the flash), its number, its bytes */
std::vector<uint8_t>
AtvTrack::kept() const
{
	std::vector<uint8_t> out(KEPT_MAGIC, KEPT_MAGIC + sizeof(KEPT_MAGIC));

	for (const uint32_t sector : m_flash_changed) {
		add_word(out, KEPT_FLASH);
		add_word(out, sector);
		out.insert(out.end(), m_program.begin() + sector * FLASH_SECTOR, m_program.begin() + (sector + 1) * FLASH_SECTOR);
	}
	for (const auto &[chip, number] : m_nand.changed()) {
		const std::span<const uint8_t> bytes = m_nand.block(chip, number);

		add_word(out, chip);
		add_word(out, number);
		out.insert(out.end(), bytes.begin(), bytes.end());
	}
	return out;
}

bool
AtvTrack::take_kept(std::span<const uint8_t> kept)
{
	if (kept.size() < sizeof(KEPT_MAGIC) || std::memcmp(kept.data(), KEPT_MAGIC, sizeof(KEPT_MAGIC)))
		return false;
	for (size_t at = sizeof(KEPT_MAGIC); at < kept.size();) {
		uint32_t chip, number;

		if (at + 2 * sizeof(uint32_t) > kept.size())
			return false;
		std::memcpy(&chip, kept.data() + at, sizeof(chip));
		std::memcpy(&number, kept.data() + at + sizeof(chip), sizeof(number));
		at += 2 * sizeof(uint32_t);

		const bool flash = chip == KEPT_FLASH;
		const size_t size = flash ? FLASH_SECTOR : NandBank::BLOCK;

		if (at + size > kept.size() || (flash ? m_program.empty() || (number + 1) * FLASH_SECTOR > m_program.size()
		                                      : !m_program.empty() || chip >= NandBank::CHIPS || number >= NandBank::PAGES / 32))
			return false;
		if (flash) {
			std::copy_n(kept.data() + at, size, m_program.begin() + number * FLASH_SECTOR);
			m_flash_changed.insert(number);
		} else {
			m_nand.set_block(chip, number, kept.subspan(at, size));
		}
		at += size;
	}
	return true;
}

bool
AtvTrack::load_kept(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);

	if (!in)
		return true;

	const std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

	if (take_kept(bytes))
		return true;
	std::fprintf(stderr, "ERROR: atvtrack: '%s' is not what this board keeps\n", path.c_str());
	return false;
}

bool
AtvTrack::save_kept(const std::string &path) const
{
	if (m_flash_changed.empty() && m_nand.changed().empty())
		return true;

	const std::vector<uint8_t> bytes = kept();
	const std::string fresh = path + ".new";
	std::error_code error;

	std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
	{
		std::ofstream out(fresh, std::ios::binary);

		out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!out) {
			std::fprintf(stderr, "ERROR: atvtrack: cannot write '%s'\n", fresh.c_str());
			return false;
		}
	}
	std::filesystem::rename(fresh, path, error);
	if (error)
		std::fprintf(stderr, "ERROR: atvtrack: cannot write '%s': %s\n", path.c_str(), error.message().c_str());
	return !error;
}

bool
AtvTrack::save_state(const std::string &path)
{
	StateFile state(path, StateFile::Mode::SAVE);

	if (!state.ok()) {
		std::fprintf(stderr, "ERROR: save state: cannot write '%s'\n", path.c_str());
		return false;
	}

	char name[32] = "";
	const std::vector<uint8_t> nv = kept();
	const uint64_t times[] = { m_sh[MASTER]->time(), m_sh[SLAVE]->time() };
	const uint8_t flags[] = { m_adc_clock, m_dac_asking, m_slave_enabled, m_slave_running, m_ball_clock };
	const uint32_t words[] = { m_control, m_port_a, m_adc_step, m_adc_command, m_adc_sample, m_dac_time, m_dac_played,
		                   m_dac_half, m_flash_step, m_ball_edge };
	const uint16_t motor_words[] = { m_motor_shift, m_motor_lines, m_system };
	const uint8_t motor_flags[] = { m_motor_clock, m_motor_load };

	std::strncpy(name, m_name.c_str(), sizeof(name) - 1);
	const bool ok = state.put(name) && state.put(m_frame) && put_bytes(state, m_shared) &&
	                put_bytes(state, m_master_ram) && put_bytes(state, m_slave_ram) && put_bytes(state, m_gpu_ram) &&
	                put_bytes(state, m_iregs[MASTER]) && put_bytes(state, m_iregs[SLAVE]) &&
	                put_bytes(state, m_cache_ram[MASTER]) && put_bytes(state, m_cache_ram[SLAVE]) &&
	                state.put(m_dac) && state.put(flags) && state.put(words) &&
	                state.put(m_core.state()) &&
	                state.put(times) && put_cpu(state, *m_sh[MASTER]) && put_cpu(state, *m_sh[SLAVE]) &&
	                state.put(m_motors) && state.put(motor_words) && state.put(motor_flags) && state.put(m_platform) &&
	                put_bytes(state, nv);

	if (!ok)
		std::fprintf(stderr, "ERROR: save state: cannot write '%s'\n", path.c_str());
	return ok;
}

bool
AtvTrack::load_state(const std::string &path)
{
	StateFile state(path, StateFile::Mode::LOAD);

	if (!state.ok()) {
		std::fprintf(stderr, "ERROR: load state: '%s' is missing or is not a state of this build\n", path.c_str());
		return false;
	}

	char name[32] = "";

	if (!state.get(name) || std::strncmp(m_name.c_str(), name, sizeof(name) - 1)) {
		std::fprintf(stderr, "ERROR: load state: '%s' is a state of another game\n", path.c_str());
		return false;
	}
	reset();

	uint8_t flags[5];
	uint32_t words[10];
	uint16_t motor_words[3];
	uint8_t motor_flags[2];
	uint64_t times[2] = {};
	Pmx1Core::State core{};
	bool ok = state.get(m_frame) && get_bytes(state, m_shared) && get_bytes(state, m_master_ram) &&
	          get_bytes(state, m_slave_ram) && get_bytes(state, m_gpu_ram) && get_bytes(state, m_iregs[MASTER]) &&
	          get_bytes(state, m_iregs[SLAVE]) && get_bytes(state, m_cache_ram[MASTER]) &&
	          get_bytes(state, m_cache_ram[SLAVE]) && state.get(m_dac) && state.get(flags) && state.get(words) &&
	          state.get(core) && state.get(times) && get_cpu(state, *m_sh[MASTER]) &&
	          get_cpu(state, *m_sh[SLAVE]) && state.get(m_motors) && state.get(motor_words) && state.get(motor_flags) &&
	          state.get(m_platform);

	if (ok) {
		std::vector<uint8_t> nv(state.next_size());

		ok = get_bytes(state, nv) && take_kept(nv);
	}
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: '%s' could not be read: resetting the board\n", path.c_str());
		reset();
		return false;
	}
	m_adc_clock = flags[0];
	m_dac_asking = flags[1];
	m_slave_enabled = flags[2];
	m_slave_running = flags[3];
	m_ball_clock = flags[4];
	m_control = words[0];
	m_port_a = static_cast<uint16_t>(words[1]);
	m_adc_step = words[2];
	m_adc_command = words[3];
	m_adc_sample = words[4];
	m_dac_time = words[5];
	m_dac_played = words[6];
	m_dac_half = words[7];
	m_flash_step = words[8];
	m_ball_edge = words[9];
	m_motor_shift = motor_words[0];
	m_motor_lines = motor_words[1];
	m_system = motor_words[2];
	m_motor_clock = motor_flags[0];
	m_motor_load = motor_flags[1];
	m_core.set_state(core);
	/* With memory and chips loaded, restore their clocks */
	m_sh[MASTER]->set_time(times[0]);
	m_sh[SLAVE]->set_time(times[1]);
	return true;
}

} /* namespace odin */
