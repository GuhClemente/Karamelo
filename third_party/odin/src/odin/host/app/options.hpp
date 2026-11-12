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

#ifndef ODIN_OPTIONS_HPP
#define ODIN_OPTIONS_HPP

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "odin/host/audio/audio_out.hpp"
#include "odin/host/video/display_options.hpp"
#include "odin/host/app/games.hpp"
#include "odin/host/app/settings.hpp"
#include "odin/host/input/seats.hpp"

namespace odin {

/* How the frames wait for the display by default, and the names of odin.ini */
inline constexpr DisplayVsync VSYNC = DisplayVsync::OFF;
inline constexpr const char *VSYNCS[] = { "off", "on", "adaptive" };
/* Around the picture by default, and the names of odin.ini */
inline constexpr DisplaySides SIDES = DisplaySides::BEZEL;
inline constexpr const char *SIDES_NAMES[] = { "off", "blur", "image" };
/* The graphics API by default, and the names of odin.ini */
#if defined(__APPLE__)
inline constexpr DisplayApi API = DisplayApi::VULKAN;
#else
inline constexpr DisplayApi API = DisplayApi::OPENGL;
#endif
inline constexpr const char *APIS[] = { "opengl", "vulkan" };
/* The names of the upscaling modes in odin.ini (DisplayUpscaleMode), raw
 * by default */
inline constexpr const char *UPSCALE_MODES[] = { "raw", "snap", "native" };

/* What Odin runs with: the command line, and odin.ini where it says
 * nothing (with_settings) */
struct Options {
	/* The folders of the games and of the sets of files they share (the
	 * BIOS...), rom_files.hpp */
	std::string rom_path, bios_path;
	/* The folder of the saves of the games and that of the save states
	 * odin.ini gives (empty: by default) */
	std::string saves, states;
	/* The folder of the pictures of the games odin.ini gives (empty: by
	 * default), user_files.hpp */
	std::string artwork;
	std::string game;
	/* The file of what Odin says; empty: none. The folder odin.ini gives
	 * it (empty: by default) */
	std::string log_file, log_folder;
	/* The settings file the command line gives (-c; empty: by default) */
	std::string config;
	unsigned num_frames = 0;
	std::string state_path;
	std::vector<unsigned> save_frames;
	bool unthrottled = false;
	bool verbose = false;
	/* The speakers of the cabinet unless -o says otherwise: those of the
	 * main board, which get the optional board too where the game opens its
	 * external inputs, and those of the optional board where the cabinet has
	 * them (the two boards on two channels) */
	std::optional<AudioOut::Mode> sound = AudioOut::Mode::MAIN;
	bool sound_asked = false;
	/* [sound] of odin.ini: the device (empty: the system's), the volume
	 * from 0 to 100, the sound kept while the window is in the background */
	std::string sound_device;
	unsigned volume = 100;
	bool background = true;
	DisplayOptions display = { .scale = 1, .anisotropy = 1, .window = 0, .fullscreen = false, .show_speed = true,
	                           .vsync = DisplayVsync::OFF, .window_w = 0, .window_h = 0, .filter = "", .api = API,
	                           .upscale_mode = DisplayUpscaleMode::RAW, .gpu = "" };
	/* [display] vsync: the display gets it when the frames are paced */
	DisplayVsync vsync = VSYNC;
	/* [display] bezel: around the picture */
	DisplaySides sides = SIDES;
	/* What the command line gave: odin.ini does not change it */
	struct {
		bool scale = false, anisotropy = false, window = false, fullscreen = false, unthrottled = false;
		bool roms = false, bios = false, verbose = false;
	} given;
};

/* RUN: go on; HELP_ASKED or WRONG: the help is to be shown (exit 0 or 1),
 * print_help; VERSION_ASKED: the version alone (exit 0) */
enum class Parsed { RUN, HELP_ASKED, VERSION_ASKED, WRONG };
void print_help(const char *program);

/* The help is shown for -h and for an option that is not right. Without
 * options Odin opens its window and waits. */
Parsed parse_options(int argc, char **argv, Options &options);

/* What the command line says, and odin.ini where it says nothing: read
 * again after the library and each game, which change odin.ini (a line
 * gone, its value by default again) */
Options with_settings(const Options &command, const Settings &settings);

/* How [sound.<game>] says to play the two boards, as played_of counts
 * them, and the mode of such a name */
inline constexpr const char *MODE_NAMES[] = { "main", "mix", "quad" };
std::optional<AudioOut::Mode> mode_named(const std::string &name);
/* [sound.<game>] option_level: the optional board in the mix, in dB, as far
 * as the list allows (Game::LOWEST_LEVEL to HIGHEST_LEVEL); woofer_level
 * the same for a woofer */
std::optional<double> level_named(const std::string &value);
/* [sound.<game>] woofer_cutoff: the lows of the woofer below that many Hz
 * in the mix, off for all of its channel (0 Hz); WOOFER_CUTOFFS[1] when it
 * does not say */
inline constexpr const char *WOOFER_CUTOFFS[] = { "80", "120", "160", "200", "off" };
std::optional<double> woofer_hz_named(const std::string &value);
/* [sound.<game>] start_volume: the volume Odin sets on Smashing Drive at
 * each power-on, one of START_VOLUMES; START_VOLUMES[START_VOLUME_STANDARD]
 * when it does not say */
inline constexpr const char *START_VOLUMES[] = { "0", "25", "50", "75", "96" };
inline constexpr int START_VOLUME_STANDARD = 3;

/* How a game's sound plays besides its mode: the level of the optional
 * board or of the woofer in the mix, in dB, and the woofer's cutoff */
struct GameSound {
	double level = 0;
	double woofer_hz = 0;
};

/* The mode of -o, else of [sound.<game>], else Both where the cabinet has
 * speakers for the optional board or a woofer, into options.sound; the
 * levels of [sound.<game>], else of the list. Its other lines refused. */
GameSound game_sound(Options &options, const Game &game, const Settings &settings);
/* [players.<game>] vibration: the pads shake unless it says off (the last
 * line says) */
bool vibration_on(const Settings &settings, const std::string &game);
/* [library]: the games the library shows, of the list: owned_only, only
 * those whose files are all there (off, when it does not say, or on);
 * variants, the versions of a game ("parent" in the list) too (on, when it
 * does not say, or off); systems, those of these systems ("hikaru",
 * "gaelco", apart by commas; all, when it does not say). The last right line of
 * each says. */
struct LibraryFilter {
	bool owned_only = false, variants = true;
	std::vector<std::string> systems;	/* none: all */

