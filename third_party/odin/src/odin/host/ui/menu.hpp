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

#ifndef ODIN_MENU_HPP
#define ODIN_MENU_HPP

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui/imgui.h"
#include "odin/host/app/options.hpp"
#include "odin/host/input/control_panel.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/app/hotkeys.hpp"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/ui/sound_view.hpp"
#include "odin/host/input/roles.hpp"
#include "odin/host/input/seats.hpp"
#include "odin/host/video/display_options.hpp"

namespace odin {

class SystemFonts;

/* What the page Input changes: a part of a control of the panel (0 the
 * control, 1 and 2 its halves or a lever down and up, bindings.hpp), or a
 * role of the cabinet (control -1) */
struct MenuSlot {
	int control = -1;
	unsigned part = 0;
	Role role = Role::COIN1;
};

/* A path as the user knows it: the home folder as ~; on Windows with its
 * separators, as its own dialogs give them; `none` when it is empty */
std::string shown_path(const std::string &path, const char *none);

/* A folder the library or the menu asks the system for (the library the
 * first two only) */
enum class MenuFolder { NONE, ROMS, BIOS, SAVES, STATES, ARTWORK, LOG };

/* The library, what Odin shows when it starts without a game (as a double
 * click starts it): the games of the list, each with the files it lacks
 * (none: it can be played), the one chosen; the folders of the games and
 * of the BIOS, whether the command line gives them; a folder to ask the
 * system for; the game to play, and why the one before did not start; what
 * the gamepad asked since the last picture (menu_nav.hpp: the choice moved
 * across and down, the game chosen played) and the name of its button that
 * plays (empty: no gamepad); the games it shows ([library]), by system in
 * the order of the list and by title in each; the systems of the list, how
 * many games it hides. The thread of the board keeps it and does what
 * changed (library.cpp). */
struct LibraryState {
	struct Game {
		std::string name, title, parent;
		std::vector<std::string> missing;
		bool gaelco;
		std::string system;	/* "hikaru", "gaelco" */
	};

	std::vector<Game> games;
	int selected = 0;
	LibraryFilter filter;
	std::vector<std::string> systems;
	size_t hidden = 0;
	std::string roms, bios;
	bool roms_given = false, bios_given = false;
	using Folder = MenuFolder;
	Folder choose = Folder::NONE;
	bool choosing = false;
	std::optional<std::string> play;
	std::string failed;
	struct Nav {
		int across = 0, down = 0;
		bool press = false;
	} nav;
	std::string press;
};

/* A light gun or a mouse read one by one (pointers.hpp), seen or named by
 * the settings: its identity, its name, plugged in, aiming (else moving) */
struct LightGun {
	std::string identity, name;
	bool present = false, aims = false;
};

/* A game whose optional sound board or woofer the user may hear, as the
 * menu and the bar show it: how it is played (Menu::PLAYED: 0 the main
 * board, 1 both on two channels, 2 the four channels apart; -1 another way
 * the command line says, for the game that runs, which `locked` says) and
 * how the cabinet plays it; the level of the optional board in the two
 * channels and the one of the list, in dB. Four channels chosen for the game that runs on
 * two, or two on four, wait for it to start again (`later`) */
struct SoundGame {
	std::string name, title;
	int played, cabinet_played;
	float level, standard;
	bool locked;
	bool later = false;
	/* Smashing Drive: the volume Odin sets at each power-on, of
	 * START_VOLUMES (-1: another game) */
	int start = -1;
	/* A way of playing to choose: two boards, or a woofer (Game::woofer:
	 * `level` is the woofer's, `cutoff` of WOOFER_CUTOFFS its lows in the
	 * mix, `cutoff_standard` when the settings do not say) */
	bool mixed = true, woofer = false;
	int cutoff = 1, cutoff_standard = 1;
	/* The part `level` is the level of, as the page Audio names it */
	const char *level_name = "Woofer";
};

/* The settings the pages and the bar both change: MenuState has them, the
 * bar a copy (MenuSession::fill_bar), given back once changed there
 * (do_bar) */
struct BarSettings {
	/* Display: the 3D that many times finer, how the flat meshes are drawn
	 * when finer (DisplayUpscaleMode: 0 raw, 1 snap, 2 native), the texture
	 * filter (1 for none), the window at start that many times the screen,
	 * how the frames wait for the display (Display::Vsync: 0 not at all, 1
	 * all, 2 those on time), around the picture where the window is wider
	 * (Display::Sides: 0 black, 1 the picture blurred, 2 the bezel of the
	 * game); the filter of the picture by its name ([display] filter;
	 * empty: none); frames without a limit (at the next launch), the frames
	 * a second in the title; what draws (DisplayApi) and the card it draws
	 * on (HostGraphics::gpus, empty: the best one), at the next launch */
	unsigned scale = 1, upscale_mode = 0, anisotropy = 1, window = 1, vsync = 0, sides = 2, api = 0;
	std::string filter, gpu_chosen;
	std::vector<std::string> gpus;	/* the cards of the computer */
	/* Only Vulkan can be told which card, with more than one to tell */
	bool picks_gpu() const { return gpus.size() > 1 && static_cast<DisplayApi>(api) == DisplayApi::VULKAN; }
	bool unthrottled = false, show_speed = true;
	/* Audio: the device chosen (empty: the system's), the volume of Odin
	 * from 0 to 100, its sound kept while its window is in the background */
	std::string device;
	unsigned volume = 100;
	bool background = true;
	/* The board saying each access, at the next launch */
	bool verbose = false;
	/* The palette of the menu, the bar and the library ([menu] theme): one
	 * of THEMES (menu_kit.hpp) */
	unsigned theme = 0;
	/* The outputs sent to the programs of lamps. The network board of the
	 * games that link cabinets, at the next launch ([network]): none, alone
	 * (its optical ring closed on itself) or linked to other Odins; the
	 * port the previous cabinet comes in on; which cabinet of the link this
	 * Odin is, 1 the master */
	bool network = false;
	int link_board = 0;
	unsigned link_port = 13404, link_cabinet = 1;

