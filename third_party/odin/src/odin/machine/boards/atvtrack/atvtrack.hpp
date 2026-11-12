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

#ifndef ODIN_ATVTRACK_HPP
#define ODIN_ATVTRACK_HPP

#include <array>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "odin/common/wav_file.hpp"
#include "odin/machine/cabinet/netbd_ring.hpp"
#include "odin/machine/chips/pmx1/pmx1.hpp"
#include "odin/machine/chips/pmx1/pmx1_core.hpp"
#include "odin/machine/cpu/sh4/sh4.hpp"

namespace odin {

/* The link cable between two Gaelco cabinets, a port of the FPGA: the
 * 16-bit words the program sends go to the other cabinet as they leave,
 * over the optical cable of Hikaru as TCP (NetCable, a ring of two); those
 * that come in wait for the program to take them. HEURISTIC: the cable's speed is in the
 * FPGA, unknown; a word comes in at the first slice of the SH-4 after it
 * arrives. */
class GaelcoLink : public NetArrivals {
public:
	/* The cable of `link` (Board::RING), for `game`: ok() false when its
	 * port cannot be opened */
	GaelcoLink(const std::string &game, const NetLinkSettings &link);
	~GaelcoLink() override;
	GaelcoLink(const GaelcoLink &) = delete;
	GaelcoLink &operator=(const GaelcoLink &) = delete;

	bool ok() const { return m_cable != nullptr; }
	/* The cabinet on each side plugged in (any thread) */
	bool previous_linked() const { return m_cable && m_cable->previous_linked(); }
	bool next_linked() const { return m_cable && m_cable->next_linked(); }
	void send(uint16_t word);
	std::optional<uint16_t> take();
	/* The words that came in, dropped */
	void flush();
	void arrive(std::vector<uint8_t> data, uint64_t start, uint64_t duration) override;

private:
	std::unique_ptr<NetCable> m_cable;
	std::mutex m_inbox_lock;
	std::deque<uint16_t> m_inbox;
};

/* Four K9F2808U0B side by side, one per byte of the 32-bit bus, each with
 * 1024 blocks of 32 pages of 528 bytes (512 plus 16 spare). */
class NandBank {
public:
	static constexpr unsigned CHIPS = 4;
	static constexpr uint32_t PAGE = 528, PAGES = 32 * 1024, SIZE = PAGE * PAGES;

	explicit NandBank(std::array<std::vector<uint8_t>, CHIPS> chips);

	uint32_t read_data();
	void write_data(uint32_t lanes);
	void write_command(uint32_t lanes);
	void write_address(uint32_t lanes);

private:
	struct Chip {
		std::vector<uint8_t> data;
		uint8_t command = 0xFF;
		uint32_t column = 0, row = 0, step = 0, at = 0, id = 0;
		uint8_t area = 0;
	};

	uint8_t read(Chip &chip);

	std::array<Chip, CHIPS> m_chips;
	std::set<std::pair<unsigned, uint32_t>> m_changed;	/* chip, block */

public:
	static constexpr uint32_t BLOCK = 32 * PAGE;
	/* Blocks written since the image was loaded, and restoring one */
	const std::set<std::pair<unsigned, uint32_t>> &changed() const { return m_changed; }
	std::span<const uint8_t> block(unsigned chip, uint32_t number) const
	{
		return std::span<const uint8_t>(m_chips[chip].data).subspan(number * BLOCK, BLOCK);
	}
	void set_block(unsigned chip, uint32_t number, std::span<const uint8_t> bytes);
	bool present() const { return m_chips[0].data.size() == SIZE; }
};

/* The Gaelco boards of two SH-4s (MAME's atvtrack), what Ref020419 and
 * Ref010131 share. Master: 8 MB, FPGA I/O. Slave: 16 MB, PowerVR Neon 250
 * with 32 MB. */
class AtvTrack {
public:
	enum : unsigned { MASTER, SLAVE };

