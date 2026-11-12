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

#ifndef ODIN_MENU_SESSION_HPP
#define ODIN_MENU_SESSION_HPP

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "odin/host/audio/audio_out.hpp"
#include "odin/machine/chips/card_reader.hpp"
#include "odin/host/input/controls.hpp"
#include "odin/host/video/display.hpp"
#include "odin/host/ui/folder_dialog.hpp"
#include "odin/host/app/games.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/host/app/hotkeys.hpp"
#include "odin/host/input/keyboard.hpp"
#include "odin/host/ui/menu.hpp"
#include "odin/host/ui/menu_nav.hpp"
#include "odin/host/app/options.hpp"
#include "odin/host/input/outputs_network.hpp"
#include "odin/host/app/settings.hpp"

namespace odin {

/* The game of an archive opened, and the folder that holds it */
struct Opened {
	std::string game, folder;
};

/* A note of Odin (BarState::note) stays that long, fading out in its last
 * half second; how much of it shows until `until` */
inline constexpr Uint64 NOTE_MS = 4000;
inline constexpr Uint64 NOTE_FADE_MS = 500;
inline float note_alpha(Uint64 until)
{
	const Uint64 now = SDL_GetTicks();

	return now >= until ? 0.0f : std::min(1.0f, static_cast<float>(until - now) / static_cast<float>(NOTE_FADE_MS));
}
/* The note of the bar as it shows until `until`: fading, then gone */
inline void show_note(BarState &bar, Uint64 until)
{
	bar.note_alpha = note_alpha(until);
	if (SDL_GetTicks() >= until)
		bar.note.clear();
}
/* The picker of an archive, asked for by Open...: no folder of the menu */
inline constexpr int ARCHIVE_ASKED = -1;

/* An archive Open... gave: its game, as MAME names its archive (the BIOS is
 * none), and the folder that holds it; else nothing, and what went wrong
 * for the bar to say (empty: nothing was chosen) */
std::optional<Opened> open_archive(const FolderDialog::Answer &answer, const GameList &games, std::string &note);

/* [outputs] network: the outputs sent to the programs of lamps. The last
 * line says. */
bool network_on(const Settings &settings);
NetLinkSettings link_settings(const Settings &settings);

/* ODIN_MENU_TEST: Escape pressed and released */
void press_escape();

/* The menu and the bar, over a game (Session) or over the library: what
 * they show and change, kept in odin.ini; the inputs the pages Input and
 * Hotkeys catch; the folders the picker of the system gives. The game that
 * runs lends its board, its sound and its programs of lamps (the menu turns
 * them on and off); the library has none of them, and for its gamepads the
 * controls of no game: its page Input shows the game chosen there.
 *
 * ODIN_MENU_TEST=<n> is a test of the menu over a game: Escape is pressed
 * after every n frames, which opens it, and again once it has drawn its
 * pictures, which closes it. The pictures of the game must be the same. */
class MenuSession {
public:
	/* What an event asks of the game or the library: the menu closed, the
	 * game closed (back to the library), Odin to quit */
	enum class Asked { NONE, CLOSE, CLOSE_GAME, QUIT };

	MenuSession(const Options &options, Display &display, Keyboard &keys, Controls &controls, Settings &settings,
	            const std::string &settings_file, const GameList &games, const Game *game, const Board *running,
	            Hikaru *board, AudioOut *audio, std::unique_ptr<OutputsNetwork> outputs, bool network);

	~MenuSession()
	{
		if (m_listing)
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
		keep_custom();
	}
	MenuSession(const MenuSession &) = delete;
	MenuSession &operator=(const MenuSession &) = delete;

	/* A line of odin.ini changed (none: taken away), for the time Odin
	 * runs without one */
	void keep(const std::string &section, const std::string &key, const std::optional<std::string> &value);

