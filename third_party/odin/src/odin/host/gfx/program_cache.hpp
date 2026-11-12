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

#ifndef ODIN_PROGRAM_CACHE_HPP
#define ODIN_PROGRAM_CACHE_HPP

/* A program the graphics driver linked, kept in a file for the next
 * launches (glGetProgramBinary): with the driver that made it and the two
 * sources it was made from, word for word. It is given back only to the
 * same driver for the same sources, which the driver checks again. No
 * OpenGL here. */

#include "odin/host/app/file_bytes.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace odin {

struct KeptProgram {
	uint32_t format;		/* as glGetProgramBinary gave it */
	std::vector<uint8_t> binary;
};

/* The bytes of the file of a program */
std::vector<uint8_t> program_file(const std::string &driver, const std::string &vertex,
                                  const std::string &fragment, const KeptProgram &program);

/* The program a file holds, when it was made by that driver from those
 * sources; nothing for any other file, or one cut short. */
std::optional<KeptProgram> program_from_file(std::span<const uint8_t> file, const std::string &driver,
                                             const std::string &vertex, const std::string &fragment);

} /* namespace odin */

#endif /* ODIN_PROGRAM_CACHE_HPP */