	bool operator==(const BarSettings &) const = default;
};

/* The bar at the top of the window, over the game that goes on and over the
 * library: FILE, INPUT, DISPLAY, AUDIO, LINK, DEBUG and ABOUT, each a menu.
 * What they show and what was asked there; the thread of the board keeps it
 * and does what was asked (session.cpp, library.cpp). */
struct BarState : BarSettings {
	/* At the right: the game and its frames a second. A note of Odin (what
	 * it did, what went wrong) in the bottom-left corner, bar or not, and
	 * how much of it shows (note_alpha: it fades out) */
	std::string title, note;
	float fps = 0.0f, note_alpha = 1.0f;
	/* Shown checked, and changed there: the whole display, the game
	 * paused */
	bool fullscreen = false, paused = false;
	/* Said on the command line: shown, not changed; no sound to set */
	bool verbose_given = false, silent = false;
	/* A state of the game kept, to load; a game runs (not in the library:
	 * nothing to close, save, load or pause) */
	bool saved = false, game = true;
	/* The first key of each action of Odin (Hotkeys::Action), as odin.ini
	 * says it */
	std::array<std::string, Hotkeys::ACTIONS> keys;
	/* The card reader of the game (Virtual-On): it has one, where the
	 * player's card is (CardReader::Where), its slot open; the player's
	 * cards and the one in hand (-1: none) */
	bool card_reader = false, card_slot_open = false;
	int card_where = 0, card = -1;
	std::vector<std::string> cards;
	/* The characters of the name of a new card (empty: the game makes
	 * none) */
	std::string card_keys;

	/* The display: the window at start of the size given by hand, kept;
	 * what the command line gives, and the vertical sync off at unlimited
	 * speed, shown, not changed. The graphics card. */
	bool custom_window = false;
	bool scale_given = false, anisotropy_given = false, window_given = false, unthrottled_given = false,
	     vsync_locked = false;
	std::string gpu;
	/* The sound: the devices (read while a menu of the bar is open); the
	 * game that runs when it has an optional sound board */
	std::vector<std::string> devices;
	std::vector<SoundGame> sound_games;
	/* The game that runs: its players (two in a game of two), the device
	 * each plays with (the one checked: it alone shakes), the gamepads and
	 * joysticks plugged in and the player each is of (-1: none), its name
	 * shown (two of a make numbered); the dead zone and the trigger threshold
	 * of the gamepad of each player, in % (-1: no gamepad); the light guns
	 * seen, that of each player (its identity; empty: none), the players
	 * with a sight; its pads shake, and do; Undo of Clear mapping offered */
	struct Pad {
		std::string name, label, guid;
		bool joystick;
		int player;
		SDL_JoystickID id;
		bool off;	/* a wheel, for a game that has none */
	};
	unsigned players = 1;
	std::array<InputDevice, 2> plays_with{};
	std::vector<Pad> pads;
	std::array<int, 2> dead_zone{ -1, -1 }, trigger_threshold{ -1, -1 };
	std::vector<LightGun> guns;
	std::array<std::string, 2> gun;
	std::array<bool, 2> sights{};
	bool can_shake = false, shake = true, undo = false, hide_cursor = false;
	/* The next cabinet of the link (typed in Settings); the link set by
	 * ODIN_NETBD; the game that runs is on a Gaelco board and does not link
	 * cabinets (Gaelco Football) */
	bool link_given = false, gaelco = false;
	std::string link_next;

