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

/* 
 * SEGA Hikaru
 *
 * Specs
 * =====
 *
 * Taken from system16.com
 *
 * CPU			2 x Hitachi SH-4 @ 200 MHz
 * Graphic Engine	Sega Custom 3D 
 * Sound Engine		2 x ARM7 Yamaha AICA @ 45 MHz + ARM7, 64 channel ADPCM
 * Main Memory		64 MB
 * Graphic Memory	28 MB
 * Sound Memory		8 MB [per AICA board]
 * Media		ROM Board (max 352 MB)
 * Colors		24bit
 * Resolution		24 KHz 496x384, 31 KHz 640x480 
 * Shading		Flat, Linear, Phong
 * Lighting		Horizontal, Spot
 *			1024 lights per scene
 *			4 lights per polygon
 *			8 window surfaces
 * Effects		Fog, Depth Queueing, Stencil, Shadow, Motion blur
 * Others Capabilties	Bitmap Layer x 2
 *			Calendar [Note: AICA RTC]
 *			Dual Monitor (24 kHz)
 * Extensions		communication, 4 channel audio, PCI, MIDI, RS-232C 
 * Connection		Jamma Video compliant
 *
 *
 * ICs
 * ===
 *
 * According to the RAM test:
 *
 * 15,16,17S,18S = @0C000000 RAM (32 MB), Master RAM
 * 22,23,24S,25S = @16000000 RAM (32 MB), Slave RAM
 * 38 39S        = @14000000 RAM (4 MB), CMDRAM
 * 41            = @16000000 RAM (4 MB), TEXRAM Bank 0
 * 42            = @16000000 RAM (4 MB), TEXRAM Bank 1
 * 44,45S,46,47S = @1B000000 RAM (8 MB), FB
 * 33,34S,35,36S = @........ RAM (8 MB), Unknown (not tested by the RAM test)
 * 91S,92S       = @00C00000 CMOS SRAM (64 KB), Backup RAM
 * 98            = @02800000 8 MB SDRAM (main AICA board)
 *
 *
 * BIOSes
 * ======
 *
 * Currently three different bios revisions are known: 0.84, 0.92, and 0.96
 * (see the MAME driver.)
 *
 * The master SH-4 is configured for external-request DMAC (DTD). Requests
 * are sent either to the master SH-4 or the slave SH-4.
 *
 * TMU Channel 2 is configured for input capture; the DMAC is automatically
 * activated whenever the interrupt fires.
 *
 *
 * EEPROMs
 * =======
 *
 * The Hikaru hosts a few EEPROMs:
 *  - One on the MAINBD, connected to the master SH4 Port A.
 *  - One on the ROMBD, which likely holds game-specific data and protection.
 *  - The 93C46 of the MIE, reached through its ports at 0800000[AC]
 *    (mie_board.hpp).
 *
 *
 * MAINBD EEPROM
 * =============
 *
 * The MAINBD EEPROM is a serial 128 x 8-bit EEPROM. It is interfaced to bits
 * 2-5 of PDTRA. The port is configured as follows:
 *
 *  PCTRA = (PCTRA & ~FF0) | 950
 *  PDTRA = PDTRA & ~3C
 *
 * See @0C00BF5C.
 *
 *  bit 2	Output		[configured as: pulled up, output]
 *  bit 3	Clock		[configured as: pulled up, output]
 *  bit 4	Chip Select	[configured as: pulled up, output]
 *  bit 5	Input		[configured as: not pulled up, input]
 *
 * See @0C0067AC, @0C0067DC.
 *
 *
 * ROMBD EEPROM, Type 1
 * ====================
 *
 * Located at 03000000, in bank_base + 0x14. It's a serial EEPROM of size
 * 128 x 8-bit entries, a 93C46, which the BOOTROM can read; the ROM board
 * of Odin has Type 2 (rom_board.cpp). Bits (active low):
 *
 *  bit 0	Output
 *  bit 1	Clock
 *  bit 2	Chip Select
 *  bit 3	Input
 *
 * See @0C00C27E, @0C00C2AE
 *
 *
 * ROMBD EEPROM, Type 2
 * ====================
 *
 * An X76F100 secure EEPROM (x76f100.cpp).
 *
 * MIE EEPROM
 * ==========
 *
 * See mie_board.cpp
 */

#include "odin/machine/boards/hikaru/hikaru.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "odin/common/bytes.hpp"
#include "odin/common/cabinet_settings.hpp"
#include "odin/common/clock_ratio.hpp"
#include "odin/machine/cabinet/drive_board.hpp"
#include "odin/common/env.hpp"
#include "odin/common/host_clock.hpp"
#include "odin/common/repeats.hpp"
#include "odin/common/stop_message.hpp"

