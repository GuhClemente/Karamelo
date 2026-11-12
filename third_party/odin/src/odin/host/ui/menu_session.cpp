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

#include "odin/host/ui/menu_session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <utility>

#include "odin/common/env.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/machine/chips/von4_card.hpp"

namespace odin {

std::optional<Opened>
open_archive(const FolderDialog::Answer &answer, const GameList &games, std::string &note)
{
	if (!answer.error.empty()) {
		std::fprintf(stderr, "ERROR: no file picker: %s\n", answer.error.c_str());
		note = "No file picker on this system";
		return std::nullopt;
	}
	if (answer.folder.empty())
		return std::nullopt;

	const std::filesystem::path file(answer.folder);
	std::string name = file.stem().string();

	std::transform(name.begin(), name.end(), name.begin(),
	               [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
	for (const auto &[game, title] : games.titles())
		if (game == name)
			return Opened{ game, file.parent_path().string() };
	std::fprintf(stderr,
	             "ERROR: '%s' is not a known game: the archive must have the game's MAME name (braveff.zip)\n",
	             answer.folder.c_str());
	note = file.filename().string() + ": not a game Odin knows";
	return std::nullopt;
}

bool
network_on(const Settings &settings)
{
	bool network = false;

	for (const auto &[key, value] : settings.section("outputs")) {
		if (key == "network" && on_off_named(value))
			network = *on_off_named(value);
		else
			refuse_setting("outputs", key, value);
	}
	return network;
}

/* [network]: the network board of the games that link cabinets
 * (NetLinkSettings). The last line says; a line not right is left out. */
NetLinkSettings
link_settings(const Settings &settings)
{
	NetLinkSettings link;

	for (const auto &[key, value] : settings.section("network")) {
		if (key == "board" && NetLinkSettings::board_named(value, link.board))
			continue;
		if (key == "port" && NetLinkSettings::port_named(value, link.port))
			continue;
		if (key == "cabinet" && NetLinkSettings::cabinet_named(value, link.cabinet))
			continue;
		if (key == "next" && NetLinkSettings::next_right(value)) {
			link.next = value;
			continue;
		}
		refuse_setting("network", key, value);
	}
	return link;
}

void
press_escape()
{
	SDL_Event event{};

	event.type = SDL_EVENT_KEY_DOWN;
	event.key.key = SDLK_ESCAPE;
	event.key.scancode = SDL_SCANCODE_ESCAPE;
	event.key.down = true;
	SDL_PushEvent(&event);
	event.type = SDL_EVENT_KEY_UP;
	event.key.down = false;
	SDL_PushEvent(&event);
}

MenuSession::MenuSession(const Options &options, Display &display, Keyboard &keys, Controls &controls,
                         Settings &settings, const std::string &settings_file, const GameList &games, const Game *game,
                         const Board *running, Hikaru *board, AudioOut *audio, std::unique_ptr<OutputsNetwork> outputs,
                         bool network)
	: m_options(options), m_display(display), m_keys(keys), m_controls(controls), m_settings(settings),
	  m_settings_file(settings_file), m_games(games), m_game(game), m_running(running), m_board(board), m_audio(audio),
	  m_outputs(std::move(outputs)), m_hotkeys(settings), m_nav(settings),
	  m_menu_test(game ? static_cast<unsigned>(std::max(env_int("ODIN_MENU_TEST", 0), 0)) : 0),
	  m_menu_test_page(m_menu_test ? env_int("ODIN_MENU_PAGE", -1) : -1),
	  m_bar_test(m_menu_test ? env_int("ODIN_BAR_MENU", -1) : -1)
{
	MenuState &state = m_menu_state;

	m_nav.hotkey(m_hotkeys.button(), m_hotkeys.combos());

	state.scale = options.display.scale;
	state.anisotropy = options.display.anisotropy;
	state.upscale_mode = static_cast<unsigned>(options.display.upscale_mode);
	state.filter = options.display.filter;
	state.window = options.display.window ? options.display.window : 1;
	state.custom_w = options.display.window_w;
	state.custom_h = options.display.window_h;
	state.unthrottled = options.unthrottled;
	state.show_speed = options.display.show_speed;
	state.vsync = static_cast<unsigned>(options.vsync);
	state.api = static_cast<unsigned>(options.display.api);
	state.sides = static_cast<unsigned>(options.sides);
	state.scale_given = options.given.scale;
	state.anisotropy_given = options.given.anisotropy;
	state.window_given = options.given.window;
	state.fullscreen_given = options.given.fullscreen;
	state.unthrottled_given = options.given.unthrottled;
	state.gpu = display.gpu();
	state.gpu_chosen = options.display.gpu;
	state.gpus = HostGraphics::gpus();
	state.vulkan = display.vulkan();
	state.kept = !settings_file.empty();
	state.roms = options.rom_path;
	state.roms_given = options.given.roms;
	state.bios = options.bios_path;
	state.bios_given = options.given.bios;
	state.saves = SaveFolder::root(options.saves);
	state.saves_given = !env_text("ODIN_SAVES").empty();
	state.states = m_states = options.states.empty() ? default_states() : options.states;
	state.settings_file = settings_file;
	state.log_file = options.log_file;
	state.log_folder = options.log_folder;
	state.log_given = !env_text("ODIN_LOG").empty();
	state.artwork = artwork_folder(options.artwork);
	state.artwork_given = !env_text("ODIN_ARTWORK").empty();
	if (game) {
		display.set_sides(options.sides, artwork_file(state.artwork, game->name, "bezel", game->parent));
		controls.set_picture_hole(display.picture_hole());
	}
	state.shader_lists = std::filesystem::path(display.program_list()).parent_path().string();
	state.verbose = options.verbose;
	state.verbose_given = options.given.verbose;
	state.network = network;
	state.network_failed = network && game && !m_outputs;

	const NetLinkSettings link = link_settings(settings);

	state.link_board = static_cast<int>(link.board);
	state.link_port = link.port;
	state.link_next = state.link_next_typed = link.next;
	state.link_cabinet = link.cabinet;
	state.link_given = std::getenv("ODIN_NETBD") != nullptr;
	state.device = options.sound_device;
	state.volume = options.volume;
	state.background = options.background;
	/* The library lists the devices of the system: its sound is
	 * started for that */
	m_listing = !game && options.sound && !options.unthrottled && SDL_InitSubSystem(SDL_INIT_AUDIO);
	state.silent = options.unthrottled ? "no sound at unlimited speed (Display page)" :
	               !options.sound ? "set on the command line: -o off" :
	               !audio && !m_listing ? "no sound device" : nullptr;
	state.library = !game;
	fill_games();
	read_menu_settings();
	state.shown = state.running = state.games.size();
	for (size_t g = 0; g < state.games.size(); g++)
		if (game && state.games[g].first == game->family())
			state.shown = state.running = g;
	/* The BIOS alone, or no game: the page Input shows the first one */
	if (state.running == state.games.size())
		show_game(0);
	take_card();
}

/* Each game of the list but the versions (they share their parent's
 * settings), by title: the ones whose optional sound board the user may
 * hear, as the cabinet plays it unless the settings or -o say; whether it
 * links cabinets */
void
MenuSession::fill_games()
{
	MenuState &state = m_menu_state;

	state.games.clear();
	for (const GameList::Entry &entry : m_games.entries())
		if (entry.parent.empty())
			state.games.emplace_back(entry.name, entry.title);
	std::stable_sort(state.games.begin(), state.games.end(),
	                 [](const auto &a, const auto &b) { return title_before(a.second, b.second); });
	for (const auto &[name, title] : state.games) {
		const bool runs = m_game && name == m_game->family();
		const std::optional<Game> other = runs ? std::nullopt : m_games.describe(name);

		const Game *const described = runs ? m_game : other ? &*other : nullptr;

		state.game_links.push_back(described && described->link.cabinets);
		state.game_gaelco.push_back(described && described->machine != "hikaru");

		MenuState::GameAudio audio;

		if (described) {
			audio.parts = described->sound_parts;
			audio.seated = described->seated;
			for (const Game::Cabinet &model : described->cabinets)
				if (model.seated && !*model.seated)
					audio.standing.push_back(model.id);
			audio.family = described->woofer ? SoundFamily::WOOFER :
			               described->option_speakers || described->option_vibration ? SoundFamily::TWO_BOARDS :
			               SoundFamily::ONE;
		}
		if (audio.parts.empty())
			for (const SoundPart::Place at : { SoundPart::Place::FRONT_LEFT, SoundPart::Place::FRONT_RIGHT }) {
				SoundPart part;

				part.at = at;
				audio.parts.push_back(part);
			}
		state.game_audio.push_back(std::move(audio));

		const bool smash = described && described->machine == "smashdrv";
		const bool woofer = described && described->woofer;

		if (!described || !(described->option_speakers || described->option_vibration || woofer || smash))
			continue;

		/* As the cabinet: both boards, or the woofer, in the speakers
		 * where it has some for the optional board, or a woofer */
		const int cabinet = described->option_speakers || woofer ? 1 : 0;
		SoundGame sound = { name, title, cabinet, cabinet, 0.0f, 0.0f, false };

		sound.mixed = described->option_speakers || described->option_vibration || woofer;
		sound.woofer = woofer;
		sound.level_name = woofer ? "Woofer" : described->option_speakers ? "Seat speakers" : "Panel shaker";
		sound.level = sound.standard = static_cast<float>(described->option_level);
		for (const auto &[key, value] : m_settings.section("sound." + name)) {
			if (key == "mode" && mode_named(value)) {
				sound.played = played_of(*mode_named(value));
			} else if (key == (woofer ? "woofer_level" : "option_level") && level_named(value)) {
				sound.level = static_cast<float>(*level_named(value));
			} else if (key == "woofer_cutoff" && woofer) {
				for (int c = 0; c < static_cast<int>(std::size(WOOFER_CUTOFFS)); c++)
					if (value == WOOFER_CUTOFFS[c])
						sound.cutoff = c;
			} else if (key == "start_volume") {
				for (int v = 0; v < static_cast<int>(std::size(START_VOLUMES)); v++)
					if (value == START_VOLUMES[v])
						sound.start = v;
			}
		}
		if (smash && sound.start < 0)
			sound.start = START_VOLUME_STANDARD;
		if (runs && m_options.sound_asked) {
			sound.played = m_options.sound ? played_of(*m_options.sound) : -1;
			sound.locked = true;
		}
		state.sound_games.push_back(sound);
	}
}

/* [menu] of odin.ini */
void
MenuSession::read_menu_settings()
{
	MenuState &state = m_menu_state;

	for (const auto &[key, value] : m_settings.section("menu")) {
		if (key == "classic" && on_off_named(value))
			state.classic = *on_off_named(value);
		else if (key == "animations" && on_off_named(value))
			state.animations = *on_off_named(value);
		else if (key == "theme" && theme_named(value) >= 0)
			state.theme = static_cast<unsigned>(theme_named(value));
		else if (!MenuNav::knows(key))
			refuse_setting("menu", key, value);
	}
}

/* The card in hand: the one of the settings (ODIN_CARD_FILE first); new
 * cards, when the program of the game has what makes them */
void
MenuSession::take_card()
{
	if (m_game && m_board && m_board->card_reader()) {
		if (const Von4Card maker(m_board->program()); maker.valid())
			m_card_keys = maker.keys();
		list_cards();
		for (const auto &[key, value] : m_settings.section("players." + m_game->name))
			if (key == "card" && !std::getenv("ODIN_CARD_FILE") &&
			    std::find(m_cards.begin(), m_cards.end(), value) != m_cards.end())
				m_board->card_reader()->choose(card_file(value));
	}
}

void
MenuSession::list_cards()
{
	const std::string folder = m_board ? m_board->cards_folder() : std::string();
	std::error_code error;

	m_cards.clear();
	if (folder.empty())
		return;
	for (auto it = std::filesystem::directory_iterator(folder, error); !error && it != std::filesystem::directory_iterator();
	     it.increment(error))
		if (it->path().extension() == ".card" && it->is_regular_file(error))
			m_cards.push_back(it->path().stem().string());
	std::sort(m_cards.begin(), m_cards.end());
}

std::string
MenuSession::card_file(const std::string &name) const
{
	const std::string folder = m_board ? m_board->cards_folder() : std::string();

	return folder.empty() ? folder : folder + "/" + name + ".card";
}

std::string
MenuSession::cursor_in_out()
{
	if (!m_game || !m_controls.on())
		return {};

	const bool on = !m_controls.hides_cursor();

	keep_hide_cursor(m_game->name, on, true, m_controls.panel().takes_mouse());
	m_controls.follow_cursor();
	return on ? "Cursor hidden: the mouse controls the game" : "Cursor shown: normal pointer";
}

/* In hand, the card goes into the slot; at the slot, it comes back to the
 * hand, a new card from the stocker kept as card-<n> */
std::string
MenuSession::card_in_out()
{
	CardReader *const reader = m_board ? m_board->card_reader() : nullptr;

	if (!reader)
		return "No card reader on this cabinet";

	const std::string name = std::filesystem::path(reader->card_file()).stem().string();

	switch (reader->where()) {
	case CardReader::Where::AT_SLOT: {
		const std::string folder = m_board->cards_folder();
		std::string fresh;
		std::error_code error;

		if (!folder.empty()) {
			std::filesystem::create_directories(folder, error);
			for (unsigned n = 1; fresh.empty() || std::filesystem::exists(fresh, error); n++)
				fresh = card_file("card-" + std::to_string(n));
		}
		reader->take(fresh);
		if (reader->where() != CardReader::Where::IN_HAND)
			return "Card removed but not saved: no save folder";

		const std::string taken = std::filesystem::path(reader->card_file()).stem().string();

		list_cards();
		keep("players." + m_game->name, "card", taken);
		return "Card removed: " + taken;
	}
	case CardReader::Where::IN_HAND:
		if (!reader->shutter_open())
			return "The card slot is closed";
		if (!reader->insert())
			return "The card " + name + " cannot be read";
		return "Card inserted: " + name;
	case CardReader::Where::INSIDE:
		return "The card is inside the reader";
	case CardReader::Where::NO_CARD:
	default:
		return "No card in hand: choose one in FILE > Card in hand";
	}
}

std::string
MenuSession::choose_card(int index)
{
	CardReader *const reader = m_board ? m_board->card_reader() : nullptr;
	const bool none = index < 0 || index >= static_cast<int>(m_cards.size());
	const std::string name = none ? std::string() : m_cards[static_cast<size_t>(index)];

	if (!reader)
		return "No card reader on this cabinet";
	if (!reader->choose(none ? std::string() : card_file(name)))
		return "The card is in the reader: remove it first";
	keep("players." + m_game->name, "card", none ? std::nullopt : std::optional<std::string>(name));
	return none ? "No card in hand" : "Card in hand: " + name;
}

namespace {

/* A pilot's name as the name of its file: what a file of Windows cannot be
 * named with as _, without the spaces at its ends, never the name of a
 * device of Windows */
std::string card_stem(const std::string &name)
{
	const size_t first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
	std::string stem = first == std::string::npos ? std::string() : name.substr(first, last - first + 1);

	for (char &c : stem)
		if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
			c = '_';
	for (const char *device : { "CON", "PRN", "AUX", "NUL" })
		if (stem == device)
			stem += '_';
	if (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')
		stem += '_';
	return stem;
}

} /* namespace */

std::string
MenuSession::new_card(const std::string &name)
{
	CardReader *const reader = m_board ? m_board->card_reader() : nullptr;
	const Von4Card maker(m_board ? m_board->program() : std::span<const uint8_t>());

	if (!reader || !maker.valid())
		return "This game cannot make new cards";

	const std::optional<std::array<uint8_t, CardReader::CARD_SIZE>> card = maker.make(name);

	if (!card)
		return "The game's terminal does not accept this name: " + name;

	const std::string folder = m_board->cards_folder();
	std::error_code error;

	if (folder.empty())
		return "Cannot make a new card: no save folder";
	std::filesystem::create_directories(folder, error);

	/* Under the pilot's name, the number after it of the first file that
	 * is not there */
	const std::string stem = card_stem(name);
	std::string path = card_file(stem);

	for (unsigned n = 2; std::filesystem::exists(path, error); n++)
		path = card_file(stem + "-" + std::to_string(n));
	if (!CardReader::write_card(path, *card, stderr))
		return "Cannot make a new card: " + path + " could not be written";
	list_cards();

	const std::string made = std::filesystem::path(path).stem().string();

	if (!reader->choose(path))
		return "New card " + made + " saved: remove the card in the reader to take it";
	keep("players." + m_game->name, "card", made);
	return "New card in hand: " + made;
}

std::string
MenuSession::card_news(bool quiet)
{
	using Where = CardReader::Where;
	CardReader *const reader = m_board ? m_board->card_reader() : nullptr;

	if (!reader)
		return {};

	const Where now = reader->where();
	const std::optional<Where> was = std::exchange(m_card_seen, now);

	if (quiet || !was || *was == now)
		return {};

	const std::string key = Hotkeys::first_text(m_hotkeys.of(Hotkeys::Action::CARD));
	const std::string with = key.empty() ? "FILE > Remove card" : key;

	if (now == Where::INSIDE)
		return "Card pulled in: " + std::filesystem::path(reader->card_file()).stem().string();
	if (now == Where::AT_SLOT)
		return (*was == Where::INSIDE ? "Card ejected: remove it with " : "New card dispensed: take it with ") + with;
	return {};
}

MenuSession::Asked
MenuSession::event(const SDL_Event &event)
{
	using Action = Hotkeys::Action;

	if (!m_menu_state.catching && !m_menu_state.mapping)
		m_catch_from.reset();
	if (m_menu_state.catching && catch_input(event)) {
		m_menu_pictures = MENU_PICTURES;
		return Asked::NONE;
	}
	if (m_menu_state.hotkey_catching && catch_hotkey(event)) {
		m_menu_pictures = MENU_PICTURES;
		return Asked::NONE;
	}

	std::optional<Action> action;
	bool pad = false;

	switch (event.type) {
	case SDL_EVENT_QUIT:
		return Asked::QUIT;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		/* The keys write in a field of the menu, Escape leaving it */
		if (event.key.down && !event.key.repeat && !m_display.menu_typing())
			action = m_hotkeys.key_action(event.key.key, event.key.mod);
		/* Escape closes a list dropped open first, then the advanced
		 * options, whatever action it has */
		if (event.key.down && event.key.key == SDLK_ESCAPE && m_display.menu_popup_open()) {
			action.reset();
		} else if (event.key.down && !event.key.repeat && event.key.key == SDLK_ESCAPE && m_menu_state.advanced) {
			action.reset();
			m_menu_state.advanced = false;
		}
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP: {
		m_controls.event(event);

		const int combo = m_controls.guide_combo();

		action = m_hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD);
		if (m_controls.guide_tapped())
			action = m_hotkeys.pad_action(Hotkeys::GUIDE_ALONE);
		pad = true;
		break;
	}
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		/* The mouse aims and presses for player 1 on the drawing of the
		 * cabinet, as in the game; it works the menu as ever */
		m_controls.event(event);
		break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	case SDL_EVENT_JOYSTICK_ADDED:
	case SDL_EVENT_JOYSTICK_REMOVED:
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
	case SDL_EVENT_JOYSTICK_HAT_MOTION:
		m_controls.event(event);
		break;
	default:
		break;
	}
	/* The test of the page Input stops first, as a list dropped open, on
	 * the menu's key or any shortcut of a gamepad; while it runs, the
	 * gamepads and the keys work no part of the menu */
	const bool testing = m_menu_state.testing;

	if (testing && (action == Action::MENU || (pad && action))) {
		m_menu_state.testing = false;
		action.reset();
	}
	if (action == Action::QUIT)
		return Asked::QUIT;
	if (action == Action::CLOSE_GAME && m_game)
		return Asked::CLOSE_GAME;
	if (action == Action::MENU)
		return Asked::CLOSE;
	if (action == Action::FULLSCREEN)
		m_display.toggle_fullscreen();
	/* The menu's own buttons; the keys of a list dropped open are the list's */
	for (const MenuNav::Action nav : m_nav.event(event)) {
		if (action || testing || (!pad && m_display.menu_popup_open()) || !navigate(nav))
			continue;
		m_menu_pictures = MENU_PICTURES;
		return Asked::CLOSE;
	}

	/* The gamepads are Odin's, not Dear ImGui's; a key let go still
	 * reaches it, pressed before the test */
	if (!pad && !(event.type == SDL_EVENT_KEY_DOWN && testing && !m_display.menu_popup_open()))
		m_display.menu_event(event);
	m_menu_pictures = MENU_PICTURES;
	return Asked::NONE;
}

std::string
MenuSession::press_name() const
{
	const unsigned player = m_controls.pad_name(0).empty() ? 1 : 0;

	if (m_controls.pad_name(player).empty())
		return {};
	for (const HostInput &in : m_nav.inputs(MenuNav::PRESS))
		if (in.kind == HostInput::Kind::PAD)
			return Menu::input_name(in.pad, m_controls.pad_type(player));
	return {};
}

/* For its next picture */
bool
MenuSession::navigate(MenuNav::Action action)
{
	using Nav = MenuNav;
	MenuState::Nav &nav = m_menu_state.nav;

	switch (action) {
	case Nav::UP: nav.move--; break;
	case Nav::DOWN: nav.move++; break;
	case Nav::LEFT: nav.side--; break;
	case Nav::RIGHT: nav.side++; break;
	case Nav::PRESS: nav.press = true; break;
	case Nav::CLEAR: nav.clear = true; break;
	case Nav::RESET: nav.reset = true; break;
	case Nav::PAGE_BEFORE: nav.page--; break;
	case Nav::PAGE_AFTER: nav.page++; break;
	case Nav::GAME_BEFORE: nav.game--; break;
	case Nav::GAME_AFTER: nav.game++; break;
	case Nav::BACK:
		/* The advanced options close first, as a list dropped open */
		if (m_menu_state.advanced && !m_display.menu_popup_open()) {
			m_menu_state.advanced = false;
			break;
		}
		if (!m_display.menu_popup_open())
			return true;
		nav.back = true;
		break;
	default:
		break;
	}
	return false;
}

void
MenuSession::open(int page)
{
	m_controls.quiet();
	if (m_audio)
		m_audio->pause(true);
	m_menu_state.hotkey_moved.reset();
	/* The page Input shows the device each player plays with, the one that
	 * shakes */
	for (unsigned player = 0; player < 2; player++)
		m_menu_state.input_device[player] = m_controls.device(player);
	m_devices_of_other = m_other.has_value();
	m_display.open_menu(page < 0 ? m_menu_test_page : page);
	m_menu_pictures = MENU_PICTURES;
}

void
MenuSession::close()
{
	m_display.close_menu();
	m_controls.follow_cursor();
	m_nav.reset();
	m_held.reset();
	m_menu_state.advanced = false;
	m_menu_state.testing = false;
	m_menu_state.nav = {};
	m_keys.swallow();
	if (m_audio)
		m_audio->pause(false);
}

void
MenuSession::show(const std::string &game)
{
	/* A version: its parent, which the pages show */
	const std::optional<Game> described = m_games.describe(game);
	const std::string family = described ? described->family() : game;

	for (size_t g = 0; g < m_menu_state.games.size(); g++)
		if (m_menu_state.games[g].first == family)
			show_game(g);
}

/* Not heard in the background, when the user says so */
void
MenuSession::focus(bool focused)
{
	m_focused = focused;
	if (m_audio)
		m_audio->set_heard(m_focused || m_menu_state.background);
}

void
MenuSession::window_event(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED)
		m_display.display_changed();
	if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
		focus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
}

void
MenuSession::wait()
{
	const Uint64 now = SDL_GetTicks();

	if (m_menu_pictures && now - m_menu_drawn >= MENU_PICTURE_MS) {
		draw_menu();
		m_menu_drawn = now;
		m_menu_pictures--;
		if (m_menu_test && !m_menu_pictures)
			press_escape();
	}
	/* Undo goes when its time is over; the prompt of an input caught
	 * beats, Start mapping counts the seconds of the control it waits for */
	const bool caught = m_menu_state.catching || m_menu_state.mapping;

	if ((m_menu_state.undo && SDL_GetTicks() >= m_undo_until) || m_menu_state.moving)
		m_menu_pictures = std::max(m_menu_pictures, 1u);
	/* The drawing of the cabinet follows where the light guns aim, and
	 * lights their buttons */
	const bool guns = m_controls.follows_guns();

	if (guns && m_controls.follow_guns_in_menu())
		m_menu_pictures = std::max(m_menu_pictures, 1u);
	if (caught && SDL_GetTicks() - m_menu_drawn >= CATCH_TICK_MS)
		m_menu_pictures = std::max(m_menu_pictures, 1u);
	if (!m_menu_pictures) {
		if (caught)
			SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(CATCH_TICK_MS));
		else if (m_menu_state.undo)
			SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(m_undo_until - std::min(m_undo_until, SDL_GetTicks())));
		else if (guns)
			SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(MENU_PICTURE_MS));
		else
			SDL_WaitEvent(nullptr);
		return;
	}

	const Uint64 since = SDL_GetTicks() - m_menu_drawn;

	SDL_WaitEventTimeout(nullptr, since >= MENU_PICTURE_MS ? 0 : static_cast<Sint32>(MENU_PICTURE_MS - since));
}

