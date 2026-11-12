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

/* The pointers where the host has no way to read them one by one: none
 * (evdev.cpp on Linux, rawinput.cpp on Windows) */

#include "odin/host/input/pointers.hpp"

#if !defined(__linux__) && !defined(_WIN32)

namespace odin {

struct Pointers::Backend {
	std::vector<Device> devices;
};

Pointers::Pointers(SDL_Window *window) : m_window(window), m_backend(std::make_unique<Backend>()) {}

Pointers::~Pointers() = default;

void
Pointers::poll()
{
}

const std::vector<Pointers::Device> &
Pointers::devices() const
{
	return m_backend->devices;
}

std::optional<Pointers::Aim>
Pointers::take(const std::string &)
{
	return std::nullopt;
}

void
Pointers::hold(std::span<const std::string>)
{
}

} /* namespace odin */

#endif
