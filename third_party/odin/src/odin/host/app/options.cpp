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

#include "odin/host/app/options.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <getopt.h>

#include "odin/common/env.hpp"
#include "odin/host/app/hotkeys.hpp"
#include "odin/host/ui/menu.hpp"
#include "odin/host/ui/menu_nav.hpp"
#include "odin/machine/cabinet/netbd_ring.hpp"
#include "odin/host/app/user_files.hpp"

namespace odin {

namespace {

/* The place of `value` among `names`, none when it is not one */
std::optional<size_t>
index_named(std::span<const char *const> names, const std::string &value)
{
	const auto at = std::find(names.begin(), names.end(), value);

	return at == names.end() ? std::nullopt : std::optional<size_t>(static_cast<size_t>(at - names.begin()));
}

/* A whole number from `lo` to `hi`, written in full */
std::optional<int>
whole_named(const std::string &value, long lo, long hi)
{
	char *end = nullptr;
	const long number = std::strtol(value.c_str(), &end, 10);

	if (value.empty() || *end || number < lo || number > hi)
		return std::nullopt;
	return static_cast<int>(number);
}

const char HELP[] = "Usage: %s [options]\n"
"	-R <path>	Path to the ROM directory\n"
"	-B <path>	Path to the BIOS directory (hikaru.zip...); none: the ROM directory\n"
"	-c <file>	Settings file (default: odin.ini in Odin's folder)\n"
"	-r <string>	Name of the game to run (none: the library of the games)\n"
"	-n <num>	Stop after the frame num\n"
"	-l <path>	Load a state at startup\n"
"	-s <num>[,...]	Save <game>-<num>.odinstate after these frames\n"
"	-u		Run as fast as the host can, not at 60 frames per second\n"
"	-x <num>	Draw the 3D num times finer than 640 by 480 (1 to 6)\n"
"	-a <num>	Filter the textures with an anisotropy of num (2 to 16); the board has none\n"
"	-w <num>	Make the window num times 640 by 480, as far as the display has room\n"
"	-f		Take the whole display (F11 or Alt+Enter goes back and forth)\n"
"	-o <mode>	Play the sound: main (the speakers of the main board), option (those\n"
"			of the optional board), mix (both on two channels, the optional one\n"
"			at the level of the list or of odin.ini), quad (four) or off; by\n"
"			default those the cabinet has: main, or mix\n"
"	-v		Set verbosity (affects debug messages)\n"
"	-V, --version	Show the version and exit\n"
"	-h, --help	Show this help\n";

/* The long names of -V and -h */
const option LONG_OPTIONS[] = {
	{ "version", no_argument, nullptr, 'V' },
	{ "help", no_argument, nullptr, 'h' },
	{ nullptr, 0, nullptr, 0 },
};

/* A size of the window in points as odin.ini writes it, <w>x<h>: from
 * 320 by 240 to 16384 by 16384 */
bool
window_size(const std::string &value, unsigned &w, unsigned &h)
{
	char rest = 0;

	return std::sscanf(value.c_str(), "%ux%u%c", &w, &h, &rest) == 2 && w >= 320 && h >= 240 && w <= 16384 &&
	       h <= 16384;
}

/* [display] of odin.ini, where the command line says nothing: the 3D 1 to
 * 6 times finer and how its flat meshes are drawn then (upscale_mode), the
 * texture filter (1 to 16), the window 1 to 6 times the
 * screen or of a size the user gave it, the whole display, no limit to the
 * frames, the speed in the title. The last line says. */
void
read_display(const Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("display")) {
		const std::optional<int> times = whole_named(value, 1, 6), anisotropy = whole_named(value, 1, 16);
		const std::optional<bool> on_off = on_off_named(value);
		unsigned w = 0, h = 0;

		if (key == "scale" && times) {
			if (!options.given.scale)
				options.display.scale = static_cast<unsigned>(*times);
		} else if (key == "anisotropy" && anisotropy) {
			if (!options.given.anisotropy)
				options.display.anisotropy = static_cast<unsigned>(*anisotropy);
		} else if (key == "window" && times) {
			if (!options.given.window) {
				options.display.window = static_cast<unsigned>(*times);
				options.display.window_w = options.display.window_h = 0;
			}
		} else if (key == "window" && window_size(value, w, h)) {
			if (!options.given.window) {
				options.display.window_w = w;
				options.display.window_h = h;
			}
		} else if (key == "fullscreen" && on_off) {
			if (!options.given.fullscreen)
				options.display.fullscreen = *on_off;
		} else if (key == "unthrottled" && on_off) {
			if (!options.given.unthrottled)
				options.unthrottled = *on_off;
		} else if (key == "show_speed" && on_off) {
			options.display.show_speed = *on_off;
		} else if (key == "vsync" && index_named(VSYNCS, value)) {
			options.vsync = static_cast<DisplayVsync>(*index_named(VSYNCS, value));
		} else if (key == "filter" && !value.empty()) {
			options.display.filter = value == "none" ? std::string() : value;
		} else if (key == "api" && index_named(APIS, value)) {
			options.display.api = static_cast<DisplayApi>(*index_named(APIS, value));
		} else if (key == "gpu") {
			options.display.gpu = value;
		} else if (key == "upscale_mode" && index_named(UPSCALE_MODES, value)) {
			options.display.upscale_mode = static_cast<DisplayUpscaleMode>(*index_named(UPSCALE_MODES, value));
		} else if (key == "bezel" && index_named(SIDES_NAMES, value)) {
			options.sides = static_cast<DisplaySides>(*index_named(SIDES_NAMES, value));
		} else {
			refuse_setting("display", key, value);
		}
	}
}

/* [paths] of odin.ini: the folder of the games where the command line
 * gives none, those of the saves and of the save states; [log]: the board
 * says each access where the command line does not. The last line says.
 * Where neither chooses, the games, the BIOS and the pictures are the roms,
 * bios and artwork folders of the folder of the program, when it has them:
 * a folder made ready with Odin (program_folder). */
void
read_paths(const Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("paths")) {
		if (key == "roms" && !value.empty()) {
			if (!options.given.roms)
				options.rom_path = value;
		} else if (key == "bios" && !value.empty()) {
			if (!options.given.bios)
				options.bios_path = value;
		} else if (key == "saves" && !value.empty()) {
			options.saves = value;
		} else if (key == "artwork" && !value.empty()) {
			options.artwork = value;
		} else if (key == "states" && !value.empty()) {
			options.states = value;
		} else if (key == "log" && !value.empty()) {
			options.log_folder = value;
		} else {
			refuse_setting("paths", key, value);
		}
	}
	for (const auto &[key, value] : settings.section("log")) {
		if (key == "verbose" && on_off_named(value)) {
			if (!options.given.verbose)
				options.verbose = *on_off_named(value);
		} else {
			refuse_setting("log", key, value);
		}
	}