void
MenuSession::fill_bar(BarState &bar) const
{
	const MenuState &state = m_menu_state;

	static_cast<BarSettings &>(bar) = state;
	bar.fullscreen = m_display.fullscreen();
	bar.verbose_given = state.verbose_given;
	bar.silent = state.silent != nullptr;
	bar.custom_window = state.custom_w != 0;
	bar.scale_given = state.scale_given;
	bar.anisotropy_given = state.anisotropy_given;
	bar.window_given = state.window_given;
	bar.unthrottled_given = state.unthrottled_given;
	bar.vsync_locked = m_options.unthrottled;
	bar.gpu = state.gpu;
	if ((m_audio || m_listing) && (bar.open || bar.devices.empty()))
		bar.devices = AudioOut::devices();
	bar.sound_games.clear();
	for (const SoundGame &game : state.sound_games)
		if (m_game && game.name == m_game->family() && game.mixed)
			bar.sound_games.push_back(game);
	bar.link_given = state.link_given;
	bar.link_next = state.link_next;
	bar.gaelco = m_game && m_game->machine != "hikaru" && !m_game->link.cabinets;
	fill_bar_inputs(bar);
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++)
		bar.keys[a] = Hotkeys::first_text(m_hotkeys.of(static_cast<Hotkeys::Action>(a)));
	if (const CardReader *const reader = m_board ? m_board->card_reader() : nullptr) {
		const std::string name = std::filesystem::path(reader->card_file()).stem().string();
		const auto chosen = std::find(m_cards.begin(), m_cards.end(), name);

		bar.card_reader = true;
		bar.card_where = static_cast<int>(reader->where());
		bar.card_slot_open = reader->shutter_open();
		bar.cards = m_cards;
		bar.card_keys = m_card_keys;
		bar.card = reader->card_file().empty() || chosen == m_cards.end() ? -1 :
		           static_cast<int>(chosen - m_cards.begin());
	}
}