	virtual ~AtvTrack() = default;
	/* Smashing Drive's ROM from its chips: two 16-bit chips side by side,
	 * then the third */
	static std::vector<uint8_t> rom_of(std::span<const uint8_t> a, std::span<const uint8_t> b,
	                                   std::span<const uint8_t> c);

	void set_name(const std::string &name) { m_name = name; }
	void reset();
	void run_frame();
	/* Non-volatile data: the NAND blocks or flash sectors the game
	 * wrote. No file means the dumps as they are. False on failure,
	 * logged to stderr. */
	bool load_kept(const std::string &path);
	bool save_kept(const std::string &path) const;
	/* The whole board at the end of a frame (atvtrack_state.cpp). Loading
	 * resets the board first. */
	bool save_state(const std::string &path);
	bool load_state(const std::string &path);
	/* Cabinet switches, pressed bits set: the four groups the FPGA puts
	 * on port A, selected by bits 11-14 */
	void set_switches(const std::array<uint8_t, 4> &switches) { m_switches = switches; }
	/* Gaelco Football: the three sensors under each ball as sent (middle,
	 * left, right), and whether it is being kicked */
	using BallSensors = std::array<std::array<uint8_t, 3>, 2>;
	static constexpr std::array<uint8_t, 3> BALL_AT_REST = { 0x30, 0x80, 0x80 };
	void set_ball_sensors(const BallSensors &sensors, const std::array<bool, 2> &kicked)
	{
		m_balls = true;
		m_ball = sensors;
		m_ball_kicked = kicked;
	}
	void set_analog(const std::array<uint8_t, 4> &analog) { m_analog = analog; }
	void set_look(unsigned fine, unsigned flat_mode, bool card_sorts)
	{
		m_pmx.set_look(fine, flat_mode, card_sorts);
	}
	PmxScene take_scene() { return m_pmx.take_scene(); }
	/* Its DAC puts out black */
	bool dac_blanked() const { return m_core.dac_blanked(); }

	/* The pins the SH-4 drove in its writes to the group 7800 of port A:
	 * bit 3 the coin counter, bit 4 the lamp (measured: Smashing Drive's
	 * OUTPUT TEST, a coin in ATV Track and Smashing Drive) */
	uint16_t system_pins() const { return m_system; }

	/* The motor DAC's four channels, 0 to 255 (motor_pins) */
	const std::array<uint8_t, 4> &motors() const { return m_motors; }

	/* ATV Track's motion platform (Game::motion_platform): its two sides
	 * move as the motor DAC drives them, and the board reads where they
	 * are on its ADC channels 2 and 3, over what set_analog gives. Each
	 * side where it is (-64 the bottom, 64 the top) and how fast it goes, a
	 * share of its top speed; at power-on on the bottom, where the game
	 * parks it. */
	struct PlatformSide {
		float at = -62.0f, speed = 0.0f;
	};
	void set_motion_platform(bool on) { m_motion_platform = on; }

	/* -v: the accesses nothing answers, and the registers of the PMX1 it
	 * does not emulate, told on stderr once each */
	void set_verbose(bool verbose)
	{
		m_verbose = verbose;
		m_core.set_verbose(verbose);
	}
	/* [network]: board = ring links this cabinet to another one; any other
	 * board leaves its cable unplugged. False when the cable cannot be
	 * opened. */
	bool set_link(const NetLinkSettings &link);
	/* The cable, as set_link() left it: none unplugged; whether its port
	 * could not be opened */
	const GaelcoLink *link() const { return m_link.get(); }
	bool link_failed() const { return m_link_failed; }
	/* Writes bits shift to shift + bits - 1 of a byte of the settings
	 * Smashing Drive keeps in its program flash, as its test menu would;
	 * false when it keeps none yet */
	bool set_flash_setting(uint32_t byte, unsigned shift, unsigned bits, uint8_t value);
	/* The same in the settings ATV Track keeps in its NAND; with `only_from`,
	 * only when those bits hold that value now */
	bool set_nand_setting(uint32_t byte, unsigned shift, unsigned bits, uint8_t value,
	                      std::optional<uint8_t> only_from = std::nullopt);

