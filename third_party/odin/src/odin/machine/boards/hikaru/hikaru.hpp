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

#ifndef ODIN_HIKARU_HPP
#define ODIN_HIKARU_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "odin/machine/boards/board.hpp"
#include "odin/machine/cabinet/drive_838_13366.hpp"
#include "odin/machine/cabinet/netbd_837_13404.hpp"
#include "odin/machine/cabinet/netbd_ring.hpp"
#include "odin/machine/chips/eeprom_93c46.hpp"
#include "odin/host/app/games.hpp"
#include "odin/machine/boards/hikaru/fb_writes.hpp"
#include "odin/machine/boards/hikaru/hikaru_gpu.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/machine/chips/memctl.hpp"
#include "odin/machine/boards/hikaru/mie_board.hpp"
#include "odin/machine/boards/hikaru/rom_board.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/machine/core/scheduler.hpp"
#include "odin/machine/cpu/sh4/sh4.hpp"
#include "odin/machine/boards/hikaru/sound_board.hpp"
#include "odin/common/wav_file.hpp"

namespace odin {

class StateFile;

/* The Hikaru: its main board, with the two SH-4, their memory controllers,
 * the graphics chips, the MIE, and what plugs into it: the ROM board of
 * the game and the two sound boards. It wires the chips, which are each in
 * a file of their own, runs them a frame at a time and keeps the save
 * states. See hikaru.cpp for the board, hikaru_bus.cpp for what each
 * processor sees, hikaru_state.cpp for the save states. */
class Hikaru final : public Board, private MemCtl::Host {
public:
	/* What is around the board: its screen and the control panel of the
	 * cabinet (hikaru_host.hpp) */
	using Screen = HikaruScreen;
	using Panel = HikaruPanel;

	/* The board of a game, or nothing, with the reason on stderr, if the
	 * game cannot run on it. The game outlives the board. */
	static std::unique_ptr<Hikaru> make(const Game &game, bool verbose, const NetLinkSettings &link = {});

	~Hikaru();
	Hikaru(const Hikaru &) = delete;
	Hikaru &operator=(const Hikaru &) = delete;

	/* To be set before anything runs. Without a screen nothing is drawn,
	 * without a panel nothing is pressed. */
	void set_screen(Screen *screen) { m_screen = screen; }
	void set_panel(Panel *panel) { m_panel = panel; }

	/* For the screen: the graphics chips and the memories they draw
	 * from. */
	HikaruGpu &gpu() { return *m_gpu; }
	std::span<const uint8_t> framebuffer_ram() const { return m_fb; }
	FbWrites &framebuffer_writes() { return m_fb_writes; }
	std::span<const uint8_t> texture_ram(unsigned bank) const { return m_texram[bank]; }

	const std::string &game_name() const override { return m_game.name; }
	/* Frames run since power-on */
	unsigned frame() const override { return m_frame; }
	/* The sound of the last frame of the main board, or of the optional
	 * one: left and right, 44 100 pairs a second */
	const std::vector<int16_t> &sound(bool optional) const override
	{
		return m_sound[optional ? SLAVE : MASTER]->samples();
	}
	/* The outputs of the I/O board of the cabinet (MieBoard::io_outputs) */
	std::span<const uint8_t> io_outputs() const override { return m_mie->io_outputs(); }
	/* The card reader of the cabinet (MieBoard::card_reader); none: null.
	 * The folder of the player's cards: cards in the folder of the saves
	 * of the game; empty when nothing is kept. */
	CardReader *card_reader() { return m_mie->card_reader(); }
	std::string cards_folder() const { return m_saves.none() ? std::string() : m_saves.path("cards"); }
	/* The EPROMs of the ROM board as the bus sees them: the program of the
	 * game, as the BOOTROM copies it from 0C000000 on */
	std::span<const uint8_t> program() const { return m_game.section("eprom"); }
	/* The network board and its cable */
	LinkStatus link_status() const override;
	/* The torque the drive board of the cabinet tells its motor, -MOST
	 * (all to the left) to MOST (all to the right), when it runs its
	 * program (drive_rom; drive_838_13366.hpp); nothing else */
	std::optional<int> drive_torque() const override
	{
		return m_drive ? std::optional(m_drive->torque()) : std::nullopt;
	}

	/* Before the power-on: what the board keeps without power comes from
	 * the files of this folder, over the images that come with the game,
	 * and goes back to them at the power-off. */
	void keep_in(const SaveFolder &saves);
	/* Power-on */
	void reset();
	void run_frame() override;
	/* The cabinet is to be switched off and on: the game made its
	 * settings at its first power-on, and its seat is now in them
	 * (take_cabinet) */
	bool restart_asked() const override { return m_restart_asked; }
	/* Power-off: what the board keeps without power goes to the disk (false,
	 * said on stderr, when some could not); and with ODIN_DUMPS=1, for
	 * looking at them (the tests), the memories to <game>-<memory>.bin in
	 * the current folder. */
	bool power_off();

