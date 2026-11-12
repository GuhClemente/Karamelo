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

#ifndef ODIN_SETTINGS_HPP
#define ODIN_SETTINGS_HPP

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace odin {

/* A text without the spaces, tabs and \r (a line of Windows) at its ends */
inline std::string_view
trimmed(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

/* The items of a list "a, b, c", each trimmed; the list ends where only
 * spaces are left */
inline std::vector<std::string_view>
list_items(std::string_view text)
{
	std::vector<std::string_view> items;

	while (!trimmed(text).empty()) {
		const size_t comma = text.find(',');

		items.push_back(trimmed(text.substr(0, comma)));
		text.remove_prefix(comma == std::string_view::npos ? text.size() : comma + 1);
	}
	return items;
}

/* A value "on" or "off"; none when it is neither */
inline std::optional<bool>
on_off_named(std::string_view value)
{
	if (value == "on" || value == "off")
		return value == "on";
	return std::nullopt;
}

/* A line of odin.ini whose value is not right, said on stderr; Odin takes
 * the value by default instead */
void refuse_setting(const std::string &section, const std::string &key, const std::string &value);

/* The settings of the user, odin.ini: "[section]" lines, then
 * "key = value" lines; a line that starts with # or ; is a comment. A key
 * keeps the order of the file. Each key Odin knows is in the file: as a
 * comment at its value by default, "# key = value", until the user changes
 * it. A version of a game keeps its settings in the sections of its parent
 * ([controls.<parent>], players., sound., outputs.); the lines of a section
 * of its own (an older Odin wrote one) still count, after its parent's. */
class Settings {
public:
	using Lines = std::vector<std::pair<std::string, std::string>>;
	/* A key Odin knows, in its section, with its value by default */
	struct Known {
		std::string section, key, value;
	};

	/* ODIN_CONFIG=<file>; none with ODIN_CONFIG=none; else the one chosen
	 * (-c), else odin.ini in the data folder of Odin, beside saves/. Empty
	 * when there is none. */
	static std::string user_file(const std::string &chosen = {});

	/* A missing file is an empty one. A line that is neither a section, a
	 * key nor a comment is said on stderr and skipped. False, said on
	 * stderr, when the file is there but cannot be read. */
	bool load(const std::string &path);
	void parse(std::string_view text, const std::string &origin);

	/* The versions of the games, with their parent */
	void versions(std::map<std::string, std::string> parents) { m_parents = std::move(parents); }

	/* The lines of a section; none when the file has no such section. A
	 * section of a version: those of its parent's, then its own. */
	Lines section(const std::string &name) const;

	/* The keys Odin knows, in the order the file shows them */
	void know(std::vector<Known> keys) { m_known = std::move(keys); }
	/* The file made whole: each key Odin knows that it lacks, as a comment
	 * at its value by default, after the last line of its section (a
	 * section it lacks at the end), and such a comment of a value by
	 * default that changed since made right; a word on top. Written as a
	 * change is, only when it changed. */
	bool complete(const std::string &path);
	static std::string completed(std::string_view text, const std::vector<Known> &keys);

	/* A key of a section given a value, or taken out (nullopt: the value
	 * by default then; a key Odin knows goes back to its comment): the
	 * file is read again, changed and written (write). False, said on
	 * stderr, if it cannot be written. A section of a version: the key of
	 * its parent's. The key of a game is taken out of its versions' own
	 * sections. */
	bool change(const std::string &path, const std::string &section, const std::string &key,
	            const std::optional<std::string> &value);
	/* The same change here only, for the time Odin runs */
	void set(const std::string &section, const std::string &key, const std::optional<std::string> &value);
	/* The text of such a file with that change, the rest of it as it was:
	 * comments, order, unknown lines and line ends. The last line of the
	 * key takes the value and the ones before it go (the last line says);
	 * a new key goes after the last line of its section, a new section at
	 * the end. With its value by default (`standard`), a key has one line,
	 * its comment "# key = value" when it has no value. */
	static std::string changed(std::string_view text, const std::string &section, const std::string &key,
	                           const std::optional<std::string> &value,
	                           const std::optional<std::string> &standard = std::nullopt);

private:
	/* The text written whole to a file beside it and renamed over it,
	 * then read here again */
	bool write(const std::string &path, const std::string &text);
	/* The section of the parent for that of a version; none for another */
	std::optional<std::string> parents_section(const std::string &section) const;
	/* The sections of its versions for that of a game */
	std::vector<std::string> versions_sections(const std::string &section) const;

	std::map<std::string, Lines> m_sections;
	std::vector<Known> m_known;
	std::map<std::string, std::string> m_parents;
};

} /* namespace odin */

#endif /* ODIN_SETTINGS_HPP */
