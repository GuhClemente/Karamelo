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

#include "odin/host/app/settings.hpp"

#include "odin/common/repeats.hpp"
#include "odin/host/app/file_bytes.hpp"
#include "odin/host/app/user_files.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace odin {

void
refuse_setting(const std::string &section, const std::string &key, const std::string &value)
{
	std::fprintf(stderr, "ERROR: settings: [%s]: not right: %s = %s\n", section.c_str(), key.c_str(), value.c_str());
}

namespace {

/* The word on top of odin.ini */
constexpr std::string_view HEADER[] = {
	"# The settings of Odin. A line that starts with # is a setting at its value",
	"# by default: take the # away and change the value to change it. OPTIONS",
	"# tells what each one does.",
};

/* A key at a value as a comment, "# key = value": the key and the value */
std::optional<std::pair<std::string_view, std::string_view>>
commented(std::string_view line)
{
	line = trimmed(line);
	if (line.empty() || line.front() != '#')
		return std::nullopt;
	line = trimmed(line.substr(1));

	const size_t equal = line.find('=');
	const std::string_view key = trimmed(line.substr(0, equal));

	if (equal == std::string_view::npos || key.empty() || key.find_first_of(" \t#") != std::string_view::npos)
		return std::nullopt;
	return std::pair(key, trimmed(line.substr(equal + 1)));
}

/* The name of the section a trimmed line "[name]" opens; none for
 * another line */
std::optional<std::string_view>
section_of(std::string_view line)
{
	if (line.size() > 2 && line.front() == '[' && line.back() == ']')
		return trimmed(line.substr(1, line.size() - 2));
	return std::nullopt;
}

/* A trimmed line "key = value" that is no comment: the key (maybe empty)
 * and the value */
std::optional<std::pair<std::string_view, std::string_view>>
setting_of(std::string_view line)
{
	const size_t equal = line.find('=');

	if (line.empty() || line.front() == '#' || line.front() == ';' || equal == std::string_view::npos)
		return std::nullopt;
	return std::pair(trimmed(line.substr(0, equal)), trimmed(line.substr(equal + 1)));
}

/* A key Odin knows in a section; none when it knows no such key */
const Settings::Known *
known_of(const std::vector<Settings::Known> &keys, std::string_view section, std::string_view key)
{
	for (const Settings::Known &known : keys)
		if (known.section == section && known.key == key)
			return &known;
	return nullptr;
}

std::string
comment(const std::string &key, const std::string &value)
{
	return "# " + key + " =" + (value.empty() ? "" : " " + value);
}

/* The lines of a text, without their line ends */
std::vector<std::string>
split(std::string_view text)
{
	std::vector<std::string> lines;

	while (!text.empty()) {
		const size_t end = text.find('\n');

		lines.emplace_back(text.substr(0, end));
		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
	}
	return lines;
}

/* "\r" when the lines of a file end so */
std::string
line_end(const std::vector<std::string> &lines)
{
	return !lines.empty() && !lines.front().empty() && lines.front().back() == '\r' ? "\r" : "";
}

/* The text of a file; nothing, said on stderr, when it is there but cannot
 * be read */
std::optional<std::string>
read_text(const std::string &path)
{
	std::error_code error;

	if (!std::filesystem::exists(path, error))
		return std::string();

	const std::optional<std::vector<uint8_t>> bytes = read_file(path);

	if (!bytes) {
		std::fprintf(stderr, "ERROR: settings: cannot read '%s'\n", path.c_str());
		return std::nullopt;
	}
	return std::string(bytes->begin(), bytes->end());
}

/* The game of a section of one ([controls.<game>], players., sound.,
 * outputs.); none for another */
std::optional<std::string_view>
game_of(std::string_view section)
{
	const size_t dot = section.find('.');
	const std::string_view kind = section.substr(0, dot);

	if (dot == std::string_view::npos || (kind != "controls" && kind != "players" && kind != "sound" && kind != "outputs"))
		return std::nullopt;
	return section.substr(dot + 1);
}

/* A section with that key */
bool
has_key(const Settings::Lines &lines, const std::string &key)
{
	return std::any_of(lines.begin(), lines.end(),
	                   [&key](const std::pair<std::string, std::string> &line) { return line.first == key; });
}

} /* namespace */

