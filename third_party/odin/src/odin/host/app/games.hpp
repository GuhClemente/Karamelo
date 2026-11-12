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

#ifndef ODIN_GAMES_HPP
#define ODIN_GAMES_HPP

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "odin/host/input/roles.hpp"
#include "odin/host/app/rom_files.hpp"
#include "odin/machine/cabinet/jvs.hpp"

struct json_t;

namespace odin {

/* A switch of a player on the JVS I/O board: the player, 0 or 1, and its
 * bit in the word of that player (see CabinetInputs). */
struct JvsSwitch {
	uint8_t player;
	uint16_t mask;
};

/* A switch of a Gaelco board (machine atvtrack or smashdrv): its group, 0
 * to 3, of the four the FPGA puts on port A of the master, and its bit in
 * the group, 0 to 7 ("1.4": group 1, bit 4) */
struct PortSwitch {
	uint8_t group;
	uint8_t bit;
};

/* A control of the cabinet of a game, with the name its INPUT TEST gives
 * it, and what it drives on the JVS I/O board:
 *
 *  - BUTTON: one switch ("switch" : "1P PUSH1" in the list);
 *  - ANALOG: an analog channel, 0 to 7, and the way of the control that
 *    gives its low values ("analog" : 0, "low" : "left"); up is away from
 *    the player, down towards;
 *  - POSITIONS: a lever whose each position closes some switches, none
 *    for some ("positions" : [ { "name" : "1", "switches" : [...] }, ...]).
 *
 * "id" is the name the settings give it (odin.ini: [controls.<game>],
 * "<id> = ..."; a lever has "<id>_down" and "<id>_up", and an analog
 * control that is not a pedal the halves of its way, "<id>_left" and
 * "<id>_right" or "<id>_up" and "<id>_down"): small letters,
 * figures and _, its own in the game. "role" is what the control does
 * (roles.hpp), which gives its keys and pad inputs until the player
 * changes them: a role that is pressed or not for a button, one that has a
 * position for an analog channel, "gear" for a lever. "player" : 2 for a
 * control of player 2, player 1 else.
 *
 * On a Gaelco board a control also says where it goes on the board, as
 * the test page of its game shows it: a switch its pin ("port" : "1.4"),
 * an analog control the values its channel of the ADC ("analog", 0 to 3)
 * takes at its low end and at its high end ("span" : [254, 2]), or the half
 * of the stick that kicks the ball of the player it is ("ball" : "side",
 * its low end left, or "forward", its low end up; Gaelco Football). */
struct GameControl {
	enum class Type { BUTTON, ANALOG, POSITIONS };
	enum class Low { LEFT, RIGHT, UP, DOWN, RELEASED, PRESSED };
	enum class Ball { NONE, SIDE, FORWARD };
	struct Position {
		std::string name;
		std::vector<JvsSwitch> switches;
	};

	/* The ids of what every cabinet has, which no control of a game takes:
	 * the SERVICE switch of each player, the coins, TEST of the I/O board,
	 * Test and Service of the main board. */
	static constexpr const char *CABINET_IDS[] = { "1p_service", "2p_service", "coin1", "coin2", "test",
	                                               "board_test", "board_service" };

	/* The suffix of the half of an analog control that goes left or up,
	 * or right or down (positive: the way the axis of a gamepad grows);
	 * none for a pedal. */
	static const char *half(Low low, bool positive)
	{
		if (low == Low::LEFT || low == Low::RIGHT)
			return positive ? "_right" : "_left";
		if (low == Low::UP || low == Low::DOWN)
			return positive ? "_down" : "_up";
		return nullptr;
	}
	/* Whether its low end is the far one of the axis of the host */
	static bool turned_over(Low low) { return low == Low::RIGHT || low == Low::DOWN || low == Low::PRESSED; }

