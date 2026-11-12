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

#include "odin/host/app/hotkeys.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>

#include "odin/host/app/settings.hpp"
#include "odin/host/input/keyboard.hpp"

namespace odin {

namespace {

constexpr const char *IDS[Hotkeys::ACTIONS] = { "menu", "pause", "save_state", "load_state", "reset", "fullscreen",
                                                "card", "light_gun", "test", "service", "close_game", "quit" };

/* Escape or F10 the menu, Guide tapped alone; P pauses (as in MAME); F2
 * saves the state and F4 loads it (as in RetroArch); Ctrl+R switches the
 * cabinet off and on; F11 or Alt+Enter the whole display; F6 the card in
 * or out; F7 hides the cursor or shows it; 0 TEST and 9 SERVICE, or Guide
 * with R3 and L3; Ctrl+C closes the game (back to the library); Ctrl+Q
 * quits. On a gamepad Guide held with Back, RB, LB, Y, B or Start; no
 * button resets. */
const Hotkeys::Binding STANDARD[Hotkeys::ACTIONS] = {
	{ { { { SDLK_ESCAPE, SDL_KMOD_NONE }, { SDLK_F10, SDL_KMOD_NONE } } }, Hotkeys::GUIDE_ALONE },
	{ { { { SDLK_P, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_BACK },
	{ { { { SDLK_F2, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
	{ { { { SDLK_F4, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
	{ { { { SDLK_R, SDL_KMOD_CTRL }, {} } }, Hotkeys::NO_PAD },
	{ { { { SDLK_F11, SDL_KMOD_NONE }, { SDLK_RETURN, SDL_KMOD_ALT } } }, Hotkeys::NO_PAD },
	{ { { { SDLK_F6, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_NORTH },
	{ { { { SDLK_F7, SDL_KMOD_NONE }, {} } }, Hotkeys::NO_PAD },
	{ { { { SDLK_0, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_RIGHT_STICK },
	{ { { { SDLK_9, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_LEFT_STICK },
	{ { { { SDLK_C, SDL_KMOD_CTRL }, {} } }, SDL_GAMEPAD_BUTTON_EAST },
	{ { { { SDLK_Q, SDL_KMOD_CTRL }, {} } }, SDL_GAMEPAD_BUTTON_START },
};

/* The buttons that may be the hotkey button, Guide first: not the D-pad
 * nor the four of the face (A presses in the menu) */
constexpr int HOTKEY_BUTTONS[] = {
	SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
	SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
	SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_TOUCHPAD,
};

/* The modifiers as odin.ini names them */
constexpr std::pair<SDL_Keymod, const char *> MODIFIERS[] = {
	{ SDL_KMOD_CTRL, "Ctrl" }, { SDL_KMOD_ALT, "Alt" }, { SDL_KMOD_SHIFT, "Shift" }, { SDL_KMOD_GUI, "Gui" },
};

} /* namespace */

Hotkeys::Hotkeys()
{
	reset();
}

Hotkeys::Hotkeys(const Settings &settings, bool tell) : Hotkeys()
{
	for (const auto &[key, value] : settings.section("hotkeys")) {
		if (key != "button")
			continue;
		if (const std::optional<int> button = button_of(value))
			m_button = *button;
		else if (tell)
			refuse_setting("hotkeys", key, value);
	}
	reset();
	for (const auto &[key, value] : settings.section("hotkeys")) {
		size_t action = 0;

		if (key == "button")
			continue;
		while (action < ACTIONS && key != IDS[action])
			action++;

		const std::optional<Binding> binding = action < ACTIONS ? parse(value) : std::nullopt;

		if (!binding) {
			if (tell)
				refuse_setting("hotkeys", key, value);
			continue;
		}
		m_bindings[action] = {};
		for (size_t slot = 0; slot < KEYS; slot++)
			if (binding->keys[slot].key != SDLK_UNKNOWN)
				set_key(static_cast<Action>(action), slot, binding->keys[slot]);
		set_pad(static_cast<Action>(action), binding->pad);
	}
}

const char *
Hotkeys::id(Action action)
{
	return IDS[static_cast<size_t>(action)];
}

Hotkeys::Binding
Hotkeys::standard(Action action, int button)
{
	Binding binding = STANDARD[static_cast<size_t>(action)];

	if (button != SDL_GAMEPAD_BUTTON_GUIDE && action == Action::MENU)
		binding.pad = SDL_GAMEPAD_BUTTON_SOUTH;
	else if (button != SDL_GAMEPAD_BUTTON_GUIDE && binding.pad == button)
		binding.pad = NO_PAD;
	return binding;
}

std::span<const int>
Hotkeys::buttons()
{
	return HOTKEY_BUTTONS;
}

std::string
Hotkeys::button_text(int button)
{
	const char *const text = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));

	return text ? text : "";
}

std::optional<int>
Hotkeys::button_of(std::string_view text)
{
	const int button = SDL_GetGamepadButtonFromString(std::string(trimmed(text)).c_str());

	if (std::find(std::begin(HOTKEY_BUTTONS), std::end(HOTKEY_BUTTONS), button) == std::end(HOTKEY_BUTTONS))
		return std::nullopt;
	return button;
}

void
Hotkeys::set_button(int button)
{
	std::array<bool, ACTIONS> standard_was{};

	for (size_t a = 0; a < ACTIONS; a++)
		standard_was[a] = m_bindings[a].pad == standard(static_cast<Action>(a), m_button).pad;
	m_button = button;
	for (Binding &binding : m_bindings)
		if (button != SDL_GAMEPAD_BUTTON_GUIDE && (binding.pad == button || binding.pad == GUIDE_ALONE))
			binding.pad = NO_PAD;
	for (size_t a = 0; a < ACTIONS; a++)
		if (standard_was[a])
			set_pad(static_cast<Action>(a), standard(static_cast<Action>(a), button).pad);
}

std::array<bool, SDL_GAMEPAD_BUTTON_COUNT>
Hotkeys::combos() const
{
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> out{};

	for (const Binding &binding : m_bindings)
		if (binding.pad != NO_PAD && binding.pad != GUIDE_ALONE && binding.pad < SDL_GAMEPAD_BUTTON_COUNT)
			out[static_cast<size_t>(binding.pad)] = true;
	return out;
}

std::optional<Hotkeys::Action>
Hotkeys::key_action(SDL_Keycode key, SDL_Keymod mods) const
{
	const SDL_Keymod down_mods = modifiers(mods);
	std::optional<Action> found;
	int most = -1;

	/* Alt+F4 closes the window (Windows): not the state F4 loads */
	if (key == SDLK_F4 && (down_mods & SDL_KMOD_ALT))
		return std::nullopt;

	for (size_t action = 0; action < ACTIONS; action++) {
		if (held(static_cast<Action>(action)))
			continue;
		for (const HotKey &k : m_bindings[action].keys) {
			const int own = std::popcount(static_cast<unsigned>(k.mods));

			if (k.key == SDLK_UNKNOWN || k.key != key || (k.mods & ~down_mods) || own <= most)
				continue;
			found = static_cast<Action>(action);
			most = own;
		}
	}
	return found;
}

std::optional<Hotkeys::Action>
Hotkeys::pad_action(int pad) const
{
	for (size_t action = 0; action < ACTIONS; action++)
		if (pad != NO_PAD && m_bindings[action].pad == pad && !held(static_cast<Action>(action)))
			return static_cast<Action>(action);
	return std::nullopt;
}

bool
Hotkeys::takes(SDL_Keycode key) const
{
	for (size_t action = 0; action < ACTIONS; action++)
		for (const HotKey &k : m_bindings[action].keys)
			if (k.key == key && k.mods == SDL_KMOD_NONE && !held(static_cast<Action>(action)))
				return true;
	return false;
}

std::optional<Hotkeys::Action>
Hotkeys::set_key(Action action, size_t slot, HotKey key)
{
	std::optional<Action> from;

	if (slot >= KEYS)
		return from;
	key.mods = held(action) ? SDL_KMOD_NONE : modifiers(key.mods);
	for (size_t a = 0; a < ACTIONS; a++)
		for (HotKey &k : m_bindings[a].keys)
			if (key.key != SDLK_UNKNOWN && k == key) {
				k = {};
				if (a != static_cast<size_t>(action))
					from = static_cast<Action>(a);
			}
	m_bindings[static_cast<size_t>(action)].keys[slot] = key;
	return from;
}

std::optional<Hotkeys::Action>
Hotkeys::set_pad(Action action, int pad)
{
	std::optional<Action> from;

	/* The hotkey button pressed with itself is no shortcut, and another
	 * one than Guide is the game's when tapped alone */
	if (m_button != SDL_GAMEPAD_BUTTON_GUIDE && (pad == m_button || pad == GUIDE_ALONE))
		pad = NO_PAD;
	for (size_t a = 0; a < ACTIONS; a++)
		if (pad != NO_PAD && m_bindings[a].pad == pad) {
			m_bindings[a].pad = NO_PAD;
			if (a != static_cast<size_t>(action))
				from = static_cast<Action>(a);
		}
	m_bindings[static_cast<size_t>(action)].pad = pad;
	return from;
}

void
Hotkeys::reset()
{
	for (size_t action = 0; action < ACTIONS; action++)
		m_bindings[action] = standard(static_cast<Action>(action), m_button);
}

SDL_Keymod
Hotkeys::modifiers(SDL_Keymod mods)
{
	SDL_Keymod held = SDL_KMOD_NONE;

	for (const auto &[mod, name] : MODIFIERS)
		if (mods & mod)
			held |= mod;
	return held;
}

bool
Hotkeys::is_modifier(SDL_Keycode key)
{
	switch (key) {
	case SDLK_LCTRL:
	case SDLK_RCTRL:
	case SDLK_LALT:
	case SDLK_RALT:
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:
	case SDLK_LGUI:
	case SDLK_RGUI:
	case SDLK_MODE:
		return true;
	default:
		return false;
	}
}

std::string
Hotkeys::first_text(const Binding &binding)
{
	return binding.keys[0].key == SDLK_UNKNOWN ? std::string() : text(binding.keys[0]);
}

std::string
Hotkeys::text(const HotKey &key)
{
	std::string out;

	for (const auto &[mod, name] : MODIFIERS)
		if (key.mods & mod)
			out = out + name + "+";
	return out + SDL_GetKeyName(key.key);
}

std::optional<HotKey>
Hotkeys::key_of(std::string_view text)
{
	HotKey key;

	text = trimmed(text);
	for (;;) {
		const size_t plus = text.find('+');
		bool found = false;

		/* "+" itself, or a key whose name ends with it */
		if (plus == std::string_view::npos || plus + 1 == text.size())
			break;
		for (const auto &[mod, name] : MODIFIERS) {
			if (text.substr(0, plus) == name) {
				key.mods |= mod;
				found = true;
			}
		}
		if (!found)
			return std::nullopt;
		text.remove_prefix(plus + 1);
	}
	key.key = key_named(std::string(text).c_str());
	if (key.key == SDLK_UNKNOWN || is_modifier(key.key))
		return std::nullopt;
	return key;
}

std::string
Hotkeys::line(const Binding &binding)
{
	std::string out;

	for (const HotKey &key : binding.keys)
		if (key.key != SDLK_UNKNOWN)
			out += (out.empty() ? "" : ", ") + text(key);
	if (binding.pad == GUIDE_ALONE)
		out += out.empty() ? "pad:guide" : ", pad:guide";
	else if (binding.pad != NO_PAD)
		out += std::string(out.empty() ? "" : ", ") + "pad:guide+" +
		       SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(binding.pad));
	return out.empty() ? "none" : out;
}

std::optional<Hotkeys::Binding>
Hotkeys::parse(std::string_view line)
{
	Binding binding;
	size_t keys = 0;

	if (trimmed(line) == "none")
		return binding;
	for (const std::string_view item : list_items(line)) {
		if (item.starts_with("pad:")) {
			const std::string_view pad = item.substr(4);

			if (binding.pad != NO_PAD)
				return std::nullopt;
			if (pad == "guide") {
				binding.pad = GUIDE_ALONE;
				continue;
			}
			if (!pad.starts_with("guide+"))
				return std::nullopt;

			const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(std::string(pad.substr(6)).c_str());

			if (button == SDL_GAMEPAD_BUTTON_INVALID || button == SDL_GAMEPAD_BUTTON_GUIDE)
				return std::nullopt;
			binding.pad = button;
			continue;
		}

		const std::optional<HotKey> key = key_of(item);

		if (!key || keys == KEYS)
			return std::nullopt;
		binding.keys[keys++] = *key;
	}
	return binding;
}

} /* namespace odin */