	/* What was asked; the page of the settings asked (Menu::Page, -1: the
	 * one shown last); the card chosen (CARD_CHOOSE, -1: none); the name
	 * of the new card (CARD_NEW); for the inputs of the game that runs
	 * (PAD, KEYBOARD, GUN, INPUTS_*): the player, the gamepad or joystick
	 * chosen (in pads), the light gun chosen (in guns, -1: none) */
	enum class Ask { NONE, OPEN, CLOSE, RESET, SAVE_STATE, LOAD_STATE, CARD, CARD_CHOOSE, CARD_NEW, SETTINGS, FULLSCREEN,
	                 PAUSE, QUIT, PAD, KEYBOARD, GUN, INPUTS_DEFAULTS, INPUTS_CLEAR, INPUTS_UNDO, HOTKEYS_DEFAULTS };
	Ask ask = Ask::NONE;
	int page = -1, card_chosen = -1;
	std::string card_name;
	unsigned asked_player = 0;
	int asked_pad = -1, asked_gun = -1;
	/* The mouse is on the bar or a menu of it is open: its buttons are the
	 * bar's, not the game's; the bottom of the bar, in points */
	bool wants_mouse = false, open = false;
	/* For the tests: the menu of the bar to open once (-1: none) */
	int open_test = -1;
	float bottom = 0.0f;
};

/* What the pages of the menu show and change. The thread of the board
 * keeps it and does what changed (menu_session.cpp). */
struct MenuState : BarSettings {
	/* Display, but BarSettings: the window at start of the size the user
	 * gave it by hand instead, in points (Custom; 0: none); the size it has
	 * now; the whole display */
	unsigned custom_w = 0, custom_h = 0, window_w = 0, window_h = 0;
	bool fullscreen = false;
	/* Said on the command line: shown, not changed */
	bool scale_given = false, anisotropy_given = false, window_given = false, fullscreen_given = false,
	     unthrottled_given = false;
	/* The graphics card and whether Vulkan draws on it (else OpenGL), the
	 * size of the display in pixels, its frames a second (0: not known) and
	 * whether they are fewer than the cabinet's; why the frames do not wait
	 * for it (none: they do as vsync says) */
	std::string gpu;
	bool vulkan = false;
	unsigned display_w = 0, display_h = 0;
	float refresh = 0.0f;
	bool refresh_short = false;
	const char *vsync_off = nullptr;
	/* odin.ini keeps what changes: not with ODIN_CONFIG=none. Opened over
	 * the library, no game runs. */
	bool kept = true, library = false;

	/* Audio, but BarSettings: the devices the system plays on; why there
	 * is no sound now (none: there is) */
	std::vector<std::string> devices;
	const char *silent = nullptr;
	/* The games whose optional sound board the user may hear: those with
	 * speakers for it on the cabinet, and Racer, whose cabinet has it shake
	 * the panel */
	std::vector<SoundGame> sound_games;

	/* Hotkeys: the keys and the gamepad buttons of each action of Odin; the
	 * action whose next key (0 or 1) or buttons (2) are caught; all of them
	 * back as by default */
	std::array<Hotkeys::Binding, Hotkeys::ACTIONS> hotkeys{};
	std::optional<std::pair<size_t, size_t>> hotkey_catching;
	bool hotkeys_default = false;
	/* The hotkey button ([hotkeys] button), and another one chosen */
	int hotkey_button = SDL_GAMEPAD_BUTTON_GUIDE;
	std::optional<int> hotkey_button_asked;
	/* What the last key or buttons caught took from another action: from
	 * it to the one changed, the key, else the button held with the
	 * hotkey button (or GUIDE_ALONE); said at the bottom of the page
	 * until the next change */
	struct HotkeyMoved {
		size_t from = 0, to = 0;
		std::optional<HotKey> key;
		int pad = Hotkeys::NO_PAD;
	};
	std::optional<HotkeyMoved> hotkey_moved;

	/* Outputs: those of the cabinet of the game shown (their names, what
	 * they are), lit while on in the game that runs, where a vibration goes
	 * ([outputs.<game>]); sent to the programs of lamps
	 * (BarSettings::network), the port taken by another program, how many
	 * programs take them */
	struct Output {
		std::string name;
		GameOutput::Role role;
		bool on;
		std::string id;
		OutputRoute route;
	};
	std::vector<Output> outputs;
	bool network_failed = false;
	size_t programs = 0;