	std::string name;
	std::string id;
	Type type;
	JvsSwitch button;
	uint8_t analog;
	Low low;
	std::vector<Position> positions;
	Role role;
	uint8_t player;		/* 0 or 1 */
	/* The keys of a button in place of those of its role (SDL names), as
	 * the drivers of MAME give some controls their own keys, and its
	 * inputs of the gamepad ("righttrigger", "a": bindings.hpp), as a
	 * trigger is the trigger of a nozzle; none: those of the role */
	std::vector<std::string> keys, pad;
	/* The part of a known wheel a switch takes in place of its role's
	 * ("gas", "paddle_left": bindings.hpp), as a pedal of Smashing Drive is
	 * a switch; empty: the role's */
	std::string wheel;
	std::optional<PortSwitch> port;
	std::optional<std::array<uint8_t, 2>> span;
	Ball ball = Ball::NONE;
	/* An analog control that is no pedal the player may turn over
	 * ("invertible" : true; [players.<game>] invert) */
	bool invertible = false;
};

/* The players with a sight among the controls of a game: player 1, then
 * player 2 */
inline std::array<bool, 2>
sights_of(const std::vector<GameControl> &controls)
{
	std::array<bool, 2> sights{};

	for (const GameControl &control : controls)
		if (control.role == Role::GUN_X)
			sights[control.player & 1] = true;
	return sights;
}

/* Whether one of the controls of a game steers (Role::STEER) */
inline bool
steers_of(const std::vector<GameControl> &controls)
{
	return std::any_of(controls.begin(), controls.end(),
	                   [](const GameControl &control) { return control.role == Role::STEER; });
}

/* An output of the I/O board of a cabinet, with the name its OUTPUT TEST
 * or its wiring gives it: "output", 1 to 32 as JVS counts them (output 1
 * is bit 7 of the first byte; on a Gaelco board, output n is bit n - 1 of
 * the group 7800 of port A for 1 to 11 (AtvTrack::system_pins; bits 11-15
 * pick the group), channel n - 17 of the motor DAC running for 17 to 20),
 * and "role", what the host does with it: "rumble", the gamepad of the
 * player shakes while it is on; "mute", the vibration device the optional sound board feeds is still
 * while it is on; "lamp", "lock" (a solenoid that holds a control) and
 * "counter" (a coin counter), nothing but what every output does: go to
 * other programs when the user asks (outputs_network.hpp). "player" : 2
 * for one of player 2. "id" as for a control. */
struct GameOutput {
	enum class Role { LAMP, LOCK, RUMBLE, MUTE, COUNTER };

	std::string name;
	std::string id;
	unsigned output;	/* 1 to 32 */
	Role role;
	uint8_t player;		/* 0 or 1 */

	/* Whether it is on, among the outputs of the board */
	bool on(std::span<const uint8_t> outputs) const
	{
		return jvs::output_on(outputs, output - 1);
	}
};

/* Whether an output of the list with that role is on */
inline bool
any_on(const std::vector<GameOutput> &list, GameOutput::Role role, std::span<const uint8_t> outputs)
{
	for (const GameOutput &output : list)
		if (output.role == role && output.on(outputs))
			return true;
	return false;
}

/* A part of the sound of the cabinet, as the page Audio draws it: "part"
 * (speaker, woofer, shaker, or unwired, a channel of the board with no
 * part on the cabinet), "at" (its place in the drawing, none when
 * unwired), what feeds it ("board": main or option, or "channel": 0 to 3
 * of a Gaelco board's DAC), "cabinet" (only on that model of the list's
 * "cabinets", or on the "standard" one). */
struct SoundPart {
	enum class Kind : uint8_t { SPEAKER, WOOFER, SHAKER, UNWIRED };
	enum class Place : uint8_t { NONE, FRONT_LEFT, FRONT_RIGHT, BASE, PANEL, SEAT_LEFT, SEAT_RIGHT, SEAT };

