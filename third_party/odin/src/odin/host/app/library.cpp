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

#include "odin/host/app/library.hpp"

#include <algorithm>

#include <SDL3/SDL.h>

#include "odin/host/input/controls.hpp"
#include "odin/host/app/rom_files.hpp"
#include "odin/host/app/user_files.hpp"

namespace odin {

namespace {

/* The games of the list the filter shows in the library, each with the
 * files it lacks in the folders now; that one chosen */
void
scan_library(LibraryState &state, const GameList &games, const Options &options, const std::string &chosen)
{
	state.games.clear();
	state.systems.clear();
	state.selected = 0;
	state.hidden = 0;

	RomFiles files(options.rom_path, options.bios_path);

	for (const GameList::Entry &entry : games.entries()) {
		const std::string &name = entry.name;
		const bool theirs = entry.machine != "hikaru";
		const std::string system = theirs ? "gaelco" : entry.machine;
		LibraryState::Game game{ name, entry.title, entry.parent, {}, theirs, system };

		if (std::find(state.systems.begin(), state.systems.end(), system) == state.systems.end())
			state.systems.push_back(system);
		/* Each file it lacks; a set that is nowhere, its zip alone */
		for (const RomFiles::File &file : games.missing(files, name)) {
			const std::string set = file.set_of(name) + ".zip";

			if (files.set_there(name, file))
				game.missing.push_back(file.name + " (" + set + ")");
			else if (std::find(game.missing.begin(), game.missing.end(), set) == game.missing.end())
				game.missing.push_back(set);
		}
		if (!state.filter.shows(system, !entry.parent.empty(), game.missing.empty())) {
			state.hidden++;
			continue;
		}
		state.games.push_back(std::move(game));
	}

	/* By system, in the order of the list, and by title in each */
	const auto rank = [&state](const LibraryState::Game &game) {
		return std::find(state.systems.begin(), state.systems.end(), game.system) - state.systems.begin();
	};
	const auto before = [&](const LibraryState::Game &a, const LibraryState::Game &b) {
		return rank(a) != rank(b) ? rank(a) < rank(b) : title_before(a.title, b.title);
	};

	std::stable_sort(state.games.begin(), state.games.end(), before);
	for (size_t i = 0; i < state.games.size(); i++)
		if (state.games[i].name == chosen)
			state.selected = static_cast<int>(i);
	state.roms = options.rom_path;
	state.bios = options.bios_path;
	state.roms_given = options.given.roms;
	state.bios_given = options.given.bios;
}

/* The picture of the library, taken, no wider than 1280 pixels (its tiles
 * are smaller on any screen): averaged by blocks of k x k pixels when its
 * sides divide by k, as a title taken at six times 640 by 480 by 3 x 3
 * (a linear scaling reads only 2 x 2 of them and skips the rest), else
 * scaled; the picture as it was when no smaller one can be made */
SDL_Surface *
shrink_picture(SDL_Surface *rgba)
{
	constexpr int WIDEST = 1280;
	const int k = rgba ? (rgba->w + WIDEST - 1) / WIDEST : 0;

	if (k < 2)
		return rgba;

	const bool blocks = rgba->w % k == 0 && rgba->h % k == 0;
	SDL_Surface *const small =
		blocks ? SDL_CreateSurface(rgba->w / k, rgba->h / k, SDL_PIXELFORMAT_RGBA32) :
		         SDL_ScaleSurface(rgba, WIDEST, std::max(1, rgba->h * WIDEST / rgba->w), SDL_SCALEMODE_LINEAR);

	if (!small)
		return rgba;
	for (int y = 0; blocks && y < small->h; y++)
		for (int x = 0; x < small->w; x++)
			for (int c = 0; c < 4; c++) {
				const auto *const from = static_cast<const uint8_t *>(rgba->pixels);
				unsigned sum = 0;

				for (int j = 0; j < k; j++)
					for (int i = 0; i < k; i++)
						sum += from[(y * k + j) * rgba->pitch + (x * k + i) * 4 + c];
				static_cast<uint8_t *>(small->pixels)[y * small->pitch + x * 4 + c] =
					static_cast<uint8_t>((sum + k * k / 2) / (k * k));
			}
	SDL_DestroySurface(rgba);
	return small;
}

/* The pictures of the games in the library, from the folder of the
 * artwork: the user's <game>_logo.png, else its <game>_title.png, else
 * those of its parent; a game with none there forgets the one it had */
void
library_pictures(Display &display, const LibraryState &state, const std::string &artwork)
{
	const std::string folder = artwork_folder(artwork);
	const auto picture = [&folder](const std::string &game) {
		const std::string logo = game.empty() ? "" : artwork_file(folder, game, "logo");

		return logo.empty() && !game.empty() ? artwork_file(folder, game, "title") : logo;
	};

	for (const LibraryState::Game &game : state.games) {
		const std::string own = picture(game.name), path = own.empty() ? picture(game.parent) : own;
		SDL_Surface *const png = path.empty() ? nullptr : SDL_LoadPNG(path.c_str());
		SDL_Surface *const rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;

		SDL_DestroySurface(png);
		display.add_library_picture(game.name, shrink_picture(rgba));
	}
}

} /* namespace */

Opened
library(const Options &command, const GameList &games, Settings &settings, const std::string &settings_file,
        Display &display, Keyboard &keys, Pointers *pointers, const std::string &last, const std::string &failed)
{
	using Ask = BarState::Ask;
	/* Some 60 pictures a second, between the events */
	static constexpr Sint32 PICTURE_MS = 16;
	Options options = with_settings(command, settings);
	/* The gamepads of the menu: the controls of no game */
	Controls controls(keys, {}, {}, settings, {});

	controls.set_pointers(pointers);
	MenuSession menus(options, display, keys, controls, settings, settings_file, games, nullptr, nullptr, nullptr,
	                  nullptr, nullptr, network_on(settings));
	LibraryState state;
	BarState bar;
	Uint64 note_until = 0;
	const auto chosen = [&state] {
		return state.games.empty() ? std::string() : state.games[static_cast<size_t>(state.selected)].name;
	};
	const auto open_menu = [&](int page) {
		menus.show(chosen());
		menus.open(page);
	};
	const auto say = [&](const std::string &note) {
		bar.note = note;
		note_until = SDL_GetTicks() + NOTE_MS;
	};

	state.filter = library_filter_of(settings);
	scan_library(state, games, options, last);
	LibraryFilter filter = state.filter;
	state.failed = failed;
	display.open_library();
	SDL_ShowCursor();
	library_pictures(display, state, options.artwork);
	for (;;) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT)
				return {};
			/* A folder chosen: the games looked for there */
			if (event.type == menus.picker_event()) {
				const std::string artwork = menus.state().artwork;
				std::string note;

				if (const std::optional<FolderDialog::Answer> archive = menus.take_folder()) {
					if (const std::optional<Opened> opened = open_archive(*archive, games, note))
						return *opened;
					if (!note.empty())
						say(note);
					continue;
				}
				options = with_settings(command, settings);
				scan_library(state, games, options, chosen());
				if (menus.state().artwork != artwork)
					library_pictures(display, state, options.artwork);
				continue;
			}
			/* A key let go here is let go for the next game: the one that
			 * closed the last game may still be held */
			if (event.type == SDL_EVENT_KEY_UP)
				keys.set(event.key.key, false);
			menus.window_event(event);
			if (display.menu_shown()) {
				const MenuSession::Asked asked = menus.event(event);

				if (asked == MenuSession::Asked::QUIT)
					return {};
				if (asked == MenuSession::Asked::CLOSE) {
					menus.close();
					display.open_library();
				}
				continue;
			}