	/* Link, but BarSettings. The game shown: its title, the most cabinets
	 * it links (0: none), how its menu shows a seat */
	enum : int { SEAT_NONE, SEAT_NUMBER, SEAT_LETTER };
	std::string link_shown_title;
	unsigned link_shown_cabinets = 0;
	int link_shown_seat = SEAT_NONE;
	/* The next cabinet as kept and as being written (not an address: not
	 * kept); ODIN_NETBD sets the link. Then the game that runs: it runs, it
	 * has the board, linked, its port taken, each end of the cable plugged
	 * in. */
	std::string link_next, link_next_typed;
	bool link_next_bad = false, link_given = false;
	bool link_running = false, link_board_now = false, link_linked = false, link_failed = false;
	bool link_previous = false, link_next_plugged = false;

	/* Paths: the folders of the games, of the files they share (the BIOS:
	 * empty, that of the games) and of their saves, at the next launch
	 * (those the command line or ODIN_SAVES gives are shown, not changed),
	 * of the save states; the settings file and the lists of the 3D
	 * programs (empty: none). The board saying each access
	 * (BarSettings::verbose) given on the command line. */
	std::string roms, bios, saves, states, artwork, settings_file, shader_lists, log_file;
	bool roms_given = false, bios_given = false, saves_given = false, artwork_given = false;
	/* The folder of the log from the next launch ([paths] log; empty: by
	 * default), and ODIN_LOG giving the file instead */
	std::string log_folder;
	bool log_given = false;
	bool verbose_given = false;
	/* A folder to ask the system for; its picker is open; the system has
	 * none */
	using Folder = MenuFolder;
	Folder choose = Folder::NONE;
	bool choosing = false, no_picker = false;

