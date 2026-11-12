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

#include "odin/host/ui/menu_nav.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>

#include "odin/host/input/control_panel.hpp"
#include "odin/host/app/settings.hpp"

namespace odin {

namespace {

/* The key of each action in [menu], and its inputs by default: the arrows
 * and the D-pad or the left stick move the frame, Return or A presses */
struct Standard {
	const char *id, *inputs;
};

constexpr Standard STANDARD[MenuNav::ACTIONS] = {
	{ "up", "key:Up, pad:dpup, pad:-lefty" },
	{ "down", "key:Down, pad:dpdown, pad:+lefty" },
	{ "left", "key:Left, pad:dpleft, pad:-leftx" },
	{ "right", "key:Right, pad:dpright, pad:+leftx" },
	{ "press", "key:Return, pad:a" },
	{ "back", "pad:b" },
	{ "clear", "pad:x" },
	{ "default", "pad:y" },
	{ "page_before", "pad:leftshoulder" },
	{ "page_after", "pad:rightshoulder" },
	{ "game_before", "pad:lefttrigger" },
	{ "game_after", "pad:righttrigger" },
};

/* "key:Up, pad:dpup": nothing when one is neither a key nor an input of a
 * gamepad that is pressed or not (not a whole stick, nor held with
 * another button: Guide makes the shortcuts) */
std::optional<std::vector<HostInput>>
parsed(std::string_view text)
{
	std::vector<HostInput> out;

	for (const std::string_view item : list_items(text)) {
		const auto input = HostInput::parse(item);

		if (!input || input->kind == HostInput::Kind::MOUSE || input->kind == HostInput::Kind::JOY ||
		    (input->kind == HostInput::Kind::PAD && (!input->pad.is_digital() || input->pad.with >= 0)))
			return std::nullopt;
		out.push_back(*input);
	}
	return out;
}

/* Whether the event is of that input, and then whether it holds it */
std::optional<bool>
held_by(const HostInput &input, const SDL_Event &event)
{
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		if (input.kind == HostInput::Kind::KEY && input.key == event.key.key)
			return event.key.down;
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		if (input.kind == HostInput::Kind::PAD && input.pad.type == PadInput::Type::BUTTON &&
		    input.pad.index == event.gbutton.button)
			return event.gbutton.down;
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (input.kind == HostInput::Kind::PAD && input.pad.type != PadInput::Type::BUTTON &&
		    input.pad.index == event.gaxis.axis)
			return input.pad.type == PadInput::Type::AXIS_NEGATIVE ? event.gaxis.value <= -ControlPanel::PRESSED :
			                                                          event.gaxis.value >= ControlPanel::PRESSED;
		break;
	default:
		break;
	}
	return std::nullopt;
}

} /* namespace */

MenuNav::MenuNav()
{
	for (size_t a = 0; a < ACTIONS; a++) {
		m_inputs[a] = *parsed(STANDARD[a].inputs);
		m_held[a].assign(m_inputs[a].size(), false);
	}
}

MenuNav::MenuNav(const Settings &settings) : MenuNav()
{
	for (const auto &[key, value] : settings.section("menu")) {
		for (size_t a = 0; a < ACTIONS; a++) {
			if (key != STANDARD[a].id)
				continue;
			if (const auto inputs = parsed(value)) {
				m_inputs[a] = *inputs;
				m_held[a].assign(m_inputs[a].size(), false);
			} else {
				refuse_setting("menu", key, value);
			}
		}
	}
}

const char *
MenuNav::id(Action action)
{
	return STANDARD[action].id;
}

bool
MenuNav::knows(std::string_view key)
{
	return std::any_of(std::begin(STANDARD), std::end(STANDARD),
	                   [key](const Standard &standard) { return key == standard.id; });
}

std::string
MenuNav::standard(Action action)
{
	return STANDARD[action].inputs;
}

std::vector<MenuNav::Action>
MenuNav::event(const SDL_Event &event)
{
	std::vector<Action> out;
	const bool button = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;

	const bool guide = m_hotkey == SDL_GAMEPAD_BUTTON_GUIDE;

	if (button && event.gbutton.button == m_hotkey) {
		if (event.gbutton.down)
			m_guide.insert(event.gbutton.which);
		else
			m_guide.erase(event.gbutton.which);
		if (guide)
			return out;
	} else if (button && m_guide.contains(event.gbutton.which) &&
	           (guide || (event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT && m_with[event.gbutton.button]))) {
		return out;
	} else if (guide && event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && m_guide.contains(event.gaxis.which)) {
		return out;
	}

	const bool repeat = event.type == SDL_EVENT_KEY_DOWN && event.key.repeat;

	for (size_t a = 0; a < ACTIONS; a++)
		for (size_t i = 0; i < m_inputs[a].size(); i++) {
			const std::optional<bool> held = held_by(m_inputs[a][i], event);

			if (!held)
				continue;
			if (*held && (!m_held[a][i] || (repeat && a <= RIGHT)) &&
			    std::find(out.begin(), out.end(), static_cast<Action>(a)) == out.end())
				out.push_back(static_cast<Action>(a));
			m_held[a][i] = *held;
		}
	return out;
}

void
MenuNav::reset()
{
	for (std::vector<bool> &held : m_held)
		std::fill(held.begin(), held.end(), false);
	m_guide.clear();
}

} /* namespace odin */