/* The light guns seen (one chosen and never plugged in since Odin started,
 * as the settings name it), that of each player in a game, the players
 * with a sight there */
void
MenuSession::light_guns(const std::string &game, const std::vector<GameControl> &controls,
                        std::vector<LightGun> &guns, std::array<std::string, 2> &gun, std::array<bool, 2> &sights) const
{
	guns.clear();
	if (const Pointers *const pointers = m_controls.pointers())
		for (const Pointers::Device &device : pointers->devices())
			guns.push_back({ device.identity, device.name, device.present, device.absolute });
	gun = {};
	for (const auto &[key, value] : m_settings.section("players." + game)) {
		if (key != "gun1" && key != "gun2")
			continue;

		const size_t space = value.find(' ');
		const std::string identity = value.substr(0, space);

		gun[static_cast<size_t>(key[3] - '1')] = identity;
		if (std::none_of(guns.begin(), guns.end(), [&identity](const LightGun &one) { return one.identity == identity; }))
			guns.push_back({ identity, space == std::string::npos ? identity : value.substr(space + 1), false, true });
	}
	sights = sights_of(controls);
}

/* The inputs of the game that runs, as the page Input shows them for the
 * game it shows: the gamepads and joysticks plugged in (two of a make
 * numbered as there), the tuning of each player's gamepad, the light guns
 * seen (never polled here: the game polls them once a frame) and those the
 * settings name, the players with a sight, the vibration */