	/* Input: the games of the list (name, title), the one whose controls
	 * are shown and the one that runs, another one asked */
	std::vector<std::pair<std::string, std::string>> games;
	size_t shown = 0, running = 0;
	/* Each game of `games` links cabinets: the page Link lists those alone */
	std::vector<bool> game_links;
	/* Each game of `games` runs on a Gaelco board */
	std::vector<bool> game_gaelco;
	/* Each game of `games`: the parts of its sound for the page Audio (those
	 * of the list, or a left and a right speaker of its main board), whether
	 * it is played seated (and on which of its other cabinets not), how its
	 * boards reach the user's speakers */
	struct GameAudio {
		std::vector<SoundPart> parts;
		bool seated = false;
		std::vector<std::string> standing;
		SoundFamily family = SoundFamily::ONE;
	};
	std::vector<GameAudio> game_audio;
	/* The page Audio: the part of the drawing pointed at last (-1: none),
	 * or the user's speakers (YourSpeaker) */
	int audio_pointed = -1;
	unsigned audio_pointed_speakers = 0;
	std::optional<size_t> show;
	/* The panel of the cabinet of the game shown (its controls and their
	 * inputs), which are worked now (the controls, then COIN1, COIN2 and
	 * TEST) and where its controls are, the gamepad of each player (its
	 * name, its kind: pad_drawing.hpp) */
	const ControlPanel *panel = nullptr;
	enum : size_t { TOUCHED_COIN1, TOUCHED_COIN2, TOUCHED_TEST, TOUCHED_ROLES };
	std::vector<bool> touched;
	std::vector<ControlNow> now;
	std::array<int, 2> pad_types{};
	/* The gamepads plugged in (name, GUID, kind, the player it is the pad
	 * of in the game shown, -1: none, by a wire or not, its id), the one
	 * asked for the player shown */
	struct Plugged {
		std::string name, guid;
		int kind, player;
		SDL_JoystickConnectionState connection;
		SDL_JoystickID id;
	};
	std::vector<Plugged> plugged;
	std::optional<Plugged> want;
	/* The joysticks that are no gamepads (a wheel), the same (kind: their
	 * SDL_JoystickType), and the one asked for the player shown */
	std::vector<Plugged> joysticks;
	/* The control of the page Input under the mouse in its list this
	 * frame (-1: none): the foot says all its inputs */
	int hovered_control = -1;
	std::optional<Plugged> want_joystick;
	/* The light guns and mice seen; that of each player in the game shown
	 * (its identity; empty: none) and whether the player has a sight there;
	 * the one asked for a player (no identity: none) */
	std::vector<LightGun> guns;
	std::array<std::string, 2> gun;
	std::array<bool, 2> sights{};
	std::optional<std::pair<unsigned, LightGun>> want_gun;
	/* The player shown, and the inputs of each player shown: of the
	 * keyboard (and the mouse), of the gamepad or of the joystick (the
	 * device each one plays with when the menu opens), the device a player
	 * picked (it alone shakes: [players.<game>] device1, device2); what
	 * the gamepad of each player does now; the controls as a list rather
	 * than on the drawing */
	using Device = InputDevice;
	unsigned player = 0;
	std::array<Device, 2> input_device{};
	std::optional<std::pair<unsigned, Device>> device_picked;
	std::array<PadState, 2> pad_states{};
	/* What the light gun of each player presses now, its buttons by
	 * GunButton (gun_drawing.hpp) */
	std::array<PadState, 2> gun_states{};
	/* The keys held now */
	std::vector<SDL_Keycode> keys_held;
	bool classic = false;
	/* The game shown shakes the pads as its cabinet does (the nozzle of
	 * Brave; experimental: the shaker of Racer, the shocks of NASCAR),
	 * whether it does, and how hard, in % ([players.<game>]
	 * vibration_force) */
	bool can_shake = false, shake = true;
	int vibration_force = 100;
	/* The cursor hidden, the mouse controlling the game, or a normal
	 * pointer ([players.<game>] hide_cursor) */
	bool hide_cursor = false;
	/* The controls of the player shown that the player may turn over
	 * (GameControl::invertible): "Invert" and their names, their ids, and
	 * whether each is turned over ([players.<game>] invert; bytes:
	 * MinGW's GCC reads a word at nullptr to compare two empty
	 * vector<bool>) */
	std::vector<std::string> invert_names, invert_ids;
	std::vector<uint8_t> inverted;
	/* Its white border for a light gun that finds the screen by it
	 * ([players.<game>] sinden_border, sinden_border_style,
	 * sinden_border_size) */
	SindenBorder sinden_border;
	/* What shakes on the cabinet ("from output NOZZLE VIBRATION") and
	 * whether Odin makes it up (Game::shaker_experimental) */
	std::string shake_from;
	bool shake_experimental = false;
	/* The game shown turns the wheel through the drive board of its
	 * cabinet (NASCAR), and how hard, in % ([players.<game>] wheel_force);
	 * it steers with a wheel (a wheel is for such a game alone), and how
	 * far a wheel turns for it, in degrees ([players.<game>]
	 * wheel_rotation) */
	bool can_force = false;
	bool steers = false;
	int wheel_force = 100;
	int wheel_rotation = WHEEL_ROTATION;
	/* The cabinets the game shown was sold in, Standard first
	 * (Game::cabinets): their ids and names, the one chosen
	 * ([players.<game>] cabinet), and whether the game that runs plays
	 * another until it is reset */
	std::vector<std::string> cabinet_ids, cabinet_names;
	int cabinet = 0;
	bool cabinet_later = false;
	/* The dead zone and the trigger threshold of the gamepad shown (its
	 * make and model's); its options open over the drawing */
	PadTuning tuning;
	bool advanced = false;
	/* The page Input tests the controls: the gamepads and the keys light
	 * them up and work no part of the menu (the mouse does) */
	bool testing = false;
	/* The animations of the menu ([menu] animations): the row of the
	 * games moves from the game shown before, since then (seconds of the
	 * menu), the one shown at the last picture; while something moves the
	 * menu draws on */
	bool animations = true, moving = false;
	std::optional<size_t> games_was;	/* games by their place in `games` */
	size_t games_from = 0;
	double games_since = 0.0;
	/* What the left of the page Display shows (Menu::Preview), what it
	 * showed before and since when, as one fades into the other */
	int preview = 0, preview_from = 0;
	double preview_since = 0.0;
	/* In a window not wide enough for both, the cabinet in place of the
	 * device */
	bool cabinet_shown = false;
	/* The light gun of a player in the game shown (nullptr: none) */
	const LightGun *gun_of(unsigned who) const
	{
		const auto it = std::find_if(guns.begin(), guns.end(),
		                             [&](const LightGun &one) { return !gun[who & 1].empty() && one.identity == gun[who & 1]; });

		return it == guns.end() ? nullptr : &*it;
	}
	bool on_pad() const { return input_device[player & 1] == Device::PAD; }
	bool on_joystick() const { return input_device[player & 1] == Device::JOYSTICK; }
	/* The kind of input of the device shown (the mouse goes with the
	 * keyboard) */
	HostInput::Kind shown_kind() const
	{
		return on_pad() ? HostInput::Kind::PAD : on_joystick() ? HostInput::Kind::JOY : HostInput::Kind::KEY;
	}
	/* The kinds of input the page shows, which Clear and Defaults change:
	 * the keyboard with the mouse; with a light gun, whatever the user
	 * put on another device too */
	std::vector<HostInput::Kind> shown_kinds() const
	{
		using Kind = HostInput::Kind;

		if (on_pad() || on_joystick())
			return { shown_kind() };
		if (gun_of(player))
			return { Kind::KEY, Kind::MOUSE, Kind::JOY, Kind::PAD };
		return { Kind::KEY, Kind::MOUSE };
	}
	/* The part whose next input is caught, and those caught after it, in
	 * turn; the part the frame of the gamepad is on (drawn as if caught) */
	std::optional<MenuSlot> catching, picked;
	std::vector<MenuSlot> catch_next;
	/* Start mapping: each control of the player in turn, the place of the
	 * one caught now in the list of the page, since when (SDL ticks) */
	std::optional<size_t> mapping;
	uint64_t mapping_since = 0;
	/* Nothing caught any more: no part, none next, no Start mapping */
	void stop_catching()
	{
		catching.reset();
		catch_next.clear();
		mapping.reset();
	}
	/* How far the button that presses is held toward Skip, 0 to 1 */
	float skip_hold = 0.0f;
	/* What the page asks of the inputs of the player and the device shown,
	 * of all its parts or of that part; back to the game or the library, a
	 * click beside the frame. Undo while it is offered. */
	enum class Ask { NONE, DEFAULTS, CLEAR, UNDO, PART_DEFAULTS, PART_CLEAR, BACK } ask = Ask::NONE;
	bool undo = false;