	Kind kind = Kind::SPEAKER;
	Place at = Place::NONE;
	bool option = false;
	int channel = -1;
	std::string cabinet;
};

/* The ROM board of a game, what changes from one to another (MAME
 * hikaru.cpp): "eprom_mb", the size of an EPROM, 2 or 4 MB; "315-5881",
 * the key of that chip in 8 hexadecimal figures, when the board has it;
 * "actel", {"key": 8, "id": 4 hexadecimal figures} of the security FPGA,
 * when the board has it. */
struct GameRomBoard {
	unsigned eprom_mb = 0;
	std::optional<uint32_t> crypt_key;
	bool has_actel = false;
	uint32_t actel_key = 0;
	uint16_t actel_id = 0;
};

/* A game and what its ROMs hold, section by section ("bootrom", "eprom",
 * "maskrom", "mie", "eeprom", "rombd_eeprom"). */
struct Game {
	std::string name;
	/* The name it is shown under: "title" in the list, 1 to MAX_TITLE
	 * printable ASCII characters; the name when the list does not say. */
	std::string title;
	static constexpr size_t MAX_TITLE = 48;
	std::string machine;
	/* The game it is a version of (MAME's cloneof): "parent" in the list;
	 * none when it does not say. A version is its parent but what its own
	 * entry says (its name, title and files at least), and takes its
	 * parent's pictures when it has none. */
	std::string parent;
	/* The game whose settings it shares ([controls.<game>]...): its
	 * parent, or itself */
	const std::string &family() const { return parent.empty() ? name : parent; }
	/* The JVS I/O board of its cabinet: "io" in the list, 837-13551 when
	 * the list does not say. */
	std::string io_board;
	/* The controls of its cabinet: "controls" in the list, none when the
	 * list does not say. */
	std::vector<GameControl> controls;
	/* The outputs of its cabinet the host uses: "outputs" in the list,
	 * none when the list does not say. */
	std::vector<GameOutput> outputs;
	/* The switches the wiring of its cabinet ties to the ground, closed
	 * for good: "grounded" in the list, none when it does not say; on a
	 * Gaelco board, pins of its port ("1.6"). */
	std::vector<JvsSwitch> grounded;
	std::vector<PortSwitch> grounded_pins;
	/* The other cabinets the game was sold in, which its program tells
	 * apart (NASCAR: Deluxe): "cabinets" in the list, [{"id": ..., "name":
	 * ..., "grounded": [...], "controls": {<id>: {fields in place of those
	 * of that control}}, "seated": false when it is played standing}]. The
	 * cabinet of the list is "standard". */
	struct Cabinet {
		std::string id, name;
		std::vector<GameControl> controls;
		std::vector<JvsSwitch> grounded;
		std::optional<bool> motion_platform, seated;
		std::optional<std::vector<GameOutput>> outputs;
		std::vector<unsigned> grounded_analog;
		/* A byte of the settings the game keeps in its NAND that this
		 * cabinet's operator sets in the test menu: "nand_setting" in the
		 * list, {"byte": "0016", "value": 3, "standard": 2}, its value and
		 * the standard cabinet's (ATV Track's Vibrator: MOTION SYSTEM,
		 * VIBRATOR and PLATFORM HARD: byte 0x16 of the settings at
		 * 0x8C1B91F8, which 0x8C05E41C takes as its mode, in Ghidra; 3 and 2
		 * as the test menu shows them) */
		struct Setting {
			uint32_t byte;
			uint8_t value, standard;
		};
		std::optional<Setting> nand_setting;
	};
	std::vector<Cabinet> cabinets;
	/* The ADC channels of a Gaelco board its cabinet ties to the ground,
	 * which read 0: "grounded_analog" of a cabinet (ATV Track's Vibrator
	 * has no sensors of a platform) */
	std::vector<unsigned> grounded_analog;
	/* The one played: its controls and wiring become those of that cabinet;
	 * false when the game has no such cabinet */
	std::string cabinet = "standard";
	bool use_cabinet(const std::string &id);
	/* Its optional sound board is wired on the cabinet played: false when
	 * "sound_parts" leave it unwired there (Star Wars Racer's Mini Deluxe) */
	bool option_wired() const;
	/* The 93C46 of the MIE of a new cabinet, the settings of the SYSTEM
	 * MENU as SEGA left them: "mie_eeprom" in the list, 128 bytes in
	 * hexadecimal; empty when the list does not say (a blank chip). */
	std::vector<uint8_t> mie_eeprom;
	/* The cabinet has speakers for the optional sound board as well as for
	 * the main one: "option_speakers" in the list, false when it does not
	 * say. */
	bool option_speakers = false;
	/* The level of the optional board against the main one when the two
	 * share two speakers (-o mix), in dB: "option_level" in the list,
	 * LOWEST_LEVEL to HIGHEST_LEVEL, with "option_speakers" or
	 * "option_vibration" (a sound the user may hear too); 0 when it does
	 * not say. HEURISTIC: on the cabinet the volumes of the switch
	 * unit set it, through an electronic volume no source gives the law of
	 * (the NASCAR manual draws REAR lower than FRONT at the factory); the
	 * levels of the list come from levels measured in a race. */
	double option_level = 0;
	/* The sound of the optional board feeds a vibration device of player 1
	 * (the control panel of SWRACER): "option_vibration" in the list,
	 * false when it does not say. */
	bool option_vibration = false;
	/* The cabinet has a woofer, fed by the board's second pair of channels
	 * (the Gaelco boards, AudioOut::woofer_pairs): "woofer" in the list,
	 * false when it does not say. */
	bool woofer = false;
	/* The wheel of player 1 has a motor the board drives (Board::drive_torque,
	 * the Gaelco boards): "wheel_motor" in the list, false when it does not
	 * say */
	bool wheel_motor = false;
	/* The cabinet stands on a motion platform of two motors, whose
	 * positions the board reads on its ADC channels 2 and 3 (ATV Track):
	 * "motion_platform" in the list, false when it does not say */
	bool motion_platform = false;
	/* The parts of its sound, for the page Audio: "sound_parts" in the
	 * list; none when it does not say (a left and a right speaker of the
	 * main board) */
	std::vector<SoundPart> sound_parts;
	/* It is played seated: "seated" in the list, false when it does not
	 * say */
	bool seated = false;
	/* The output of TX0 of the drive board of its cabinet, which takes a
	 * command on eight outputs from there (drive_board.hpp): "drive_board"
	 * in the list, {"first_output": 1 to 25}; 0 when it does not say. */
	unsigned drive_first_output = 0;
	/* The card reader of its cabinet, on the serial block of the MIE:
	 * "card_reader" in the list, "CRP-1231BR-10" (card_reader.hpp); empty
	 * when it does not say (no reader). */
	std::string card_reader;
	/* The game links cabinets through the network board 837-13404 in
	 * page 0E (netbd_837_13404.hpp): "link" in the list, {"cabinets": the
	 * most it links, "seat": where each cabinet keeps its own}; 0
	 * cabinets when it does not say (the cabinet has no board). The seat
	 * of cabinet n is first + n - 1, in bits shift to shift + bits - 1 of
	 * a byte of the settings of the BOOTROM in the 93C46 of the MIE
	 * ("in": "mie") or of the block of the game in the backup RAM
	 * ("backup"), or of the settings of a Gaelco board in its flash ("flash") or NAND ("nand"),
 * as its test menu writes it (cabinet_settings.hpp). */
	struct Link {
		/* FLASH: the settings Smashing Drive keeps in its program flash
		 * (AtvTrack::set_flash_setting); NAND: those ATV Track keeps in
		 * its NAND (set_nand_setting) */
		enum class In { NONE, MIE, BACKUP, FLASH, NAND };