	const std::string program = program_folder();
	const auto beside = [&](std::string &folder, const char *name) {
		std::error_code error;
		const std::string path = program + "/" + name;

		if (folder.empty() && !program.empty() && std::filesystem::is_directory(path, error))
			folder = path;
	};

	beside(options.rom_path, "roms");
	beside(options.bios_path, "bios");
	beside(options.artwork, "artwork");
}

/* [sound] of odin.ini: the device, the volume, the sound in the
 * background. The last line says. */
void
read_sound(const Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("sound")) {
		const std::optional<int> volume = whole_named(value, 0, 100);

		if (key == "device" && !value.empty())
			options.sound_device = value;
		else if (key == "volume" && volume)
			options.volume = static_cast<unsigned>(*volume);
		else if (key == "background" && on_off_named(value))
			options.background = *on_off_named(value);
		else
			refuse_setting("sound", key, value);
	}
}

/* What [players.<game>] says of a key, its last right line; `standard`
 * without one */
bool
player_on(const Settings &settings, const std::string &game, const char *key, bool standard)
{
	bool on = standard;

	for (const auto &[name, value] : settings.section("players." + game))
		if (name == key && on_off_named(value))
			on = *on_off_named(value);
	return on;
}

int
player_number(const Settings &settings, const std::string &game, const char *key, int standard,
              std::optional<int> (*named)(const std::string &))
{
	int number = standard;

	for (const auto &[name, value] : settings.section("players." + game))
		if (name == key && named(value))
			number = *named(value);
	return number;
}

} /* namespace */