namespace odin {

namespace {

constexpr uint32_t KB = 1024;
constexpr uint32_t MB = 1024 * 1024;

/* The dot clock of the 31 kHz screen, which draws the 818 dots of a line.
 * HEURISTIC: the value of MAME (hikaru.cpp, 51894580 / 2), which does not
 * say where it comes from; no game sets or reads it. It gives 60.08 frames
 * a second. */
constexpr uint32_t DOT_CLOCK = 25947290;

/* What the scheduler dates. Those due at the same time are taken in this
 * order. */
enum : uint32_t {
	EVENT_LINE,		/* the end of a line */
};

/* SH7091 at 200 MHz (2 x SH-4 @ 200 MHz, see the top of this file).
 * Debug: ODIN_SH4_MHZ lowers it, every phase of a game then takes fewer
 * host seconds. */
uint32_t
sh4_clock()
{
	const int mhz = env_int("ODIN_SH4_MHZ", 200);

	return static_cast<uint32_t>((mhz < 1 || mhz > 200) ? 200 : mhz) * 1000000u;
}

/* The text at an offset of a ROM, as far as it goes */
std::string
text_at(std::span<const uint8_t> rom, size_t offs)
{
	std::string text;

	for (; offs < rom.size() && rom[offs]; offs++)
		text += static_cast<char>(rom[offs]);
	return text;
}

/* The security FPGA of the ROM board of a game, when it has one */
std::optional<RomBoard::Actel>
actel_of(const Game &game)
{
	if (!game.rom_board || !game.rom_board->has_actel)
		return std::nullopt;
	return RomBoard::Actel{ game.rom_board->actel_key, game.rom_board->actel_id };
}

} /* namespace */

std::unique_ptr<Hikaru>
Hikaru::make(const Game &game, bool verbose, const NetLinkSettings &link)
{
	const std::optional<GameRomBoard> &board = game.rom_board;
	const size_t mie = game.section("mie").size();

	if (game.section("bootrom").size() != 2 * MB) {
		std::fprintf(stderr, "ERROR: '%s' has no BOOTROM of 2 MB\n", game.name.c_str());
		return nullptr;
	}
	if (!game.section("eprom").empty() && !board) {
		std::fprintf(stderr, "ERROR: the list does not tell the ROM board of '%s'\n", game.name.c_str());
		return nullptr;
	}
	if (game.section("eprom").empty() && board) {
		std::fprintf(stderr, "ERROR: '%s' has a ROM board and no EPROM\n", game.name.c_str());
		return nullptr;
	}
	if (mie != Mie::ROM_SIZE) {
		std::fprintf(stderr, "ERROR: the ROM of the MIE of '%s' has %zu bytes, not %u\n", game.name.c_str(), mie,
		             Mie::ROM_SIZE);
		return nullptr;
	}
	if (!MieBoard::knows_io_board(game.io_board)) {
		std::fprintf(stderr, "ERROR: the I/O board %s of '%s' is not known\n", game.io_board.c_str(),
		             game.name.c_str());
		return nullptr;
	}
	const RomBoard::Config rombd = RomBoard::config_of(board ? board->eprom_mb : 0,
	                                                   board ? board->crypt_key : std::nullopt);

	return std::unique_ptr<Hikaru>(new Hikaru(game, rombd, verbose, link));
}

Hikaru::Hikaru(const Game &game, const RomBoard::Config &rombd, bool verbose, const NetLinkSettings &link)
	: m_game(game), m_verbose(verbose), m_screen(nullptr), m_panel(nullptr), m_clock(sh4_clock()), m_frame(0),
	  m_resetting(true), m_current(MASTER),
	  m_ram{std::vector<uint8_t>(32 * MB), std::vector<uint8_t>(32 * MB)},
	  m_cmdram(4 * MB), m_fb(8 * MB), m_fb_writes(8 * MB),
	  m_texram{std::vector<uint8_t>(4 * MB), std::vector<uint8_t>(4 * MB)},
	  m_sound_ram{std::vector<uint8_t>(8 * MB), std::vector<uint8_t>(8 * MB)},
	  m_mie_ram(32 * KB), m_bram(64 * KB), m_mscomm{},
	  m_iregs{std::vector<uint8_t>(0x10000), std::vector<uint8_t>(0x10000)},
	  m_bootrom(game.section("bootrom")),
	  m_memctl{MemCtl(*this, true), MemCtl(*this, false)},
	  m_rombd(rombd, game.section("eprom"), game.section("maskrom"),
	          actel_of(game),
	          [this](const char *what) { cpu_error("%s", what); }),
	  m_eeprom(m_clock),
	  m_bus{Bus(*this, MASTER), Bus(*this, SLAVE)}, m_hooked{false, false},
	  m_porta{0, 0}, m_irq_lines(0), m_mie_port_d(0),
	  m_line(0), m_dots_per_line(0), m_lines_per_frame(0), m_lines_shown(0), m_line_rest(0),
	  m_mie_trace(nullptr), m_sh_trace{nullptr, nullptr}, m_stats(env_flag("ODIN_STATS")),
	  m_stats_ns{}, m_stats_slices(0), m_stats_told{}, m_stats_told_at(0)
{
	std::printf("%s\n", text_at(m_bootrom, 0xD4).c_str());

	build_mie();
	build_netbd(link);
	load_eeproms();
	build_gpu();
	build_sound(MASTER);
	build_sound(SLAVE);
	/* HEURISTIC: the optional board also
	 * feeds the external inputs of the AICA of the main board. Air Trix
	 * and Virtual-On play their music on it and open EXTS on the main
	 * board, and the Air Trix cabinet has the board but main speakers
	 * only. */
	m_sound[MASTER]->set_source(m_sound[SLAVE].get());
	/* Debug: ODIN_WAV=<path>, the sound of the main board in a WAV file */
	if (const char *path = std::getenv("ODIN_WAV")) {
		m_wav = std::make_unique<WavFile>(path, Aica::SAMPLE_RATE, 2);
		if (!m_wav->ok())
			std::fprintf(stderr, "ERROR: ODIN_WAV: cannot write '%s'\n", path);
	}

	build_cpus();
	m_resetting = false;
}

/* The EEPROMs of the main board and of the ROM board: their images in the
 * list, else blank; the chips of the ROM board that guard its programs,
 * said */
void
Hikaru::load_eeproms()
{
	if (const std::span<const uint8_t> image = m_game.section("eeprom"); !image.empty()) {
		m_eeprom.load(image);
		std::printf("main board EEPROM: 93c46 image loaded\n");
	} else {
		std::printf("main board EEPROM: no image, blank\n");
	}
	if (const std::span<const uint8_t> image = m_game.section("rombd_eeprom"); !image.empty()) {
		m_rombd.eeprom().load(image);
		std::printf("ROM board EEPROM: x76f100 image loaded\n");
	} else {
		std::printf("ROM board EEPROM: no image, blank\n");
	}
	if (m_rombd.crypt())
		std::printf("ROM board 315-5881: key %08X, 64 KB RAM\n", m_rombd.config().crypt_key);
	if (m_rombd.fpga())
		std::printf("ROM board security FPGA: 317-0283-COM, ID 4252\n");
}

/* The GPU on the memories of the board, its hooks into the board */
void
Hikaru::build_gpu()
{
	HikaruGpu::Memory memory;
	HikaruGpu::Hooks hooks;

	memory.cmdram = m_cmdram;
	memory.ram_s = m_ram[SLAVE];
	memory.fb = m_fb;
	memory.texram[0] = m_texram[0];
	memory.texram[1] = m_texram[1];
	hooks.ctx = this;
	hooks.irq_changed = [](void *ctx) { static_cast<Hikaru *>(ctx)->update_irqs(); };
	/* A list the master launches starts at that date: its run ends there
	 * (run_cycles). One the slave launches starts with the next slice. */
	hooks.list_started = [](void *ctx) {
		Hikaru *const board = static_cast<Hikaru *>(ctx);

		if (board->m_current == MASTER)
			board->m_sh[MASTER]->cut();
		if (board->m_screen)
			board->m_screen->list_launched();
	};
	hooks.before_texture_write = [](void *ctx, unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
		Hikaru *const board = static_cast<Hikaru *>(ctx);

		if (board->m_screen)
			board->m_screen->before_texture_write(bank, x, y, w, h);
	};
	hooks.fb_written = [](void *ctx, size_t offs, size_t size) {
		static_cast<Hikaru *>(ctx)->m_fb_writes.wrote(offs, size);
	};
	m_gpu = std::make_unique<HikaruGpu>(memory, hooks);
	/* Debug: the instructions of the GPU, its texture transfers and its
	 * DMA, on stdout */
	m_gpu->antarctic().set_disasm(env_flag("GPU_LOG_CP") ? stdout : nullptr);
	m_gpu->antarctic().set_transfer_log(env_flag("GPU_LOG_IDMA") ? stdout : nullptr);
	m_gpu->eurasia().set_log(env_flag("GPU_LOG_DMA") ? stdout : nullptr);
}

/* The two SH-4 */
void
Hikaru::build_cpus()
{
	/* Clock operating mode 5: a 33.3333 MHz crystal, the CPU at six times
	 * that and the peripheral clock at 50 MHz. The timers of PHARRIER
	 * count 0.08 microseconds (@0C0CF150), a quarter of that clock; MAME
	 * gives the pins (hikaru.cpp, set_md). */
	for (unsigned which : { MASTER, SLAVE }) {
		m_sh[which] = std::make_unique<Sh4>(m_bus[which], std::span<uint8_t>(m_iregs[which]),
		                                    which == MASTER, true, 5);
		/* The kept blocks run as host code where the host allows it. For
		 * the tests: ODIN_SH4_TRANSLATE=0 has them read; the memory and
		 * the states are the same either way. */
		if (env_flag("ODIN_SH4_TRANSLATE", true)) {
			m_sh[which]->set_translate(true);
			if (!m_sh[which]->translates())
				std::fprintf(stderr, "sh4: no host code on this machine, the blocks are read\n");
		}
		/* For the tests: ODIN_TURNS=0, the loops that wait on a timer
		 * run every turn. */
		m_sh[which]->set_clock_waits(env_flag("ODIN_TURNS", true));
		/* Debug: ODIN_SH4_TRACE=<prefix>, one line per slice in
		 * <prefix>-m.txt and <prefix>-s.txt, the PC and a hash of the
		 * registers. */
		if (const char *prefix = std::getenv("ODIN_SH4_TRACE")) {
			const std::string path = std::string(prefix) + (which == MASTER ? "-m.txt" : "-s.txt");

			m_sh_trace[which] = std::fopen(path.c_str(), "w");
		}
	}
	m_fb_writes.set_cpu(m_sh[MASTER].get(), 0x1B000000 / Sh4::PAGE_SIZE);
}

Hikaru::~Hikaru()
{
	/* The board stops sending before its cable stops bringing frames */
	if (m_netbd_runner)
		m_netbd_runner->stop();
	if (m_netbd_cable)
		m_netbd_cable->stop();
	if (m_mie_trace)
		std::fclose(m_mie_trace);

	/* ODIN_STATS=1: what each SH-4 ran, and the host time each part of
	 * the board took. A part that waits for the screen before it writes
	 * the texture RAM has that wait in its time. */
	for (unsigned which : { MASTER, SLAVE }) {
		if (m_sh_trace[which])
			std::fclose(m_sh_trace[which]);
		if (m_stats)
			std::fprintf(stderr, "sh4 %c: %" PRIu64 " instructions run, %" PRIu64 " cycles skipped, %zu blocks"
			             " kept of %" PRIu64 " decoded, all dropped %" PRIu64 " times\n",
			             which == MASTER ? 'M' : 'S', m_sh[which]->instructions(), m_sh[which]->skipped(),
			             m_sh[which]->blocks(), m_sh[which]->blocks_decoded(), m_sh[which]->blocks_dropped());
		if (m_stats && m_sh[which]->translates())
			std::fprintf(stderr, "sh4 %c: host code of %u KB, %" PRIu64 " calls out of direct reach\n",
			             which == MASTER ? 'M' : 'S', m_sh[which]->host_bytes() / 1024,
			             m_sh[which]->host_far_calls());
		if (m_stats && m_sound[which]->arm_host_bytes())
			std::fprintf(stderr, "arm7 %c: host code of %u KB\n", which == MASTER ? 'M' : 'S',
			             m_sound[which]->arm_host_bytes() / 1024);
	}
	if (m_stats) {
		const auto seconds = [this](unsigned part) { return static_cast<double>(m_stats_ns[part]) / 1e9; };

		std::fprintf(stderr, "board: %" PRIu64 " slices: master SH-4 %.3f s, slave SH-4 %.3f s, "
		             "graphics chips %.3f s, MIE %.3f s, sound boards %.3f s\n", m_stats_slices,
		             seconds(MASTER), seconds(SLAVE), seconds(PART_GPU), seconds(PART_MIE), seconds(PART_SOUND));
	}
	if (m_stats && m_netbd)
		std::fprintf(stderr, "network board: %" PRIu64 " frames sent, %" PRIu64 " received (%" PRIu64
		             " waited for its 68000), %" PRIu64 " lost (in reset), %" PRIu64 " lost (waited too long)\n",
		             m_netbd->counts().sent, m_netbd->counts().received, m_netbd->counts().held,
		             m_netbd->counts().lost, m_netbd->counts().stale);
}

/* The drive board as far as the SH-4 ran: the command the game puts on
 * the outputs of the I/O board, the wheel where the board last saw it
 * (the same potentiometer as the converter of the I/O board) */
void
Hikaru::run_drive(uint32_t cycles)
{
	const uint64_t clocks = take_ticks(m_drive_acc, cycles, Drive838_13366::CLOCK_HZ, m_clock);

	m_drive_owed += clocks;
	/* A clock, not a number of clocks: a run ends on an instruction a
	 * little after its clock, which the next run does not add again (it
	 * ran 1.8 % fast that way) */
	if (clocks)
		m_drive->run_until(m_drive_owed, DriveBoard::command(io_outputs(), m_game.drive_first_output),
		                   static_cast<uint8_t>(m_wheel_seen >> 8));
}

/* The MIE: the chip, its ROM and its board */
void
Hikaru::build_mie()
{
	m_mie = std::make_unique<MieBoard>(m_game.section("mie"), m_mie_ram, m_clock, [this](MieBoard::Inputs &inputs) {
		if (m_panel)
			m_panel->read_inputs(inputs);
		if (m_steer_channel >= 0)
			m_wheel_seen = inputs.jvs_analog[m_steer_channel];
		/* The reply of the drive board on PUSH3-PUSH6 of both players, low
		 * when set: bit 0 on PUSH3 of player 1 to bit 7 on PUSH6 of player 2 */
		if (m_drive) {
			const uint8_t reply = m_drive->reply();

			for (unsigned player = 0; player < 2; player++) {
				const unsigned bits = ~reply >> (4 * player);
				uint16_t &word = inputs.jvs_player[player];

				word &= 0xFF0F;
				for (unsigned bit = 0; bit < 4; bit++)
					if ((bits >> bit) & 1)
						word |= static_cast<uint16_t>(0x0080 >> bit);
			}
		}
	}, m_game.io_board, m_game.section("io_rom"));
	std::printf("I/O board %s: %s\n", m_game.io_board.c_str(),
	            m_mie->runs_io_program() ? "its program run (io_rom)" : "its imitation");

	build_drive();
	m_mie->set_port_d([this](uint8_t pins) { set_resets(pins); });
	m_mie->set_log(stderr);
	/* For the tests: ODIN_TURNS=0, the programs of the MIE and of the chip
	 * of its I/O board run every turn of their waits. The memory and the
	 * states must be the same. */
	m_mie->set_waits_counted(env_flag("ODIN_TURNS", true));
	open_mie_trace();
	attach_card_reader();
	std::printf("MIE: LLE (315-6146 ROM + uploaded Z80 program)\n");
	std::printf("JVS: I/O board %s\n", m_game.io_board.c_str());
}

/* The drive board 838-13366 of NASCAR (drive_838_13366.hpp): without
 * its program, the host takes the force of each command as it comes
 * (drive_board.hpp) */
void
Hikaru::build_drive()
{
	const char *const drive = std::getenv("ODIN_DRIVE");

	if (m_game.drive_first_output && m_game.section("drive_rom").size() == Drive838_13366::ROM_SIZE &&
	    !(drive && !std::strcmp(drive, "imitation"))) {
		m_drive = std::make_unique<Drive838_13366>(m_game.section("drive_rom"));
		/* For the tests: ODIN_TURNS=0, its program runs every turn of
		 * its waits. The states must be the same. */
		m_drive->set_waits_counted(env_flag("ODIN_TURNS", true));
	}
	for (const GameControl &control : m_game.controls)
		if (control.type == GameControl::Type::ANALOG && control.role == Role::STEER && control.player == 0)
			m_steer_channel = control.analog;
	if (m_game.drive_first_output)
		std::printf("drive board: %s\n", m_drive ? "its program run (drive_rom)" : "its imitation");
}

/* Debug: ODIN_MIE_TRACE=<path>, the bus of the Z80 for
 * ODIN_MIE_TRACE_N instructions, from the first time its PC is
 * ODIN_MIE_TRACE_START */
void
Hikaru::open_mie_trace()
{
	if (const char *path = std::getenv("ODIN_MIE_TRACE")) {
		const unsigned count = static_cast<unsigned>(env_int("ODIN_MIE_TRACE_N", 300000));
		const char *const start = std::getenv("ODIN_MIE_TRACE_START");
		const unsigned start_pc = start ? static_cast<unsigned>(std::strtoul(start, nullptr, 16)) : 0;

		if (count) {
			m_mie_trace = std::fopen(path, "w");
			if (m_mie_trace)
				m_mie->set_trace(m_mie_trace, count, static_cast<uint16_t>(start_pc));
			else
				std::fprintf(stderr, "ERROR: MIE: can't open trace file '%s'\n", path);
		}
	}
}

/* The CRP-1231BR-10 card reader (card_reader.hpp) goes on the serial block
 * of the MIE when the cabinet has one ("card_reader" in the list):
 * ODIN_CARD=none leaves the line open, ODIN_CARD=reader attaches it to any
 * game (a test), ODIN_CARD_FILE=<path> is the player's card (its three
 * tracks as on the card, 216 bytes; 207, their data only, is read too),
 * ODIN_CARD_TRACE=1 logs every frame on the line to stderr. */
void
Hikaru::attach_card_reader()
{
	const char *const card = std::getenv("ODIN_CARD");

	if (card && std::strcmp(card, "none") && std::strcmp(card, "reader"))
		std::fprintf(stderr, "ERROR: MIE: ODIN_CARD=%s unknown, taken as the list says\n", card);
	if (card && !std::strcmp(card, "none")) {
		std::printf("MIE: card reader: none (ODIN_CARD=none)\n");
		return;
	}
	/* None on its cabinet */
	if (m_game.card_reader.empty() && !(card && !std::strcmp(card, "reader")))
		return;

	const char *const file = std::getenv("ODIN_CARD_FILE");
	/* Debug: ODIN_CARD_TRACE=1, what the reader says and is told */
	const char *const trace = std::getenv("ODIN_CARD_TRACE");
	const bool loaded = m_mie->attach_card(file, (trace && std::atoi(trace)) ? stderr : nullptr);
	if (file)
		std::printf("MIE: card reader CRP-1231BR-10 (its replies), card file '%s'%s\n",
		            file, loaded ? "" : " not found: the player has no card");
	else
		std::printf("MIE: card reader CRP-1231BR-10 (its replies), no card file\n");
}

void
Hikaru::build_sound(unsigned which)
{
	m_sound[which] = std::make_unique<SoundBoard>(m_sound_ram[which], m_clock, [this](bool) { update_irqs(); });
	m_sound[which]->set_log(stderr);
	/* For the tests: ODIN_TURNS=0, the programs of the sound boards run
	 * every turn of their waits. The memory and the states must be the
	 * same. */
	m_sound[which]->set_turn_replay(env_flag("ODIN_TURNS", true));
	/* For the tests: ODIN_ARM7_DECODED=0, the ARM7 decodes every
	 * instruction it runs. */
	m_sound[which]->set_arm_decoded(env_flag("ODIN_ARM7_DECODED", true));
	/* The blocks of the ARM7 run as host code where the host allows it.
	 * For the tests: ODIN_ARM7_TRANSLATE=0 has them read; the memory and
	 * the sound are the same either way. */
	m_sound[which]->set_arm_translate(env_flag("ODIN_ARM7_TRANSLATE", true));

	/* Debug: ODIN_ARM7_TRACE=<prefix> writes <prefix>-m.txt / <prefix>-s.txt */
	if (const char *prefix = std::getenv("ODIN_ARM7_TRACE")) {
		const std::string path = std::string(prefix) + (which == MASTER ? "-m.txt" : "-s.txt");

		m_sound[which]->set_arm_trace(std::fopen(path.c_str(), "w"));
	}
}

/* The battery of the clock: the RTC of the first sound board keeps counting
 * when the cabinet is off. The count it starts with here is the local time
 * of the host, as on a board whose clock was set once and whose battery
 * never died. The board keeps the clock it was set to (CLOCK SETTING in the
 * SYSTEM MENU) in the file "clock" of the save folder, or ODIN_CLOCK_FILE
 * =<path>: it holds, in seconds, how far that clock is ahead of the time of
 * the host. */
void
Hikaru::reset_sound(unsigned which)
{
	const std::string path = kept_path("clock");

	m_sound[which]->reset();
	if (path.empty() || which != MASTER)
		return;

	std::FILE *const file = std::fopen(path.c_str(), "r");
	int ahead = 0;

	if (file && std::fscanf(file, "%d", &ahead) == 1) {
		m_sound[which]->set_rtc_ahead(ahead);
		std::printf("AICA: clock file '%s': %d s ahead of the host\n", path.c_str(), ahead);
	} else {
		std::printf("AICA: clock file '%s' not found: the time of the host\n", path.c_str());
	}
	if (file)
		std::fclose(file);
}

void
Hikaru::save_clock()
{
	const std::string path = kept_path("clock");
	const std::string ahead = std::to_string(m_sound[MASTER]->rtc_ahead()) + "\n";

	if (!path.empty())
		write_kept(path, std::span(reinterpret_cast<const uint8_t *>(ahead.data()), ahead.size()));
}

/* Power-on. The memories are emptied, but the backup RAM on its battery,
 * then the chips are reset, in an order that matters to nothing: what one
 * tells another meanwhile waits for the end (m_resetting). */
void
Hikaru::reset()
{
	if (m_screen)
		m_screen->before_memory_changes();

	m_resetting = true;

	for (unsigned which : { MASTER, SLAVE }) {
		std::fill(m_ram[which].begin(), m_ram[which].end(), 0);
		std::fill(m_texram[which].begin(), m_texram[which].end(), 0);
		std::fill(m_sound_ram[which].begin(), m_sound_ram[which].end(), 0);
		std::fill(m_iregs[which].begin(), m_iregs[which].end(), 0);
	}
	std::fill(m_cmdram.begin(), m_cmdram.end(), 0);
	std::fill(m_fb.begin(), m_fb.end(), 0);
	m_fb_writes.wrote_all();
	std::fill(m_mie_ram.begin(), m_mie_ram.end(), 0);

	m_memctl[MASTER].reset();
	m_memctl[SLAVE].reset();
	m_mscomm.fill(0);
	m_mie->reset();
	if (m_drive) {
		m_drive->reset();
		m_drive_acc = 0;
		m_drive_owed = m_drive->clocks();
	}
	if (m_netbd) {
		const auto hold = netbd_meet();

		m_netbd->reset();
		m_netbd_acc = 0;
		m_netbd_owed = m_netbd->clocks();
	}
	m_gpu->reset();
	reset_sound(MASTER);
	reset_sound(SLAVE);
	map_pages();
	m_sh[MASTER]->reset(true);
	m_sh[SLAVE]->reset(true);

	m_resetting = false;

	m_sh[SLAVE]->set_run(Sh4Run::Running);

	/* The pins of port A are active low. */
	m_porta[MASTER] = 0xFFFF;
	m_porta[SLAVE] = 0xFFFF;
	/* HEURISTIC: nothing is known to pull the NMI pin of the master. */
	m_sh[MASTER]->set_nmi(true);
	m_sh[SLAVE]->set_nmi(true);
	m_mie_port_d = 0xFF;
	set_gpu_reset_lines();

	m_rombd.reset();

	m_sched.reset();
	m_line_rest = 0;
	begin_frame();

	update_irqs();

	if (m_screen)
		m_screen->memory_changed();
	m_frame = 0;
}

/*
 * IRQs
 * ====
 *
 * The master SH-4 is configured for independent per-pin external IRQs. It
 * looks like that the IRQ sources are as follows:
 *
 *  IRL0 unused/unhandled
 *  IRL1 memory controller DMA termination
 *  IRL2 the interrupt controller of the board, see below
 *  IRL3 unused/unhandled
 *
 * IRQ-related Registers
 * =====================
 *
 * The IRL2 handler (@0C00174C) reads PDTRA, active low, to tell the
 * sources apart (the table below; @0C000A30 is the one of PDTRA 0100). The
 * causes of the GPU: hikaru_gpu.cpp.
 *
 * The interrupt controller
 * ========================
 *
 * Five lines, named as on the schematics (MAME hikaru.cpp, MIE GPIO
 * ports), go to port B of the MIE, and four of them to port A of the
 * master as well:
 *
 *  ANT_INT	the command processor	port B bit 0	PDTRA 0040
 *  SZK_INT	the sound boards	port B bit 4	PDTRA 0080
 *  CN4_INT	connector CN4		port B bit 5	PDTRA 0100
 *  CN3_INT0	connector CN3		port B bit 6
 *  CN3_INT1	connector CN3		port B bit 7	PDTRA 0200
 *
 * The MIE holds IRL2 while one of them passes the mask its program writes
 * to port C (0xE1 in every game: the sound boards do not interrupt). Each
 * is a level, held until its handler has acknowledged the chip it comes
 * from. Nothing is on CN3 and CN4.
 *
 * HEURISTIC: the second sound board is taken to share SZK_INT with the
 * first, whose handler acknowledges both (PHARRIER @0C0102F2).
 */

void
Hikaru::update_irqs()
{
	uint8_t lines = 0;

	/* A chip may call while the board is being built. While it is reset
	 * the lines wait for the end: the registers of the SH-4 are cleared
	 * first, and a chip that is not reset yet may still ask. */
	if (m_resetting)
		return;

	if (m_gpu->irq_line())
		lines |= 0x01;
	if (m_sound[MASTER]->main_irq() || m_sound[SLAVE]->main_irq())
		lines |= 0x10;
	m_irq_lines = lines;
	m_mie->set_irq_lines(lines);

	m_sh[MASTER]->set_irq(Sh4Source::IRL1, m_memctl[MASTER].irq_line());
	m_sh[MASTER]->set_irq(Sh4Source::IRL2, m_mie->irq_out());
}

/*
 * Resets
 * ======
 *
 * Port D of the MIE holds the reset lines of the board, active low (MAME
 * hikaru.cpp, MIE GPIO ports): bit 0 the system, 1 the graphics chips
 * (GP_RES), 2 the slave SH-4, 3 CN4, 4 the display chip (PCI1_RES), 5 the
 * sound boards; bits 6 and 7 choose the video clock. The master writes the
 * port through the window of the MIE:
 *
 *  - the BOOTROM, as soon as the MIE gives its bus (@0C00BE52): 0xFB, then
 *    0xFF some 250 cycles later, for the slave;
 *  - the system library of the game (0xDF, then 0xFF): the sound boards;
 *  - VON4, when two lists in a row did not end (@0C711C30): 0xEF, 0xFD,
 *    then 0xFF, for the graphics chips, see HikaruGpu::set_reset_lines.
 *
 * The slave is held while its line is low and starts again from its reset
 * vector when it goes high. The BOOTROM uses that to know the slave ready:
 * holding the bus of the MIE (@0C00BE44), the master drives port D at 0xFB,
 * port E at 0xF0 (bits 0-3, the LEDs 7 to 10, low), then port D at 0xFF;
 * the slave starts again, clears the word where the master leaves its jobs
 * (@0C00BEAE, its 10000000, 14000000 of the master), then through its own
 * window on the MIE reads port E and writes 0xFF until it has read bits 0-3
 * high (@0C00BEE0); the master waits for these bits (@0C00B860) before it
 * copies the game from the ROM board and leaves its first job. HEURISTIC:
 * the line resets the SH-4 as at power-on, and not its bridge.
 */

/* Bits 1 and 4 of port D, to the graphics chips */
void
Hikaru::set_gpu_reset_lines()
{
	m_gpu->set_reset_lines(!(m_mie_port_d & 0x02), !(m_mie_port_d & 0x10));
}

/* The pins of port D of the MIE, each time it drives them. */
void
Hikaru::set_resets(uint8_t pins)
{
	const uint8_t fell = m_mie_port_d & static_cast<uint8_t>(~pins);
	const uint8_t rose = pins & static_cast<uint8_t>(~m_mie_port_d);

	m_mie_port_d = pins;

	if ((pins & 0xC0) != 0xC0)
		stop_fatal("The 24 kHz screen is not supported yet (video clock %u).\n"
		           "DISPLAY MODE 24KHz was not kept: start the game again.", pins >> 6);
	if (~pins & 0x09)
		stop_fatal("resets %02X asked on port D of the MIE, which are not implemented", ~pins & 0x09);

	set_gpu_reset_lines();

	/* The slave stays stopped from its reset to the end of it. Its clock
	 * stays that of the board, as after a load. */
	if (fell & 0x04) {
		m_sh[SLAVE]->reset(true);
		m_sh[SLAVE]->set_time(m_sched.now());
	}
	if (rose & 0x04)
		m_sh[SLAVE]->set_run(Sh4Run::Running);

	/* The boards are held as long as the line is low, see run_cycles. */
	if (fell & 0x20) {
		m_sound[MASTER]->reset_pin();
		m_sound[SLAVE]->reset_pin();
		update_irqs();
	}
}

int
Hikaru::run_cpu(unsigned which, int cycles)
{
	m_current = which;

	/* The address the board looks at the program at, see Bus::hook */
	if (!m_hooked[which]) {
		m_hooked[which] = true;
		if (which == SLAVE)
			m_sh[which]->set_hook(0x0C002030);
	}

	m_sh[which]->run(cycles);
	if (m_sh_trace[which])
		trace_slice(which);
	return m_sh[which]->ran();
}

/* A slice of the frame, up to `cycles` long; returns the cycles it took.
 * The display list goes first, one instruction a cycle of the SH-4
 * (HEURISTIC): a list that ends in the slice ends it, and the end is told
 * at its date, for the master to take the interrupt there. A list the
 * master launches ends the slice too, see the hook list_started. */
int
Hikaru::run_cycles(int cycles)
{
	/* ODIN_STATS: the host time since the part before goes to this one. */
	uint64_t mark = m_stats ? host_ns() : 0;
	const auto took = [this, &mark](unsigned part) {
		if (m_stats) {
			const uint64_t now = host_ns();

			m_stats_ns[part] += now - mark;
			mark = now;
		}
	};

	uint32_t length = static_cast<uint32_t>(cycles);
	const bool list_ends = m_gpu->run_list(length);
	took(PART_GPU);

	const int ran = run_cpu(MASTER, static_cast<int>(length));
	took(MASTER);
	run_cpu(SLAVE, ran);
	took(SLAVE);

	m_eeprom.tick(static_cast<uint32_t>(ran));

	/* The memory controllers have nothing to run: a transfer is done at
	 * once. The end of the list is told here, at its date, unless a list
	 * launched in the slice has taken its place. */
	if (list_ends && !m_gpu->antarctic().running())
		m_gpu->list_ended();

	m_mie->run(static_cast<uint32_t>(ran));
	/* Its program may have written the mask of the interrupts. */
	update_irqs();
	if (m_drive)
		run_drive(static_cast<uint32_t>(ran));
	if (m_netbd)
		run_netbd(static_cast<uint32_t>(ran));
	took(PART_MIE);

	/* The two sound boards, unless their reset line is low */
	if (m_mie_port_d & 0x20) {
		m_sound[SLAVE]->run(static_cast<uint32_t>(ran));
		m_sound[MASTER]->run(static_cast<uint32_t>(ran));
	} else {
		m_sound[MASTER]->silence(static_cast<uint32_t>(ran));
		m_sound[SLAVE]->silence(static_cast<uint32_t>(ran));
	}
	took(PART_SOUND);
	m_stats_slices++;
	return ran;
}

/* The cycles of the next line. A line is not a whole number of cycles: the
 * rest is kept. */
uint64_t
Hikaru::line_cycles()
{
	const uint64_t n = static_cast<uint64_t>(m_dots_per_line) * m_clock + m_line_rest;

	m_line_rest = static_cast<uint32_t>(n % DOT_CLOCK);
	return n / DOT_CLOCK;
}

/* Starts a frame with the lines the game has set by now. The line counter
 * of the graphics chips is 0 on the first line shown (PHARRIER waits for 240
 * to 479, @0C01C106): a frame goes from that line to the end of the
 * blanking. */
void
Hikaru::begin_frame()
{
	uint32_t dots, lines, shown;

	m_gpu->eurasia().frame_size(dots, lines, shown);
	m_dots_per_line = dots;
	m_lines_per_frame = lines;
	m_lines_shown = shown;
	if (m_dots_per_line != 818)
		stop_fatal("A screen of %u dots a line is not supported yet.\nOdin has only the 31 kHz one.",
		           m_dots_per_line);
	if (m_lines_shown == 0 || m_lines_shown >= m_lines_per_frame)
		stop_fatal("a screen of %u lines with %u shown", m_lines_per_frame, m_lines_shown);

	m_line = 0;
	m_sched.set(EVENT_LINE, m_sched.now() + line_cycles());
}

/* The chips run up to the next event, then the events due are taken. */
void
Hikaru::run_frame()
{
	m_sound[MASTER]->clear_samples();
	m_sound[SLAVE]->clear_samples();
	bool done = false;

	if (m_verbose)
		std::printf(" *** VBLANK-OUT %s ***\n", board_string().c_str());

	while (!done) {
		const uint64_t next = m_sched.next();
		const int ran = run_cycles(static_cast<int>(next - m_sched.now()));
		uint32_t event;

		m_sched.advance(m_sched.now() + static_cast<uint64_t>(ran));

		while (m_sched.take(event)) {
			m_gpu->end_of_line(m_line++);
			if (m_line == m_lines_shown) {
				if (m_verbose)
					std::printf(" *** VBLANK-IN  %s ***\n", board_string().c_str());
				m_gpu->vblank_in();
			}
			if (m_line < m_lines_per_frame) {
				m_sched.set(EVENT_LINE, next + line_cycles());
				continue;
			}

			/* this may actually be an hblank-out IRQ */
			m_gpu->vblank_out();

			done = true;
		}
	}
	begin_frame();
	m_frame++;
	if (m_netbd)
		netbd_sync();
	/* The game made its block of settings: the cabinet takes its seat and
	 * is to be switched off and on (take_cabinet) */
	if (m_seat_owed && cabinet::backup_at_rest(m_bram) && take_seat()) {
		m_seat_owed = false;
		m_restart_asked = true;
		std::printf("network board: the cabinet in its seat, switched off and on\n");
	}

	if (m_wav)
		m_wav->write(m_sound[MASTER]->samples());

	/* ODIN_STATS: every 60 frames, the host time of a frame in all and
	 * that of each part, in ms. What is not in the parts went to the rest
	 * of the frame: the screen, the wait for the drawing. */
	if (m_stats && m_frame % 60 == 0) {
		const uint64_t now = host_ns();
		const auto ms = [](uint64_t ns) { return static_cast<double>(ns) / 60e6; };

		if (m_stats_told_at)
			std::fprintf(stderr, "PARTS frame=%u all %.2f master %.2f slave %.2f chips %.2f mie %.2f sound %.2f\n",
			             m_frame, ms(now - m_stats_told_at), ms(m_stats_ns[MASTER] - m_stats_told[MASTER]),
			             ms(m_stats_ns[SLAVE] - m_stats_told[SLAVE]),
			             ms(m_stats_ns[PART_GPU] - m_stats_told[PART_GPU]),
			             ms(m_stats_ns[PART_MIE] - m_stats_told[PART_MIE]),
			             ms(m_stats_ns[PART_SOUND] - m_stats_told[PART_SOUND]));
		std::copy(std::begin(m_stats_ns), std::end(m_stats_ns), std::begin(m_stats_told));
		m_stats_told_at = now;
	}
}

/* The idle loop of the slave, in the BOOTROM, counts in R7 while the byte
 * the master posts at GBR stays zero:
 *
 *	0C002030  mov.b @(0,gbr),r0
 *	          mov   #2,r2
 *	          shll8 r2
 *	          addc  r2,r7
 *	          mov   #-128,r1
 *	          bf/s  1f
 *	          shll16 r1
 *	          (on a carry: writes port A)
 *	1:        tst   r0,r0
 *	          mov   #0,r0
 *	          bt    0C002030
 *
 * A turn without a carry takes ten steps and only adds 0x201 to R7, and
 * nothing else runs before the end of the slice: such turns are counted
 * without being run, up to the turn that carries or the last of the slice. */
void
Hikaru::slave_idle_loop()
{
	Sh4 &cpu = *m_sh[SLAVE];
	const int32_t left = cpu.remaining();
	const uint32_t gbr = cpu.gbr();
	uint16_t code[19];

	if ((cpu.pc() & 0x1FFFFFFF) != 0x0C002030 || left <= 10)
		return;

	std::memcpy(code, &m_ram[SLAVE][0x2030], sizeof(code));
	if (code[0] != 0xC400 || code[1] != 0xE202 || code[2] != 0x4218 ||
	    code[3] != 0x372E || code[4] != 0xE180 ||
	    (code[5] & 0xFF80) != 0x8F00 || code[6] != 0x4128)
		return;

	const uint32_t tst = 7u + (code[5] & 0x7F);
	if (tst > 16 || code[tst] != 0x2008 || code[tst + 1] != 0xE000 ||
	    code[tst + 2] != (0x8900 | ((0 - (tst + 4)) & 0xFF)))
		return;

	/* The registers a turn leaves, and the posted byte still zero */
	if (cpu.reg(0) != 0 || cpu.reg(1) != 0xFF800000 || cpu.reg(2) != 0x200 || !(cpu.sr() & 1) ||
	    (gbr >> 29) == 7 || (gbr & 0x1E000000) != 0x0C000000 ||
	    m_ram[SLAVE][gbr & 0x01FFFFFF])
		return;

	const uint32_t turns = std::min((0xFFFFFFFF - cpu.reg(7)) / 0x201,
	                                static_cast<uint32_t>(left - 1) / 10);
	if (!turns)
		return;
	cpu.set_reg(7, cpu.reg(7) + turns * 0x201);
	cpu.skip(turns * 10);
}

/* Before the wave memories are saved or written to a file: the ARM7 of a
 * sound board puts there what it was holding back. */
void
Hikaru::settle()
{
	m_sound[MASTER]->settle();
	m_sound[SLAVE]->settle();
}

/* What the board keeps without power, by the names MAME gives them: the
 * 93C46 of the main board, the 93C46 of the MIE (the SYSTEM MENU of the
 * BOOTROM: country, coin chutes...), the X76F100 of the ROM board and the
 * backup RAM (the records and settings of the game). */
std::vector<std::pair<const char *, std::span<uint8_t>>>
Hikaru::kept_memories()
{
	std::vector<std::pair<const char *, std::span<uint8_t>>> memories = {
		{ "main_eeprom", m_eeprom.data() },
		{ "rombd_eeprom", m_rombd.eeprom().data() },
		{ "nvram", m_bram },
		{ "mie_eeprom", m_mie->eeprom().data() },
	};

	return memories;
}

/* The file of a memory kept, or of the clock, in the save folder. For the
 * tests, ODIN_MIE_EEPROM=<path> keeps the EEPROM of the MIE elsewhere (the
 * settings of a cabinet) and ODIN_CLOCK_FILE=<path> the clock. Empty when
 * there is no place for it. */
std::string
Hikaru::kept_path(const std::string &name) const
{
	const char *const variable = name == "mie_eeprom" ? "ODIN_MIE_EEPROM" : name == "clock" ? "ODIN_CLOCK_FILE" : nullptr;

	if (const std::string path = variable ? env_text(variable) : std::string(); !path.empty())
		return path;
	return m_saves.path(name);
}

void
Hikaru::keep_in(const SaveFolder &saves)
{
	m_saves = saves;
	if (saves.none())
		std::printf("saves: none\n");
	else
		std::printf("saves: '%s'\n", saves.dir().c_str());

	bool settings_kept = false;

	for (const auto &[name, bytes] : kept_memories()) {
		const std::string path = kept_path(name);

		if (path.empty())
			continue;
		if (const std::optional<std::vector<uint8_t>> kept = read_kept(path, bytes.size())) {
			std::copy(kept->begin(), kept->end(), bytes.begin());
			std::printf("%s: '%s' loaded\n", name, path.c_str());
			settings_kept = settings_kept || !std::strcmp(name, "mie_eeprom");
		}
	}
	/* Without settings of its own, the chip of a new cabinet of the game
	 * ("mie_eeprom" in the list): SEGA wrote the country of the market in
	 * it at the factory, which the BOOTROM does not do (a blank chip is
	 * JAPAN for it, and no page of its menu reaches the country). */
	if (!settings_kept && m_game.mie_eeprom.size() == m_mie->eeprom().data().size()) {
		std::copy(m_game.mie_eeprom.begin(), m_game.mie_eeprom.end(), m_mie->eeprom().data().begin());
		std::printf("mie_eeprom: the settings of a new cabinet\n");
	}
	take_cabinet();
}

bool
Hikaru::power_off()
{
	const struct {
		const char *name;
		std::span<const uint8_t> bytes;
	} memories[] = {
		{ "ram-m", m_ram[MASTER] }, { "ram-s", m_ram[SLAVE] }, { "cmdram", m_cmdram },
		{ "texram-0", m_texram[0] }, { "texram-1", m_texram[1] }, { "fb", m_fb },
		{ "aica-m", m_sound_ram[MASTER] }, { "aica-s", m_sound_ram[SLAVE] },
		{ "bram", m_bram }, { "mie", m_mie_ram },
	};

	settle();
	/* For the tests: ODIN_DUMPS=1, each memory in <game>-<memory>.bin */
	if (env_flag("ODIN_DUMPS")) {
		for (const auto &memory : memories) {
			const std::string path = m_game.name + "-" + memory.name + ".bin";
			std::FILE *const file = std::fopen(path.c_str(), "wb");

			if (!file)
				continue;
			std::fwrite(memory.bytes.data(), 1, memory.bytes.size(), file);
			std::fclose(file);
		}
		/* The network board, as a state keeps it */
		if (m_netbd) {
			const auto board = std::make_unique<NetBoard837_13404::State>();
			const std::string path = m_game.name + "-netbd.bin";

			std::memset(board.get(), 0, sizeof(*board));
			{
				const auto hold = netbd_meet();

				m_netbd->save(*board);
			}
			if (std::FILE *const file = std::fopen(path.c_str(), "wb")) {
				std::fwrite(board.get(), 1, sizeof(*board), file);
				std::fclose(file);
			}
		}
	}

	bool kept = true;

	for (const auto &[name, bytes] : kept_memories())
		if (const std::string path = kept_path(name); !path.empty())
			kept = write_kept(path, bytes) && kept;
	return kept;
}

std::string
Hikaru::cpu_string(unsigned which) const
{
	char text[64];

	std::snprintf(text, sizeof(text), "%c @%08X @%08X %08X", which == MASTER ? 'M' : 'S',
	              m_sh[which]->pc(), m_sh[which]->pr(), m_sh[which]->sr());
	return text;
}

std::string
Hikaru::board_string() const
{
	char text[512];

	std::snprintf(text, sizeof(text), "[%s %04X] [%s %04X] [%s]",
	              cpu_string(MASTER).c_str(), m_porta[MASTER],
	              cpu_string(SLAVE).c_str(), m_porta[SLAVE],
	              m_gpu->debug_string().c_str());
	return text;
}

/* A message goes out whole, in one write: the drawing thread writes to
 * stderr too, and a formatted print to a stream without a buffer is a
 * system call for each of its pieces on some hosts. One that repeats is
 * told once, whatever the program was doing (repeats.hpp); `kind` is the
 * format it comes from. */
void
Hikaru::tell_error(const char *kind, const std::string &where, const char *what)
{
	if (!repeats::say(kind, what))
		return;

	const std::string line = "ERROR: " + where + " : " + what + "\n";

	std::fputs(line.c_str(), stderr);
}

/* What a program did that has no answer here, with where the SH-4 that runs
 * is, or with where the whole board is. */
void
Hikaru::cpu_error(const char *fmt, ...)
{
	char what[256];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);
	tell_error(fmt, cpu_string(m_current), what);
}

