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

#ifndef ODIN_ROM_FILES_HPP
#define ODIN_ROM_FILES_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odin {

/* An archive of files, zip (miniz) or 7z (the LZMA SDK), told by its first
 * bytes whatever its name says: both kinds are found under the other's
 * name. It lists the files it holds, with their size and CRC32, and reads
 * one whole, its CRC checked. */
class Archive {
public:
	struct Entry {
		std::string name;
		uint64_t size;
		uint32_t crc;
		bool crc_known;
	};

	/* Nothing when there is no file there, or it is no archive */
	static std::unique_ptr<Archive> open(const std::string &path);
	virtual ~Archive() = default;

	const std::vector<Entry> &entries() const { return m_entries; }
	/* Nothing when it cannot be read whole or its CRC is not the one the
	 * archive tells */
	virtual std::optional<std::vector<uint8_t>> read(size_t entry) = 0;

protected:
	Archive() = default;

	std::vector<Entry> m_entries;
};

/* The CRC32 of bytes, as the archives and MAME tell it */
uint32_t crc32(const std::vector<uint8_t> &bytes);

/* The files of the games where the player keeps them, as MAME does: the
 * folder of the ROMs holds a folder or an archive (zip or 7z) of each
 * game, of its name in MAME; the folder of the BIOS, else that of the
 * ROMs, the archives of the sets a game takes some files from (the BIOS
 * hikaru, the MIE, the I/O board...). A file of the game list is the one
 * of its CRC and size there: in an archive by its CRC, in a folder by its
 * name, else by the CRC of the files of its size. */
class RomFiles {
public:
	/* A file the game list names: its CRC (MAME's) when it gives one, the
	 * set MAME keeps it in when it is not the game's, the parent of the
	 * game (a merged set keeps the game's own files in its archive) */
	struct File {
		std::string name;
		uint64_t size;
		std::optional<uint32_t> crc;
		std::string set;
		std::string parent = {};

		/* The set MAME keeps it in, for a file of `game` */
		const std::string &set_of(const std::string &game) const { return set.empty() ? game : set; }
	};

	/* Either folder may be empty: none */
	RomFiles(std::string roms, std::string bios);

	/* Where a file of that game is, for what is said; empty when it is
	 * nowhere */
	std::string find(const std::string &game, const File &file);
	/* The file whole and where it was found; a file found by its name
	 * whose CRC is not the one told is said on stderr, and taken, as MAME
	 * does */
	std::optional<std::vector<uint8_t>> read(const std::string &game, const File &file, std::string &where);
	/* Whether the set MAME keeps that file of the game in is there, as a
	 * folder or an archive (false: the set is missing whole, not a file
	 * of it) */
	bool set_there(const std::string &game, const File &file) const;

private:
	/* A file found: in a folder, or an entry of an archive */
	struct Place {
		std::string path;
		Archive *archive;
		size_t entry;

		/* The file, or the archive and its entry */
		std::string text() const;
	};

	std::optional<Place> locate(const std::string &game, const File &file);
	std::optional<Place> in_archive(const std::string &path, const File &file);
	std::optional<Place> in_folder(const std::string &folder, const File &file, bool by_crc);
	/* Opened once and kept: nullptr when there is none there */
	Archive *archive(const std::string &path);
	std::optional<uint32_t> crc_of(const std::string &path);

	std::string m_roms, m_bios;
	std::map<std::string, std::unique_ptr<Archive>> m_archives;
	std::map<std::string, std::optional<uint32_t>> m_crcs;	/* of the files of folders read for their CRC */
};

} /* namespace odin */

#endif /* ODIN_ROM_FILES_HPP */