std::string
Settings::user_file(const std::string &chosen)
{
	return user_path("ODIN_CONFIG", chosen, "odin.ini");
}

bool
Settings::load(const std::string &path)
{
	const std::optional<std::string> text = read_text(path);

	if (text)
		parse(*text, path);
	return text.has_value();
}

void
Settings::parse(std::string_view text, const std::string &origin)
{
	Lines *section = &m_sections[""];
	unsigned number = 0;

	while (!text.empty()) {
		const size_t end = text.find('\n');
		const std::string_view line = trimmed(text.substr(0, end));

		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
		number++;
		if (line.empty() || line.front() == '#' || line.front() == ';')
			continue;
		if (const std::optional<std::string_view> name = section_of(line)) {
			section = &m_sections[std::string(*name)];
			continue;
		}
		const auto setting = setting_of(line);

		if (!setting || setting->first.empty()) {
			repeats::print(stderr, "ERROR: settings: %s:%u: not a setting: %.*s\n", origin.c_str(), number,
			             static_cast<int>(line.size()), line.data());
			continue;
		}
		section->emplace_back(std::string(setting->first), std::string(setting->second));
	}
}

std::optional<std::string>
Settings::parents_section(const std::string &section) const
{
	const std::optional<std::string_view> game = game_of(section);

	if (!game)
		return std::nullopt;

	const auto parent = m_parents.find(std::string(*game));

	return parent == m_parents.end() ? std::nullopt
	                                 : std::optional(section.substr(0, section.size() - game->size()) + parent->second);
}

std::vector<std::string>
Settings::versions_sections(const std::string &section) const
{
	const std::optional<std::string_view> game = game_of(section);
	std::vector<std::string> out;

	if (game)
		for (const auto &[version, parent] : m_parents)
			if (parent == *game)
				out.push_back(section.substr(0, section.size() - game->size()) + version);
	return out;
}

Settings::Lines
Settings::section(const std::string &name) const
{
	const auto lines = [this](const std::string &of) {
		const auto found = m_sections.find(of);

		return found == m_sections.end() ? Lines{} : found->second;
	};
	const std::optional<std::string> parents = parents_section(name);
	Lines out = parents ? lines(*parents) : Lines{};
	const Lines own = lines(name);

	out.insert(out.end(), own.begin(), own.end());
	return out;
}

std::string
Settings::changed(std::string_view text, const std::string &section, const std::string &key,
                  const std::optional<std::string> &value, const std::optional<std::string> &standard)
{
	const bool ended = !text.empty() && text.back() == '\n';
	std::vector<std::string> lines = split(text);

	/* The lines of the key (its comment too when it has a value by
	 * default), and where a new one goes in the section: after its last
	 * line that is not blank, or its header (the lines before the first
	 * header are the section without a name) */
	const std::string eol = line_end(lines);
	std::vector<size_t> found;
	std::optional<size_t> place;
	bool inside = section.empty();

	if (inside)
		place = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string_view line = trimmed(lines[i]);

		if (const std::optional<std::string_view> name = section_of(line)) {
			inside = *name == section;
			if (inside)
				place = i + 1;
			continue;
		}
		if (!inside || line.empty())
			continue;
		place = i + 1;
		if (const auto setting = setting_of(line); setting && setting->first == key)
			found.push_back(i);
		else if (const auto held = commented(line); standard && held && held->first == key)
			found.push_back(i);
	}

	const bool one = value || standard;
	const std::string setting = value ? key + " = " + *value + eol : standard ? comment(key, *standard) + eol : "";

	if (one && !found.empty()) {
		lines[found.back()] = setting;
		found.pop_back();
	} else if (one && place) {
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*place), setting);
	} else if (one) {
		if (!lines.empty() && !trimmed(lines.back()).empty())
			lines.push_back(eol);
		lines.push_back("[" + section + "]" + eol);
		lines.push_back(setting);
	}
	for (auto i = found.rbegin(); i != found.rend(); i++)
		lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*i));

	std::string out;

	for (size_t i = 0; i < lines.size(); i++) {
		out += lines[i];
		if (i + 1 < lines.size() || ended || one)
			out += '\n';
	}
	return out;
}

