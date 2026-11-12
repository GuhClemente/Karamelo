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

#ifndef ODIN_X11_WINDOW_HPP
#define ODIN_X11_WINDOW_HPP

#include <SDL3/SDL.h>

namespace odin {

/* Tells the window manager of X11 that the window leaves the whole display.
 * SDL tells it only when the manager said the window was on it, and some
 * do not say it (gamescope): such a manager keeps the window as large as
 * the display. Nothing where the window is not one of X11 (Windows,
 * Wayland). */
void x11_leave_fullscreen(SDL_Window *window);

} /* namespace odin */

#endif /* ODIN_X11_WINDOW_HPP */