	/* The menu at that page (-1: the one shown last): the game waits, no
	 * pad shakes, its sound stops where it is. Closed, the keys held then
	 * are not the game's. */
	void open(int page = -1);
	void close();
	/* With the menu open: its hotkey asks to close it, that of closing the
	 * game (over one) to close the game and that of quitting to quit; that
	 * of the whole display still works. The
	 * keyboard and the gamepads are followed as they go; all the rest is
	 * the menu's. */
	Asked event(const SDL_Event &event);
	/* The menu draws its pictures, then sleeps until the next event */
	void wait();
	/* The page Input shows that game */
	void show(const std::string &game);
	/* The window is the active one, or in the background */
	void focus(bool focused);
	/* What the window says, with a game or not: the display it went to,
	 * the focus */
	void window_event(const SDL_Event &event);
	/* The picker of the system: the event of its answer; the folder it
	 * gave, taken (the answer for an archive, Open..., is the caller's);
	 * a folder asked for; whether it is open */
	Uint32 picker_event() const { return m_dialog.event(); }
	std::optional<FolderDialog::Answer> take_folder();
	void ask_folder(MenuState::Folder folder);
	bool choosing() const { return m_dialog.open(); }
	/* The bar from what is so now; what it changed, done, and of what it
	 * asked, what the menu does: the rest is the caller's */
	void fill_bar(BarState &bar) const;
	BarState::Ask do_bar(BarState &bar);
	/* What the last change of the bar asks the user to do, said once
	 * ("": nothing) */
	std::string bar_note() { return std::exchange(m_bar_note, std::string()); }
	/* The player's card, on a cabinet with a card reader (Virtual-On):
	 * into the slot, or back out of it once the reader ejected it; the
	 * card of the list in hand (-1: none). What the note says of it. */
	std::string card_in_out();
	/* The cursor of the game that runs hidden or shown, the other way,
	 * and kept (Hotkeys::Action::LIGHT_GUN); what the note says of it
	 * (empty: no game, or the controls off) */
	std::string cursor_in_out();
	std::string choose_card(int index);
	/* A new card of a pilot of that name, as the terminal of the game
	 * makes it (Von4Card), kept under the name in the folder of the cards
	 * and put in hand */
	std::string new_card(const std::string &name);
	/* What the reader did with a card since the last call, the game's
	 * doing, to be said: the player's card taken in, a card out at the
	 * slot; empty: nothing new. `quiet`: only seen (the player's own
	 * gestures, a state loaded). */
	std::string card_news(bool quiet = false);
	/* For the tests: ODIN_MENU_TEST, the menu opens after that frame, on
	 * the page ODIN_MENU_PAGE (Menu::Page; else the last shown); with
	 * ODIN_BAR_MENU, the bar instead, that menu of it open (Menu::bar) */
	bool tested_at(unsigned frame) const { return m_menu_test && frame % m_menu_test == 0; }
	int bar_tested() const { return m_bar_test; }
	/* A click beside the frame asked to go back to the game or the
	 * library, taken */
	bool leaving() { return std::exchange(m_leaving, false); }
	/* The library: what the buttons of the menu an event starts ask
	 * there; the name of the one that presses on the gamepad of player 1,
	 * else 2 (empty: no gamepad) */
	std::vector<MenuNav::Action> nav(const SDL_Event &event) { return m_nav.event(event); }
	std::string press_name() const;

	const MenuState &state() const { return m_menu_state; }
	const Hotkeys &hotkeys() const { return m_hotkeys; }
	/* The folder of the save states (empty: the one Odin is started
	 * from) */
	const std::string &states() const { return m_states; }
	/* The programs of lamps (none: nothing for them) */
	OutputsNetwork *outputs() const { return m_outputs.get(); }

private:
	/* After an event the menu draws this many pictures, one in so many
	 * milliseconds at most */
	static constexpr unsigned MENU_PICTURES = 3;
	static constexpr Uint64 MENU_PICTURE_MS = 16;
	/* How long Undo is offered after Clear mapping; how often the menu is
	 * drawn while an input is caught, its prompt beating (Start mapping
	 * counts its seconds too) */
	static constexpr Uint64 UNDO_MS = 5000;
	static constexpr Uint64 CATCH_TICK_MS = 33;
	/* How long the button that presses is held to skip a control while
	 * Start mapping: a choice of the emulator */
	static constexpr Uint64 SKIP_HOLD_MS = 1000;
	/* How long Escape is held on the page Hotkeys to be the key caught
	 * (tapped, it lets the catch go): a choice of the emulator */
	static constexpr Uint64 ESCAPE_HOLD_MS = 1000;