	bool operator==(const LibraryFilter &) const = default;
	bool shows(const std::string &machine, bool variant, bool owned) const
	{
		return (variants || !variant) && (!owned_only || owned) &&
		       (systems.empty() || std::find(systems.begin(), systems.end(), machine) != systems.end());
	}
	/* The line of odin.ini of its systems */
	std::string systems_line() const;
};
LibraryFilter library_filter_of(const Settings &settings);
/* [outputs.<game>] <id of an output>: the light gun a vibration of the
 * cabinet goes to: none (when it does not say), the motor of the gun of a
 * player (gun1, gun2) or its solenoid (gun1 recoil, gun2 recoil); the last
 * right line says. (A gamepad shakes as its options on the page Input
 * say: [players.<game>] vibration.) */
struct OutputRoute {
	enum class To : uint8_t { NOTHING, GUN, GUN_RECOIL };

	To to = To::NOTHING;
	unsigned player = 0;

	bool operator==(const OutputRoute &) const = default;
};
std::optional<OutputRoute> output_route_named(const std::string &value);
std::string output_route_name(const OutputRoute &route);
std::optional<OutputRoute> output_route_of(const Settings &settings, const std::string &game, const std::string &id,
                                           bool tell = true);
/* [players.<game>] hide_cursor (light_gun_mode before 0.1.1): no cursor,
 * no bar, the mouse controls the game (on), or a normal pointer (off); its
 * last line says, else the last of light_gun_mode, else `standard`
 * (ControlPanel::takes_mouse) */
bool hide_cursor_on(const Settings &settings, const std::string &game, bool standard);
/* [players.<game>] sinden_border: a white border for a light gun that
 * finds the screen by it, in a game with one, as xemu's Chihiro draws it:
 * off when it does not say, or on; sinden_border_style: inside the edges of
 * the picture of the game (game, when it does not say) or of the window
 * (window); sinden_border_size: so many pixels of the window wide,
 * LEAST_SINDEN_BORDER_SIZE to MOST_SINDEN_BORDER_SIZE (SINDEN_BORDER_SIZE
 * when it does not say); the last right line of each says */
inline constexpr const char *SINDEN_BORDER_STYLES[] = { "game", "window" };
std::optional<int> sinden_border_style_named(const std::string &value);
std::optional<int> sinden_border_size_named(const std::string &value);
SindenBorder sinden_border_of(const Settings &settings, const std::string &game);
/* [players.<game>] wheel_force: how hard the wheel turns as the drive board
 * tells it, in % of its force (0 to MOST_WHEEL_FORCE; at WHEEL_FORCE, when
 * it does not say, the force the board asks at its most is the most of the
 * wheel; above, the wheel reaches its most sooner); the last line says */
inline constexpr int WHEEL_FORCE = 100, MOST_WHEEL_FORCE = 200;
std::optional<int> wheel_force_named(const std::string &value);
int wheel_force_of(const Settings &settings, const std::string &game);
/* [players.<game>] vibration_force: how hard the pads shake, in % of the
 * strength Odin gives each shake (0 to MOST_VIBRATION_FORCE,
 * VIBRATION_FORCE when it does not say); the last line says */
inline constexpr int VIBRATION_FORCE = 100, MOST_VIBRATION_FORCE = 200;
std::optional<int> vibration_force_named(const std::string &value);
int vibration_force_of(const Settings &settings, const std::string &game);
/* [players.<game>] wheel_rotation: the degrees a wheel of the host turns to
 * take the steering of the game from one end to the other, one of
 * WHEEL_ROTATIONS (0: the whole way of the wheel); WHEEL_ROTATION when it
 * does not say: the whole way, the angles taken as xemu's Chihiro wheels
 * take them; the last line says */
inline constexpr int WHEEL_ROTATIONS[] = { 0, 270, 360, 540, 900, 1080 };
inline constexpr int WHEEL_ROTATION = 0;
std::optional<int> wheel_rotation_named(const std::string &value);
int wheel_rotation_of(const Settings &settings, const std::string &game);
/* [players.<game>] device1, device2: the kind of device a player chose
 * (DEVICE_NAMES, seats.hpp; nothing: Odin picks); the last line says */
std::optional<InputDevice> device_named(const std::string &value);
std::optional<InputDevice> device_chosen(const Settings &settings, const std::string &game, unsigned player);
/* [players.<game>] cabinet: the id of the cabinet of the game played
 * (Game::cabinets; "standard": that of the list, when it does not say) */
std::string cabinet_of(const Settings &settings, const std::string &game);

/* The way of playing of the page Audio: 0 the main board, 1 both, 2 four
 * channels, -1 the optional board alone */
int played_of(AudioOut::Mode mode);

/* And back */
AudioOut::Mode mode_of(int played);

/* The folder of the save states by default: states in odin_data_dir(),
 * else the folder Odin is started from (empty) */
std::string default_states();

/* The list of the games: in the folder Odin is started from, else with the
 * program (program_data_folders), else in the home of the user, else where
 * an install to ~/.local puts it. */
bool load_game_list(GameList &games);

/* Every key of odin.ini, at its value by default, in the order the file
 * shows them: what holds for every game (OPTIONS), then the sound, the
 * controls and the gamepads of each game of the list; the hotkeys as by
 * default with that hotkey button */
std::vector<Settings::Known> known_settings(const GameList &games, int button = SDL_GAMEPAD_BUTTON_GUIDE);

} /* namespace odin */

#endif /* ODIN_OPTIONS_HPP */
