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


#ifndef ODIN_STOP_MESSAGE_HPP
#define ODIN_STOP_MESSAGE_HPP

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace odin {

/* What a board says when it cannot go on, before Odin quits (stop_fatal):
 * the application shows it to the player when it set where to */
using StopMessage = void (*)(const char *text);
inline StopMessage stop_message = nullptr;

/* The emulated machine cannot go on: why, on stderr after "FATAL: " and in
 * the stop message, then Odin stops */
[[noreturn]] inline void stop_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

[[noreturn]] inline void
stop_fatal(const char *fmt, ...)
{
	char text[1024];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	std::fprintf(stderr, "FATAL: %s\n", text);
	std::fflush(stderr);
	if (stop_message)
		stop_message(text);
	std::abort();
}

} /* namespace odin */

#endif /* ODIN_STOP_MESSAGE_HPP */