Parsed
parse_options(int argc, char **argv, Options &options)
{
	int opt = 0;
	bool wrong = false;

	while (opt != 'h' && opt != 'V' &&
	       (opt = getopt_long(argc, argv, "R:B:c:r:n:l:s:ux:a:w:fo:vVh?", LONG_OPTIONS, nullptr)) != -1) {
		switch (opt) {
		case 'c':
			options.config = optarg;
			break;
		case 'R':
			options.rom_path = optarg;
			options.given.roms = true;
			break;
		case 'B':
			options.bios_path = optarg;
			options.given.bios = true;
			break;
		case 'r':
			options.game = optarg;
			break;
		case 'n':
			options.num_frames = static_cast<unsigned>(std::atoi(optarg));
			break;
		case 'l':
			options.state_path = optarg;
			break;
		case 's':
			for (const std::string_view frame : list_items(optarg))
				options.save_frames.push_back(static_cast<unsigned>(std::atoi(std::string(frame).c_str())));
			break;
		case 'o': {
			const std::string mode = optarg;

			if (mode == "off")
				options.sound.reset();
			else if (mode == "option")
				options.sound = AudioOut::Mode::OPTION;
			else if (mode_named(mode))
				options.sound = mode_named(mode);
			options.sound_asked = true;
			if (mode != "off" && mode != "option" && !mode_named(mode)) {
				opt = 'h';
				wrong = true;
			}
			break;
		}
		case 'u':
			options.unthrottled = options.given.unthrottled = true;
			break;
		case 'x':
			if (const int scale = std::atoi(optarg); scale >= 1 && scale <= 6) {
				options.display.scale = static_cast<unsigned>(scale);
				options.given.scale = true;
			}
			break;
		case 'a':
			if (const int level = std::atoi(optarg); level >= 2 && level <= 16) {
				options.display.anisotropy = static_cast<unsigned>(level);
				options.given.anisotropy = true;
			}
			break;
		case 'w':
			options.display.window = static_cast<unsigned>(std::atoi(optarg));
			options.given.window = true;
			break;
		case 'f':
			options.display.fullscreen = options.given.fullscreen = true;
			break;
		case 'v':
			options.verbose = options.given.verbose = true;
			break;
		case 'V':
			break;
		default:
			wrong = opt != 'h';
			opt = 'h';
			break;
		}
	}
	if (opt == 'V')
		return Parsed::VERSION_ASKED;
	if (opt != 'h')
		return Parsed::RUN;
	return wrong ? Parsed::WRONG : Parsed::HELP_ASKED;
}

void
print_help(const char *program)
{
	std::printf(HELP, program);
}

Options
with_settings(const Options &command, const Settings &settings)
{
	Options options = command;

	read_paths(settings, options);
	read_display(settings, options);
	read_sound(settings, options);
	return options;
}

std::optional<AudioOut::Mode>
mode_named(const std::string &name)
{
	if (const std::optional<size_t> played = index_named(MODE_NAMES, name))
		return mode_of(static_cast<int>(*played));
	return std::nullopt;
}

std::string
LibraryFilter::systems_line() const
{
	std::string line;

	for (const std::string &system : systems)
		line += (line.empty() ? "" : ",") + system;
	return line;
}

LibraryFilter
library_filter_of(const Settings &settings)
{
	LibraryFilter filter;

	for (const auto &[key, value] : settings.section("library")) {
		if (key == "owned_only" && on_off_named(value)) {
			filter.owned_only = *on_off_named(value);
		} else if (key == "variants" && on_off_named(value)) {
			filter.variants = *on_off_named(value);
		} else if (key == "systems") {
			filter.systems.clear();
			for (size_t at = 0; at <= value.size();) {
				const size_t comma = std::min(value.find(',', at), value.size());
				std::string system = value.substr(at, comma - at);

				std::erase(system, ' ');
				if (!system.empty())
					filter.systems.push_back(system);
				at = comma + 1;
			}
		} else {
			refuse_setting("library", key, value);
		}
	}
	return filter;
}

std::optional<double>
level_named(const std::string &value)
{
	char *end = nullptr;
	const double db = std::strtod(value.c_str(), &end);

	if (value.empty() || *end || db < Game::LOWEST_LEVEL || db > Game::HIGHEST_LEVEL)
		return std::nullopt;
	return db;
}

std::optional<double>
woofer_hz_named(const std::string &value)
{
	if (value == "off")
		return 0.0;
	for (const char *hz : WOOFER_CUTOFFS)
		if (value == hz)
			return std::strtod(hz, nullptr);
	return std::nullopt;
}

