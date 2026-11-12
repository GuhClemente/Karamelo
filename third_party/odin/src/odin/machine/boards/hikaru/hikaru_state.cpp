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
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/boards/hikaru/hikaru.hpp"

#include <cstring>

#include "odin/machine/core/state_file.hpp"

/*
 * Save states
 * ===========
 *
 * A state is the board between two frames: the name of the game and the
 * number of the frame, the memories, the chips one after the other, what
 * the board itself holds, then the pictures the screen keeps from a frame
 * to the next. The order is part of the format (StateFile::VERSION): a
 * state is read back item by item in the order it was written.
 */

namespace odin {

namespace {

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

/* A chip saves and loads a block of plain data: one item. */
template <typename Block, typename Chip>
bool
put_chip(StateFile &state, Chip &chip)
{
	const auto block = std::make_unique<Block>();

	std::memset(block.get(), 0, sizeof(Block));
	chip.save(*block);
	return state.put(*block);
}

template <typename Block, typename Chip>
bool
get_chip(StateFile &state, Chip &chip)
{
	const auto block = std::make_unique<Block>();

	if (!state.get(*block))
		return false;
	chip.load(*block);
	return true;
}

/* The network board and the clock the host owes it */
struct NetbdItem {
	NetBoard837_13404::State board;
	uint64_t acc, owed;
};

/* What the two sound boards keep of their count of the silence, after the
 * boards a cabinet may not have: known by its size as they are */
struct SilenceItem {
	uint64_t rest[2];
};

static_assert(sizeof(SilenceItem) != sizeof(Drive838_13366::State) && sizeof(SilenceItem) != sizeof(NetbdItem));

} /* namespace */

/* The memories of a state, in its order: the main RAMs, the command RAM,
 * the frame buffer, the texture RAMs, the sound RAMs, the RAM of the MIE,
 * the backup RAM, the registers of the two memory controllers and the box
 * of the master and the slave */
std::array<std::span<uint8_t>, 13>
Hikaru::state_memories()
{
	return { m_ram[MASTER], m_ram[SLAVE], m_cmdram, m_fb, m_texram[0], m_texram[1], m_sound_ram[MASTER],
		 m_sound_ram[SLAVE], m_mie_ram, m_bram, m_memctl[MASTER].registers(), m_memctl[SLAVE].registers(),
		 m_mscomm };
}

bool
Hikaru::save_items(StateFile &state)
{
	char name[32] = "";
	RomBoard::Config rombd;
	bool ok;

	if (m_netbd_runner) {
		std::fprintf(stderr, "ERROR: save state: not with the network board linked to other cabinets\n");
		return false;
	}
	std::strncpy(name, m_game.name.c_str(), sizeof(name) - 1);
	if (!state.put(name) || !state.put(m_frame)) {
		std::fprintf(stderr, "ERROR: save state: cannot save header\n");
		return false;
	}

	/* The memories */
	ok = true;
	for (const std::span<uint8_t> memory : state_memories())
		ok = ok && put_bytes(state, memory);
	if (ok && !m_rombd.local_ram().empty())
		ok = put_bytes(state, m_rombd.local_ram());
	ok = ok && put_bytes(state, m_iregs[MASTER]) && put_bytes(state, m_iregs[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: save state: cannot save buffer\n");
		return false;
	}

	/* The chips */
	ok = put_chip<MieBoard::State>(state, *m_mie) && put_chip<HikaruGpu::State>(state, *m_gpu) &&
	     put_chip<SoundBoard::State>(state, *m_sound[MASTER]) &&
	     put_chip<SoundBoard::State>(state, *m_sound[SLAVE]) &&
	     put_chip<Sh4State>(state, *m_sh[MASTER]) && put_chip<Sh4State>(state, *m_sh[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: save state: cannot save device\n");
		return false;
	}

	/* The board */
	m_rombd.save(rombd);
	ok = state.put(m_porta[MASTER]) && state.put(m_porta[SLAVE]) && state.put(rombd) &&
	     put_chip<Eeprom93c46State>(state, m_eeprom) &&
	     put_chip<X76f100State>(state, m_rombd.eeprom());
	if (ok && m_rombd.crypt())
		ok = put_chip<Crypt315_5881State>(state, *m_rombd.crypt());
	if (ok && m_rombd.fpga())
		ok = put_chip<ActelM1State>(state, *m_rombd.fpga());
	ok = ok && (!m_screen || m_screen->save_pictures(state)) &&
	     put_chip<SchedulerState>(state, m_sched) &&
	     state.put(m_line) && state.put(m_dots_per_line) && state.put(m_lines_per_frame) &&
	     state.put(m_lines_shown) && state.put(m_line_rest);
	/* The boards a cabinet may not have, last, each known by the size of
	 * its item: a state without one of them still loads. The drive board
	 * when it runs its program, the network board and the frames on their
	 * way to it. */
	if (ok && m_drive)
		ok = put_chip<Drive838_13366::State>(state, *m_drive);
	if (ok && m_netbd) {
		const auto item = std::make_unique<NetbdItem>();

		std::memset(item.get(), 0, sizeof(NetbdItem));
		m_netbd->save(item->board);
		item->acc = m_netbd_acc;
		item->owed = m_netbd_owed;
		ok = state.put(*item) && put_bytes(state, m_netbd->frames_save());
	}
	if (ok) {
		const SilenceItem silence = { { m_sound[MASTER]->silence_rest(), m_sound[SLAVE]->silence_rest() } };

		ok = state.put(silence);
	}
	if (!ok)
		std::fprintf(stderr, "ERROR: save state: cannot save machine\n");
	return ok;
}

bool
Hikaru::load_items(StateFile &state)
{
	char name[32];
	RomBoard::Config rombd;
	bool ok;

	if (m_netbd_runner) {
		std::fprintf(stderr, "ERROR: load state: not with the network board linked to other cabinets\n");
		return false;
	}
	ok = state.get(name);
	if (ok && std::strncmp(name, m_game.name.c_str(), sizeof(name))) {
		std::fprintf(stderr, "ERROR: load state: made with the game '%.32s'\n", name);
		ok = false;
	}
	if (!ok || !state.get(m_frame)) {
		std::fprintf(stderr, "ERROR: load state: cannot load header\n");
		return false;
	}

	ok = true;
	for (const std::span<uint8_t> memory : state_memories())
		ok = ok && get_bytes(state, memory);
	m_fb_writes.wrote_all();
	if (ok && !m_rombd.local_ram().empty())
		ok = get_bytes(state, m_rombd.local_ram());
	ok = ok && get_bytes(state, m_iregs[MASTER]) && get_bytes(state, m_iregs[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load buffer\n");
		return false;
	}

	{
		const auto block = std::make_unique<MieBoard::State>();

		ok = state.get(*block) && m_mie->load(*block);
	}
	ok = ok && get_chip<HikaruGpu::State>(state, *m_gpu) &&
	     get_chip<SoundBoard::State>(state, *m_sound[MASTER]) &&
	     get_chip<SoundBoard::State>(state, *m_sound[SLAVE]) &&
	     get_chip<Sh4State>(state, *m_sh[MASTER]) && get_chip<Sh4State>(state, *m_sh[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load device\n");
		return false;
	}

	ok = state.get(m_porta[MASTER]) && state.get(m_porta[SLAVE]) && state.get(rombd);
	if (ok)
		m_rombd.load(rombd);
	ok = ok && get_chip<Eeprom93c46State>(state, m_eeprom) &&
	     get_chip<X76f100State>(state, m_rombd.eeprom());
	if (ok && m_rombd.crypt())
		ok = get_chip<Crypt315_5881State>(state, *m_rombd.crypt());
	if (ok && m_rombd.fpga())
		ok = get_chip<ActelM1State>(state, *m_rombd.fpga());
	ok = ok && (!m_screen || m_screen->load_pictures(state)) &&
	     get_chip<SchedulerState>(state, m_sched) &&
	     state.get(m_line) && state.get(m_dots_per_line) && state.get(m_lines_per_frame) &&
	     state.get(m_lines_shown) && state.get(m_line_rest);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load machine\n");
		return false;
	}
	/* A board the state does not have starts again (from the reset
	 * above); one the cabinet does not have is passed */
	if (state.next_size() == sizeof(Drive838_13366::State))
		ok = m_drive ? get_chip<Drive838_13366::State>(state, *m_drive) : state.skip();
	else if (m_drive)
		std::printf("load state: no drive board in it: the board starts again\n");
	if (ok && state.next_size() == sizeof(NetbdItem)) {
		if (m_netbd) {
			const auto item = std::make_unique<NetbdItem>();
			std::vector<uint8_t> frames;

			ok = state.get(*item);
			if (ok) {
				m_netbd->load(item->board);
				m_netbd_acc = item->acc;
				m_netbd_owed = item->owed;
				frames.resize(state.next_size());
				ok = get_bytes(state, frames) && m_netbd->frames_load(frames);
			}
		} else {
			ok = state.skip() && state.skip();
		}
	} else if (ok && m_netbd) {
		std::printf("load state: no network board in it: the board starts again\n");
	}
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load the drive or network board\n");
		return false;
	}
	/* A state made before the count of the silence was kept: none */
	{
		SilenceItem silence = {};

		if (state.next_size() == sizeof(SilenceItem) && !state.get(silence)) {
			std::fprintf(stderr, "ERROR: load state: cannot load the silence of the sound boards\n");
			return false;
		}
		m_sound[MASTER]->set_silence_rest(silence.rest[0]);
		m_sound[SLAVE]->set_silence_rest(silence.rest[1]);
	}
	if (m_drive)
		m_drive_owed = m_drive->clocks();

	/* What the board works out from its chips */
	quiet_windows();
	m_sh[MASTER]->set_time(m_sched.now());
	m_sh[SLAVE]->set_time(m_sched.now());
	m_mie_port_d = m_mie->port_d();
	set_gpu_reset_lines();
	update_irqs();
	return true;
}

bool
Hikaru::save_state(const std::string &path)
{
	StateFile state(path, StateFile::Mode::SAVE);

	if (!state.ok()) {
		std::fprintf(stderr, "ERROR: save state: cannot write '%s'\n", path.c_str());
		return false;
	}
	settle();
	return save_items(state);
}

/* The state is loaded into a board just powered on; one that cannot be
 * loaded to its end leaves the board that way. */
bool
Hikaru::load_state(const std::string &path)
{
	bool ok;

	{
		StateFile state(path, StateFile::Mode::LOAD);

		ok = state.ok();
		if (ok) {
			reset();
			ok = load_items(state);
		} else {
			std::fprintf(stderr, "ERROR: load state: '%s' is missing or is not a state of this build\n", path.c_str());
		}
	}
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: resetting machine\n");
		reset();
	}
	return ok;
}

} /* namespace odin */