	/* What the gamepad asked since the last picture (menu_nav.hpp): the
	 * page or the game before (-1) or after (1), the frame of focus moved
	 * up or down, a choice or a slider moved, pressed, cleared or set to
	 * its default; a list dropped open closed */
	struct Nav {
		int page = 0, game = 0, move = 0, side = 0;
		bool press = false, clear = false, reset = false, back = false;
	} nav;
	/* The names of the buttons that change the page and the game, before
	 * and after, then of those that change, clear and set to its default
	 * the part picked, on the gamepad of player 1 (empty: no pill) */
	enum Pill : size_t {
		PILL_PAGE_BEFORE, PILL_PAGE_AFTER, PILL_GAME_BEFORE, PILL_GAME_AFTER, PILL_PRESS, PILL_CLEAR, PILL_RESET,
		PILLS
	};
	std::array<std::string, PILLS> pills;
};

/* The menu of Odin, drawn with Dear ImGui over the picture of the last
 * frame, frozen and blurred, while the game is paused (session.cpp); its bar
 * over the frames of the game that goes on, and the library.
 *
 * It is made at its first opening: nothing of it runs before. Two threads
 * work it (display.hpp), never both at once: the one that runs the board
 * gives it the events and builds it, the drawing thread, which holds the
 * OpenGL context, freezes the picture and draws. Its OpenGL objects are
 * let go of where the context is (release) before it is destroyed. */
class Menu {
public:
	/* The words of a choice of two, Off first everywhere, and of what is
	 * around the picture */
	static constexpr const char *OFF_ON[] = { "Off", "On" };
	static constexpr const char *SIDES_SHOWN[] = { "Off", "Blur", "Bezel" };
	/* The choices of the page Display, as the bar offers them too: the 3D
	 * that many times finer, how its flat pieces are, the anisotropic
	 * filtering (ANISOTROPY: its values), the window (that many times the
	 * screen; its sizes shorter where the row is narrow; Custom among them:
	 * the size the user gives the window by hand), how frames wait for the
	 * display, the screens of the cabinets, or none (PRESETS: the filters
	 * they name), the speed limit */
	static constexpr const char *SCALES[] = { "1×", "2×", "3×", "4×", "5×", "6×" };
	static constexpr const char *UPSCALE_MODES_SHOWN[] = { "Raw", "Snap 2D", "Native 2D" };
	static constexpr const char *FILTERS[] = { "Off", "2×", "4×", "8×", "16×" };
	static constexpr unsigned ANISOTROPY[] = { 1, 2, 4, 8, 16 };
	/* The place of an anisotropy among them; -1 for another */
	static int anisotropy_index(unsigned anisotropy);
	static constexpr const char *WINDOW_SIZES[] = { "640 × 480", "1280 × 960", "1920 × 1440", "2560 × 1920", "Custom" };
	static constexpr const char *WINDOW_WIDTHS[] = { "640", "1280", "1920", "2560", "Custom" };
	static constexpr int CUSTOM = 4;
	static constexpr const char *SYNCS[] = { "Off", "On", "Adaptive" };
	static constexpr const char *APIS_SHOWN[] = { "OpenGL", "Vulkan" };
	static constexpr const char *SCREENS[] = {
		"None", "Sega monitor 29\"", "Sega projector 50\"", "Gaelco monitor 28\"", "Gaelco monitor 34\"",
	};
	static constexpr const char *PRESETS[] = {
		"", "lola-retro/sega-monitor-29", "lola-retro/sega-projector-50", "lola-retro/gaelco-monitor-28",
		"lola-retro/gaelco-monitor-34",
	};
	static_assert(std::size(SCREENS) == std::size(PRESETS));
	static constexpr const char *PACES[] = { "60 fps (cabinet)", "Unlimited" };
	/* How a game with an optional sound board plays it (page Audio) */
	static constexpr const char *PLAYED[] = { "Main", "Both", "Quad" };
	/* The first key of an action of Odin as the user set it: "" for none */
	static std::string first_key(const MenuState &state, Hotkeys::Action action);
	/* The ports the page Link offers, from NetCable::PORT */
	static constexpr unsigned LINK_PORTS = 8;
	/* The number of a cabinet of a link, as the page Link and the bar show
	 * it: the first leads the others */
	static std::string cabinet_number(unsigned n) { return n == 1 ? "1 · master" : std::to_string(n); }
	/* What Odin is, and the libraries it is made with: who wrote them,
	 * their licence */
	static constexpr const char *WHAT = "Sega Hikaru and Gaelco emulator, as close to the hardware as it gets";
	static constexpr const char *LICENCES[][3] = {
		{ "Odin", "Réda Chérif-Touil", "GPL 3.0 or later" },
		{ "Valkyrie (the start of Odin)", "Stefano Teso", "GPL 3.0 or later" },
		{ "ATV Track boot program", "Samuele Zannoli (MAME)", "BSD-3-Clause" },
		{ "Dear ImGui", "Omar Cornut", "MIT" },
		{ "SDL 3", "Sam Lantinga", "zlib" },
		{ "GLEW", "Milan Ikits, Marcelo Magallon", "BSD / MIT" },
		{ "Jansson", "Petri Lehtinen", "MIT" },
		{ "miniz", "Rich Geldreich, RAD Game Tools, Valve", "MIT" },
		{ "LZMA SDK", "Igor Pavlov", "public domain" },
		{ "Gamepad drawings", "AL2009man, Gamepad-Asset-Pack", "MIT" },
		{ "Cozette", "the-moonwitch", "MIT" },
		{ "Junicode (the rune)", "Peter S. Baker", "OFL 1.1" },
		{ "Lola Retro (CRT filter)", "Zoki", "MIT" },
	};