	/* The DAC's samples a second: the 33 MHz crystal divided by 1024,
	 * 32,226.6 a second. INFERRED for Smashing Drive and ATV Track: their
	 * cabinets' sound plays 1.007 times as high as at 32,000 (Smashing
	 * Drive's 1.00705), and 1024 gives 1.00708; the boards have no other
	 * crystal but the PowerVR's. HEURISTIC for Gaelco Football, never heard
	 * on a cabinet. DAC_RATE: the nearest whole rate, for the host */
	static constexpr uint32_t DAC_CLOCK = 33'000'000, DAC_DIVIDER = 1024;
	static constexpr uint32_t DAC_RATE = (DAC_CLOCK + DAC_DIVIDER / 2) / DAC_DIVIDER;

	/* Last frame's sound: left and right, DAC_CLOCK / DAC_DIVIDER pairs a
	 * second; and the other two channels of the DAC, the woofer's and
	 * channel 0, which no sound test plays alone (unwired, odin-games.json) */
	const std::vector<int16_t> &sound() const { return m_sound; }
	const std::vector<int16_t> &woofer() const { return m_woofer; }
	/* The slices of 1/120000 s a frame of 1/60 s is run in */
	static constexpr uint32_t SLICES_PER_60TH = 2000;
	unsigned frame() const { return m_frame; }
	/* How long the last frame lasted: from one start of frame of the DAC
	 * feed to the next, or 1/60 s before it shows one */
	uint64_t frame_nanoseconds() const { return m_frame_slices * 1000000000ull / (60 * SLICES_PER_60TH); }

protected:
	/* Ref020419: the NAND and the FPGA's boot RAM at 0 */
	AtvTrack(std::array<std::vector<uint8_t>, NandBank::CHIPS> nand, std::span<const uint8_t> fpga_config);
	/* Ref010131: the ROM and the program */
	AtvTrack(std::vector<uint8_t> rom, std::vector<uint8_t> program);
	/* What only one board has: its pages of a processor, its accesses of
	 * the master (false: nothing answers) */
	virtual void map_board(Sh4 &sh, unsigned which) = 0;
	virtual bool board_read(uint32_t addr, uint32_t size, uint64_t &value) = 0;
	virtual bool board_write(uint32_t addr, uint32_t size, uint64_t value) = 0;

	class Bus : public Sh4Bus {
	public:
		Bus(AtvTrack &board, unsigned which) : m_board(board), m_which(which) {}

		bool read(uint32_t addr, uint32_t size, uint64_t &value) override;
		bool write(uint32_t addr, uint32_t size, uint64_t value) override;
		uint16_t port_a_read() override;
		void port_a_write(uint16_t pins) override;
		void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) override;
		void hook() override {}

	private:
		AtvTrack &m_board;
		unsigned m_which;
	};

	bool master_read(uint32_t addr, uint32_t size, uint64_t &value);
	bool master_write(uint32_t addr, uint32_t size, uint64_t value);
	bool slave_read(uint32_t addr, uint32_t size, uint64_t &value);
	bool slave_write(uint32_t addr, uint32_t size, uint64_t value);
	static bool mirror(std::vector<uint8_t> &ram, uint32_t addr, uint32_t size, uint64_t *read, uint64_t value);
	void control_write(uint32_t value);
	void tell_once(bool is_write, unsigned which, uint32_t addr, uint32_t size, uint64_t value, const char *what);
	void play_dac(unsigned slices_per_frame);
	void flash_write(uint32_t offset, uint8_t byte);
	std::vector<uint8_t> kept() const;
	bool take_kept(std::span<const uint8_t> kept);
	void adc_pins(uint16_t pins);
	void motor_pins(uint16_t pins, uint16_t driven);
	void move_platform();
	void serve_link();
	bool adc_out() const;
	void ball_pins(uint16_t pins);
	bool ball_out(unsigned ball) const;
	void build();
	void map_pages();
	void gpu_written(uint32_t offset, uint32_t size);
	bool gpu_kept(uint32_t from, uint32_t to, uint64_t since);
	uint64_t gpu_keep(uint32_t from, uint32_t to);

