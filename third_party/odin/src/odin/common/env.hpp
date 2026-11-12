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

#ifndef ODIN_ENV_HPP
#define ODIN_ENV_HPP

#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

namespace odin {

/* A variable of the environment as a switch: TRUE or 1, FALSE or 0. */
inline bool
env_flag(const char *name, bool fallback = false)
{
	const char *const value = std::getenv(name);

	if (value) {
		if (!strcasecmp(value, "TRUE") || !std::strcmp(value, "1"))
			return true;
		if (!strcasecmp(value, "FALSE") || !std::strcmp(value, "0"))
			return false;
	}
	return fallback;
}

/* And as a number */
inline int
env_int(const char *name, int fallback)
{
	const char *const value = std::getenv(name);

	return value ? std::atoi(value) : fallback;
}

/* And as text; empty when it is not set */
inline std::string
env_text(const char *name)
{
	const char *const value = std::getenv(name);

	return value ? value : std::string();
}

/* The folder of the user, HOME, or USERPROFILE where there is no HOME; empty
 * when neither is set. */
inline std::string
home_dir()
{
	for (const char *name : { "HOME", "USERPROFILE" })
		if (std::string value = env_text(name); !value.empty())
			return value;
	return {};
}

} /* namespace odin */

#endif /* ODIN_ENV_HPP */