	/* False, with the reason on stderr, if the state could not be written
	 * or is not one of this game and build; the board is then as after a
	 * power-on. */
	bool save_state(const std::string &path) override;
	bool load_state(const std::string &path) override;

private:
	void run_drive(uint32_t cycles);
	void run_netbd(uint32_t cycles);
	void netbd_sync();
	std::unique_lock<std::mutex> netbd_meet();
	/* The SH-4 are told apart by their rank here */
	enum : unsigned {
		MASTER,
		SLAVE
	};

	/* What a SH-4 sees outside its plain memory */
	class Bus : public Sh4Bus {
	public:
		Bus(Hikaru &board, unsigned which) : m_board(board), m_which(which) {}

		bool read(uint32_t addr, uint32_t size, uint64_t &value) override;
		bool write(uint32_t addr, uint32_t size, uint64_t value) override;
		std::span<const uint8_t> read_span(uint32_t addr) override;
		std::span<uint8_t> write_span(uint32_t addr) override;
		void span_written(uint32_t addr, size_t size) override;
		uint16_t port_a_read() override;
		void port_a_write(uint16_t pins) override;
		void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) override;
		void hook() override;

	private:
		Hikaru &m_board;
		unsigned m_which;
	};

	Hikaru(const Game &game, const RomBoard::Config &rombd, bool verbose, const NetLinkSettings &link);

