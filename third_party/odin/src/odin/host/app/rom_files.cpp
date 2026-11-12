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

#include "odin/host/app/rom_files.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <string_view>
#include <utility>

/* As they come (src/miniz, src/lzma): system headers for our warnings */
#include <miniz.h>
#include <7z.h>
#include <7zAlloc.h>
#include <7zCrc.h>
#include <7zFile.h>

namespace odin {

namespace {

class Zip final : public Archive {
public:
	explicit Zip(const std::string &path) : m_zip{}
	{
		m_open = mz_zip_reader_init_file(&m_zip, path.c_str(), 0);
		if (!m_open)
			return;
		for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&m_zip); i++) {
			mz_zip_archive_file_stat stat;

			if (!mz_zip_reader_file_stat(&m_zip, i, &stat) || stat.m_is_directory)
				continue;
			m_entries.push_back({ stat.m_filename, stat.m_uncomp_size, stat.m_crc32, true });
			m_index.push_back(i);
		}
	}

	~Zip() override
	{
		if (m_open)
			mz_zip_reader_end(&m_zip);
	}

	bool opened() const { return m_open; }

	/* miniz checks the CRC itself */
	std::optional<std::vector<uint8_t>> read(size_t entry) override
	{
		std::vector<uint8_t> bytes(static_cast<size_t>(m_entries[entry].size));

		if (!mz_zip_reader_extract_to_mem(&m_zip, m_index[entry], bytes.data(), bytes.size(), 0))
			return std::nullopt;
		return bytes;
	}

private:
	mz_zip_archive m_zip;
	bool m_open;
	std::vector<mz_uint> m_index;
};

/* A name of a 7z, in UTF-16, as UTF-8: a pair of surrogates is one
 * character, one alone none (U+FFFD) */
