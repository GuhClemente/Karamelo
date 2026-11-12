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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "odin/host/video/display.hpp"
#include "odin/host/app/games.hpp"
#include "odin/host/app/hotkeys.hpp"
#include "odin/host/app/atvtrack_play.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/host/input/keyboard.hpp"
#include "odin/host/app/library.hpp"
#include "odin/host/app/log_file.hpp"
#include "odin/host/app/options.hpp"
#include "odin/common/env.hpp"
#include "odin/common/repeats.hpp"
#include "odin/common/stop_message.hpp"
#include "odin/host/app/session.hpp"
#include "odin/host/app/settings.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/host/app/version.hpp"

namespace odin {
namespace {

/* The files of a game read, the archives let go of then; the cabinet of
 * [players.<game>]. None, and what is missing told, when they are not all
 * there */
std::optional<Game>
load_or_tell(const Options &options, const std::string &name, const GameList &games, const Settings &settings)
{
	RomFiles files(options.rom_path, options.bios_path);
	std::optional<Game> loaded = games.load_game(files, name);

	if (loaded) {
		const std::string cabinet = cabinet_of(settings, name);

		if (!loaded->use_cabinet(cabinet))
			std::fprintf(stderr, "ERROR: settings: [players.%s]: not right: cabinet = %s\n", name.c_str(),
			             cabinet.c_str());
		else if (cabinet != "standard")
			std::printf("cabinet: %s\n", cabinet.c_str());
		return loaded;
	}
	for (const RomFiles::File &file : games.missing(files, name)) {
		char crc[16] = "";

		if (file.crc)
			std::snprintf(crc, sizeof(crc), ", CRC %08X", *file.crc);
		std::fprintf(stderr, "ERROR: '%s': %s (%llu bytes%s) is missing; MAME keeps it in %s.zip\n", name.c_str(),
		             file.name.c_str(), static_cast<unsigned long long>(file.size), crc, file.set_of(name).c_str());
	}
	return loaded;
}

/* The sound of a game as game_sound says. None at unthrottled frames,
 * which do not keep time (-u) */
std::unique_ptr<AudioOut>
open_sound(Options &options, const Game &game, const Settings &settings)
{
	const GameSound sound = game_sound(options, game, settings);
	std::unique_ptr<AudioOut> audio;

	if (options.sound && options.unthrottled && options.sound_asked)
		std::fprintf(stderr, "sound: not with -u\n");
	else if (options.sound && !options.unthrottled)
		audio = AudioOut::make(*options.sound, sound.level, options.sound_device);
	if (audio && options.volume != 100)
		audio->set_volume(static_cast<float>(options.volume) / 100.0f);
	if (audio && options.sound == AudioOut::Mode::MIX && sound.level != 0)
		std::printf("sound: the optional board at %+g dB in the mix\n", sound.level);
	return audio;
}

/* A game in the window, from its power-on, or from the state of -l, to its
 * end; the game of an archive Open... gave in `opened`. The options of the
 * command line and odin.ini hold for it, what it changes of them for itself
 * does not stay. */
Ended
play(const Options &given, const std::string &name, const GameList &games, Settings &settings,
     const std::string &settings_file, Display &display, Keyboard &keys, Pointers *pointers, Opened &opened)
{
	Options options = given;

	repeats::begin(options.verbose);
	if (!games.has(name)) {
		std::fprintf(stderr, "ERROR: failed to load '%s': game name not in game list\n", name.c_str());
		return Ended::FAILED;
	}

	const std::optional<Game> game = load_or_tell(options, name, games, settings);
	if (!game) {
		std::fprintf(stderr, "ERROR: failed to load '%s': can't load game files\n", name.c_str());
		return Ended::FAILED;
	}
	if (game->machine == "atvtrack" || game->machine == "smashdrv") {
		const Ended ended = play_atvtrack(options, *game, games, settings, settings_file, display, keys, pointers,
		                                  opened);

		repeats::end();
		return ended;
	}
	if (game->machine != "hikaru") {
		std::fprintf(stderr, "ERROR: failed to load '%s': it is a %s game, not a Hikaru game\n", name.c_str(),
		             game->machine.c_str());
		return Ended::FAILED;
	}

	const std::unique_ptr<Hikaru> board = Hikaru::make(*game, options.verbose, link_settings(settings));
	if (!board)
		return Ended::FAILED;

	display.attach(*board, game->title);
	Controls controls(keys, game->controls, game->outputs, settings, game->name);

	controls.set_pointers(pointers);
	board->set_panel(&controls);

	const bool network = network_on(settings);
	std::unique_ptr<OutputsNetwork> outputs =
		network ? OutputsNetwork::open(game->name, game->outputs) : nullptr;

	const std::unique_ptr<AudioOut> audio = open_sound(options, *game, settings);
	/* The pads shake unless [players.<game>] says off */
	const bool vibration = vibration_on(settings, game->name);

	controls.feel_game(*game, vibration);
	controls.set_wheel_force(wheel_force_of(settings, game->name));
	controls.set_vibration_force(vibration_force_of(settings, game->name));
	controls.set_wheel_rotation(wheel_rotation_of(settings, game->name));
	display.set_sinden_border(sights_of(game->controls)[0] ? sinden_border_of(settings, game->name) : SindenBorder());
	controls.ground(game->grounded);
	if (vibration && (game->option_vibration || game->drive_first_output))
		std::printf("controls: the pad of player 1 shakes as the%s%s of the cabinet (experimental)\n",
		            game->option_vibration ? " vibration device" : "",
		            game->drive_first_output ? (game->option_vibration ? " and the wheel" : " wheel") : "");

	bool started = true;

	board->keep_in(SaveFolder::of_game(game->name, options.saves));
	board->reset();
	if (!options.state_path.empty()) {
		started = board->load_state(options.state_path);
		if (started)
			std::printf("loaded state '%s', frame %u\n", options.state_path.c_str(), board->frame());
		else
			std::fprintf(stderr, "ERROR: failed to load state '%s'\n", options.state_path.c_str());
	}
	Ended ended = Ended::FAILED;

	if (started) {
		Session session(options, *game, *board, board.get(), display, keys, controls, audio.get(),
		                std::move(outputs), network, settings, settings_file, games);

		std::printf("Running\n");
		ended = session.run();
		opened = session.opened();
	}

	std::printf("Finalizing\n");
	/* Started again with the settings lost, it would ask again */
	if (!board->power_off() && board->restart_asked()) {
		std::fprintf(stderr, "ERROR: '%s': its settings could not be kept: not started again\n", name.c_str());
		ended = Ended::FAILED;
	}
	display.detach();
	repeats::end();
	return ended;
}

/* What Odin says goes to odin.log too, from its first line, in the folder
 * odin.ini gives it ([paths] log); on Windows, with no console of its own
 * after a double click. A board that cannot go on says why in a box too
 * (ODIN_STOP_BOX=0: not, as in the tests, which nobody watches) */
void
start_log(Options &options)
{
	Settings settings;
	std::string folder;

	LogFile::leave_own_console();
	if (const std::string file = Settings::user_file(options.config); !file.empty() && settings.load(file))
		for (const auto &[key, value] : settings.section("paths"))
			if (key == "log" && !value.empty())
				folder = value;
	if (const std::string log = LogFile::path(folder); LogFile::start(log))
		options.log_file = log;
	if (env_flag("ODIN_STOP_BOX", true))
		stop_message = [](const char *text) {
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Odin stopped", text, nullptr);
		};
}

void
print_banner()
{
	std::printf(" ██████╗ ██████╗ ██╗███╗   ██╗\n"
		    "██╔═══██╗██╔══██╗██║████╗  ██║\n"
		    "██║   ██║██║  ██║██║██╔██╗ ██║\n"
		    "██║   ██║██║  ██║██║██║╚██╗██║\n"
		    "╚██████╔╝██████╔╝██║██║ ╚████║\n"
		    " ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝\n\n");
	std::printf("Odin (ᚨ)%s%s, Copyright (C) 2026 Réda Chérif-Touil\n", *odin_version() ? " " : "", odin_version());
	std::printf("A hard fork of Valkyrie, Copyright (C) 2011-2013 Stefano Teso\n");
	std::printf("Released under the GNU GPL, version 3 or later.\n");
}

/* odin.ini of the user (of this folder for a portable Odin; the one of -c),
 * each key Odin knows written in it; its path, empty when there is none.
 * A version of a game keeps its settings in its parent's sections. */
std::string
load_settings(Settings &settings, const GameList &games, const std::string &chosen)
{
	settings.versions(games.parents());
	if (const std::string portable = portable_folder(); !portable.empty())
		std::printf("portable: the files of this Odin in '%s'\n", portable.c_str());

	const std::string settings_file = Settings::user_file(chosen);

	if (!settings_file.empty() && settings.load(settings_file)) {
		std::printf("settings: '%s'\n", settings_file.c_str());
		settings.know(known_settings(games, Hotkeys(settings, false).button()));
		settings.complete(settings_file);
	}
	return settings_file;
}

/* For the tests: ODIN_GAMES_TEST=<game>,..., these games after the first,
 * each from its power-on to its frame -n, in the same window */
std::vector<std::string>
games_test_list()
{
	std::vector<std::string> next;
	const char *const games_test = std::getenv("ODIN_GAMES_TEST");

	for (const std::string_view game : list_items(games_test ? games_test : ""))
		next.emplace_back(game);
	return next;
}

} /* namespace */
} /* namespace odin */