			/* The hotkeys that work without a game: the menu (not while a
			 * menu of the bar is open: Escape closes it), the whole display,
			 * quitting */
			std::optional<Hotkeys::Action> action;
			const Hotkeys &hotkeys = menus.hotkeys();

			/* The joysticks plugged in and out, for the page Input */
			if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED) {
				controls.event(event);
				continue;
			}
			if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED ||
			    event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
			    event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
				controls.event(event);

				const int combo = controls.guide_combo();

				action = hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD);
				if (controls.guide_tapped())
					action = hotkeys.pad_action(Hotkeys::GUIDE_ALONE);
				/* The buttons of the menu choose and play, as the arrows
				 * and Enter */
				for (const MenuNav::Action nav : menus.nav(event)) {
					using Nav = MenuNav;

					state.nav.across += nav == Nav::RIGHT ? 1 : nav == Nav::LEFT ? -1 : 0;
					state.nav.down += nav == Nav::DOWN ? 1 : nav == Nav::UP ? -1 : 0;
					state.nav.press = state.nav.press || nav == Nav::PRESS;
				}
			} else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
				action = hotkeys.key_action(event.key.key, event.key.mod);
			}
			if (action == Hotkeys::Action::QUIT)
				return {};
			if (action == Hotkeys::Action::FULLSCREEN) {
				display.toggle_fullscreen();
				continue;
			}
			if (action == Hotkeys::Action::MENU && !bar.open) {
				open_menu(-1);
				continue;
			}
			display.menu_event(event);
		}
		if (display.menu_shown()) {
			menus.wait();
			if (menus.leaving()) {
				menus.close();
				display.open_library();
			}
			continue;
		}

		state.press = menus.press_name();
		menus.fill_bar(bar);
		bar.game = false;
		show_note(bar, note_until);
		display.draw_library(state, bar);
		/* What it shows changed: kept, the games looked at again */
		if (state.filter != filter) {
			const auto line = [](bool kept, const char *value) {
				return kept ? std::optional<std::string>(value) : std::nullopt;
			};

			filter = state.filter;
			menus.keep("library", "owned_only", line(filter.owned_only, "on"));
			menus.keep("library", "variants", line(!filter.variants, "off"));
			menus.keep("library", "systems", line(!filter.systems.empty(), filter.systems_line().c_str()));
			scan_library(state, games, options, chosen());
			library_pictures(display, state, options.artwork);
		}
		/* Without a game, the bar has no game to close, save, load or
		 * pause */
		switch (menus.do_bar(bar)) {
		case Ask::SETTINGS:
			open_menu(bar.page);
			continue;
		case Ask::QUIT:
			return {};
		default:
			break;
		}
		if (state.play)
			return { *state.play, {} };
		if (state.choose != LibraryState::Folder::NONE) {
			menus.ask_folder(state.choose);
			state.choose = LibraryState::Folder::NONE;
		}
		state.choosing = menus.choosing();
		SDL_WaitEventTimeout(nullptr, PICTURE_MS);
	}
}

} /* namespace odin */
