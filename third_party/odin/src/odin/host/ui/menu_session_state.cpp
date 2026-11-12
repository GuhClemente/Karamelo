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

#include "odin/host/app/user_files.hpp"
#include "odin/host/ui/gun_drawing.hpp"
#include "odin/host/input/seats.hpp"

namespace odin {

namespace {

/* The make and model of the gamepad of the player shown in the menu; empty
 * when the player has none */
std::string
pad_shown(const MenuState &state)
{
	for (const MenuState::Plugged &pad : state.plugged)
		if (pad.player == static_cast<int>(state.player))
			return pad.guid;
	return {};
}

/* The device a player has among those shown (0: none) */
SDL_JoystickID
seated_id(const std::vector<MenuState::Plugged> &devices, unsigned player)
{
	for (const MenuState::Plugged &one : devices)
		if (one.player == static_cast<int>(player & 1))
			return one.id;
	return 0;
}

/* A setting as odin.ini keeps it: none when it is the one by default */
std::optional<std::string>
kept_number(unsigned value, unsigned standard)
{
	return value == standard ? std::nullopt : std::optional<std::string>(std::to_string(value));
}

std::optional<std::string>
kept_on_off(bool value, bool standard)
{
	return value == standard ? std::nullopt : std::optional<std::string>(value ? "on" : "off");
}

} /* namespace */

std::string
MenuSession::game_shown() const
{
	const MenuState &state = m_menu_state;

	return state.shown < state.games.size() ? state.games[state.shown].first :
	       m_running ? m_running->game_name() : std::string();
}

/* Another game in the page Input: a panel of its own, made from the list
 * and the settings, for its inputs only */
void
MenuSession::show_game(size_t game)
{
	MenuState &state = m_menu_state;
	const std::optional<Game> other = game < state.games.size() && game != state.running ?
	                                        m_games.describe(state.games[game].first) : std::nullopt;

	state.shown = other ? game : state.running;
	state.catching.reset();
	state.catch_next.clear();
	m_other.reset();
	m_devices_of_other = other.has_value();
	if (!other) {
		for (unsigned player = 0; player < 2; player++)
			state.input_device[player] = m_controls.device(player);
		return;
	}
	m_other_controls = other->controls;
	m_other_outputs = other->outputs;
	m_other_shakes = other->shakes();
	m_other_forces = other->drive_first_output != 0 || other->wheel_motor;
	m_other_cabinets = other->cabinets;
	m_other_shakers = { other->shaker(0), other->shaker(1) };
	m_other_experimental = { other->shaker_experimental(0), other->shaker_experimental(1) };
	m_other_link = other->link;
	m_other_title = other->title;
	m_other.emplace(m_other_controls, m_settings, other->name);
	m_other->set_wheel_rotation(wheel_rotation_of(m_settings, other->name));
}

SDL_JoystickID
MenuSession::joystick_shown(unsigned player) const
{
	if (!m_other)
		return m_controls.joystick_id(player);
	return seated_id(m_menu_state.joysticks, player);
}

HostState
MenuSession::host_shown() const
{
	HostState host = m_controls.host_state();

	if (m_other)
		for (unsigned player = 0; player < 2; player++) {
			host.pads[player] = m_controls.gamepad_state(seated_id(m_menu_state.plugged, player));
			host.joys[player] = m_controls.joystick_state(joystick_shown(player));
		}
	return host;
}

/* The display as it is now: F11 may have changed it */
void
MenuSession::fill_display(MenuState &state)
{
	state.fullscreen = m_display.fullscreen();
	{
		int w = 0, h = 0;

		SDL_GetWindowSize(m_display.window(), &w, &h);
		state.window_w = static_cast<unsigned>(std::max(w, 0));
		state.window_h = static_cast<unsigned>(std::max(h, 0));
	}
	follow_hand_size();
	m_display.display_size(state.display_w, state.display_h);
	state.refresh = m_display.refresh();
	state.vsync_off = m_options.unthrottled ? "off at unlimited speed" : nullptr;
	state.refresh_short = state.refresh > 0.0f && state.refresh < Display::LEAST_REFRESH;
}

/* What the controls of the game shown do now, the gamepads and the
 * buttons the pills show, Undo, the hold of Skip */
void
MenuSession::fill_inputs(MenuState &state)
{
	state.panel = &panel_shown();
	state.touched.clear();
	state.now.clear();
	{
		const HostState host = host_shown();

		for (size_t c = 0; c < state.panel->controls().size(); c++) {
			state.now.push_back(state.panel->now(host, c));
			state.touched.push_back(state.now.back().on);
		}
		/* In the order of MenuState::TOUCHED_COIN1... */
		for (const Role role : { Role::COIN1, Role::COIN2, Role::TEST })
			state.touched.push_back(state.panel->touched(host, role));
		state.keys_held = m_keys.held();
	}
	std::array<std::string, 2> pads;

	for (unsigned player = 0; player < 2; player++) {
		pads[player] = m_controls.pad_name(player);
		state.pad_types[player] = m_controls.pad_type(player);
		state.pad_states[player] = m_controls.pad_state(player);
	}
	/* The pills of the pages and the games: the first button of each, as
	 * the pad of player 1 (else 2) names it */
	{
		using Nav = MenuNav;
		const int kind = !pads[0].empty() ? state.pad_types[0] :
		                 !pads[1].empty() ? state.pad_types[1] : SDL_GAMEPAD_TYPE_UNKNOWN;
		const Nav::Action pills[] = { Nav::PAGE_BEFORE, Nav::PAGE_AFTER, Nav::GAME_BEFORE, Nav::GAME_AFTER,
		                              Nav::PRESS,       Nav::CLEAR,      Nav::RESET };
		static_assert(std::size(pills) == MenuState::PILLS);

		for (size_t i = 0; i < state.pills.size(); i++) {
			const std::vector<HostInput> &inputs = m_nav.inputs(pills[i]);
			const auto pad = std::find_if(inputs.begin(), inputs.end(),
			                              [](const HostInput &in) { return in.kind == HostInput::Kind::PAD; });

			state.pills[i] = pad == inputs.end() ? std::string() : Menu::input_name(pad->pad, kind);
		}
	}
	state.undo = m_undo_until && SDL_GetTicks() < m_undo_until;
	/* The button that presses held long enough while Start mapping: Skip */
	state.skip_hold = 0.0f;
	if (m_held && !m_held->skipped) {
		const uint64_t held = SDL_GetTicks() - m_held->since;

		if (!state.mapping) {
			m_held.reset();
		} else if (held >= SKIP_HOLD_MS) {
			state.mapping_since = 0;
			m_held->skipped = true;
		} else {
			state.skip_hold = static_cast<float>(held) / SKIP_HOLD_MS;
		}
	}
}

/* What the game shown has: what shakes, its wheel, its cabinets, how it
 * links cabinets, its outputs */
void
MenuSession::fill_game_shown(MenuState &state)
{
	/* The pad shakes as the cabinet of the game shown, unless
	 * [players.<game>] says off (the last line says) */
	state.can_shake = m_other ? m_other_shakes : m_game && m_game->shakes();
	state.shake_from = m_other ? m_other_shakers[state.player & 1] :
	                   m_game ? m_game->shaker(state.player & 1) : std::string();
	state.shake_experimental = m_other ? m_other_experimental[state.player & 1] :
	                           m_game && m_game->shaker_experimental(state.player & 1);
	state.shake = vibration_on(m_settings, game_shown());
	state.vibration_force = vibration_force_of(m_settings, game_shown());
	state.hide_cursor = hide_cursor_on(m_settings, game_shown(), panel_shown().takes_mouse());
	state.sinden_border = sinden_border_of(m_settings, game_shown());
	/* What the player shown may turn over, and what is turned over */
	const std::vector<std::string> inverted = inverted_of(m_settings, game_shown());

	state.invert_names.clear();
	state.invert_ids.clear();
	state.inverted.clear();
	for (const GameControl &control : panel_shown().controls())
		if (control.invertible && control.player == (state.player & 1)) {
			state.invert_names.push_back("Invert " + control.name);
			state.invert_ids.push_back(control.id);
			state.inverted.push_back(std::find(inverted.begin(), inverted.end(), control.id) != inverted.end());
		}
	/* The wheel the drive board turns, and how hard */
	state.can_force = m_other ? m_other_forces : m_game && (m_game->drive_first_output || m_game->wheel_motor);
	state.steers = steers_of(panel_shown().controls());
	state.wheel_force = wheel_force_of(m_settings, game_shown());
	state.wheel_rotation = wheel_rotation_of(m_settings, game_shown());
	/* Its cabinets, and the one chosen; the game that runs plays the one it
	 * started with */
	static const std::vector<Game::Cabinet> NONE;
	const std::vector<Game::Cabinet> &cabinets = m_other ? m_other_cabinets : m_game ? m_game->cabinets : NONE;
	const std::string chosen = cabinet_of(m_settings, game_shown());

	state.cabinet_ids = { "standard" };
	state.cabinet_names = { "Standard" };
	state.cabinet = 0;
	for (const Game::Cabinet &cabinet : cabinets) {
		if (cabinet.id == chosen)
			state.cabinet = static_cast<int>(state.cabinet_ids.size());
		state.cabinet_ids.push_back(cabinet.id);
		state.cabinet_names.push_back(cabinet.name);
	}
	state.cabinet_later = !m_other && m_game && m_game->cabinet != state.cabinet_ids[static_cast<size_t>(state.cabinet)];
	/* How the game shown links cabinets */
	const Game::Link shown_link = m_other ? m_other_link : m_game ? m_game->link : Game::Link{};

	state.link_shown_title = m_other ? m_other_title : m_game ? m_game->title : std::string();
	state.link_shown_cabinets = shown_link.cabinets;
	state.link_shown_seat = shown_link.seat_in == Game::Link::In::NONE ? MenuState::SEAT_NONE :
	                        shown_link.seat_letter ? MenuState::SEAT_LETTER : MenuState::SEAT_NUMBER;
	/* The outputs of the game shown, lit as the board left them; the light
	 * gun a vibration goes to */
	state.outputs.clear();
	for (const GameOutput &output : m_other || !m_game ? m_other_outputs : m_game->outputs)
		state.outputs.push_back({ output.name, output.role, !m_other && m_running && output.on(m_running->io_outputs()),
		                          output.id,
		                          output_route_of(m_settings, game_shown(), output.id, false).value_or(OutputRoute{}) });
	if (m_outputs) {
		m_outputs->welcome();
		state.programs = m_outputs->clients();
	}
}

/* The link of the game that runs, as it is now: the network board of a
 * Hikaru, the cable of a Gaelco board */
void
MenuSession::fill_link(MenuState &state)
{
	state.link_running = m_running != nullptr;
	if (m_running) {
		const Board::LinkStatus link = m_running->link_status();

		state.link_board_now = link.board;
		state.link_linked = link.linked;
		state.link_failed = link.failed;
		state.link_previous = link.previous;
		state.link_next_plugged = link.next;
	} else {
		state.link_board_now = state.link_linked = state.link_failed = false;
		state.link_previous = state.link_next_plugged = false;
	}
}

/* The gamepads, the joysticks and the light guns, with their players */
void
MenuSession::fill_devices(MenuState &state)
{
	/* The gamepads and the joysticks plugged in and their players; those of
	 * a game that does not run as its settings would seat them */
	const auto list = [this, &state](std::vector<MenuState::Plugged> &out, const std::vector<Controls::Plugged> &plugged,
	                                 const char *keys, bool pads) {
		std::array<SDL_JoystickID, 2> seats{};

		if (m_other) {
			std::array<std::string, 2> wanted, paths;
			std::vector<OpenPad> open;

			for (const auto &[key, value] : m_settings.section("players." + game_shown())) {
				for (unsigned player = 0; player < 2; player++) {
					if (key == keys + std::to_string(player + 1))
						wanted[player] = identity_of(value);
					else if (key == keys + std::to_string(player + 1) + "_path")
						paths[player] = device_path(value);
				}
			}
			std::vector<int> kinds;

			for (const Controls::Plugged &one : plugged)
				if (pads || joystick_fits(one.kind, state.steers)) {
					open.push_back({ one.id, one.guid, one.path });
					kinds.push_back(one.kind);
				}
			if (!pads && state.steers)
				wheels_first(open, kinds);
			seats = seat_pads(open, wanted, {}, {}, paths);
		}
		out.clear();
		for (const Controls::Plugged &one : plugged)
			out.push_back({ one.name, one.guid, one.kind,
			                !m_other ? one.player : player_seated(one.id, seats),
			                one.connection, one.id });
	};

	list(state.plugged, m_controls.plugged(), "p", true);
	list(state.joysticks, m_controls.joysticks(), "joy", false);

	/* The light guns, of the game shown */
	if (Pointers *const pointers = m_controls.pointers())
		pointers->poll();
	light_guns(game_shown(), panel_shown().controls(), state.guns, state.gun, state.sights);

	/* A game that does not run shows the device each player would play it
	 * with: as Controls::device(), from its settings and what they seat */
	if (m_devices_of_other) {
		m_devices_of_other = false;
		for (unsigned player = 0; player < 2; player++) {
			const auto seated = [player](const std::vector<MenuState::Plugged> &devices) {
				return std::any_of(devices.begin(), devices.end(),
				                   [player](const MenuState::Plugged &one) { return one.player == static_cast<int>(player); });
			};
			const std::optional<InputDevice> chosen = device_chosen(m_settings, game_shown(), player);

			state.input_device[player] = state.gun_of(player) ? InputDevice::KEYBOARD :
			                             device_of(chosen, state.steers, seated(state.plugged), seated(state.joysticks));
		}
	}
	/* A player without the device shown is shown on the keyboard */
	for (unsigned player = 0; player < 2; player++) {
		const std::vector<MenuState::Plugged> &shown =
			state.input_device[player] == MenuState::Device::PAD ? state.plugged : state.joysticks;

		if (state.input_device[player] == MenuState::Device::KEYBOARD ||
		    std::any_of(shown.begin(), shown.end(),
		                [player](const MenuState::Plugged &one) { return one.player == static_cast<int>(player); }))
			continue;
		state.input_device[player] = MenuState::Device::KEYBOARD;
		if (state.player == player)
			state.stop_catching();
	}
	state.tuning = PadTuning::of(m_settings, pad_shown(state), false);
	/* What moves says so as it is drawn */
	state.moving = false;

	/* What the light gun of each player presses now, lit on its drawing */
	const HostState host = host_shown();

	for (unsigned player = 0; player < 2; player++) {
		const LightGun *const gun = state.gun_of(player);

		state.gun_states[player] = {};
		if (!gun)
			continue;

		const auto inputs = gun_inputs(gun_kind(gun->name, gun->identity), gun->name, gun->identity);

		for (size_t b = 0; b < GUN_BUTTONS; b++)
			state.gun_states[player].buttons[b] = inputs[b] && panel_shown().held(host, *inputs[b], player);
	}
}

/* What the menu asked as it was drawn, done */
void
MenuSession::do_asked(MenuState &state)
{
	if (state.hotkeys_default) {
		m_hotkeys.set_button(SDL_GAMEPAD_BUTTON_GUIDE);
		m_hotkeys.reset();
		keep_hotkeys();
		state.hotkeys_default = false;
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.hotkey_button_asked) {
		m_hotkeys.set_button(*state.hotkey_button_asked);
		keep_hotkeys();
		state.hotkey_button_asked.reset();
		state.hotkey_moved.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.choose != MenuState::Folder::NONE) {
		ask_folder(state.choose);
		state.choose = MenuState::Folder::NONE;
	}

	if (state.show) {
		show_game(*state.show);
		state.show.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	/* The light gun a player wants for the game shown: kept, and taken at
	 * once by the game that runs */
	if (state.want_gun) {
		const auto &[player, gun] = *state.want_gun;

		want_gun(game_shown(), player, gun.identity, gun.name, !m_other);
		/* The gun is the player's device, with the keyboard; none, the
		 * device they had */
		if (!gun.identity.empty())
			state.input_device[player] = MenuState::Device::KEYBOARD;
		else if (!state.device_picked && m_other)
			m_devices_of_other = true;
		else if (!state.device_picked)
			state.input_device[player] = m_controls.device(player);
		state.want_gun.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	/* The gamepad a player wants for the game: [players.<game>] p1 or p2 */
	if (state.want) {
		want_pad(game_shown(), state.player, state.want->guid, state.want->name, false, !m_other, state.want->id);
		state.want.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	/* The joystick a player wants for the game: [players.<game>] joy1 or
	 * joy2 */
	if (state.want_joystick) {
		want_pad(game_shown(), state.player, state.want_joystick->guid, state.want_joystick->name, true, !m_other,
		         state.want_joystick->id);
		state.want_joystick.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.device_picked) {
		keep_device(game_shown(), state.device_picked->first, state.device_picked->second, !m_other);
		state.device_picked.reset();
	}
	if (state.ask == MenuState::Ask::DEFAULTS) {
		change_inputs(true);
	} else if (state.ask == MenuState::Ask::CLEAR) {
		clear_inputs();
	} else if (state.ask == MenuState::Ask::UNDO) {
		undo();
	} else if (state.ask == MenuState::Ask::BACK) {
		m_leaving = true;
	} else if ((state.ask == MenuState::Ask::PART_DEFAULTS || state.ask == MenuState::Ask::PART_CLEAR) &&
	           state.picked) {
		change_part(*state.picked, state.ask == MenuState::Ask::PART_DEFAULTS);
	}
	if (state.ask != MenuState::Ask::NONE)
		m_menu_pictures = MENU_PICTURES;
	state.ask = MenuState::Ask::NONE;
}

/* The menu drawn once from what is so now, then what it changed done */
void
MenuSession::draw_menu()
{
	MenuState &state = m_menu_state;

	state.choosing = m_dialog.open();
	fill_display(state);
	fill_inputs(state);
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++)
		state.hotkeys[a] = m_hotkeys.of(static_cast<Hotkeys::Action>(a));
	state.hotkey_button = m_hotkeys.button();
	if (m_audio || m_listing)
		state.devices = AudioOut::devices();
	fill_game_shown(state);
	fill_link(state);
	fill_devices(state);
	const MenuState was = state;

	m_display.draw_menu(state);
	state.nav = {};
	apply(was);

	do_asked(state);
	m_controls.settle();
}

/* A setting changed in the menu is done now, and kept in odin.ini when it
 * is not the one by default; the pace waits for the next launch */
void
MenuSession::apply(const MenuState &was)
{
	apply_display(was);
	apply_sound(was);
	apply_game(was);
	apply_link(was);
}

void
MenuSession::apply_display(const MenuState &was)
{
	const MenuState &now = m_menu_state;

	if (now.scale != was.scale) {
		m_display.set_scale(now.scale);
		keep("scale", kept_number(now.scale, 1));
	}
	if (now.filter != was.filter) {
		m_display.set_filter(now.filter);
		keep("filter", now.filter.empty() ? std::nullopt : std::optional<std::string>(now.filter));
	}
	if (now.anisotropy != was.anisotropy) {
		m_display.set_anisotropy(now.anisotropy);
		keep("anisotropy", kept_number(now.anisotropy, 1));
	}
	if (now.upscale_mode != was.upscale_mode && now.upscale_mode < std::size(UPSCALE_MODES)) {
		m_display.set_upscale_mode(static_cast<DisplayUpscaleMode>(now.upscale_mode));
		keep("upscale_mode", now.upscale_mode == 0 ? std::nullopt :
		                     std::optional<std::string>(UPSCALE_MODES[now.upscale_mode]));
	}
	if (now.fullscreen != was.fullscreen) {
		m_display.set_fullscreen(now.fullscreen);
		keep("fullscreen", kept_on_off(now.fullscreen, false));
	}
	if (now.window != was.window || now.custom_w != was.custom_w || now.custom_h != was.custom_h) {
		/* Custom keeps the window where it is, of the size it has */
		if (!now.custom_w)
			m_display.resize(now.window);
		else if (now.custom_w != now.window_w || now.custom_h != now.window_h)
			m_display.resize_to(now.custom_w, now.custom_h);
		keep_window();
	}
	if (now.sides != was.sides) {
		const auto sides = static_cast<Display::Sides>(now.sides);

		m_display.set_sides(sides, m_game ? artwork_file(now.artwork, m_game->name, "bezel", m_game->parent) :
		                                    std::string());
		m_controls.set_picture_hole(m_display.picture_hole());
		keep("bezel", sides == SIDES ? std::nullopt : std::optional<std::string>(SIDES_NAMES[now.sides]));
	}
	if (now.vsync != was.vsync) {
		const auto vsync = static_cast<Display::Vsync>(now.vsync);

		m_display.set_vsync(m_options.unthrottled ? Display::Vsync::OFF : vsync);
		keep("vsync", vsync == VSYNC ? std::nullopt : std::optional<std::string>(VSYNCS[now.vsync]));
	}
	if (now.gpu_chosen != was.gpu_chosen)
		keep("gpu", now.gpu_chosen.empty() ? std::nullopt : std::optional<std::string>(now.gpu_chosen));
	if (now.api != was.api)
		keep("api", static_cast<DisplayApi>(now.api) == API ? std::nullopt : std::optional<std::string>(APIS[now.api]));
	if (now.unthrottled != was.unthrottled)
		keep("unthrottled", kept_on_off(now.unthrottled, false));
	if (now.show_speed != was.show_speed) {
		m_display.set_show_speed(now.show_speed);
		keep("show_speed", kept_on_off(now.show_speed, true));
	}
	if (now.classic != was.classic && !m_settings_file.empty())
		m_settings.change(m_settings_file, "menu", "classic", kept_on_off(now.classic, false));
	if (now.theme != was.theme)
		keep("menu", "theme", now.theme ? std::optional<std::string>(THEMES[now.theme].key) : std::nullopt);
	if (now.verbose != was.verbose)
		keep("log", "verbose", kept_on_off(now.verbose, false));
}

/* The sound: at once for the game that runs */
void
MenuSession::apply_sound(const MenuState &was)
{
	const MenuState &now = m_menu_state;

	if (now.device != was.device) {
		if (m_audio)
			m_audio->change(m_audio->mode(), now.device);
		keep("sound", "device", now.device.empty() ? std::nullopt : std::optional(now.device));
	}
	if (now.volume != was.volume) {
		if (m_audio)
			m_audio->set_volume(static_cast<float>(now.volume) / 100.0f);
		keep("sound", "volume", kept_number(now.volume, 100));
	}
	if (now.background != was.background) {
		if (m_audio)
			m_audio->set_heard(m_focused || now.background);
		keep("sound", "background", kept_on_off(now.background, true));
	}
	for (size_t g = 0; g < now.sound_games.size() && g < was.sound_games.size(); g++) {
		const SoundGame &game = now.sound_games[g], &before = was.sound_games[g];
		const std::string section = "sound." + game.name;
		const bool runs = m_game && game.name == m_game->family() && m_audio;

		if (game.start != before.start && game.start >= 0)
			keep(section, "start_volume",
			     game.start == START_VOLUME_STANDARD ? std::nullopt : std::optional<std::string>(START_VOLUMES[game.start]));
		if (!game.mixed)
			continue;
		if (game.played != before.played) {
			/* SDL keeps the device open on as many channels as it had:
			 * four or two again take the game started again */
			const bool four = mode_of(game.played) == AudioOut::Mode::QUAD;
			const bool later = runs && four != (m_audio->mode() == AudioOut::Mode::QUAD);

			m_menu_state.sound_games[g].later = later;
			if (runs && !later)
				m_audio->change(mode_of(game.played), now.device);
			keep(section, "mode", game.played == game.cabinet_played || game.played < 0 ? std::nullopt :
			                      std::optional<std::string>(MODE_NAMES[game.played]));
		}
		if (game.level != before.level) {
			char db[16];

			std::snprintf(db, sizeof(db), "%g", static_cast<double>(game.level));
			if (runs)
				m_audio->set_option_level(game.level);
			keep(section, game.woofer ? "woofer_level" : "option_level",
			     game.level == game.standard ? std::nullopt : std::optional<std::string>(db));
		}
		if (game.cutoff != before.cutoff && game.woofer) {
			const char *const cutoff = WOOFER_CUTOFFS[game.cutoff];

			if (runs)
				m_audio->set_woofer(*woofer_hz_named(cutoff));
			keep(section, "woofer_cutoff",
			     game.cutoff == game.cutoff_standard ? std::nullopt : std::optional<std::string>(cutoff));
		}
	}
}

void
MenuSession::apply_game(const MenuState &was)
{
	const MenuState &now = m_menu_state;

	/* The vibration of the game shown: at once for the game that runs */
	if (now.shake != was.shake && now.can_shake)
		keep_vibration(game_shown(), now.shake, !m_other && m_game);
	if (now.vibration_force != was.vibration_force && now.can_shake)
		keep_vibration_force(game_shown(), now.vibration_force, !m_other && m_game);
	/* Whether the mouse plays: the pointer follows as the menu closes */
	if (now.hide_cursor != was.hide_cursor)
		keep_hide_cursor(game_shown(), now.hide_cursor, !m_other && m_game, panel_shown().takes_mouse());
	/* Its white border for a light gun, at once too */
	if (now.sinden_border != was.sinden_border && now.sights[0])
		keep_sinden_border(game_shown(), now.sinden_border, !m_other && m_game);
	/* Its controls turned over, at once too */
	if (now.inverted != was.inverted)
		keep_inverted(game_shown(), now.invert_ids, now.inverted);
	/* The force of its wheel, at once too */
	if (now.wheel_force != was.wheel_force && now.can_force)
		keep_wheel_force(game_shown(), now.wheel_force, !m_other && m_game);
	/* How far a wheel turns for its steering, at once too */
	if (now.wheel_rotation != was.wheel_rotation && now.steers)
		keep_wheel_rotation(game_shown(), now.wheel_rotation, !m_other && m_game);
	/* Its cabinet: the game that runs takes it when it is reset */
	if (now.cabinet != was.cabinet && now.cabinet >= 0 && static_cast<size_t>(now.cabinet) < now.cabinet_ids.size())
		keep_cabinet(game_shown(), now.cabinet_ids[static_cast<size_t>(now.cabinet)]);
	/* The dead zone and the trigger threshold of the gamepad shown */
	if (const std::string guid = pad_shown(now); !guid.empty())
		keep_tuning(guid, now.tuning, was.tuning);
}

void
MenuSession::apply_link(const MenuState &was)
{
	const MenuState &now = m_menu_state;

	/* The network board: for the games that start from now */
	if (now.link_board != was.link_board)
		keep("network", "board", now.link_board == 0 ? std::nullopt :
		     std::optional<std::string>(NetLinkSettings::board_name(static_cast<NetLinkSettings::Board>(now.link_board))));
	if (now.link_port != was.link_port)
		keep("network", "port", kept_number(now.link_port, NetCable::PORT));
	if (now.link_next != was.link_next)
		keep("network", "next", now.link_next.empty() ? std::nullopt : std::optional<std::string>(now.link_next));
	if (now.link_cabinet != was.link_cabinet)
		keep("network", "cabinet", kept_number(now.link_cabinet, 1));
	/* The programs of lamps: the port opened or closed now, for the game
	 * that runs */
	if (now.network != was.network) {
		if (now.network && m_game)
			m_outputs = OutputsNetwork::open(m_game->name, m_game->outputs);
		else
			m_outputs.reset();
		m_menu_state.network_failed = now.network && m_game && !m_outputs;
		m_menu_state.programs = 0;
		keep("outputs", "network", kept_on_off(now.network, false));
	}
	/* Where a vibration goes: kept, and taken at once by the game that
	 * runs */
	for (size_t o = 0; o < now.outputs.size() && o < was.outputs.size(); o++)
		if (now.outputs[o].route != was.outputs[o].route) {
			keep("outputs." + game_shown(), now.outputs[o].id, output_route_name(now.outputs[o].route));
			if (!m_other)
				m_controls.route_outputs();
		}
}

/* Clear mapping: all the inputs of the player shown, none; Undo offered a
 * while */
void
MenuSession::clear_inputs()
{
	m_cleared_game = game_shown();
	m_cleared = m_settings.section("controls." + m_cleared_game);
	change_inputs(false);
	m_undo_until = SDL_GetTicks() + UNDO_MS;
}

/* [players.<game>] p1 or p2 (none: the first free gamepad), joy1 or joy2;
 * p1_path... the very one picked when another of its make is plugged in,
 * which the other player lets go of (none: any of that make) */
void
MenuSession::want_pad(const std::string &game, unsigned player, const std::string &guid, const std::string &name,
                      bool joystick, bool runs, SDL_JoystickID picked)
{
	const std::string section = "players." + game;
	const std::string own = std::string(joystick ? "joy" : "p") + (player ? "2" : "1");
	const std::string other = std::string(joystick ? "joy" : "p") + (player ? "1" : "2");
	const std::vector<Controls::Plugged> plugged = joystick ? m_controls.joysticks() : m_controls.plugged();
	const auto one = std::find_if(plugged.begin(), plugged.end(),
	                              [picked](const Controls::Plugged &device) { return picked && device.id == picked; });
	const auto alike = std::count_if(plugged.begin(), plugged.end(),
	                                 [&guid](const Controls::Plugged &device) { return device.guid == guid; });
	const std::string taken = one != plugged.end() ? one->path : std::string();
	const std::string path = !guid.empty() && alike > 1 ? taken : std::string();

	if (joystick) {
		if (runs)
			m_controls.want_joystick(player, guid, picked);
		keep(section, own, guid + " " + name);
	} else {
		if (runs)
			m_controls.want(player, guid, picked);
		keep(section, own, guid.empty() ? std::nullopt : std::optional(guid + " " + name));
	}
	keep(section, own + "_path", path.empty() ? std::nullopt : std::optional(path));
	for (const auto &[key, value] : m_settings.section(section))
		if (key == other + "_path" && same_path(device_path(value), taken))
			keep(section, key, std::nullopt);
}

/* [players.<game>] gun1 or gun2 (none: no light gun) */
void
MenuSession::want_gun(const std::string &game, unsigned player, const std::string &identity, const std::string &name,
                      bool runs)
{
	keep("players." + game, player ? "gun2" : "gun1",
	     identity.empty() ? std::nullopt : std::optional<std::string>(identity + " " + name));
	if (runs)
		m_controls.want_gun(player, identity);
}

/* [players.<game>] device1 or device2: the device a player picked, which
 * alone shakes */
void
MenuSession::keep_device(const std::string &game, unsigned player, InputDevice device, bool runs)
{
	if (runs)
		m_controls.choose_device(player, device);
	keep("players." + game, player ? "device2" : "device1", std::string(DEVICE_NAMES[static_cast<size_t>(device)]));
}

/* [players.<game>] vibration: a line only when off */
void
MenuSession::keep_vibration(const std::string &game, bool on, bool runs)
{
	if (runs)
		m_controls.feel_game(*m_game, on);
	keep("players." + game, "vibration", on ? std::nullopt : std::optional<std::string>("off"));
}

/* [players.<game>] cabinet: a line only for another than Standard */
void
MenuSession::keep_cabinet(const std::string &game, const std::string &id)
{
	keep("players." + game, "cabinet", id == "standard" ? std::nullopt : std::optional<std::string>(id));
}

/* [players.<game>] hide_cursor: a line only when not as by default
 * (`standard`, ControlPanel::takes_mouse); light_gun_mode, its name before,
 * goes */
void
MenuSession::keep_hide_cursor(const std::string &game, bool on, bool runs, bool standard)
{
	if (runs)
		m_controls.set_hide_cursor(on == standard ? std::nullopt : std::optional(on));
	keep("players." + game, "hide_cursor", on == standard ? std::nullopt : std::optional<std::string>(on ? "on" : "off"));
	keep("players." + game, "light_gun_mode", std::nullopt);
}

/* [players.<game>] invert: the controls of the player shown turned over or
 * not, those of the other player as they were; the panels made again, as
 * after a binding */
void
MenuSession::keep_inverted(const std::string &game, const std::vector<std::string> &ids, const std::vector<uint8_t> &on)
{
	std::vector<std::string> kept = inverted_of(m_settings, game);
	std::string line;

	std::erase_if(kept, [&ids](const std::string &id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); });
	for (size_t c = 0; c < ids.size(); c++)
		if (on[c])
			kept.push_back(ids[c]);
	for (const std::string &id : kept)
		line += (line.empty() ? "" : ", ") + id;
	keep("players." + game, "invert", line.empty() ? std::nullopt : std::optional<std::string>(line));
	rebind_panels();
}

/* [players.<game>] sinden_border, sinden_border_style and
 * sinden_border_size: a line only for another than off, game and
 * SINDEN_BORDER_SIZE */
void
MenuSession::keep_sinden_border(const std::string &game, const SindenBorder &border, bool runs)
{
	if (runs)
		m_display.set_sinden_border(border);
	keep("players." + game, "sinden_border", border.on ? std::optional<std::string>("on") : std::nullopt);
	keep("players." + game, "sinden_border_style",
	     border.window ? std::optional<std::string>(SINDEN_BORDER_STYLES[1]) : std::nullopt);
	keep("players." + game, "sinden_border_size",
	     border.size == SINDEN_BORDER_SIZE ? std::nullopt : std::optional<std::string>(std::to_string(border.size)));
}

/* [players.<game>] vibration_force: a line only when another than 100 % */
void
MenuSession::keep_vibration_force(const std::string &game, int percent, bool runs)
{
	if (runs)
		m_controls.set_vibration_force(percent);
	keep("players." + game, "vibration_force",
	     percent == VIBRATION_FORCE ? std::nullopt : std::optional<std::string>(std::to_string(percent)));
}

/* [players.<game>] wheel_force: a line only when another than 100 % */
void
MenuSession::keep_wheel_force(const std::string &game, int percent, bool runs)
{
	if (runs)
		m_controls.set_wheel_force(percent);
	keep("players." + game, "wheel_force",
	     percent == WHEEL_FORCE ? std::nullopt : std::optional<std::string>(std::to_string(percent)));
}

/* [players.<game>] wheel_rotation: a line only when another than WHEEL_ROTATION */
void
MenuSession::keep_wheel_rotation(const std::string &game, int degrees, bool runs)
{
	if (runs)
		m_controls.set_wheel_rotation(degrees);
	else if (m_other)
		m_other->set_wheel_rotation(degrees);
	keep("players." + game, "wheel_rotation",
	     degrees == WHEEL_ROTATION ? std::nullopt : std::optional<std::string>(std::to_string(degrees)));
}

/* The dead zone and the trigger threshold of a make and model of gamepad:
 * a line of their own where they differ from [gamepads] */
void
MenuSession::keep_tuning(const std::string &guid, const PadTuning &now, const PadTuning &was)
{
	if (now.dead_zone == was.dead_zone && now.trigger_threshold == was.trigger_threshold)
		return;

	const PadTuning all = PadTuning::of(m_settings, {}, false);
	const std::string section = "gamepad." + guid;
	const auto line = [](int percent, int standard) {
		return percent == standard ? std::nullopt : std::optional(std::to_string(percent));
	};

	if (now.dead_zone != was.dead_zone)
		keep(section, "dead_zone", line(now.dead_zone, all.dead_zone));
	if (now.trigger_threshold != was.trigger_threshold)
		keep(section, "trigger_threshold", line(now.trigger_threshold, all.trigger_threshold));
	m_controls.retune();
}

} /* namespace odin */