	/* hikaru.cpp */
	void build_mie();
	void build_drive();
	void open_mie_trace();
	void attach_card_reader();
	void build_netbd(NetLinkSettings link);
	void load_eeproms();
	void build_gpu();
	void build_cpus();
	void take_cabinet();
	bool take_seat();
	unsigned link_cabinet() const;
	void build_sound(unsigned which);
	void reset_sound(unsigned which);
	void update_irqs();
	void set_resets(uint8_t pins);
	void set_gpu_reset_lines();
	int run_cycles(int cycles);
	int run_cpu(unsigned which, int cycles);
	uint64_t line_cycles();
	void begin_frame();
	void settle();
	std::vector<std::pair<const char *, std::span<uint8_t>>> kept_memories();
	std::string kept_path(const std::string &name) const;
	void save_clock();
	void slave_idle_loop();
	std::string cpu_string(unsigned which) const;
	std::string board_string() const;
	void tell_error(const char *kind, const std::string &where, const char *what);
	void cpu_error(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void board_error(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void log_access(const char *name, char rw, unsigned size, uint32_t addr, uint64_t value);
	void trace_slice(unsigned which);

	/* hikaru_bus.cpp */
	void map_pages();
	bool master_read(uint32_t addr, unsigned size, uint64_t &value);
	bool master_write(uint32_t addr, unsigned size, uint64_t value);
	bool slave_read(uint32_t addr, unsigned size, uint64_t &value);
	bool slave_write(uint32_t addr, unsigned size, uint64_t value);
	bool gpu_read(uint32_t addr, unsigned size, uint64_t &value);
	bool gpu_write(uint32_t addr, unsigned size, uint64_t value);
	/* The window of the master or of the slave on the MIE */
	bool mie_read(unsigned which, uint32_t addr, unsigned size, uint64_t &value);
	bool mie_write(unsigned which, uint32_t addr, unsigned size, uint64_t value);
	bool mscomm_read(uint32_t addr, unsigned size, uint64_t &value);
	bool mscomm_write(uint32_t addr, unsigned size, uint64_t value);
	bool slave_config_read(uint32_t addr, unsigned size, uint64_t &value);
	bool slave_config_write(uint32_t addr, unsigned size, uint64_t value);
	bool sound_read(unsigned which, uint32_t addr, unsigned size, uint64_t &value);
	bool sound_write(unsigned which, uint32_t addr, unsigned size, uint64_t value);
	void texram_write(unsigned bank, uint32_t offs, unsigned size, uint64_t value);
	void texram_store(unsigned bank, uint32_t offs, unsigned size, uint64_t value, bool twiddled);
	std::span<const uint8_t> bus_span(uint32_t bus_addr) const;
	std::span<uint8_t> bus_ram(uint32_t bus_addr);
	void ram_written(uint32_t bus_addr, size_t size);
	SoundBoard *wave_board(uint32_t bus_addr);
	/* MemCtl::Host: the external bus */
	bool bus_read(uint32_t bus_addr, unsigned size, uint64_t &value) override;
	bool bus_write(uint32_t bus_addr, unsigned size, uint64_t value) override;
	uint32_t bus_copy(uint32_t src, uint32_t dst, uint32_t words) override;
	std::span<const uint8_t> bus_read_span(uint32_t bus_addr) override;
	std::span<uint8_t> bus_write_span(uint32_t bus_addr) override;
	void memctl_irq_changed() override { update_irqs(); }
	void memctl_windows_changed() override { quiet_windows(); }
	void quiet_windows();
	void memctl_error(const char *what) override { cpu_error("%s", what); }

	/* hikaru_state.cpp */
	bool save_items(StateFile &state);
	bool load_items(StateFile &state);
	std::array<std::span<uint8_t>, 13> state_memories();

	const Game &m_game;
	bool m_verbose;
	Screen *m_screen;
	Panel *m_panel;
	uint32_t m_clock;	/* of the SH-4, in Hz */
	unsigned m_frame;
	/* The memories and the chips are being reset one by one: what one of
	 * them shows of another is half old, half new until all are. */
	bool m_resetting;
	unsigned m_current;	/* the SH-4 that runs: for the messages, and the list it launches */

	/* Memories */
	std::vector<uint8_t> m_ram[2];		/* 32 MB each */
	std::vector<uint8_t> m_cmdram;		/* 4 MB */
	std::vector<uint8_t> m_fb;		/* 8 MB */
	FbWrites m_fb_writes;
	std::vector<uint8_t> m_texram[2];	/* 4 MB each */
	std::vector<uint8_t> m_sound_ram[2];	/* 8 MB each */
	std::vector<uint8_t> m_mie_ram;		/* 32 KB */
	std::vector<uint8_t> m_bram;		/* 64 KB */
	std::array<uint8_t, 0x40> m_mscomm;	/* the master and slave box */
	std::vector<uint8_t> m_iregs[2];	/* on-chip registers of the SH-4 */
	std::span<const uint8_t> m_bootrom;
	SaveFolder m_saves;

	/* Chips and boards */
	MemCtl m_memctl[2];
	RomBoard m_rombd;
	/* 93C46 of the main board, on port A of the master: bit 2 DI, 3 SK,
	 * 4 CS, 5 DO */
	Eeprom93c46 m_eeprom;
	std::unique_ptr<MieBoard> m_mie;
	/* The drive board on outputs 15-22 of the I/O board, when its program
	 * is there (ODIN_DRIVE=imitation leaves it out): its clocks still to
	 * run times the clock of the SH-4, the clock it is owed; the wheel as
	 * the board last saw it, on the channel of the control that steers
	 * (-1: none) */
	std::unique_ptr<Drive838_13366> m_drive;
	uint64_t m_drive_acc = 0;
	uint64_t m_drive_owed = 0;
	uint16_t m_wheel_seen = 0x8000;
	int m_steer_channel = -1;
	/* The network board 837-13404 in bus page 0E, when ODIN_NETBD asks
	 * for it (a cabinet has none by default): its clocks still to run
	 * times the clock of the SH-4, the clock it is owed (it runs to it
	 * when the host reaches it and at the end of a frame), and where its
	 * frames go */
	std::unique_ptr<NetBoard837_13404> m_netbd;
	uint64_t m_netbd_acc = 0;
	uint64_t m_netbd_owed = 0;
	std::unique_ptr<NetLink> m_netbd_link;
	/* ODIN_NETBD=ring: the board on a thread of its own, and its cable to
	 * the other cabinets (netbd_ring.hpp) */
	std::unique_ptr<NetBoardRunner> m_netbd_runner;
	std::unique_ptr<NetCable> m_netbd_cable;
	bool m_netbd_failed = false;	/* its port could not be opened */
	/* [network] of odin.ini, ODIN_NETBD* over it (build_netbd) */
	NetLinkSettings m_link;
	/* The seat of this cabinet waits for the block of settings the game
	 * makes in its backup RAM at its first power-on (take_cabinet) */
	bool m_seat_owed = false;
	bool m_restart_asked = false;
	std::unique_ptr<HikaruGpu> m_gpu;
	std::unique_ptr<SoundBoard> m_sound[2];
	std::unique_ptr<WavFile> m_wav;	/* ODIN_WAV: the sound of the main board */
	Bus m_bus[2];
	std::unique_ptr<Sh4> m_sh[2];
	bool m_hooked[2];
	Scheduler m_sched;

	/* Port A of the two SH-4 */
	uint16_t m_porta[2];
	uint8_t m_irq_lines;	/* see update_irqs */
	uint8_t m_mie_port_d;	/* see set_resets */

	/* The frame being run: its line, and the screen the game has set */
	unsigned m_line;
	unsigned m_dots_per_line, m_lines_per_frame, m_lines_shown;
	uint32_t m_line_rest;	/* of a cycle, in dot clocks */

	/* The accesses nothing answered that were told, see Bus::unanswered */
	std::unordered_set<uint64_t> m_unanswered_told;

	/* Debug: traces and counts asked by the environment */
	FILE *m_mie_trace;
	FILE *m_sh_trace[2];
	/* Debug: ODIN_STATS=1, the host time each part of the board took in its
	 * slices, told at the end and for every 60 frames. The first two are
	 * the SH-4. */
	enum : unsigned {
		PART_GPU = 2,
		PART_MIE,
		PART_SOUND,
		NUM_PARTS
	};
	bool m_stats;
	uint64_t m_stats_ns[NUM_PARTS], m_stats_slices;
	uint64_t m_stats_told[NUM_PARTS], m_stats_told_at;	/* at the last 60 frames told */
};

} /* namespace odin */

#endif /* ODIN_HIKARU_HPP */
