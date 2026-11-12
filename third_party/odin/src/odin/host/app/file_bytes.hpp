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

#ifndef ODIN_FILE_BYTES_HPP
#define ODIN_FILE_BYTES_HPP

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace odin {

/* All the bytes of a file; nothing when it cannot be opened, or when its
 * reading fails before its end */
inline std::optional<std::vector<uint8_t>>
read_file(const std::string &path)
{
	std::FILE *const file = std::fopen(path.c_str(), "rb");
	std::vector<uint8_t> bytes;
	uint8_t chunk[65536];
	size_t got;

	if (!file)
		return std::nullopt;
	while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
		bytes.insert(bytes.end(), chunk, chunk + got);
	const bool failed = std::ferror(file) != 0;

	std::fclose(file);
	if (failed)
		return std::nullopt;
	return bytes;
}

} /* namespace odin */

#endif /* ODIN_FILE_BYTES_HPP */