	std::vector<uint8_t> m_boot;	/* the 1 KB the FPGA starts with */
	std::vector<uint8_t> m_rom, m_program;	/* Ref010131 */
	std::string m_name;
	uint32_t m_control_at, m_dac_at;
	std::vector<uint8_t> m_shared, m_master_ram, m_slave_ram, m_gpu_ram;
	std::array<std::vector<uint8_t>, 2> m_iregs, m_cache_ram;
	NandBank m_nand;
	std::array<Bus, 2> m_bus;
	std::array<std::unique_ptr<Sh4>, 2> m_sh;
	Pmx1 m_pmx;
	Pmx1Core m_core;
	/* Per page of the frame buffer, the time it was last known written,
	 * for the textures of the 3D core; the time */
	std::vector<uint64_t> m_gpu_written;
	uint64_t m_gpu_time = 0;
	uint32_t m_control = 0;
	uint16_t m_port_a = 0;
	std::array<uint8_t, 4> m_switches{};
	std::array<uint8_t, 64> m_dac{};
	std::array<uint8_t, 4> m_analog{ 0x80, 0x80, 0x80, 0x80 };
	uint32_t m_adc_step = 0, m_adc_command = 0, m_adc_sample = 0;
	bool m_adc_clock = false;
	std::array<uint8_t, 4> m_motors{};
	uint16_t m_motor_shift = 0, m_motor_lines = 0;
	bool m_motor_clock = false, m_motor_load = false;
	std::array<PlatformSide, 2> m_platform{};
	std::unique_ptr<GaelcoLink> m_link;
	bool m_link_failed = false;
	bool m_motion_platform = false;
	BallSensors m_ball{};
	std::array<bool, 2> m_ball_kicked{};
	bool m_balls = false, m_ball_clock = false;
	uint32_t m_ball_edge = 0;
	uint32_t m_dac_time = 0, m_dac_played = 0, m_dac_half = 0;
	bool m_dac_asking = false;
	/* Smashing Drive flash: the command step, and the sectors written
	 * since the dump */
	uint32_t m_flash_step = 0;
	std::set<uint32_t> m_flash_changed;
	std::vector<int16_t> m_sound, m_woofer;
	std::unique_ptr<WavFile> m_wav;	/* ODIN_WAV: the sound of the speakers */
	/* The accesses told on stderr (tell_once) */
	bool m_verbose = false;
	std::unordered_set<uint64_t> m_told;
	uint16_t m_system = 0;
	bool m_slave_enabled = false, m_slave_running = false;
	unsigned m_frame = 0;
	uint32_t m_frame_slices = SLICES_PER_60TH;
};

/* REF. 020419 (020201 for Gaelco Football): ATV Track, Gaelco Football.
 * The FPGA's 1 KB boot RAM at 0, the program in NAND */
class Ref020419 final : public AtvTrack {
public:
	Ref020419(std::array<std::vector<uint8_t>, NandBank::CHIPS> nand, std::span<const uint8_t> fpga_config);

private:
	void map_board(Sh4 &sh, unsigned which) override;
	bool board_read(uint32_t addr, uint32_t size, uint64_t &value) override;
	bool board_write(uint32_t addr, uint32_t size, uint64_t value) override;
};

/* REF. 010131: Smashing Drive. 64 MB of ROM at 0 (rom_of), the program in
 * flash at 14000000, the RAM both SH-4s share at 10000000 on the master */
class Ref010131 final : public AtvTrack {
public:
	Ref010131(std::vector<uint8_t> rom, std::vector<uint8_t> program);

private:
	void map_board(Sh4 &sh, unsigned which) override;
	bool board_read(uint32_t addr, uint32_t size, uint64_t &value) override;
	bool board_write(uint32_t addr, uint32_t size, uint64_t value) override;
};

} /* namespace odin */

#endif