	/* What a button of the menu asks: true for B with no list dropped open,
	 * the menu to close */
	bool navigate(MenuNav::Action action);
	bool presses(int button) const;
	void catch_pad(bool axis, int index, int value);
	void catch_joystick(const SDL_Event &event, int was);
	bool catch_aim(const SDL_Event &event);
	void button_on_analog();
	void draw_menu();
	void fill_display(MenuState &state);
	void fill_inputs(MenuState &state);
	void fill_game_shown(MenuState &state);
	void fill_link(MenuState &state);
	void fill_devices(MenuState &state);
	void do_asked(MenuState &state);
	void apply(const MenuState &was);
	void apply_display(const MenuState &was);
	void apply_sound(const MenuState &was);
	void apply_game(const MenuState &was);
	void apply_link(const MenuState &was);
	void change_part(const MenuSlot &slot, bool to_defaults);
	void keep(const char *key, const std::optional<std::string> &value);
	/* [display] window: the size of Custom, or how many times the screen;
	 * the size the user gave the window by hand, Custom from now on
	 * (followed at each picture of the menu, kept in odin.ini when Odin is
	 * done with this game or the library: a drag gives many sizes) */
	void keep_window();
	void follow_hand_size();
	void keep_custom();
	/* The game whose controls the page Input shows: its name and panel */
	std::string game_shown() const;
	const ControlPanel &panel_shown() const { return m_other ? *m_other : m_controls.panel(); }
	/* The joystick of a player in the game shown (0: none), and what the
	 * host holds for it: of a game that does not run, the gamepad and the
	 * joystick its settings seat (fill_devices), which the game that runs
	 * may leave unseated (the library seats no wheel) */
	SDL_JoystickID joystick_shown(unsigned player) const;
	HostState host_shown() const;
	void show_game(size_t game);
	bool catch_input(const SDL_Event &event);
	bool catch_hotkey(const SDL_Event &event);
	void keep_hotkeys();
	void clear_hotkey(Hotkeys::Action action, size_t slot);
	void set_hotkey_key(Hotkeys::Action action, size_t slot, const HotKey &key);
	void caught(const HostInput &input);
	void change_inputs(bool to_defaults);
	void clear_inputs();
	void undo();
	/* As the page Input and the bar do them for a game: the gamepad or the
	 * joystick of a player, its light gun, its vibration ([players.<game>]);
	 * the game that runs takes them at once when it is that game (`runs`);
	 * the dead zone and the trigger threshold of a make and model of
	 * gamepad ([gamepad.<guid>]) */
	void want_pad(const std::string &game, unsigned player, const std::string &guid, const std::string &name,
	              bool joystick, bool runs, SDL_JoystickID picked = 0);
	void want_gun(const std::string &game, unsigned player, const std::string &identity, const std::string &name,
	              bool runs);
	void keep_device(const std::string &game, unsigned player, InputDevice device, bool runs);
	void keep_vibration(const std::string &game, bool on, bool runs);
	void keep_vibration_force(const std::string &game, int percent, bool runs);
	void keep_hide_cursor(const std::string &game, bool on, bool runs, bool standard);
	void keep_inverted(const std::string &game, const std::vector<std::string> &ids, const std::vector<uint8_t> &on);
	void keep_sinden_border(const std::string &game, const SindenBorder &border, bool runs);
	void keep_wheel_force(const std::string &game, int percent, bool runs);
	void keep_wheel_rotation(const std::string &game, int degrees, bool runs);
	void keep_cabinet(const std::string &game, const std::string &id);
	void keep_tuning(const std::string &guid, const PadTuning &now, const PadTuning &was);
	void fill_bar_inputs(BarState &bar) const;
	void light_guns(const std::string &game, const std::vector<GameControl> &controls, std::vector<LightGun> &guns,
	                std::array<std::string, 2> &gun, std::array<bool, 2> &sights) const;
	void keep_binding(const Bindings::Line &line, const std::string &game = "");
	void rebind_panels();
	void fill_games();
	void read_menu_settings();
	void take_card();
	/* The cards in the folder of the game, by name; the file of one */
	void list_cards();
	std::string card_file(const std::string &name) const;