std::string
Settings::completed(std::string_view text, const std::vector<Known> &keys)
{
	std::vector<std::string> lines = split(text);
	const std::string eol = line_end(lines);

	if (std::none_of(lines.begin(), lines.end(), [](const std::string &line) { return trimmed(line) == HEADER[0]; })) {
		std::vector<std::string> top;

		for (const std::string_view line : HEADER)
			top.push_back(std::string(line) + eol);
		top.push_back(eol);
		lines.insert(lines.begin(), top.begin(), top.end());
	}

	/* Where each section ends (after its last line that is not blank, or
	 * its header), the keys it has; the comments of a value by default made
	 * right */
	std::map<std::string, size_t> ends;
	std::map<std::string, std::set<std::string>> has;
	std::string inside;

	for (size_t i = 0; i < lines.size(); i++) {
		const std::string_view line = trimmed(lines[i]);

		if (const std::optional<std::string_view> name = section_of(line)) {
			inside = *name;
			ends[inside] = i + 1;
			continue;
		}
		if (line.empty())
			continue;
		ends[inside] = i + 1;
		if (const auto held = commented(line)) {
			const Known *const k = known_of(keys, inside, held->first);

			if (k && held->second != k->value)
				lines[i] = comment(k->key, k->value) + eol;
			if (k)
				has[inside].insert(k->key);
		} else if (const auto setting = setting_of(line)) {
			has[inside].insert(std::string(setting->first));
		}
	}

	/* What each section lacks, and the sections the file lacks, in the
	 * order of the keys */
	std::map<std::string, std::vector<std::string>> lacking;
	std::vector<std::pair<std::string, std::vector<std::string>>> missing;

	for (const Known &k : keys) {
		if (!ends.contains(k.section)) {
			if (missing.empty() || missing.back().first != k.section)
				missing.push_back({ k.section, {} });
			missing.back().second.push_back(comment(k.key, k.value) + eol);
		} else if (!has[k.section].contains(k.key)) {
			lacking[k.section].push_back(comment(k.key, k.value) + eol);
		}
	}

	/* From the bottom up, so that the places above stay right */
	std::vector<std::pair<size_t, std::string>> places;

	for (const auto &[section, added] : lacking)
		places.push_back({ ends[section], section });
	std::sort(places.begin(), places.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	for (const auto &[at, section] : places)
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), lacking[section].begin(), lacking[section].end());
	for (const auto &[section, added] : missing) {
		if (!lines.empty() && !trimmed(lines.back()).empty())
			lines.push_back(eol);
		lines.push_back("[" + section + "]" + eol);
		lines.insert(lines.end(), added.begin(), added.end());
	}

	std::string out;

	for (const std::string &line : lines)
		out += line + '\n';
	return out;
}

void
Settings::set(const std::string &section, const std::string &key, const std::optional<std::string> &value)
{
	const std::string into = parents_section(section).value_or(section);
	const auto take_out = [&key](Lines &lines) {
		std::erase_if(lines, [&key](const std::pair<std::string, std::string> &line) { return line.first == key; });
	};

	for (const std::string &own : versions_sections(into))
		if (const auto lines = m_sections.find(own); lines != m_sections.end())
			take_out(lines->second);
	Lines &lines = m_sections[into];

	take_out(lines);
	if (value)
		lines.emplace_back(key, *value);
}

bool
Settings::change(const std::string &path, const std::string &section, const std::string &key,
                 const std::optional<std::string> &value)
{
	const std::optional<std::string> text = read_text(path);
	std::optional<std::string> standard;

	if (!text)
		return false;

	const std::string into = parents_section(section).value_or(section);

	if (const Known *const known = known_of(m_known, into, key))
		standard = known->value;

	std::string out = changed(*text, into, key, value, standard);

	for (const std::string &own : versions_sections(into))
		if (const auto lines = m_sections.find(own); lines != m_sections.end() && has_key(lines->second, key))
			out = changed(out, own, key, std::nullopt);
	return write(path, out);
}

bool
Settings::complete(const std::string &path)
{
	const std::optional<std::string> text = read_text(path);

	if (!text)
		return false;

	const std::string out = completed(*text, m_known);

	return out == *text || write(path, out);
}

bool
Settings::write(const std::string &path, const std::string &out)
{
	if (!write_kept(path, std::span(reinterpret_cast<const uint8_t *>(out.data()), out.size())))
		return false;
	m_sections.clear();
	parse(out, path);
	return true;
}

} /* namespace odin */