	/* The pages, in the order of their tabs */
	enum Page { INPUT, DISPLAY, AUDIO, HOTKEYS, OUTPUTS, LINK, PATHS, ABOUT, PAGES };

	/* The window it is drawn in, and the card it is drawn with */
	Menu(SDL_Window *window, HostGraphics &graphics);
	~Menu();
	Menu(const Menu &) = delete;
	Menu &operator=(const Menu &) = delete;

	/* An input of a gamepad of that kind (pad_drawing.hpp), as its maker
	 * prints it */
	static std::string input_name(const PadInput &pad, int kind);

	/* The thread of the board: at that page (-1: the one shown last) */
	void open(int page = -1);
	void close();
	void event(const SDL_Event &event);
	void build(MenuState &state);
	/* The keys and the mouse held before forgotten: the bar shown, the
	 * library opened (a key let go in a game) */
	void forget_input();
	/* The bar alone over the game (menu_bar.cpp), opened and closed as the
	 * menu is: built from the state; hidden, its menus closed */
	void build_bar(BarState &state);
	/* The note of the bar alone, the bar hidden: through none of the
	 * backend of SDL, the pointer and the keys stay the game's */
	void build_note(BarState &state);
	void hide_bar();
	/* The library, over the whole window under the bar (menu_library.cpp) */
	void build_library(LibraryState &state, BarState &bar);
	/* A picture of a game for its tile, RGBA, taken (destroyed once made
	 * an image of the card at the next picture drawn); one before it
	 * replaced, or forgotten (none) */
	void add_library_picture(const std::string &game, SDL_Surface *rgba);
	/* A list dropped open, which Escape closes before the menu */
	bool popup_open() const;
	/* Whether a click at that point of the window falls on something of
	 * the menu (a button, a tab, a row): the frame built again from the
	 * state with the pointer there, not drawn */
	bool item_at(MenuState &state, float x, float y);
	/* The keyboard writes in a field of the menu */
	bool typing() const;

	/* The drawing thread: the picture of the last frame becomes the
	 * background (none: there is none yet, black), or the window as built
	 * last (the library), drawn again; then the menu is drawn on it in the
	 * window. */
	void freeze(HostPicture picture);
	void freeze_window(unsigned window_w, unsigned window_h);
	void draw(unsigned window_w, unsigned window_h);
	/* The bar built last over what the window shows */
	void draw_bar(unsigned window_w, unsigned window_h);
	void release();

private:
	/* The pictures of the page Display (menu_pictures.S): real captures
	 * of Odin, the same place at each scale and at each filter, a frame
	 * whole and torn, the timer of VON4 at 1× and in each upscaling mode
	 * at 4×, a close-up of each CRT screen (in the order of PRESETS) */
	enum Picture { UPSCALE = 0, FILTER = UPSCALE + 6, SCREEN = FILTER + 5, WHOLE, TORN, MODE, CRT = MODE + 4,
	               PICTURES = CRT + 5 };
	/* What the left of the page Display shows: what the row pointed at
	 * or in the frame does */
	enum Preview {
		PREVIEW_CARD, PREVIEW_UPSCALE, PREVIEW_MODE, PREVIEW_FILTER, PREVIEW_SCREEN, PREVIEW_VSYNC, PREVIEW_CRT
	};

