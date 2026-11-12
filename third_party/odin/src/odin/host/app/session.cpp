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

#include "odin/host/app/session.hpp"

#include <cstdio>
#include <filesystem>

namespace odin {

namespace {

/* A frame of the cabinet, 60 a second */
constexpr std::chrono::nanoseconds FRAME(1000000000 / 60);

} /* namespace */

/* F2 and F4: <game>.odinstate in the folder of the save states */
std::string
Session::state_path() const
{
	const std::string &states = m_menus.states();

	return (states.empty() ? "" : states + "/") + m_board.game_name() + ".odinstate";
}

/* A state of the game saved there: the bar offers to load it */
bool
Session::state_saved() const
{
	std::error_code error;

	return std::filesystem::exists(state_path(), error);
}

void
Session::load_or_save_state(bool load)
{
	const std::string path = state_path();
	std::error_code error;

	/* None saved yet: the game goes on */
	if (load && !std::filesystem::exists(path, error)) {
		std::fprintf(stderr, "no state '%s' to load yet\n", path.c_str());
		return;
	}
	if (!load && !m_menus.states().empty())
		std::filesystem::create_directories(m_menus.states(), error);
	if (load ? m_board.load_state(path) : m_board.save_state(path)) {
		std::printf("%s state '%s'\n", load ? "loaded" : "saved", path.c_str());
		/* The card where the state has it: no news */
		m_menus.card_news(true);
	} else {
		std::fprintf(stderr, "ERROR: failed to %s state '%s'\n", load ? "load" : "save", path.c_str());
	}
}

/* The hotkeys of the emulator (their keys by default: STANDARD in
 * hotkeys.cpp); the menu pauses the game. The keys are the control panel's
 * too, see keyboard.hpp. True to quit. */
bool
Session::process_events()
{
	SDL_Event event;
	bool quit = false;

	while (SDL_PollEvent(&event)) {
		if (event.type == m_menus.picker_event()) {
			take_folder();
			continue;
		}
		m_menus.window_event(event);
		if (m_display.menu_shown()) {
			const MenuSession::Asked asked = m_menus.event(event);

			if (asked == MenuSession::Asked::CLOSE)
				close_menu();
			else if (asked == MenuSession::Asked::CLOSE_GAME)
				act(Hotkeys::Action::CLOSE_GAME);
			quit = asked == MenuSession::Asked::QUIT || quit;
			continue;
		}
		/* A menu of the bar open: the keys are its own (Escape closes it)
		 * but those of quitting, of the whole display and of closing the
		 * game (not while a field is typed in, Ctrl+C copies there); a key
		 * let go is let go for the game too */
		if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && m_display.bar_shown() &&
		    m_bar.open) {
			const std::optional<Hotkeys::Action> action =
				event.key.down && !event.key.repeat ? m_menus.hotkeys().key_action(event.key.key, event.key.mod) :
				                                      std::nullopt;

			if (!event.key.down)
				m_keys.set(event.key.key, false);
			if (action == Hotkeys::Action::QUIT || action == Hotkeys::Action::FULLSCREEN ||
			    (action == Hotkeys::Action::CLOSE_GAME && !m_display.menu_typing()))
				quit = act(*action) || quit;
			else
				m_display.bar_event(event);
			continue;
		}
		/* What is typed, for a field of the bar (New card...) */
		if ((event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING) && m_display.bar_shown() &&
		    m_bar.open) {
			m_display.bar_event(event);
			continue;
		}
		if (bar_event(event))
			continue;
		if (m_controls.event(event)) {
			const Hotkeys &hotkeys = m_menus.hotkeys();
			const int combo = m_controls.guide_combo();

			if (const auto action = hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD))
				quit = act(*action) || quit;
			if (m_controls.guide_tapped())
				if (const auto action = hotkeys.pad_action(Hotkeys::GUIDE_ALONE))
					quit = act(*action) || quit;
			continue;
		}
		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			m_keys.set(event.key.key, true);
			if (!event.key.repeat)
				if (const auto action = m_menus.hotkeys().key_action(event.key.key, event.key.mod))
					quit = act(*action) || quit;
			break;
		case SDL_EVENT_KEY_UP:
			m_keys.set(event.key.key, false);
			break;
		case SDL_EVENT_QUIT:
			quit = true;
			break;
		default:
			break;
		}
	}
	return quit;
}

