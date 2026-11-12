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

#include "odin/host/video/picture_preset.hpp"
#include "odin/host/app/file_bytes.hpp"
#include "odin/host/app/settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>

namespace odin::preset {

namespace {

std::string
read_text(const std::filesystem::path &path, bool &ok)
{
	const std::optional<std::vector<uint8_t>> bytes = read_file(path.string());

	ok = bytes.has_value();
	return ok ? std::string(bytes->begin(), bytes->end()) : std::string();
}

std::string
key_or(const std::map<std::string, std::string> &keys, const std::string &name, const std::string &otherwise)
{
	const auto key = keys.find(name);

	return key == keys.end() ? otherwise : key->second;
}

} /* namespace */

std::map<std::string, std::string>
read_keys(const std::string &text)
{
	std::map<std::string, std::string> keys;
	size_t at = 0;

	while (at < text.size()) {
		size_t end = text.find('\n', at);

		end = end == std::string::npos ? text.size() : end;
		const std::string line(trimmed(std::string_view(text).substr(at, end - at)));

		at = end + 1;
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if (line[0] == '[')
			break;

		const size_t name_end = line.find_first_of("= ");
		const std::string name = line.substr(0, name_end);
		std::string value = name_end == std::string::npos ? std::string() : line.substr(name_end);

		value = value.substr(std::min(value.find_first_not_of("= "), value.size()));
		value = value.substr(0, value.find('#'));
		if (!value.empty() && value.front() == '"')
			value.erase(0, 1);
		value = value.substr(0, value.find('"'));
		value = std::string(trimmed(value));
		if (!value.empty())
			keys.emplace(name, value);
	}
	return keys;
}

bool
key_on(const std::map<std::string, std::string> &keys, const std::string &name)
{
	const auto key = keys.find(name);

	if (key == keys.end())
		return false;
	if (key->second == "true" || key->second == "false")
		return key->second == "true";
	return std::atoi(key->second.c_str()) != 0;
}

std::string
part(const std::string &source, const char *which)
{
	constexpr std::string_view VERSION = "#version";
	std::string text = source, version;

	for (const char *start : { "#pragma parameter", "#define texture" })
		for (size_t at = text.find(start); at != std::string::npos; at = text.find(start)) {
			const size_t end = std::min(text.find('\n', at), text.size());

			text.replace(at, end - at, end - at, ' ');
		}
	if (const size_t at = text.find(VERSION); at != std::string::npos) {
		size_t end = text.find_first_not_of(" \t", at + VERSION.size());

		end = text.find_first_of(" \t\r\n", end);
		version = text.substr(at, end - at);
		text.replace(at, end - at, end - at, ' ');
	}
	return version + "\n\n#extension GL_ARB_shading_language_420pack : enable\n#define " + which +
	       "\n#define PARAMETER_UNIFORM\n\n#line 1\n" + text;
}

void
declared(const std::string &source, std::vector<std::pair<std::string, float>> &parameters)
{
	static constexpr char PRAGMA[] = "#pragma parameter";

	for (size_t at = source.find(PRAGMA); at != std::string::npos; at = source.find(PRAGMA, at + 1)) {
		const std::string line = source.substr(at, source.find('\n', at) - at);
		const size_t name_at = line.find_first_not_of(" \t", sizeof(PRAGMA) - 1);
		const size_t name_end = line.find_first_of(" \t", name_at);
		const size_t open = line.find('"', name_end), close = line.find('"', open + 1);

		if (name_at == std::string::npos || close == std::string::npos)
			continue;

		const std::string name = line.substr(name_at, name_end - name_at);
		const size_t described = close - open - 1;
		bool known = false;

		for (const auto &[have, value] : parameters)
			known = known || have == name;
		if (!known && name.size() <= LONGEST_NAME && described > 0 && described <= LONGEST_NAME)
			parameters.emplace_back(name, std::strtof(line.c_str() + close + 1, nullptr));
	}
}

namespace {
/* What a pass draws: read as the next pass says, with its own wrap; at
 * what scale */
void
read_drawing(const std::map<std::string, std::string> &keys, int i, Plan::Pass &pass)
{
	const std::string n = std::to_string(i);
	const std::string wrap = key_or(keys, "wrap_mode" + n, "");

	pass.linear = key_on(keys, "filter_linear" + std::to_string(i + 1));
	pass.is_float = key_on(keys, "float_framebuffer" + n);
	pass.wrap = wrap == "repeat" ? Plan::Wrap::REPEAT : wrap == "mirrored_repeat" ? Plan::Wrap::MIRRORED :
	            wrap == "clamp_to_edge" ? Plan::Wrap::EDGE : Plan::Wrap::BORDER;
	for (size_t axis = 0; axis < 2; axis++) {
		const std::string ax = axis ? "y" : "x";
		const std::string type = key_or(keys, "scale_type" + n, key_or(keys, "scale_type_" + ax + n, "source"));

		pass.scale_type[axis] = type == "viewport" ? Plan::Scale::VIEWPORT :
		                        type == "absolute" ? Plan::Scale::ABSOLUTE : Plan::Scale::SOURCE;
		pass.scale[axis] = std::strtof(key_or(keys, "scale" + n, key_or(keys, "scale_" + ax + n, "1.0")).c_str(),
		                               nullptr);
	}
}

/* The values the preset sets */
void
read_values(const std::map<std::string, std::string> &keys, Plan &plan)
{
	for (std::string names = key_or(keys, "parameters", ""); !names.empty();) {
		const size_t end = names.find(';');
		const std::string name = names.substr(0, end);

		for (auto &[have, value] : plan.parameters)
			if (have == name)
				value = std::strtof(key_or(keys, name, "0").c_str(), nullptr);
		names = end == std::string::npos ? std::string() : names.substr(end + 1);
	}
}

} /* namespace */

std::optional<Plan>
read_plan(const std::string &preset)
{
	bool ok;
	const std::string text = read_text(preset, ok);

	if (!ok) {
		std::fprintf(stderr, "ERROR: filter: cannot read '%s'\n", preset.c_str());
		return std::nullopt;
	}

	const std::map<std::string, std::string> keys = read_keys(text);
	const std::filesystem::path folder = std::filesystem::path(preset).parent_path();
	const int count = std::atoi(key_or(keys, "shaders", "0").c_str());
	Plan plan;

	if (count <= 0) {
		std::fprintf(stderr, "ERROR: filter: '%s' has no passes\n", preset.c_str());
		return std::nullopt;
	}
	plan.passes.resize(static_cast<size_t>(count));
	for (int i = 0; i < count; i++) {
		const std::string n = std::to_string(i);
		Plan::Pass &pass = plan.passes[static_cast<size_t>(i)];

		pass.file = (folder / key_or(keys, "shader" + n, "")).string();
		pass.source = read_text(pass.file, ok);
		if (!ok) {
			std::fprintf(stderr, "ERROR: filter: cannot read '%s'\n", pass.file.c_str());
			return std::nullopt;
		}
		declared(pass.source, plan.parameters);
		read_drawing(keys, i, pass);
	}

	const Plan::Pass &last = plan.passes.back();
	if (last.scale_type[0] != Plan::Scale::SOURCE || last.scale_type[1] != Plan::Scale::SOURCE ||
	    last.scale[0] != 1.0f || last.scale[1] != 1.0f) {
		std::fprintf(stderr, "ERROR: filter: the last pass of '%s' has a scale: not taken\n", preset.c_str());
		return std::nullopt;
	}
	read_values(keys, plan);
	return plan;
}

} /* namespace odin::preset */
