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

#ifndef ODIN_REPEATS_HPP
#define ODIN_REPEATS_HPP

/* What repeats in the log (odin.log keeps all the terminal gets). A program
 * that meets what is not emulated meets it again at every frame or every
 * access: a line is said once a game, and a kind of message, the format it
 * comes from, MOST_OF_A_KIND times at most. The detailed log (-v) or
 * ODIN_LOG_REPEATS=1 says them all. The end of a game tells how many lines
 * were left out. */

#include "odin/common/env.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace odin::repeats {

inline constexpr unsigned MOST_OF_A_KIND = 8;

struct Said {
	std::mutex lock;
	bool all = false;
	std::unordered_set<std::string> lines;
	std::unordered_map<const void *, unsigned> kinds;
	unsigned long left_out = 0;
};

/* Never destroyed: the drawing thread may tell an error after main */
inline Said &
said()
{
	static Said *const s = new Said;

	return *s;
}

/* A game starts, or the library: nothing said yet. */
inline void
begin(bool detailed)
{
	Said &s = said();
	const std::lock_guard<std::mutex> hold(s.lock);

	s.all = detailed || env_flag("ODIN_LOG_REPEATS");
	s.lines.clear();
	s.kinds.clear();
	s.left_out = 0;
}

/* Every line is said: what only the detailed log prints is printed */
inline bool
detailed()
{
	Said &s = said();
	const std::lock_guard<std::mutex> hold(s.lock);

	return s.all;
}

/* Whether `line`, a message of the kind `kind`, is said */
inline bool
say(const void *kind, std::string_view line)
{
	Said &s = said();
	const std::lock_guard<std::mutex> hold(s.lock);

	if (s.all)
		return true;
	if (s.lines.count(std::string(line))) {
		s.left_out++;
		return false;
	}

	unsigned &of_kind = s.kinds[kind];

	if (of_kind >= MOST_OF_A_KIND) {
		s.left_out++;
		return false;
	}
	of_kind++;
	s.lines.emplace(line);
	return true;
}

/* fputs, unless the line repeats on stderr: a file of its own gets them
 * all */
inline void
put(std::FILE *out, const void *kind, const char *line)
{
	if (out != stderr || say(kind, line))
		std::fputs(line, out);
}

/* fprintf, unless the line repeats */
inline void print(std::FILE *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

inline void
print(std::FILE *out, const char *fmt, ...)
{
	char line[512];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (say(fmt, line))
		std::fputs(line, out);
}

/* "ERROR: " and the line on stderr, unless it repeats */
inline void error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

inline void
error(const char *fmt, ...)
{
	char what[512];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);
	if (say(fmt, what))
		std::fputs(("ERROR: " + std::string(what) + "\n").c_str(), stderr);
}

/* A game ends: how many lines it left out */
inline void
end()
{
	Said &s = said();
	const std::lock_guard<std::mutex> hold(s.lock);

	if (s.left_out)
		std::printf("log: %lu lines that repeat left out (all of them: -v or ODIN_LOG_REPEATS=1)\n", s.left_out);
	s.left_out = 0;
}

} /* namespace odin::repeats */

#endif /* ODIN_REPEATS_HPP */
