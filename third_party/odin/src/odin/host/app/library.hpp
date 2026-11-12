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

#ifndef ODIN_LIBRARY_HPP
#define ODIN_LIBRARY_HPP

#include <string>

#include "odin/host/video/display.hpp"
#include "odin/host/app/games.hpp"
#include "odin/host/input/keyboard.hpp"
#include "odin/host/ui/menu_session.hpp"
#include "odin/host/app/options.hpp"
#include "odin/host/app/settings.hpp"

namespace odin {

/* The library, Odin started without a game, until one is chosen there or
 * by Open... (its name, and the folder of its archive: empty, that of the
 * games), or Odin is to quit (no name). The game played before is chosen
 * again; `failed`, one that did not start, says so. The bar is over it, at
 * all times; the menu opens over it as over a game, its page Input on the
 * game chosen. What they change is kept in odin.ini and taken at once. */
Opened library(const Options &command, const GameList &games, Settings &settings, const std::string &settings_file,
               Display &display, Keyboard &keys, Pointers *pointers, const std::string &last,
               const std::string &failed);

} /* namespace odin */

#endif /* ODIN_LIBRARY_HPP */
