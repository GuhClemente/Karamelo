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

#ifndef ODIN_LOG_FILE_HPP
#define ODIN_LOG_FILE_HPP

#include <cstdint>
#include <string>

namespace odin {

/* What Odin says, on its terminal as before and in a file too (path()), the
 * one of the launch before kept as odin-previous.log. stdout and stderr each go through a pipe that a thread of its own
 * empties into the terminal and the file, so that all is caught, what the
 * libraries say too, and stdout and stderr stay apart on the terminal.
 * The file stops at a size (the detailed log says a lot); the terminal
 * still gets all. A crash (abort, a bad access) gives the threads a moment
 * to write its last lines before Odin goes. */
class LogFile {
public:
	static constexpr uint64_t MOST_BYTES = 64ull << 20;

	/* The file: ODIN_LOG=<file>, else odin.log in the folder chosen
	 * ([paths] log), else in the data folder of Odin (beside saves/);
	 * empty with ODIN_LOG=none or without a data folder */
	static std::string path(const std::string &folder = {});
	/* Where the one before goes */
	static std::string previous(const std::string &path);
	/* Windows: the console a double click opened for Odin alone is let
	 * go, before start(); a console Odin shares (a prompt) stays */
	static void leave_own_console();
	/* To be called before anything is written to stdout. False, said on
	 * stderr, when the file cannot be written: Odin says it on its
	 * terminal alone. */
	static bool start(const std::string &path, uint64_t most = MOST_BYTES);
	/* All that was said is in the file and on the terminal, which takes
	 * stdout and stderr back; at exit, by itself */
	static void stop();
};

} /* namespace odin */

#endif /* ODIN_LOG_FILE_HPP */
