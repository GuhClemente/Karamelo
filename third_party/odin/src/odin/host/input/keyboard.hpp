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

#ifndef ODIN_KEYBOARD_HPP
#define ODIN_KEYBOARD_HPP

#include <vector>

#include <SDL3/SDL.h>

namespace odin {

/* The keys held. A key pressed and released before a frame saw it stays
 * held until that frame has run (settle), so that no press is lost. What
 * each key does on the cabinet is in bindings.cpp. */
class Keyboard {
public:
	/* A key went down or up. */
	void set(SDL_Keycode key, bool down);
	bool down(SDL_Keycode key) const;
	const std::vector<SDL_Keycode> &held() const { return m_held; }

	/* A frame has seen the keys. */
	void settle();
	/* The keys down now are let go of, and pressed again only once
	 * released: the key that closes the menu is not the game's. */
	void swallow();

private:
	std::vector<SDL_Keycode> m_held;
	std::vector<SDL_Keycode> m_fresh;	/* down since the last frame */
	std::vector<SDL_Keycode> m_release;	/* up before a frame saw them */
	std::vector<SDL_Keycode> m_swallowed;	/* down when swallowed, until released */
};

/* The key a name gives (odin.ini, the list of games) as the key events
 * give it: on a French keyboard, "5" is the key 5, not '(' */
inline SDL_Keycode
key_named(const char *name)
{
	const SDL_Keycode key = SDL_GetKeyFromName(name);
	/* A keyboard without latin letters (Russian, Greek...): the names and
	 * the events both read those of a QWERTY, the name as it is */
	const bool own_letters = SDL_GetKeyFromScancode(SDL_SCANCODE_A, SDL_KMOD_NONE, true) ==
	                         SDL_GetKeyFromScancode(SDL_SCANCODE_A, SDL_KMOD_NONE, false);
	/* The keymaps of Windows and macOS hold keys without a keycode */
	const SDL_Scancode scancode =
		key == SDLK_UNKNOWN || !own_letters ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromKey(key, nullptr);

	return scancode == SDL_SCANCODE_UNKNOWN ? key : SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true);
}

} /* namespace odin */

#endif /* ODIN_KEYBOARD_HPP */