void
MenuSession::fill_bar_inputs(BarState &bar) const
{
	bar.pads.clear();
	bar.guns.clear();
	bar.gun = {};
	bar.sights = {};
	bar.dead_zone = { -1, -1 };
	bar.trigger_threshold = { -1, -1 };
	bar.can_shake = m_game && m_game->shakes();
	bar.undo = m_undo_until && SDL_GetTicks() < m_undo_until;
	bar.players = 1;
	if (!m_game)
		return;
	bar.players = m_controls.panel().two_players() ? 2 : 1;
	for (unsigned player = 0; player < 2; player++)
		bar.plays_with[player] = m_controls.device(player);

	const std::vector<GameControl> &controls = m_controls.panel().controls();
	const bool steers = steers_of(controls);
	const auto add = [&bar, steers](const std::vector<Controls::Plugged> &plugged, bool joystick) {
		for (size_t i = 0; i < plugged.size(); i++) {
			const Controls::Plugged &one = plugged[i];
			const auto before = std::count_if(plugged.begin(), plugged.begin() + static_cast<std::ptrdiff_t>(i),
			                                  [&one](const Controls::Plugged &other) { return other.name == one.name; });

			bar.pads.push_back({ one.name,
			                     (before ? one.name + " (" + std::to_string(before + 1) + ")" : one.name) +
			                             (joystick ? " · joystick" : ""),
			                     one.guid, joystick, one.player, one.id,
			                     joystick && one.kind == SDL_JOYSTICK_TYPE_WHEEL && !steers });
		}
	};
	const std::vector<Controls::Plugged> pads = m_controls.plugged();

	add(pads, false);
	add(m_controls.joysticks(), true);
	for (const Controls::Plugged &pad : pads)
		if (pad.player == 0 || pad.player == 1) {
			const PadTuning tuning = PadTuning::of(m_settings, pad.guid, false);

			bar.dead_zone[static_cast<size_t>(pad.player)] = tuning.dead_zone;
			bar.trigger_threshold[static_cast<size_t>(pad.player)] = tuning.trigger_threshold;
		}
	light_guns(m_game->name, controls, bar.guns, bar.gun, bar.sights);
	bar.shake = vibration_on(m_settings, m_game->name);
	bar.hide_cursor = m_controls.hides_cursor();
}