std::string utf8_of(const std::vector<UInt16> &utf16)
{
	std::string out;

	for (size_t i = 0; i < utf16.size() && utf16[i]; i++) {
		uint32_t c = utf16[i];

		if (c >= 0xD800 && c < 0xDC00 && i + 1 < utf16.size() && utf16[i + 1] >= 0xDC00 && utf16[i + 1] < 0xE000)
			c = 0x10000 + ((c - 0xD800) << 10) + (utf16[++i] - 0xDC00u);
		else if (c >= 0xD800 && c < 0xE000)
			c = 0xFFFD;
		if (c < 0x80) {
			out += static_cast<char>(c);
		} else if (c < 0x800) {
			out += static_cast<char>(0xC0 | c >> 6);
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			out += static_cast<char>(0xE0 | c >> 12);
			out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | c >> 18);
			out += static_cast<char>(0x80 | (c >> 12 & 0x3F));
			out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
	return out;
}

const ISzAlloc ALLOC = { SzAlloc, SzFree };
const ISzAlloc ALLOC_TEMP = { SzAllocTemp, SzFreeTemp };
/* What the LZMA SDK reads ahead of the archive at a time, as its example */
constexpr size_t LOOK_AHEAD = size_t{1} << 18;

class SevenZip final : public Archive {
public:
	explicit SevenZip(const std::string &path) : m_look_buffer(LOOK_AHEAD)
	{
		static std::once_flag crc_table;

		std::call_once(crc_table, [] { CrcGenerateTable(); });
		SzArEx_Init(&m_db);
		m_file_open = InFile_Open(&m_file.file, path.c_str()) == 0;
		if (!m_file_open)
			return;
		FileInStream_CreateVTable(&m_file);
		LookToRead2_CreateVTable(&m_look, False);
		m_look.realStream = &m_file.vt;
		m_look.buf = m_look_buffer.data();
		m_look.bufSize = m_look_buffer.size();
		LookToRead2_INIT(&m_look);
		m_open = SzArEx_Open(&m_db, &m_look.vt, &ALLOC, &ALLOC_TEMP) == SZ_OK;
		if (!m_open)
			return;
		for (UInt32 i = 0; i < m_db.NumFiles; i++) {
			if (SzArEx_IsDir(&m_db, i))
				continue;

			std::vector<UInt16> utf16(SzArEx_GetFileNameUtf16(&m_db, i, nullptr));

			SzArEx_GetFileNameUtf16(&m_db, i, utf16.data());
			const std::string name = utf8_of(utf16);
			const bool crc_known = SzBitWithVals_Check(&m_db.CRCs, i);
			m_entries.push_back({ name, SzArEx_GetFileSize(&m_db, i), crc_known ? m_db.CRCs.Vals[i] : 0, crc_known });
			m_index.push_back(i);
		}
	}

	~SevenZip() override
	{
		if (m_block)
			ISzAlloc_Free(&ALLOC, m_block);
		SzArEx_Free(&m_db, &ALLOC);
		if (m_file_open)
			File_Close(&m_file.file);
	}

	bool opened() const { return m_open; }

	/* The block a file is in is unpacked whole, and kept for the next
	 * file of the same block; the LZMA SDK checks the CRC */
	std::optional<std::vector<uint8_t>> read(size_t entry) override
	{
		size_t offset = 0, size = 0;

		if (SzArEx_Extract(&m_db, &m_look.vt, m_index[entry], &m_block_index, &m_block, &m_block_size, &offset, &size,
		                   &ALLOC, &ALLOC_TEMP) != SZ_OK)
			return std::nullopt;
		return std::vector<uint8_t>(m_block + offset, m_block + offset + size);
	}

private:
	CSzArEx m_db;
	CFileInStream m_file{};
	CLookToRead2 m_look{};
	std::vector<Byte> m_look_buffer;
	bool m_file_open = false, m_open = false;
	std::vector<UInt32> m_index;
	UInt32 m_block_index = 0xFFFFFFFF;
	Byte *m_block = nullptr;
	size_t m_block_size = 0;
};

constexpr std::initializer_list<const char *> ARCHIVES = { ".zip", ".7z" };

/* A name as the sets write it, the dashes left out and the case ignored:
 * epr23601a.ic29 of the list is epr-23601a.ic29 of MAME */
std::string loose(std::string_view name)
{
	std::string out;

	for (const char c : name)
		if (c != '-')
			out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

/* The last part of the name of an entry of an archive: some keep folders */
std::string_view base_name(std::string_view name)
{
	const size_t slash = name.find_last_of("/\\");

	return slash == std::string_view::npos ? name : name.substr(slash + 1);
}

/* The whole file, if it has that size */
std::optional<std::vector<uint8_t>> read_file(const std::string &path, uint64_t size)
{
	std::FILE *const file = std::fopen(path.c_str(), "rb");
	std::optional<std::vector<uint8_t>> out;

	if (!file)
		return out;
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	/* One byte more than the size told would be a longer file */
	if (std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size() && std::fgetc(file) == EOF)
		out = std::move(bytes);
	std::fclose(file);
	return out;
}

} /* namespace */

std::unique_ptr<Archive>
Archive::open(const std::string &path)
{
	static constexpr std::array<uint8_t, 4> ZIP = { 'P', 'K', 3, 4 }, EMPTY_ZIP = { 'P', 'K', 5, 6 };
	static constexpr std::array<uint8_t, 6> SEVEN_ZIP = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
	std::array<uint8_t, 6> head{};
	std::FILE *const file = std::fopen(path.c_str(), "rb");

	if (!file)
		return nullptr;
	const size_t got = std::fread(head.data(), 1, head.size(), file);
	std::fclose(file);

	if (got >= ZIP.size() && (!std::memcmp(head.data(), ZIP.data(), ZIP.size()) ||
	                          !std::memcmp(head.data(), EMPTY_ZIP.data(), EMPTY_ZIP.size()))) {
		auto zip = std::make_unique<Zip>(path);
		return zip->opened() ? std::move(zip) : nullptr;
	}
	if (got == SEVEN_ZIP.size() && !std::memcmp(head.data(), SEVEN_ZIP.data(), SEVEN_ZIP.size())) {
		auto seven = std::make_unique<SevenZip>(path);
		return seven->opened() ? std::move(seven) : nullptr;
	}
	return nullptr;
}

uint32_t
crc32(const std::vector<uint8_t> &bytes)
{
	return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, bytes.data(), bytes.size()));
}

RomFiles::RomFiles(std::string roms, std::string bios) : m_roms(std::move(roms)), m_bios(std::move(bios))
{
}

Archive *
RomFiles::archive(const std::string &path)
{
	const auto [kept, first] = m_archives.try_emplace(path);

	if (first)
		kept->second = Archive::open(path);
	return kept->second.get();
}

std::optional<uint32_t>
RomFiles::crc_of(const std::string &path)
{
	const auto [kept, first] = m_crcs.try_emplace(path);
	std::error_code error;

	if (first) {
		const uintmax_t size = std::filesystem::file_size(path, error);

		if (!error)
			if (const auto bytes = read_file(path, size))
				kept->second = crc32(*bytes);
	}
	return kept->second;
}