	void follow_scale();
	void add_fonts();
	/* The palette of that theme (THEMES), Dear ImGui's colours with it */
	void use_palette(unsigned theme);
	void begin_frame(unsigned theme, MenuState *state = nullptr);
	/* The bar and its menus, in the frame of Dear ImGui built now; at the
	 * start of a frame, the menus of a bar hidden since the last one
	 * closed */
	void bar(BarState &state);
	struct BarItems;
	void bar_file(BarItems &b);
	void bar_input(BarItems &b);
	void bar_display(BarItems &b);
	void bar_audio(BarItems &b);
	void bar_link(BarItems &b);
	void bar_debug(BarItems &b);
	void bar_about(BarItems &b);
	void new_card(BarState &state, const MenuKit &kit);
	void note(const BarState &state, const MenuKit &kit);
	void close_bar_menus();
	/* The drawing thread: the pictures handed over to the library made
	 * images of the card */
	void upload_library_pictures();
	void navigate(MenuState &state);
	void page_input(MenuKit &kit, MenuState &state) const;
	void page_display(MenuKit &kit, MenuState &state) const;
	void preview(const MenuKit &kit, const MenuState &state, int which, ImVec2 a, ImVec2 b, float shown) const;
	void picture(ImDrawList *list, int which, ImVec2 a, ImVec2 b, float shown) const;
	void page_audio(MenuKit &kit, MenuState &state) const;
	void page_hotkeys(MenuKit &kit, MenuState &state) const;
	void page_outputs(MenuKit &kit, MenuState &state) const;
	void page_link(MenuKit &kit, MenuState &state) const;
	void page_paths(MenuKit &kit, MenuState &state) const;
	void page_about(MenuKit &kit) const;
	/* What the pages say at the bottom; Input, at that place, that wide
	 * and high: the control caught or touched, its inputs, what to do.
	 * The height it takes, drawn when `draw`. */
	static std::string help(int page, const MenuState &state);
	static float input_foot(const MenuKit &kit, MenuState &state, ImVec2 at, float width, float height, bool draw);
	/* Whether Input says a control there now (framed, under the mouse or
	 * worked): the keys beside it give it their room */
	static bool input_tells(const MenuState &state);
	static std::string hotkeys_help(const MenuState &state);
	/* A button of a gamepad of that kind (pad_drawing.hpp), as its maker
	 * prints it */
	static std::string button_name(int button, int kind);
	/* The games of the row of the pages Input, Outputs and Link, by their
	 * place in MenuState::games: all of them, or those that link cabinets */
	static std::vector<size_t> row_games(const MenuState &state, bool links_only);
	/* That row: its bottom */
	static float games_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width, bool focusable = false,
	                       bool links_only = false);
	/* The drawing thread: Dear ImGui's drawing and the pictures, at first */
	void start_drawing();

	SDL_Window *m_window;
	HostGraphics &m_graphics;
	ImGuiContext *m_imgui;
	/* Mapped while Dear ImGui reads them (add_fonts) */
	std::unique_ptr<SystemFonts> m_fonts;
	/* The style at a scale of 1, and the scale of the display now, its
	 * pixels a point */
	ImGuiStyle m_style;
	float m_scale, m_density = 1.0f;
	int m_page;
	const MenuPalette *m_palette = &RUNASTEINN;
	/* The frame the gamepad moves on the page */
	MenuFocus m_focus;
	/* What Odin had where Dear ImGui changes it while the menu is open:
	 * two hints of the mouse, the pointer shown or hidden */
	std::array<std::optional<std::string>, 2> m_hints;
	bool m_cursor_shown;
	/* The bar was hidden: its menus close at the next frame */
	bool m_bar_hidden = false;
	/* New card... was chosen: its window opens at the next frame; the name
	 * typed there */
	bool m_new_card = false;
	std::array<char, 9> m_card_name{};
	/* When the last note alone was built, in nanoseconds (its time step) */
	Uint64 m_note_time = 0;

	/* The drawing has started; the background (HostGraphics) there is,
	 * and it is the window, which it covers as it was */
	bool m_drawing = false;
	bool m_frozen = false, m_frozen_window = false;
	/* The pictures as images of the card */
	std::array<HostImage, PICTURES> m_pictures{};
	/* The library: the pictures of the games handed over, then as images
	 * of the card */
	std::vector<std::pair<std::string, SDL_Surface *>> m_library_handed;
	std::map<std::string, HostImage> m_library_pictures;
};

} /* namespace odin */

#endif /* ODIN_MENU_HPP */