/* As the menu does them: kept in odin.ini the same way */
BarState::Ask
MenuSession::do_bar(BarState &bar)
{
	using Ask = BarState::Ask;
	MenuState &state = m_menu_state;
	/* The games of the bar among those of the menu, by name */
	const auto sound_game = [&state](const SoundGame &game) -> SoundGame * {
		for (SoundGame &one : state.sound_games)
			if (one.name == game.name)
				return &one;
		return nullptr;
	};
	bool sound = false;

	for (const SoundGame &game : bar.sound_games)
		if (const SoundGame *const one = sound_game(game))
			sound = sound || game.played != one->played || game.level != one->level;
	if (sound || static_cast<const BarSettings &>(bar) != state || bar.custom_window != (state.custom_w != 0)) {
		const MenuState was = state;

		static_cast<BarSettings &>(state) = bar;
		/* A size chosen in the bar ends the one given by hand */
		if (!bar.custom_window)
			state.custom_w = state.custom_h = 0;
		for (const SoundGame &game : bar.sound_games)
			if (SoundGame *const one = sound_game(game)) {
				one->played = game.played;
				one->level = game.level;
			}
		apply(was);
		/* A mode that waits for the game to start again says so once the
		 * menu of the bar is closed */
		for (size_t g = 0; g < state.sound_games.size() && g < was.sound_games.size(); g++)
			if (state.sound_games[g].later && state.sound_games[g].played != was.sound_games[g].played)
				m_bar_note = "Reset the game to apply the sound mode";
	}
	/* The inputs of the game that runs */
	if (m_game) {
		if (bar.can_shake && bar.shake != vibration_on(m_settings, m_game->name))
			keep_vibration(m_game->name, bar.shake, true);
		if (bar.hide_cursor != m_controls.hides_cursor()) {
			keep_hide_cursor(m_game->name, bar.hide_cursor, true, m_controls.panel().takes_mouse());
			m_controls.follow_cursor();
		}
		for (const Controls::Plugged &pad : m_controls.plugged())
			if (pad.player == 0 || pad.player == 1) {
				const PadTuning was = PadTuning::of(m_settings, pad.guid, false);
				PadTuning now = was;

				now.dead_zone = bar.dead_zone[static_cast<size_t>(pad.player)];
				now.trigger_threshold = bar.trigger_threshold[static_cast<size_t>(pad.player)];
				if (now.dead_zone >= 0 && now.trigger_threshold >= 0)
					keep_tuning(pad.guid, now, was);
			}
	}

	const Ask ask = std::exchange(bar.ask, Ask::NONE);
	const unsigned player = bar.asked_player & 1;

	switch (ask) {
	case Ask::OPEN:
		m_dialog.ask_archive(m_display.window(), state.roms, ARCHIVE_ASKED);
		return Ask::NONE;
	case Ask::FULLSCREEN:
		m_display.toggle_fullscreen();
		return Ask::NONE;
	case Ask::PAD:
		if (m_game && bar.asked_pad >= 0 && static_cast<size_t>(bar.asked_pad) < bar.pads.size()) {
			const BarState::Pad &pad = bar.pads[static_cast<size_t>(bar.asked_pad)];

			want_pad(m_game->name, player, pad.guid, pad.name, pad.joystick, true, pad.id);
			/* In the place of the light gun */
			if (!bar.gun[player].empty())
				want_gun(m_game->name, player, {}, {}, true);
			keep_device(m_game->name, player, pad.joystick ? InputDevice::JOYSTICK : InputDevice::PAD, true);
		}
		return Ask::NONE;
	case Ask::KEYBOARD:
		if (m_game)
			keep_device(m_game->name, player, InputDevice::KEYBOARD, true);
		return Ask::NONE;
	case Ask::GUN:
		if (m_game && bar.asked_gun < static_cast<int>(bar.guns.size())) {
			const LightGun none{}, &gun = bar.asked_gun < 0 ? none : bar.guns[static_cast<size_t>(bar.asked_gun)];

			want_gun(m_game->name, player, gun.identity, gun.name, true);
		}
		return Ask::NONE;
	case Ask::INPUTS_DEFAULTS:
	case Ask::INPUTS_CLEAR:
		/* As the page does them, on the game that runs, for that player:
		 * the page shows them so from now on */
		if (m_game) {
			show_game(state.running);
			state.player = player;
			if (ask == Ask::INPUTS_DEFAULTS)
				change_inputs(true);
			else
				clear_inputs();
		}
		return Ask::NONE;
	case Ask::INPUTS_UNDO:
		if (m_undo_until && SDL_GetTicks() < m_undo_until)
			undo();
		return Ask::NONE;
	case Ask::HOTKEYS_DEFAULTS:
		m_hotkeys.set_button(SDL_GAMEPAD_BUTTON_GUIDE);
		m_hotkeys.reset();
		keep_hotkeys();
		return Ask::NONE;
	default:
		return ask;
	}
}

