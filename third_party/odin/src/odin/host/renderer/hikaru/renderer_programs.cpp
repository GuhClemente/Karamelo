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
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/host/renderer/hikaru/renderer_programs.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace odin {

namespace {

/* The file, open after its first line, or nullptr if it is not a list
 * with that header */
std::FILE *
open_program_list(const std::string &path, const char *header)
{
	char line[64];
	std::FILE *file = std::fopen(path.c_str(), "r");

	if (!file)
		return nullptr;
	if (!std::fgets(line, sizeof(line), file) || std::strcmp(line, header) != 0) {
		std::fclose(file);
		return nullptr;
	}
	return file;
}

/* The keys of the list that `valid` takes, in its order */
template <typename Valid>
std::vector<uint64_t>
read_keys(const std::string &path, const char *header, Valid valid)
{
	std::FILE *const file = open_program_list(path, header);
	std::vector<uint64_t> keys;
	char line[32];

	if (!file)
		return keys;
	while (std::fgets(line, sizeof(line), file)) {
		char *end;
		const uint64_t key = std::strtoull(line, &end, 16);

		/* Sixteen digits and the end of the line: a line cut short
		 * by a crash is not a key. */
		if (end == line + 16 && *end == '\n' && valid(key))
			keys.push_back(key);
	}
	std::fclose(file);
	return keys;
}

} /* namespace */

uint64_t
MeshVariant::key() const
{
	uint64_t k = 0;
	unsigned at = 0;
	const auto put = [&](unsigned value, unsigned bits) {
		k |= uint64_t{value} << at;
		at += bits;
	};

	put(has_texture, 1);
	put(has_lighting, 1);
	put(shading_mode, 2);
	put(alpha_mode, 1);
	put(tex_mode, 3);
	for (const Light &light : lights) {
		put(light.on, 1);
		put(static_cast<unsigned>(light.type), 2);
		put(static_cast<unsigned>(light.attenuation), 3);
	}
	put(has_fog, 1);
	put(has_cue, 1);
	put(collect, 1);
	put(has_pattern, 1);
	put(native, 1);
	put(families, 1);
	return k;
}

std::optional<MeshVariant>
MeshVariant::from_key(uint64_t key)
{
	MeshVariant variant;
	unsigned at = 0;
	const auto take = [&](unsigned bits) {
		const unsigned value = static_cast<unsigned>(key >> at) & ((1u << bits) - 1);

		at += bits;
		return value;
	};
	bool known = true;

	variant.has_texture = take(1);
	variant.has_lighting = take(1);
	variant.shading_mode = take(2);
	variant.alpha_mode = take(1);
	variant.tex_mode = take(3);
	known = known && variant.tex_mode <= 4;
	for (Light &light : variant.lights) {
		light.on = take(1);
		light.type = static_cast<LightType>(take(2));
		const unsigned attenuation = take(3);

		light.attenuation = static_cast<LightAttenuation>(attenuation);
		known = known && attenuation <= static_cast<unsigned>(LightAttenuation::NONE);
	}
	variant.has_fog = take(1);
	variant.has_cue = take(1);
	variant.collect = take(1);
	variant.has_pattern = take(1);
	variant.native = take(1);
	variant.families = take(1);

	if (!known || variant.key() != key)
		return std::nullopt;
	return variant;
}

std::vector<uint64_t>
read_program_list(const std::string &path)
{
	return read_keys(path, PROGRAM_LIST_HEADER,
	                 [](uint64_t key) { return MeshVariant::from_key(key).has_value(); });
}

/* Blended: not the lists'; else no blend code */
std::vector<uint64_t>
read_pmx_list(const std::string &path)
{
	return read_keys(path, PMX_LIST_HEADER, [](uint64_t key) {
		return key < PMX_KEYS && ((key & 1) ? (key & 0x100) == 0 : (key & 0x7F) == 0);
	});
}

ProgramList::~ProgramList()
{
	if (m_file)
		std::fclose(m_file);
}

void
ProgramList::list_in(const std::string &path, const char *header)
{
	std::FILE *const held = open_program_list(path, header);

	if (m_file)
		std::fclose(m_file);
	if (held) {
		std::fclose(held);
		m_file = std::fopen(path.c_str(), "a");
	} else {
		m_file = std::fopen(path.c_str(), "w");
		if (m_file) {
			std::fputs(header, m_file);
			std::fflush(m_file);
		}
	}
}

void
ProgramList::add(uint64_t key)
{
	if (!m_file)
		return;
	std::fprintf(m_file, "%016" PRIx64 "\n", key);
	std::fflush(m_file);
}

} /* namespace odin */
