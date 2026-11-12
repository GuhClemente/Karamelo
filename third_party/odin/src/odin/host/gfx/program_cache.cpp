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

#include "odin/host/gfx/program_cache.hpp"
#include "odin/common/bytes.hpp"

#include <cstdio>
#include <cstring>

namespace odin {

namespace {

/* The first line, then the format and the sizes of the four parts, then
 * the parts: the driver, the two sources and the program */
constexpr char MAGIC[] = "odin program 1\n";
constexpr size_t MAGIC_SIZE = sizeof(MAGIC) - 1;
constexpr size_t HEAD_SIZE = MAGIC_SIZE + 5 * 4;

} /* namespace */

std::vector<uint8_t> program_file(const std::string &driver, const std::string &vertex,
                                  const std::string &fragment, const KeptProgram &program)
{
	const size_t sizes[4] = { driver.size(), vertex.size(), fragment.size(), program.binary.size() };
	std::vector<uint8_t> out(HEAD_SIZE);
	uint8_t *const at = out.data() + MAGIC_SIZE;

	std::memcpy(out.data(), MAGIC, MAGIC_SIZE);
	put_le32(at, program.format);
	for (unsigned i = 0; i < 4; i++)
		put_le32(at + 4 * (i + 1), static_cast<uint32_t>(sizes[i]));
	for (const std::string *text : { &driver, &vertex, &fragment })
		out.insert(out.end(), text->begin(), text->end());
	out.insert(out.end(), program.binary.begin(), program.binary.end());
	return out;
}

std::optional<KeptProgram> program_from_file(std::span<const uint8_t> file, const std::string &driver,
                                             const std::string &vertex, const std::string &fragment)
{
	if (file.size() < HEAD_SIZE || std::memcmp(file.data(), MAGIC, MAGIC_SIZE) != 0)
		return std::nullopt;

	const uint8_t *at = file.data() + MAGIC_SIZE;
	KeptProgram program;
	uint32_t sizes[4];

	program.format = le32(at);
	for (unsigned i = 0; i < 4; i++)
		sizes[i] = le32(at + 4 * (i + 1));
	if (static_cast<uint64_t>(sizes[0]) + sizes[1] + sizes[2] + sizes[3] != file.size() - HEAD_SIZE || !sizes[3])
		return std::nullopt;

	size_t offset = HEAD_SIZE;
	unsigned part = 0;

	for (const std::string *text : { &driver, &vertex, &fragment }) {
		if (sizes[part] != text->size() || std::memcmp(file.data() + offset, text->data(), text->size()) != 0)
			return std::nullopt;
		offset += sizes[part++];
	}
	program.binary.assign(file.begin() + static_cast<std::ptrdiff_t>(offset), file.end());
	return program;
}


} /* namespace odin */