/* The folders of the games, of the BIOS and of the saves wait for the next
 * launch; the save states go to theirs now. A folder of their own by
 * default is no line in odin.ini. */
std::optional<FolderDialog::Answer>
MenuSession::take_folder()
{
	using Folder = MenuState::Folder;
	MenuState &state = m_menu_state;
	const std::optional<FolderDialog::Answer> answer = m_dialog.answer();

	m_menu_pictures = MENU_PICTURES;
	if (!answer || answer->asked == ARCHIVE_ASKED)
		return answer;
	state.no_picker = !answer->error.empty();
	if (state.no_picker)
		std::fprintf(stderr, "ERROR: no folder picker: %s\n", answer->error.c_str());
	if (answer->folder.empty())
		return std::nullopt;

	const std::string &folder = answer->folder;

	switch (static_cast<Folder>(answer->asked)) {
	case Folder::ROMS:
		state.roms = folder;
		keep("paths", "roms", folder);
		break;
	case Folder::BIOS:
		state.bios = folder;
		keep("paths", "bios", folder);
		break;
	case Folder::SAVES:
		state.saves = folder;
		keep("paths", "saves", folder == SaveFolder::root() ? std::nullopt : std::optional(folder));
		break;
	case Folder::LOG:
		state.log_folder = folder;
		keep("paths", "log", folder == odin_data_dir() ? std::nullopt : std::optional(folder));
		break;
	case Folder::STATES:
		state.states = m_states = folder;
		keep("paths", "states", folder == default_states() ? std::nullopt : std::optional(folder));
		break;
	case Folder::ARTWORK:
		state.artwork = folder;
		keep("paths", "artwork", folder == artwork_folder() ? std::nullopt : std::optional(folder));
		if (m_game) {
			m_display.set_sides(static_cast<Display::Sides>(state.sides),
			                    artwork_file(folder, m_game->name, "bezel", m_game->parent));
			m_controls.set_picture_hole(m_display.picture_hole());
		}
		break;
	default:
		break;
	}
	return std::nullopt;
}

