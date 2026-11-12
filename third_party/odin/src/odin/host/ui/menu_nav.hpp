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

#ifndef ODIN_MENU_NAV_HPP
#define ODIN_MENU_NAV_HPP

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "odin/host/input/bindings.hpp"

namespace odin {

class Settings;

/* What a gamepad does in the menu, and the keys the user gives it: the
 * frame of focus goes up, down, left or right, A presses, B goes back,
 * X clears and Y sets back to the default (page Input), LB and RB change
 * the page, LT and RT the game. [menu] of odin.ini gives each its inputs
 * as [controls.<game>] does: "page_after = pad:rightshoulder, key:F3". */
class MenuNav {
public:
	enum Action : uint8_t {
		UP, DOWN, LEFT, RIGHT, PRESS, BACK, CLEAR, RESET, PAGE_BEFORE, PAGE_AFTER, GAME_BEFORE, GAME_AFTER, ACTIONS
	};

	MenuNav();
	/* A line of [menu] that is not right is said on stderr */
	explicit MenuNav(const Settings &settings);

	/* Its key in [menu]; the key of an action there, or nothing */
	static const char *id(Action action);
	static bool knows(std::string_view key);
	/* Its inputs by default, as odin.ini writes them */
	static std::string standard(Action action);
	const std::vector<HostInput> &inputs(Action action) const { return m_inputs[action]; }

	/* The actions an event starts: an input of theirs pressed from let go
	 * (a key held down again: the frame moves on); none from a gamepad
	 * while its Guide is held, which makes a shortcut. Another hotkey
	 * button ([hotkeys] button) works the menu as any, and holds back
	 * only the buttons that are a shortcut with it (`with`). */
	std::vector<Action> event(const SDL_Event &event);
	void hotkey(int button, const std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> &with)
	{
		m_hotkey = button;
		m_with = with;
	}
	/* Everything let go (the menu closes) */
	void reset();

private:
	std::array<std::vector<HostInput>, ACTIONS> m_inputs;
	std::array<std::vector<bool>, ACTIONS> m_held;
	std::set<SDL_JoystickID> m_guide;	/* those whose hotkey button is held */
	int m_hotkey = SDL_GAMEPAD_BUTTON_GUIDE;
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> m_with{};
};

} /* namespace odin */

#endif /* ODIN_MENU_NAV_HPP */