GameSound
game_sound(Options &options, const Game &game, const Settings &settings)
{
	const std::string section = "sound." + game.name;
	const char *const level_key = game.woofer ? "woofer_level" : "option_level";
	GameSound sound = { game.option_level, game.woofer ? *woofer_hz_named(WOOFER_CUTOFFS[1]) : 0.0 };

	/* Not Star Wars Racer: its optional board drives only the panel
	 * shaker, felt and not heard on the cabinet (the pad feels it in
	 * every mode) */
	if (!options.sound_asked && (game.option_speakers || game.woofer))
		options.sound = AudioOut::Mode::MIX;
	for (const auto &[key, value] : settings.section(section)) {
		if (key == "mode" && mode_named(value)) {
			if (!options.sound_asked)
				options.sound = mode_named(value);
		} else if (key == level_key && level_named(value)) {
			sound.level = *level_named(value);
		} else if (key == "woofer_cutoff" && game.woofer && woofer_hz_named(value)) {
			sound.woofer_hz = *woofer_hz_named(value);
		} else if (key != "start_volume" || game.machine != "smashdrv") {
			refuse_setting(section, key, value);
		}
	}
	/* A cabinet that does not wire the optional board plays the main one */
	if (!options.sound_asked && options.sound && !game.option_wired())
		options.sound = AudioOut::Mode::MAIN;
	return sound;
}

bool
vibration_on(const Settings &settings, const std::string &game)
{
	return player_on(settings, game, "vibration", true);
}

std::optional<OutputRoute>
output_route_named(const std::string &value)
{
	using To = OutputRoute::To;

	for (const unsigned player : { 0u, 1u }) {
		const std::string number = std::to_string(player + 1);

		if (value == "gun" + number)
			return OutputRoute{ To::GUN, player };
		if (value == "gun" + number + " recoil")
			return OutputRoute{ To::GUN_RECOIL, player };
	}
	if (value == "none")
		return OutputRoute{};
	return std::nullopt;
}

std::string
output_route_name(const OutputRoute &route)
{
	const std::string number = std::to_string(route.player + 1);

	switch (route.to) {
	case OutputRoute::To::GUN: return "gun" + number;
	case OutputRoute::To::GUN_RECOIL: return "gun" + number + " recoil";
	case OutputRoute::To::NOTHING: break;
	}
	return "none";
}

std::optional<OutputRoute>
output_route_of(const Settings &settings, const std::string &game, const std::string &id, bool tell)
{
	std::optional<OutputRoute> route;

	for (const auto &[key, value] : settings.section("outputs." + game)) {
		if (key != id || value.empty())
			continue;
		if (const std::optional<OutputRoute> named = output_route_named(value))
			route = named;
		else if (tell)
			refuse_setting("outputs." + game, key, value);
	}
	return route;
}

bool
hide_cursor_on(const Settings &settings, const std::string &game, bool standard)
{
	return player_on(settings, game, "hide_cursor", player_on(settings, game, "light_gun_mode", standard));
}

std::optional<int>
sinden_border_style_named(const std::string &value)
{
	if (const std::optional<size_t> style = index_named(SINDEN_BORDER_STYLES, value))
		return static_cast<int>(*style);
	return std::nullopt;
}

std::optional<int>
sinden_border_size_named(const std::string &value)
{
	return whole_named(value, LEAST_SINDEN_BORDER_SIZE, MOST_SINDEN_BORDER_SIZE);
}

SindenBorder
sinden_border_of(const Settings &settings, const std::string &game)
{
	SindenBorder border;

	border.on = player_on(settings, game, "sinden_border", false);
	border.window = player_number(settings, game, "sinden_border_style", 0, sinden_border_style_named) == 1;
	border.size = static_cast<unsigned>(
		player_number(settings, game, "sinden_border_size", SINDEN_BORDER_SIZE, sinden_border_size_named));
	return border;
}

std::optional<int>
wheel_force_named(const std::string &value)
{
	return whole_named(value, 0, MOST_WHEEL_FORCE);
}

int
wheel_force_of(const Settings &settings, const std::string &game)
{
	return player_number(settings, game, "wheel_force", WHEEL_FORCE, wheel_force_named);
}

std::optional<int>
vibration_force_named(const std::string &value)
{
	return whole_named(value, 0, MOST_VIBRATION_FORCE);
}

int
vibration_force_of(const Settings &settings, const std::string &game)
{
	return player_number(settings, game, "vibration_force", VIBRATION_FORCE, vibration_force_named);
}