void
Hikaru::board_error(const char *fmt, ...)
{
	char what[256];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);
	tell_error(fmt, board_string(), what);
}

/* With -v: an access to what is worth watching */
void
Hikaru::log_access(const char *name, char rw, unsigned size, uint32_t addr, uint64_t value)
{
	if (!m_verbose)
		return;
	if (rw == 'R')
		std::printf("%s : %s R%u %08X\n", board_string().c_str(), name, size * 8, addr);
	else
		std::printf("%s : %s W%u %08X = %" PRIX64 "\n", board_string().c_str(), name, size * 8, addr, value);
}

/* A line of ODIN_SH4_TRACE: the PC and a hash of the registers, FNV-1a over
 * them one after the other. */
void
Hikaru::trace_slice(unsigned which)
{
	Sh4State st;
	uint64_t hash = FNV_START;
	const auto add = [&hash](const void *data, size_t size) { hash = fnv1a(hash, data, size); };

	m_sh[which]->save(st);
	add(st.r, sizeof(st.r));
	add(&st.pc, 4);
	add(&st.sr, 4);
	add(&st.pr, 4);
	add(&st.gbr, 4);
	add(&st.vbr, 4);
	add(&st.dbr, 4);
	add(&st.macl, 4);
	add(&st.mach, 4);
	add(&st.spc, 4);
	add(&st.ssr, 4);
	add(&st.sgr, 4);
	add(st.rbank, sizeof(st.rbank));
	add(st.fr, sizeof(st.fr));
	add(st.xf, sizeof(st.xf));
	add(&st.fpul, 4);
	add(&st.fpscr, 4);
	add(st.tmu_counter, sizeof(st.tmu_counter));
	add(&st.irq_index, 4);

	std::fprintf(m_sh_trace[which], "%08X %016" PRIX64 "\n", st.pc, hash);
}

} /* namespace odin */
