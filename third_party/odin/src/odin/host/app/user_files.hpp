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

#ifndef ODIN_USER_FILES_HPP
#define ODIN_USER_FILES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace odin {

/* Where the user keeps the data of programs: %APPDATA% on Windows,
 * ~/Library/Application Support on macOS, XDG_DATA_HOME or ~/.local/share
 * elsewhere. Empty when it cannot be told. */
std::string user_data_dir();
/* The folder of the program when it holds portable.txt or odin.ini: this
 * copy of Odin is then portable, its settings, saves, states, log and cache
 * kept there, apart from those of any other copy. Empty otherwise. */
std::string portable_folder();
/* Where Odin keeps the files of the user: the portable folder, else odin in
 * user_data_dir(); empty when it cannot be told */
std::string odin_data_dir();
/* A file or a folder of the user: the variable when it is set (none: no
 * such file), else the one chosen, else <name> in odin_data_dir(); empty
 * when there is none */
std::string user_path(const char *variable, const std::string &chosen, const char *name);
/* The folder the program is in, that of the AppImage it runs from (APPIMAGE)
 * else that of its executable; empty when it cannot be told. Its bios, roms
 * and artwork folders are those of Odin where nothing else is chosen. */
std::string program_folder();
/* Where the data of Odin may come with the program (odin-games.json,
 * shaders/, filters/): the folder of the program, then share/odin beside the
 * folder of its executable (an AppImage, or an install in a prefix) */
std::vector<std::string> program_data_folders();
/* The folder of the pictures of the games, as MAME has one: ODIN_ARTWORK,
 * else the one chosen ([paths] artwork), else artwork in the data folder
 * of Odin (beside saves/); empty with ODIN_ARTWORK=none. A picture of a
 * game there is named <game>_<kind>.png: _title, its title screen;
 * _logo, the picture of the user the library shows instead; _bezel, laid
 * around the game. */
std::string artwork_folder(const std::string &chosen = {});
/* That picture of that game in the folder, its name in any case (as
 * Swracer_Bezel.png), else that of its parent (a version of it takes its
 * pictures): empty when there is none */
std::string artwork_file(const std::string &folder, const std::string &game, const std::string &kind,
                         const std::string &parent = {});

/* The folder where a game keeps what its board holds without power, one
 * file for each memory. Nothing is read or written when there is none. */
class SaveFolder {
public:
	/* <folder>/<game> with ODIN_SAVES=<folder>; else <chosen>/<game>, the
	 * folder the settings give; else saves/<game> in odin_data_dir():
	 * beside a portable Odin, or in <data>/odin, <data> being
	 * user_data_dir(). None with ODIN_SAVES=none, or when there is no such
	 * place. */
	static SaveFolder of_game(const std::string &game, const std::string &chosen = "");
	/* The folder of the folders of the games; empty: none */
	static std::string root(const std::string &chosen = "");

	SaveFolder() = default;
	explicit SaveFolder(std::string dir) : m_dir(std::move(dir)) {}

	bool none() const { return m_dir.empty(); }
	const std::string &dir() const { return m_dir; }
	/* The path of a file of the folder; empty when there is none. */
	std::string path(const std::string &name) const;

private:
	std::string m_dir;
};

/* The bytes of a file that holds exactly `size` of them. Nothing when it is
 * missing, or holds another number of bytes (said on stderr). */
std::optional<std::vector<uint8_t>> read_kept(const std::string &path, size_t size);

/* Writes a file through a temporary one that then takes its place: a write
 * cut short leaves the old file whole. The folders are made when missing.
 * False, said on stderr, when the file could not be written. */
bool write_kept(const std::string &path, std::span<const uint8_t> bytes);

} /* namespace odin */

#endif /* ODIN_USER_FILES_HPP */