std::optional<int>
wheel_rotation_named(const std::string &value)
{
	const std::optional<int> degrees = whole_named(value, 0, std::end(WHEEL_ROTATIONS)[-1]);

	if (!degrees || std::find(std::begin(WHEEL_ROTATIONS), std::end(WHEEL_ROTATIONS), *degrees) == std::end(WHEEL_ROTATIONS))
		return std::nullopt;
	return degrees;
}

int
wheel_rotation_of(const Settings &settings, const std::string &game)
{
	return player_number(settings, game, "wheel_rotation", WHEEL_ROTATION, wheel_rotation_named);
}

std::optional<InputDevice>
device_named(const std::string &value)
{
	if (const std::optional<size_t> device = index_named(DEVICE_NAMES, value))
		return static_cast<InputDevice>(*device);
	return std::nullopt;
}

std::optional<InputDevice>
device_chosen(const Settings &settings, const std::string &game, unsigned player)
{
	std::optional<InputDevice> chosen;

	for (const auto &[key, value] : settings.section("players." + game))
		if (key == (player & 1 ? "device2" : "device1") && device_named(value))
			chosen = device_named(value);
	return chosen;
}

std::string
cabinet_of(const Settings &settings, const std::string &game)
{
	std::string id = "standard";

	for (const auto &[key, value] : settings.section("players." + game))
		if (key == "cabinet")
			id = value;
	return id;
}

int
played_of(AudioOut::Mode mode)
{
	switch (mode) {
	case AudioOut::Mode::MAIN:
		return 0;
	case AudioOut::Mode::MIX:
		return 1;
	case AudioOut::Mode::QUAD:
		return 2;
	default:
		return -1;
	}
}

AudioOut::Mode
mode_of(int played)
{
	return played == 0 ? AudioOut::Mode::MAIN :
	       played == 2 ? AudioOut::Mode::QUAD : AudioOut::Mode::MIX;
}

std::string
default_states()
{
	const std::string data = odin_data_dir();

	return data.empty() ? data : data + "/states";
}

bool
load_game_list(GameList &games)
{
	const std::string home = home_dir();
	const std::string base = home.empty() ? "." : home;
	std::error_code error;
	std::vector<std::string> paths = { "./odin-games.json" };

	for (const std::string &folder : program_data_folders())
		paths.push_back(folder + "/odin-games.json");
	paths.push_back(base + "/odin-games.json");
	paths.push_back(base + "/.local/share/odin/odin-games.json");
	for (const std::string &path : paths) {
		if (!std::filesystem::exists(path, error))
			continue;
		if (!games.load(path))
			return false;
		std::printf("loading game list from '%s'\n", path.c_str());
		return true;
	}
	std::fprintf(stderr, "ERROR: game list not found in '%s', next to the program ('%s') or in '%s'\n",
	             paths.front().c_str(), paths[paths.size() - 2].c_str(), paths.back().c_str());
	return false;
}