	const Options &m_options;
	Display &m_display;
	Keyboard &m_keys;
	Controls &m_controls;
	Settings &m_settings;
	const std::string &m_settings_file;	/* empty: nothing is kept */
	const GameList &m_games;
	/* The game that runs and its board (none: the library), its sound
	 * (none: no sound), its programs of lamps (none: nothing for them) */
	const Game *m_game;
	/* The board that runs (its game, its lamps, its link), and when it is
	 * a Hikaru, that one (its cards) */
	const Board *m_running;
	Hikaru *m_board;
	AudioOut *m_audio;
	std::unique_ptr<OutputsNetwork> m_outputs;
	/* The sound of SDL started for the devices the library lists */
	bool m_listing = false;
	Hotkeys m_hotkeys;
	MenuNav m_nav;
	/* The panel of a game shown that does not run, its controls and its
	 * outputs */
	std::vector<GameControl> m_other_controls;
	std::vector<GameOutput> m_other_outputs;
	bool m_other_shakes = false;
	bool m_other_forces = false;
	std::vector<Game::Cabinet> m_other_cabinets;
	std::array<std::string, 2> m_other_shakers;
	std::array<bool, 2> m_other_experimental{};
	Game::Link m_other_link;
	std::string m_other_title;
	std::optional<ControlPanel> m_other;
	/* The game shown does not run: its devices, as its settings seat them,
	 * at the next fill_devices */
	bool m_devices_of_other = false;
	MenuState m_menu_state;
	/* The inputs as Clear mapping found them, and until when Undo puts them back */
	Settings::Lines m_cleared;
	std::string m_cleared_game;
	Uint64 m_undo_until = 0;
	/* Guide is held while an input is caught: the next button goes with it */
	bool m_catch_guide = false;
	/* Start mapping: the button that presses, held since then; it skipped */
	struct Held {
		int button;
		uint64_t since;
		bool skipped;
	};
	std::optional<Held> m_held;
	/* A part caught (or Map all): where each axis of each joystick was
	 * as it began (a pedal at rest; none: not known yet), the move of one
	 * taken from there (on Windows, SDL's first value is the middle until
	 * the device reports); the axes caught, with where they were, none
	 * again until back there */
	std::optional<std::map<SDL_JoystickID, std::vector<std::optional<int16_t>>>> m_catch_from;
	std::map<std::pair<SDL_JoystickID, size_t>, int> m_catch_held;
	/* The page Hotkeys: Escape down since then, with those modifiers, as
	 * that place waited for its key */
	struct EscapeDown {
		Uint64 since;
		SDL_Keymod mods;
		std::pair<size_t, size_t> place;
	};
	std::optional<EscapeDown> m_escape_down;
	/* A sight caught whole: where the pointer was as it began */
	struct AimFrom {
		MenuSlot slot;
		float x, y;
	};
	std::optional<AimFrom> m_aim_from;
	/* The window is the active one */
	bool m_focused = true;
	/* The folder of the save states, and the picker of the system that
	 * changes the folders */
	std::string m_states;
	std::vector<std::string> m_cards;
	std::optional<CardReader::Where> m_card_seen;
	/* The characters of a new card's name (Von4Card::keys); empty: the
	 * game makes none */
	std::string m_card_keys;
	std::string m_bar_note;
	FolderDialog m_dialog;
	bool m_leaving = false;
	/* A size of the window from the hands of the user, not kept yet */
	bool m_custom_waits = false;
	unsigned m_menu_pictures = 0;	/* still to draw */
	Uint64 m_menu_drawn = 0;	/* when the last one was */
	const unsigned m_menu_test;
	const int m_menu_test_page, m_bar_test;
};

} /* namespace odin */

#endif /* ODIN_MENU_SESSION_HPP */