		unsigned cabinets = 0;
		In seat_in = In::NONE;
		unsigned seat_byte = 0, seat_shift = 0, seat_bits = 8, seat_first = 0;
		bool seat_letter = false;	/* the menu shows it as A, B... */
	} link;
	/* Its ROM board: "rom_board" in the list; none when it does not say,
	 * as for the BOOTROM alone. */
	std::optional<GameRomBoard> rom_board;
	static constexpr double LOWEST_LEVEL = -60;
	static constexpr double HIGHEST_LEVEL = 12;
	std::vector<std::pair<std::string, std::vector<uint8_t>>> sections;

	/* The bytes of a section; empty when the game has no such section. */
	std::span<const uint8_t> section(const std::string &name) const;
	/* A pad shakes as its cabinet does: a rumble output, the vibration
	 * device of the optional board, the shocks of the drive board */
	bool shakes() const
	{
		return option_vibration || drive_first_output ||
		       std::any_of(outputs.begin(), outputs.end(),
		                   [](const GameOutput &output) { return output.role == GameOutput::Role::RUMBLE; });
	}
	/* What shakes the pad of a player (0 or 1), as the menu says it: its
	 * rumble outputs ("from output 1P NOZZLE VIBRATION"), the vibration
	 * device of the optional board or the shocks of the drive board (player
	 * 1); empty: nothing. Whether it is one of the last two, which Odin
	 * makes from what the game does (experimental), not an output. */
	std::string shaker(unsigned player) const;
	bool shaker_experimental(unsigned player) const;
};

/* Whether a title comes before another, the case of their letters aside */
inline bool
title_before(const std::string &a, const std::string &b)
{
	return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
	});
}

/* The list of the games, a JSON file (odin-games.json). A game has sections
 * and a section the files that make it up, each with its size, put together
 * as its type tells:
 *
 *  - "alternative": the first of the files that is there;
 *  - "interleave": the files go in pairs, `amnt` bytes of the first, then
 *    `amnt` bytes of the second, and so on; the pairs follow each other;
 *  - "concatenate": one file after the other. */
class GameList {
public:
	GameList();
	~GameList();
	GameList(const GameList &) = delete;
	GameList &operator=(const GameList &) = delete;

	/* Reads the list. False, with the reason on stderr, when the file is
	 * not one. */
	bool load(const std::string &path);

	bool has(const std::string &name) const;
	std::string machine(const std::string &name) const;
	/* The names and titles of the games of a machine, in the order of the
	 * list: those that have controls (not the BIOS alone) */
	std::vector<std::pair<std::string, std::string>> titles(const std::string &machine) const;
	/* Those of every machine, with their machine and their parent */
	struct Entry {
		std::string name, title, machine, parent;
	};
	std::vector<Entry> entries() const;
	std::vector<std::pair<std::string, std::string>> titles() const;
	/* The versions of the list, with their parent */
	std::map<std::string, std::string> parents() const;
	/* What the list tells of a game, without its files: nothing when it is
	 * not in the list, or not right */
	std::optional<Game> describe(const std::string &name) const;
	/* The files of a game that are nowhere (rom_files.hpp): one of an
	 * alternative, all those of the other sections, but those of a
	 * section it runs without */
	std::vector<RomFiles::File> missing(RomFiles &files, const std::string &name) const;
	/* The game with its files. Nothing when the game is not in the list, a
	 * file is missing or has another size than the list tells. */
	std::optional<Game> load_game(RomFiles &files, const std::string &name) const;

private:
	/* 2: a version takes from its parent what it does not say */
	static constexpr unsigned VERSION = 2;

	const json_t *find(const std::string &name) const;

	json_t *m_root;
};

} /* namespace odin */

#endif /* ODIN_GAMES_HPP */