/* The picker opened where the folder is now; that of the BIOS, else where
 * the games are */
void
MenuSession::ask_folder(MenuState::Folder folder)
{
	using Folder = MenuState::Folder;
	MenuState &state = m_menu_state;

	state.no_picker = false;
	m_dialog.ask(m_display.window(), folder == Folder::ROMS ? state.roms :
	             folder == Folder::BIOS ? (state.bios.empty() ? state.roms : state.bios) :
	             folder == Folder::SAVES ? state.saves :
	             folder == Folder::ARTWORK ? state.artwork :
	             folder == Folder::LOG ? (state.log_folder.empty() ? odin_data_dir() : state.log_folder) :
	                                     state.states, static_cast<int>(folder));
	m_menu_pictures = MENU_PICTURES;
}

void
MenuSession::keep(const char *key, const std::optional<std::string> &value)
{
	keep("display", key, value);
}

void
MenuSession::keep_window()
{
	const MenuState &state = m_menu_state;

	m_custom_waits = false;
	if (state.custom_w)
		keep("window", std::to_string(state.custom_w) + "x" + std::to_string(state.custom_h));
	else
		keep("window", state.window == 1 ? std::nullopt : std::optional(std::to_string(state.window)));
}

void
MenuSession::follow_hand_size()
{
	MenuState &state = m_menu_state;
	unsigned w = 0, h = 0;

	if (state.window_given || !m_display.hand_size(w, h) || (w == state.custom_w && h == state.custom_h))
		return;
	state.custom_w = w;
	state.custom_h = h;
	m_custom_waits = true;
}

void
MenuSession::keep_custom()
{
	follow_hand_size();
	if (m_custom_waits)
		keep_window();
}

/* In odin.ini; with none, for the time Odin runs */
void
MenuSession::keep(const std::string &section, const std::string &key, const std::optional<std::string> &value)
{
	if (m_settings_file.empty())
		m_settings.set(section, key, value);
	else
		m_settings.change(m_settings_file, section, key, value);
}

} /* namespace odin */
