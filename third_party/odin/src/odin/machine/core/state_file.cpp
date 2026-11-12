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

#include "odin/machine/core/state_file.hpp"

#include <cstring>

namespace odin {

StateFile::StateFile(const std::string &path, Mode mode)
	: m_file(std::fopen(path.c_str(), mode == Mode::LOAD ? "rb" : "wb")), m_mode(mode)
{
	char name[32] = "", found[32];

	if (!m_file)
		return;

	std::snprintf(name, sizeof(name), "odin state %08X\n", VERSION);
	const bool right = (mode == Mode::LOAD) ?
		std::fread(found, 1, sizeof(found), m_file) == sizeof(found) &&
		std::memcmp(name, found, sizeof(name)) == 0 :
		std::fwrite(name, 1, sizeof(name), m_file) == sizeof(name);

	if (!right) {
		std::fclose(m_file);
		m_file = nullptr;
	}
}

StateFile::~StateFile()
{
	if (m_file)
		std::fclose(m_file);
}

bool
StateFile::put(const void *data, uint32_t size)
{
	if (!m_file || m_mode != Mode::SAVE)
		return false;
	if (std::fwrite(&size, 1, sizeof(size), m_file) != sizeof(size))
		return false;
	return std::fwrite(data, 1, size, m_file) == size && !std::ferror(m_file);
}

bool
StateFile::get(void *data, uint32_t size)
{
	uint32_t stored;

	if (!m_file || m_mode != Mode::LOAD)
		return false;
	if (std::fread(&stored, 1, sizeof(stored), m_file) != sizeof(stored) || stored != size) {
		std::fprintf(stderr, "ERROR: state: item of another size, made by another build?\n");
		return false;
	}
	return std::fread(data, 1, size, m_file) == size && !std::ferror(m_file);
}

uint32_t
StateFile::next_size()
{
	uint32_t stored;

	if (!m_file || m_mode != Mode::LOAD)
		return 0;
	const long at = std::ftell(m_file);
	if (at < 0 || std::fread(&stored, 1, sizeof(stored), m_file) != sizeof(stored))
		stored = 0;
	if (at < 0 || std::fseek(m_file, at, SEEK_SET) != 0)
		return 0;
	return stored;
}

bool
StateFile::skip()
{
	uint32_t stored;

	if (!m_file || m_mode != Mode::LOAD)
		return false;
	return std::fread(&stored, 1, sizeof(stored), m_file) == sizeof(stored) &&
	       std::fseek(m_file, static_cast<long>(stored), SEEK_CUR) == 0;
}

} /* namespace odin */