std::vector<Settings::Known>
known_settings(const GameList &games, int button)
{
	std::vector<Settings::Known> out = {
		{ "display", "scale", "1" }, { "display", "upscale_mode", UPSCALE_MODES[0] }, { "display", "anisotropy", "1" },
		{ "display", "window", "1" },
		{ "display", "fullscreen", "off" }, { "display", "vsync", VSYNCS[static_cast<unsigned>(VSYNC)] },
		{ "display", "bezel", SIDES_NAMES[static_cast<unsigned>(SIDES)] },
		{ "display", "unthrottled", "off" }, { "display", "show_speed", "on" }, { "display", "filter", "none" },
		{ "display", "api", APIS[static_cast<unsigned>(API)] }, { "display", "gpu", "" },
		{ "sound", "device", "" }, { "sound", "volume", "100" }, { "sound", "background", "on" },
	};

	out.push_back({ "hotkeys", "button", Hotkeys::button_text(SDL_GAMEPAD_BUTTON_GUIDE) });
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<Hotkeys::Action>(a);

		out.push_back({ "hotkeys", Hotkeys::id(action), Hotkeys::line(Hotkeys::standard(action, button)) });
	}
	out.insert(out.end(), { { "outputs", "network", "off" }, { "network", "board", "none" },
	                        { "network", "port", std::to_string(NetCable::PORT) }, { "network", "next", "" },
	                        { "network", "cabinet", "1" },
	                        { "paths", "roms", "" }, { "paths", "bios", "" },
	                        { "paths", "saves", SaveFolder::root() }, { "paths", "states", default_states() },
	                        { "paths", "artwork", artwork_folder() }, { "paths", "log", odin_data_dir() },
	                        { "log", "verbose", "off" }, { "library", "owned_only", "off" },
	                        { "library", "variants", "on" }, { "library", "systems", "" },
	                        { "menu", "classic", "off" },
	                        { "menu", "animations", "on" }, { "menu", "theme", THEMES[0].key } });
	for (size_t a = 0; a < MenuNav::ACTIONS; a++) {
		const auto action = static_cast<MenuNav::Action>(a);

		out.push_back({ "menu", MenuNav::id(action), MenuNav::standard(action) });
	}
	out.insert(out.end(), { { "gamepads", "dead_zone", std::to_string(PadTuning::DEAD_ZONE) },
	                        { "gamepads", "trigger_threshold", std::to_string(PadTuning::TRIGGER_THRESHOLD) } });
	for (const auto &[name, title] : games.titles()) {
		const std::optional<Game> game = games.describe(name);

		/* A version: its parent's sections */
		if (!game || !game->parent.empty())
			continue;
		if (game->option_speakers || game->option_vibration) {
			char db[16];

			std::snprintf(db, sizeof(db), "%g", game->option_level);
			out.push_back({ "sound." + name, "mode", game->option_speakers ? "mix" : "main" });
			out.push_back({ "sound." + name, "option_level", db });
		}
		if (game->woofer)
			out.insert(out.end(), { { "sound." + name, "mode", "mix" }, { "sound." + name, "woofer_level", "0" },
			                        { "sound." + name, "woofer_cutoff", WOOFER_CUTOFFS[1] } });
		if (game->machine == "smashdrv")
			out.push_back({ "sound." + name, "start_volume", START_VOLUMES[START_VOLUME_STANDARD] });
		for (const auto &[key, value] : ControlPanel(game->controls, Settings(), name).bindings().standard_lines())
			out.push_back({ "controls." + name, key, value });
		for (const char *key : { "p1", "p2", "joy1", "joy2", "device1", "device2" })
			out.push_back({ "players." + name, key, "" });
		for (const GameControl &control : game->controls)
			if (control.role == Role::GUN_X) {
				out.push_back({ "players." + name, control.player ? "gun2" : "gun1", "" });
				if (!control.player)
					out.insert(out.end(), { { "players." + name, "hide_cursor", "on" },
					                        { "players." + name, "sinden_border", "off" },
					                        { "players." + name, "sinden_border_style", SINDEN_BORDER_STYLES[0] },
					                        { "players." + name, "sinden_border_size",
					                          std::to_string(SINDEN_BORDER_SIZE) } });
			}
		if (game->shakes()) {
			out.push_back({ "players." + name, "vibration", "on" });
			out.push_back({ "players." + name, "vibration_force", std::to_string(VIBRATION_FORCE) });
		}
		if (game->drive_first_output || game->wheel_motor)
			out.push_back({ "players." + name, "wheel_force", std::to_string(WHEEL_FORCE) });
		if (steers_of(game->controls))
			out.push_back({ "players." + name, "wheel_rotation", std::to_string(WHEEL_ROTATION) });
		if (std::any_of(game->controls.begin(), game->controls.end(),
		                [](const GameControl &control) { return control.invertible; }))
			out.push_back({ "players." + name, "invert", "" });
		if (!game->cabinets.empty())
			out.push_back({ "players." + name, "cabinet", "standard" });
		if (!game->card_reader.empty())
			out.push_back({ "players." + name, "card", "" });
		std::vector<std::string> rumbles;

		for (const GameOutput &output : game->outputs)
			if (output.role == GameOutput::Role::RUMBLE)
				rumbles.push_back(output.id);
		/* And those of its other cabinets */
		for (const Game::Cabinet &other : game->cabinets)
			for (const GameOutput &output : other.outputs.value_or(std::vector<GameOutput>{}))
				if (output.role == GameOutput::Role::RUMBLE &&
				    std::find(rumbles.begin(), rumbles.end(), output.id) == rumbles.end())
					rumbles.push_back(output.id);
		for (const std::string &id : rumbles)
			out.push_back({ "outputs." + name, id, "" });
	}
	return out;
}

} /* namespace odin */