bool
Session::act(Hotkeys::Action action)
{
	using Action = Hotkeys::Action;

	switch (action) {
	case Action::MENU:
		open_menu();
		break;
	case Action::PAUSE:
		m_paused = !m_paused;
		if (m_paused)
			m_controls.quiet();
		m_display.set_paused(m_paused);
		break;
	case Action::SAVE_STATE:
		load_or_save_state(false);
		break;
	case Action::LOAD_STATE:
		load_or_save_state(true);
		break;
	case Action::RESET:
		m_end = Ended::RESTART;
		break;
	case Action::FULLSCREEN:
		m_display.toggle_fullscreen();
		break;
	case Action::CARD:
		say(m_menus.card_in_out());
		m_menus.card_news(true);
		break;
	case Action::LIGHT_GUN:
		if (const std::string note = m_menus.cursor_in_out(); !note.empty())
			say(note);
		if (m_controls.aims())
			leave_bar();
		break;
	case Action::CLOSE_GAME:
		m_end = Ended::CLOSED;
		break;
	case Action::QUIT:
		return true;
	case Action::TEST:
	case Action::SERVICE:
		/* Held, as the controls of the game (Hotkeys::held) */
		break;
	}
	return false;
}

void
Session::open_menu(int page)
{
	m_bar_until = 0;
	m_bar_buttons = 0;
	m_menus.open(page);
}

void
Session::close_menu()
{
	m_menus.close();
	m_next = std::chrono::steady_clock::now();
}

/* The mouse moved shows the bar a while (not where the system first puts
 * it: no move), never where it aims in the game: all of it is the game's
 * there. Its buttons pressed on the bar, or with one of its menus open,
 * are the bar's, and let go there; all the rest of the mouse is the game's
 * too. */
bool
Session::bar_event(const SDL_Event &event)
{
	switch (event.type) {
	case SDL_EVENT_MOUSE_MOTION:
		if ((event.motion.xrel != 0.0f || event.motion.yrel != 0.0f) && !m_controls.aims())
			m_bar_until = std::max(m_bar_until, SDL_GetTicks() + BAR_MS);
		follow_bar();
		m_display.bar_event(event);
		return false;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (!m_display.bar_shown() || !(m_bar.wants_mouse || m_bar.open || event.button.y < m_bar.bottom))
			return false;
		m_bar_buttons |= 1u << event.button.button;
		m_display.bar_event(event);
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const bool bar = (m_bar_buttons >> event.button.button) & 1u;

		m_bar_buttons &= ~(1u << event.button.button);
		m_display.bar_event(event);
		return bar;
	}
	case SDL_EVENT_MOUSE_WHEEL:
		m_display.bar_event(event);
		return m_display.bar_shown() && m_bar.wants_mouse;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		m_bar_until = 0;
		m_display.bar_event(event);
		return false;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		m_display.bar_event(event);
		return false;
	default:
		return false;
	}
}

/* Shown while the mouse moved lately, is on it, holds a button pressed on
 * it or a menu of it is open; never with the menu. Hidden while the game is
 * paused, the picture is shown again without it; hidden, the cursor is
 * again as the controls want it (the bar showed it). */
void
Session::follow_bar()
{
	const bool held = m_display.bar_shown() && (m_bar.wants_mouse || m_bar.open || m_bar_buttons);
	const bool shown = !m_display.menu_shown() && (SDL_GetTicks() < m_bar_until || held);

	if (shown == m_display.bar_shown())
		return;
	m_display.show_bar(shown);
	m_bar.wants_mouse = m_bar.open = false;
	m_bar_buttons = 0;
	if (shown)
		m_bar.saved = state_saved();
	else if (m_paused)
		m_display.show_again(nullptr);
	if (!shown)
		m_controls.follow_cursor();
}

/* The mouse controls the game: no bar, even with the mouse on it */
void
Session::leave_bar()
{
	m_bar_until = 0;
	m_bar.wants_mouse = m_bar.open = false;
	m_bar_buttons = 0;
}

void
Session::fill_bar()
{
	m_menus.fill_bar(m_bar);
	m_bar.title = m_game.title;
	m_bar.fps = m_display.fps();
	m_bar.paused = m_paused;
	m_bar.game = true;
	show_note(m_bar, m_note_until);
}

void
Session::do_bar()
{
	using Ask = BarState::Ask;

	const bool aimed = m_controls.aims();
	const Ask asked = m_menus.do_bar(m_bar);

	if (const std::string note = m_menus.bar_note(); !note.empty())
		say(note);
	if (!aimed && m_controls.aims())
		leave_bar();
	switch (asked) {
	case Ask::CLOSE:
		act(Hotkeys::Action::CLOSE_GAME);
		break;
	case Ask::RESET:
		act(Hotkeys::Action::RESET);
		break;
	case Ask::SAVE_STATE:
		load_or_save_state(false);
		m_bar.saved = state_saved();
		break;
	case Ask::LOAD_STATE:
		load_or_save_state(true);
		break;
	case Ask::CARD:
		act(Hotkeys::Action::CARD);
		break;
	case Ask::CARD_CHOOSE:
		say(m_menus.choose_card(m_bar.card_chosen));
		m_menus.card_news(true);
		break;
	case Ask::CARD_NEW:
		say(m_menus.new_card(m_bar.card_name));
		m_menus.card_news(true);
		break;
	case Ask::SETTINGS:
		open_menu(m_bar.page);
		break;
	case Ask::PAUSE:
		act(Hotkeys::Action::PAUSE);
		break;
	case Ask::QUIT:
		m_end = Ended::QUIT;
		break;
	default:
		break;
	}
}