int
main(int argc, char **argv)
{
	odin::Options options;
	odin::GameList games;

	/* With the OpenGL driver of Intel loaded, a process of Windows ends
	 * without what its buffers still hold: it goes out here. */
	std::atexit([] { std::fflush(nullptr); });

	/* The options first: -c names the settings, where the log is */
	const odin::Parsed parsed = odin::parse_options(argc, argv, options);

	/* -V: the version alone, before the log: odin.log stays as it is, and
	 * on Windows the console still takes the line (the log lets it go) */
	if (parsed == odin::Parsed::VERSION_ASKED) {
		std::printf("Odin %s\n", *odin::odin_version() ? odin::odin_version() : "unknown");
		return 0;
	}
	odin::start_log(options);
	odin::print_banner();
	if (parsed != odin::Parsed::RUN) {
		odin::print_help(argv[0]);
		return parsed == odin::Parsed::HELP_ASKED ? 0 : 1;
	}

	if (!odin::load_game_list(games)) {
		std::fprintf(stderr, "ERROR: failed to load the game list\n");
		return 1;
	}

	odin::Settings settings;
	const std::string settings_file = odin::load_settings(settings, games, options.config);
	odin::Options command = options;
	options = odin::with_settings(command, settings);
	odin::repeats::begin(options.verbose);

	odin::Keyboard keys;
	options.display.vsync = options.unthrottled ? odin::Display::Vsync::OFF : options.vsync;
	const std::unique_ptr<odin::Display> display = odin::Display::make(keys, options.display);
	if (!display)
		return 1;
	/* The light guns and the mice, each read on its own (held only for the
	 * game that names them); none with ODIN_CONTROLLERS=none, as the
	 * gamepads and the mouse */
	const std::unique_ptr<odin::Pointers> pointers =
		odin::Controls::all_off() ? nullptr : std::make_unique<odin::Pointers>(display->window());

	const std::vector<std::string> next = odin::games_test_list();

	/* Without a game, the library: a game chosen there goes back to it when
	 * it ends, but by the hotkey that quits. Close game goes back to it from
	 * any game; Open... plays the game of an archive, from the folder that
	 * holds it, as if chosen there. What the library and each game change in
	 * the settings holds for what comes after them. */
	std::string name = options.game, last, failed;
	odin::Opened opened;
	bool chosen = name.empty();
	for (size_t played = 0;; played++) {
		if (name.empty()) {
			opened = odin::library(command, games, settings, settings_file, *display, keys, pointers.get(), last, failed);
			if (opened.game.empty())
				return 0;
			name = opened.game;
			chosen = true;
			options = odin::with_settings(command, settings);
		}

		odin::Options launch = options;
		if (!opened.folder.empty())
			launch.rom_path = opened.folder;
		opened = {};

		odin::Ended ended =
			odin::play(launch, name, games, settings, settings_file, *display, keys, pointers.get(), opened);

		/* The cabinet switched off and on: the game from its power-on */
		while (ended == odin::Ended::RESTART) {
			launch.state_path.clear();
			launch.save_frames.clear();
			ended = odin::play(launch, name, games, settings, settings_file, *display, keys, pointers.get(),
			                   opened);
		}

		/* -l and -s were for the first game */
		command.state_path.clear();
		command.save_frames.clear();
		options = odin::with_settings(command, settings);
		if (ended == odin::Ended::OPENED) {
			name = last = opened.game;
			failed.clear();
			chosen = true;
			continue;
		}
		if (ended == odin::Ended::CLOSED || (chosen && ended != odin::Ended::QUIT)) {
			failed = ended == odin::Ended::FAILED ? name : "";
			last = name;
			name.clear();
			continue;
		}
		if (ended == odin::Ended::FAILED)
			return 1;
		if (ended == odin::Ended::QUIT || played >= next.size())
			return 0;
		name = next[played];
	}
}