std::optional<RomFiles::Place>
RomFiles::in_archive(const std::string &path, const File &file)
{
	Archive *const opened = archive(path);

	if (!opened)
		return std::nullopt;
	for (size_t i = 0; i < opened->entries().size(); i++) {
		const Archive::Entry &entry = opened->entries()[i];

		if (entry.size != file.size)
			continue;
		if (file.crc && entry.crc_known ? entry.crc == *file.crc : loose(base_name(entry.name)) == loose(file.name))
			return Place{ path, opened, i };
	}
	return std::nullopt;
}

/* By its name first, as written or loosely; by_crc, by the CRC of the files
 * of its size, which are read */
std::optional<RomFiles::Place>
RomFiles::in_folder(const std::string &folder, const File &file, bool by_crc)
{
	std::error_code error;
	std::vector<std::string> same_size;

	if (folder.empty())
		return std::nullopt;
	if (const std::string exact = folder + "/" + file.name; std::filesystem::file_size(exact, error) == file.size && !error)
		return Place{ exact, nullptr, 0 };
	for (auto it = std::filesystem::directory_iterator(folder, error); !error && it != std::filesystem::directory_iterator();
	     it.increment(error)) {
		std::error_code size_error;
		/* As the name given is written: with / on Windows too */
		const std::string name = it->path().filename().string();

		if (!it->is_regular_file(size_error) || it->file_size(size_error) != file.size || size_error)
			continue;
		if (loose(name) == loose(file.name))
			return Place{ folder + "/" + name, nullptr, 0 };
		same_size.push_back(folder + "/" + name);
	}
	if (by_crc && file.crc)
		for (const std::string &path : same_size)
			if (crc_of(path) == file.crc)
				return Place{ path, nullptr, 0 };
	return std::nullopt;
}

/* The folder and the archives of the game in that of the ROMs, the
 * archives and the folder of its set, then of its parent, in that of the
 * BIOS, then in that of the ROMs; last, the files of its size of those
 * folders, read for their CRC */
std::optional<RomFiles::Place>
RomFiles::locate(const std::string &game, const File &file)
{
	const std::string own = m_roms.empty() ? std::string() : m_roms + "/" + game;
	std::vector<std::string> folders = { own };

	if (auto place = in_folder(own, file, false))
		return place;
	for (const char *const kind : ARCHIVES)
		if (!m_roms.empty())
			if (auto place = in_archive(m_roms + "/" + game + kind, file))
				return place;
	for (const std::string *const set : { &file.set, &file.parent }) {
		if (set->empty() || (set == &file.parent && file.parent == file.set))
			continue;
		for (const std::string &dir : { m_bios, m_roms }) {
			if (dir.empty())
				continue;
			for (const char *const kind : ARCHIVES)
				if (auto place = in_archive(dir + "/" + *set + kind, file))
					return place;
			if (auto place = in_folder(dir + "/" + *set, file, false))
				return place;
			folders.push_back(dir + "/" + *set);
		}
	}
	for (const std::string &folder : folders)
		if (auto place = in_folder(folder, file, true))
			return place;
	return std::nullopt;
}

bool
RomFiles::set_there(const std::string &game, const File &file) const
{
	std::error_code error;
	const std::string &set = file.set_of(game);
	const auto there = [&error, &set](const std::string &dir) {
		if (dir.empty())
			return false;
		for (const char *const kind : ARCHIVES)
			if (std::filesystem::exists(dir + "/" + set + kind, error))
				return true;
		return std::filesystem::is_directory(dir + "/" + set, error);
	};

	/* The game's own set is looked for in the folder of the ROMs only */
	return there(m_roms) || (set != game && there(m_bios));
}

std::string
RomFiles::find(const std::string &game, const File &file)
{
	const std::optional<Place> place = locate(game, file);

	if (!place)
		return {};
	return place->text();
}

std::string
RomFiles::Place::text() const
{
	return archive ? path + ": " + archive->entries()[entry].name : path;
}

std::optional<std::vector<uint8_t>>
RomFiles::read(const std::string &game, const File &file, std::string &where)
{
	const std::optional<Place> place = locate(game, file);

	if (!place)
		return std::nullopt;
	if (place->archive) {
		where = place->text();
		return place->archive->read(place->entry);
	}
	where = place->path;

	std::optional<std::vector<uint8_t>> bytes = read_file(place->path, file.size);

	if (bytes && file.crc)
		if (const uint32_t crc = crc32(*bytes); crc != *file.crc)
			std::fprintf(stderr, "ERROR: '%s': CRC %08X, not MAME's %08X: another dump of the chip\n", where.c_str(),
			             crc, *file.crc);
	return bytes;
}

} /* namespace odin */