/* A note in the bottom-left corner, shown a while */
void
Session::say(const std::string &note)
{
	m_bar.note = note;
	m_note_until = SDL_GetTicks() + NOTE_MS;
}

void
Session::take_folder()
{
	std::string note;

	if (const std::optional<FolderDialog::Answer> archive = m_menus.take_folder()) {
		if (const std::optional<Opened> opened = open_archive(*archive, m_games, note)) {
			m_opened = *opened;
			m_end = Ended::OPENED;
		} else if (!note.empty()) {
			say(note);
		}
	}
}

/* -s: <game>-<frame>.odinstate, written once the frame is drawn */
void
Session::save_state_at_frame()
{
	char path[64];

	for (const unsigned frame : m_options.save_frames) {
		if (frame != m_board.frame())
			continue;
		std::snprintf(path, sizeof(path), "%s-%06u.odinstate", m_board.game_name().c_str(), frame);
		if (m_board.save_state(path))
			std::printf("saved state '%s'\n", path);
		else
			std::fprintf(stderr, "ERROR: failed to save state '%s'\n", path);
	}
}

/* Holds the frames at the rate of the board: each as long as the board
 * says (1/60 s, or a frame of a Gaelco board's screen). A frame late by
 * less than two periods is caught up, a longer delay is dropped. */
void
Session::limit_frame_rate()
{
	const auto now = std::chrono::steady_clock::now();
	const std::chrono::nanoseconds frame(m_board.frame_nanoseconds());

	m_next += frame;
	if (now - m_next > 2 * frame)
		m_next = now;
	else if (now < m_next)
		/* Not std::this_thread::sleep_until: with MinGW it sleeps again
		 * until a tick count of 15.6 ms steps has moved, up to 15 ms
		 * late. SDL sleeps on a timer of high resolution there. */
		SDL_DelayNS(static_cast<Uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(m_next - now).count()));
}

Ended
Session::run()
{
	const Ended ended = frames();

	/* The next game, or the window without one, finds the menu and the
	 * bar closed */
	if (m_display.menu_shown())
		close_menu();
	m_display.show_bar(false);
	return ended;
}

Ended
Session::frames()
{
	while (!process_events()) {
		if (m_end)
			return *m_end;
		if (m_board.restart_asked())
			return Ended::RESTART;
		if (m_options.num_frames > 0 && m_board.frame() >= m_options.num_frames)
			return Ended::LAST_FRAME;
		if (m_display.menu_shown()) {
			m_menus.wait();
			if (m_menus.leaving())
				close_menu();
			continue;
		}
		follow_bar();
		if (m_paused) {
			/* Asleep until something comes, the next key; the bar drawn
			 * over the picture while it is shown, a note until it is gone */
			if (m_display.bar_shown() || !m_bar.note.empty()) {
				fill_bar();
				m_display.show_again(&m_bar);
				if (m_display.bar_shown())
					do_bar();
				SDL_WaitEventTimeout(nullptr, BAR_PAUSED_MS);
			} else {
				SDL_WaitEvent(nullptr);
			}
			continue;
		}
		m_display.begin_frame();
		m_controls.update();
		m_board.run_frame();
		if (const std::string news = m_menus.card_news(); !news.empty())
			say(news);
		m_controls.show(m_board.io_outputs(), m_board.output_levels(), shaker_sound(), m_board.drive_torque());
		if (OutputsNetwork *const outputs = m_menus.outputs())
			outputs->show(m_board.io_outputs());
		if (m_audio)
			m_audio->play(m_board.sound(false), m_board.sound(true));

		/* The bar, or a note alone, over the frame */
		const bool bar = m_display.bar_shown(), over = bar || !m_bar.note.empty();

		if (over)
			fill_bar();
		m_display.end_frame(m_board.frame(), std::chrono::steady_clock::now() > m_next + FRAME, over ? &m_bar : nullptr);
		if (bar)
			do_bar();
		save_state_at_frame();
		if (m_menus.tested_at(m_board.frame())) {
			if (m_menus.bar_tested() < 0) {
				press_escape();
			} else {
				m_bar_until = SDL_GetTicks() + BAR_MS;
				m_bar.open_test = m_menus.bar_tested();
			}
		}
		if (!m_options.unthrottled)
			limit_frame_rate();
	}
	return Ended::QUIT;
}

std::span<const int16_t>
Session::shaker_sound()
{
	if (!m_shaker_on_main)
		return m_board.sound(true);

	const std::vector<int16_t> &main = m_board.sound(false);

	m_seat.resize(main.size());
	for (size_t i = 0; i + 1 < main.size(); i += 2)
		m_seat[i] = m_seat[i + 1] = m_seat_lows(static_cast<int16_t>((main[i] + main[i + 1]) / 2));
	return m_seat;
}

} /* namespace odin */
