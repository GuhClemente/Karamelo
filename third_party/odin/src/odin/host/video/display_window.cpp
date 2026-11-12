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

/* The window of Odin: its size, the whole display or not, how its frames
 * wait for the display. */

#include "odin/host/video/display.hpp"

#include <cstdio>

#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/host/video/x11_window.hpp"

namespace odin {

/* The window `times` the screen of the board, or of that size: not larger
 * than what the display has room for, borders included, and of that
 * shape. */
void
Display::set_window(unsigned times, bool fullscreen)
{
	set_window_size(static_cast<int>(Renderer::WIDTH * times), static_cast<int>(Renderer::HEIGHT * times), fullscreen);
}

void
Display::set_window_size(int w, int h, bool fullscreen)
{
	int top = 0, left = 0, bottom = 0, right = 0;
	SDL_Rect room;

	finish();

	SDL_GetWindowBordersSize(m_window, &top, &left, &bottom, &right);
	if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(m_window), &room)) {
		room.w -= left + right;
		room.h -= top + bottom;
		if (w > room.w) {
			h = h * room.w / w;
			w = room.w;
		}
		if (h > room.h) {
			w = w * room.h / h;
			h = room.h;
		}
	}
	SDL_SetWindowSize(m_window, w, h);
	SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	m_given_w = w;
	m_given_h = h;
	m_by_hand = false;
	if (fullscreen) {
		SDL_SetWindowFullscreen(m_window, true);
		m_fullscreen_asked = true;
	}
	/* Done before the size is read again: not taken for the user's */
	SDL_SyncWindow(m_window);
}

/* On the whole display: as the system says, or asked and as large as the
 * display (covers_display) */
bool
Display::fullscreen() const
{
	int w = 0, h = 0;

	if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN)
		return true;
	return m_fullscreen_asked && SDL_GetWindowSize(m_window, &w, &h) && covers_display(w, h);
}

/* A window of that size as large as its display: it takes the whole
 * display, whether the system says so or not (gamescope does not) */
bool
Display::covers_display(int w, int h) const
{
	SDL_Rect whole{};

	return SDL_GetDisplayBounds(SDL_GetDisplayForWindow(m_window), &whole) && w >= whole.w && h >= whole.h;
}

void
Display::set_fullscreen(bool on)
{
	if (on != fullscreen())
		toggle_fullscreen();
}

void
Display::resize(unsigned times)
{
	if (!fullscreen())
		set_window(times, false);
}

void
Display::resize_to(unsigned w, unsigned h)
{
	if (!fullscreen())
		set_window_size(static_cast<int>(w), static_cast<int>(h), false);
}

/* A window as large as its display (covers_display) takes the whole
 * display */
bool
Display::hand_size(unsigned &w, unsigned &h)
{
	int now_w = 0, now_h = 0;
	const bool sized = SDL_GetWindowSize(m_window, &now_w, &now_h) && now_w > 0 && now_h > 0;
	const bool everything = covers_display(now_w, now_h);

	if (sized && !everything &&
	    !(SDL_GetWindowFlags(m_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) &&
	    (now_w != m_given_w || now_h != m_given_h)) {
		m_given_w = now_w;
		m_given_h = now_h;
		m_by_hand = true;
	}
	w = static_cast<unsigned>(m_given_w);
	h = static_cast<unsigned>(m_given_h);
	return m_by_hand;
}

void
Display::set_vsync(Vsync vsync)
{
	const float hz = refresh();
	const bool keeps_up = hz == 0.0f || hz >= LEAST_REFRESH;
	const bool late = vsync == Vsync::ON && keeps_up, on_time = late || vsync == Vsync::ADAPTIVE;

	m_vsync = vsync;
	run([this, on_time, late] {
		m_wait_on_time = on_time;
		m_wait_late = late;
		m_waits = on_time;
		if (!m_graphics->set_waits(on_time))
			std::fprintf(stderr, "ERROR: screen: no vsync: '%s'\n", SDL_GetError());
	});
	if (vsync == Vsync::ON && !keeps_up)
		std::printf("screen: no vsync, the display shows %.2f frames a second\n", static_cast<double>(hz));
	else if (vsync != Vsync::OFF)
		std::printf("screen: the frames%s wait for the display (%.2f Hz)\n", vsync == Vsync::ON ? "" : " on time",
		            static_cast<double>(hz));
}

float
Display::refresh() const
{
	const SDL_DisplayMode *const mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(m_window));

	return mode ? mode->refresh_rate : 0.0f;
}

void
Display::display_size(unsigned &w, unsigned &h) const
{
	const SDL_DisplayMode *const mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(m_window));

	w = mode ? static_cast<unsigned>(static_cast<float>(mode->w) * mode->pixel_density) : 0;
	h = mode ? static_cast<unsigned>(static_cast<float>(mode->h) * mode->pixel_density) : 0;
}

/* The size the user gave the window is taken before it goes; back from
 * the whole display, the window has its size again. A manager that never
 * said the window was on the whole display is told it leaves it by Odin:
 * SDL tells it nothing then */
void
Display::toggle_fullscreen()
{
	const bool on = fullscreen(), said = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
	unsigned w = 0, h = 0;

	finish();
	hand_size(w, h);
	SDL_SetWindowFullscreen(m_window, !on);
	if (on && !said)
		x11_leave_fullscreen(m_window);
	m_fullscreen_asked = !on;
	if (on)
		SDL_SetWindowSize(m_window, static_cast<int>(w), static_cast<int>(h));
	SDL_SyncWindow(m_window);
}

} /* namespace odin */
