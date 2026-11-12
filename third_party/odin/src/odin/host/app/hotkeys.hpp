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

#ifndef ODIN_HOTKEYS_HPP
#define ODIN_HOTKEYS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>

namespace odin {

class Settings;

/* A key of Odin: the key, and Ctrl, Alt, Shift or Gui held with it (either
 * side); none is SDLK_UNKNOWN */
struct HotKey {
	SDL_Keycode key = SDLK_UNKNOWN;
	SDL_Keymod mods = SDL_KMOD_NONE;

	bool operator==(const HotKey &) const = default;
};

/* What Odin does itself on a key or on a gamepad (the page
 * Hotkeys): up to two keys for each action, and a button pressed with the
 * hotkey button held (Guide unless the user chose another; Guide alone:
 * tapped, the menu by default). The user changes them in the menu;
 * [hotkeys] of odin.ini keeps those that are not as by default, one line an
 * action, "guide" there being the hotkey button:
 *
 *   button = back
 *   pause = Pause, pad:guide+x
 *   quit = Ctrl+Q, pad:guide+start
 *
 * A key may be held with others than its own (a player holding Left Ctrl
 * still pauses with P); the key with the most of its own held goes first.
 * A key or a button is one action's: given to another, it leaves the first
 * one. */
class Hotkeys {
public:
	/* CARD: the player's card into the slot of the card reader, or back
	 * out of it (Virtual-On); LIGHT_GUN: the cursor hidden (the mouse
	 * controls the game) or shown, in every game; TEST and SERVICE: the
	 * TEST switch of the cabinet and the SERVICE of player 1, held, in
	 * every game; CLOSE_GAME: back to the library */
	enum class Action : uint8_t {
		MENU, PAUSE, SAVE_STATE, LOAD_STATE, RESET, FULLSCREEN, CARD, LIGHT_GUN, TEST, SERVICE, CLOSE_GAME, QUIT
	};
	static constexpr size_t ACTIONS = static_cast<size_t>(Action::QUIT) + 1;
	static constexpr size_t KEYS = 2;
	/* The gamepad of an action: none, Guide tapped alone, or a button with
	 * the hotkey button held (an SDL_GamepadButton) */
	static constexpr int NO_PAD = -1;
	static constexpr int GUIDE_ALONE = SDL_GAMEPAD_BUTTON_GUIDE;

	struct Binding {
		std::array<HotKey, KEYS> keys{};
		int pad = NO_PAD;

		bool operator==(const Binding &) const = default;
	};

	/* Those by default */
	Hotkeys();
	/* [hotkeys] of the settings over those by default; a line that is not
	 * right is said on stderr (unless `tell` is false) and left out */
	explicit Hotkeys(const Settings &settings, bool tell = true);

	/* An action that is a switch of the cabinet, held as long as its key
	 * (with no modifier) or its button is: no action of Odin, the inputs
	 * by default of its role in every game (bindings.cpp) */
	static bool held(Action action) { return action == Action::TEST || action == Action::SERVICE; }

	/* The name of an action in odin.ini; those by default with that
	 * hotkey button: one other than Guide is the game's tapped alone, so
	 * the menu is that button with A, and no action takes the button */
	static const char *id(Action action);
	static Binding standard(Action action, int button = SDL_GAMEPAD_BUTTON_GUIDE);
	const Binding &of(Action action) const { return m_bindings[static_cast<size_t>(action)]; }

	/* The buttons that may be the hotkey button, Guide first; the name of
	 * one in odin.ini ("back"), and the button of a name */
	static std::span<const int> buttons();
	static std::string button_text(int button);
	static std::optional<int> button_of(std::string_view text);
	/* The hotkey button; another, which those as by default follow (an
	 * action leaves the button itself) */
	int button() const { return m_button; }
	void set_button(int button);
	/* The buttons that, with the hotkey button held, are an action */
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> combos() const;

	/* The action of a key pressed with those modifiers held; of a button
	 * pressed with the hotkey button held, or GUIDE_ALONE */
	std::optional<Action> key_action(SDL_Keycode key, SDL_Keymod mods) const;
	std::optional<Action> pad_action(int pad) const;
	/* A key that is an action's alone: no control of a game takes it (a
	 * held one may still be a game's) */
	bool takes(SDL_Keycode key) const;

	/* A key (slot 0 or 1) or the gamepad of an action changed; the action
	 * that had it before loses it: that action, when another. All back as
	 * by default. */
	std::optional<Action> set_key(Action action, size_t slot, HotKey key);
	std::optional<Action> set_pad(Action action, int pad);
	void reset();

	/* Ctrl, Alt, Shift and Gui of a state of the modifiers, either side */
	static SDL_Keymod modifiers(SDL_Keymod mods);
	/* Whether a key is Ctrl, Alt, Shift or Gui itself */
	static bool is_modifier(SDL_Keycode key);
	/* A key as odin.ini says it ("Ctrl+Q", "F10"), and back */
	static std::string text(const HotKey &key);
	/* The first key of a binding as text; empty when it has none */
	static std::string first_text(const Binding &binding);
	static std::optional<HotKey> key_of(std::string_view text);
	/* A line of [hotkeys], and back ("Escape, F10, pad:guide") */
	static std::string line(const Binding &binding);
	static std::optional<Binding> parse(std::string_view line);

private:
	std::array<Binding, ACTIONS> m_bindings;
	int m_button = SDL_GAMEPAD_BUTTON_GUIDE;
};

} /* namespace odin */

#endif /* ODIN_HOTKEYS_HPP */
