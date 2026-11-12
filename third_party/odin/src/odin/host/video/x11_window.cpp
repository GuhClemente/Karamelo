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

/* The word Odin sends the window manager of X11 itself, apart: the macros
 * of Xlib (None, Bool, Display...) stay in this file. */

#include "odin/host/video/x11_window.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <X11/Xlib.h>
#endif

namespace odin {

#if !defined(_WIN32) && !defined(__APPLE__)

/* As SDL sends it (SDL_x11window.c): _NET_WM_STATE, remove FULLSCREEN, to
 * the root window */
void
x11_leave_fullscreen(SDL_Window *window)
{
	const SDL_PropertiesID props = SDL_GetWindowProperties(window);
	auto *const display = static_cast<::Display *>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
	                                                                      nullptr));
	const auto xwindow = static_cast<::Window>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	constexpr long REMOVE = 0;
	XEvent e{};

	if (!display || !xwindow)
		return;
	e.xclient.type = ClientMessage;
	e.xclient.window = xwindow;
	e.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
	e.xclient.format = 32;
	e.xclient.data.l[0] = REMOVE;
	e.xclient.data.l[1] = static_cast<long>(XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False));
	XSendEvent(display, XDefaultRootWindow(display), False, SubstructureNotifyMask | SubstructureRedirectMask, &e);
	XFlush(display);
}

#else

void
x11_leave_fullscreen(SDL_Window *)
{
}

#endif

} /* namespace odin */
