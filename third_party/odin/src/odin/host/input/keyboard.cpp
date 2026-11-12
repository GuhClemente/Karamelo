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

#include "odin/host/input/keyboard.hpp"

#include <algorithm>

namespace odin {

namespace {

bool
has(const std::vector<SDL_Keycode> &keys, SDL_Keycode key)
{
	return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void
take_out(std::vector<SDL_Keycode> &keys, SDL_Keycode key)
{
	keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
}

} /* namespace */

void
Keyboard::set(SDL_Keycode key, bool down)
{
	if (has(m_swallowed, key)) {
		if (!down)
			take_out(m_swallowed, key);
		return;
	}
	if (down) {
		if (!has(m_held, key)) {
			m_held.push_back(key);
			m_fresh.push_back(key);
		}
		take_out(m_release, key);
	} else if (has(m_fresh, key)) {
		if (!has(m_release, key))
			m_release.push_back(key);
	} else {
		take_out(m_held, key);
	}
}

bool
Keyboard::down(SDL_Keycode key) const
{
	return has(m_held, key);
}

void
Keyboard::settle()
{
	for (const SDL_Keycode key : m_release)
		take_out(m_held, key);
	m_release.clear();
	m_fresh.clear();
}

void
Keyboard::swallow()
{
	/* A key already released waits for no release */
	for (const SDL_Keycode key : m_held)
		if (!has(m_release, key) && !has(m_swallowed, key))
			m_swallowed.push_back(key);
	m_held.clear();
	m_fresh.clear();
	m_release.clear();
}

} /* namespace odin */
