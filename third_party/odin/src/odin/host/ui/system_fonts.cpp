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

#include "odin/host/ui/system_fonts.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "odin/common/bytes.hpp"
#include "odin/common/env.hpp"

namespace odin {

namespace {

#ifdef _WIN32
/* In the folder of the fonts of Windows 10 and 11, whatever their
 * language: Latin, Greek, Cyrillic, Arabic, Hebrew (Segoe UI); the signs
 * of the gamepads, ✕ ○ □ △ and the arrows (Segoe UI Symbol); Indian
 * scripts (Nirmala UI); Thai (Leelawadee UI); Japanese (Yu Gothic, else
 * MS Gothic); Chinese (Microsoft YaHei, JhengHei); Korean (Malgun Gothic) */
const std::vector<std::vector<const char *>> WANTED = {
	{ "segoeui.ttf" }, { "seguisym.ttf" }, { "Nirmala.ttf" }, { "LeelawUI.ttf" }, { "YuGothR.ttc", "msgothic.ttc" },
	{ "msyh.ttc" }, { "msjh.ttc" }, { "malgun.ttf" },
};
#elif defined(__APPLE__)
const std::vector<std::vector<const char *>> WANTED = {
	{ "Arial Unicode.ttf", "LucidaGrande.ttc" }, { "Hiragino Sans GB.ttc", "STHeiti Medium.ttc" },
	{ "AppleSDGothicNeo.ttc", "AppleGothic.ttf" }, { "Ayuthaya.ttf", "ThonburiUI.ttc" },
	{ "Kohinoor.ttc", "DevanagariMT.ttc" },
};
#else
/* Anywhere in the folders of fonts of Linux, the first of each line found:
 * Latin, Greek, Cyrillic, Arabic, Hebrew (DejaVu Sans, on nearly all);
 * Chinese, Japanese and Korean (Noto Sans CJK, else Droid Sans Fallback,
 * else WenQuanYi); Thai; Hindi */
const std::vector<std::vector<const char *>> WANTED = {
	{ "DejaVuSans.ttf", "NotoSans-Regular.ttf" },
	{ "NotoSansCJK-Regular.ttc", "DroidSansFallbackFull.ttf", "DroidSansFallback.ttf", "wqy-microhei.ttc",
	  "wqy-zenhei.ttc" },
	{ "NotoSansThai-Regular.ttf" }, { "NotoSansDevanagari-Regular.ttf" },
};
#endif

/* Where each file name wanted is, the first found */
std::map<std::string, std::string>
find_files()
{
	std::map<std::string, std::string> out;
	std::vector<std::string> folders;

#ifdef _WIN32
	const std::string windows = env_text("WINDIR");

	folders.push_back((windows.empty() ? "C:\\Windows" : windows) + "\\Fonts");
#elif defined(__APPLE__)
	const char *const home = std::getenv("HOME");

	if (home && home[0])
		folders.push_back(std::string(home) + "/Library/Fonts");
	folders.push_back("/Library/Fonts");
	folders.push_back("/System/Library/Fonts");
#else
	const std::string home = env_text("HOME"), data = env_text("XDG_DATA_HOME");

	if (!data.empty())
		folders.push_back(data + "/fonts");
	else if (!home.empty())
		folders.push_back(home + "/.local/share/fonts");
	if (!home.empty())
		folders.push_back(home + "/.fonts");
	folders.push_back("/usr/local/share/fonts");
	folders.push_back("/usr/share/fonts");
#endif
	for (const std::vector<const char *> &line : WANTED)
		for (const char *name : line)
			out[name];
	for (const std::string &folder : folders) {
		std::error_code error;

		for (auto it = std::filesystem::recursive_directory_iterator(
		             folder, std::filesystem::directory_options::skip_permission_denied, error);
		     !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
			const auto wanted = out.find(it->path().filename().string());

			if (wanted != out.end() && wanted->second.empty())
				wanted->second = it->path().string();
		}
	}
	return out;
}

/* The file mapped whole: nothing when it cannot be */
const unsigned char *
map_file(const std::string &path, size_t &size)
{
#ifdef _WIN32
	const HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	LARGE_INTEGER length{};
	const void *view = nullptr;

	if (file == INVALID_HANDLE_VALUE)
		return nullptr;
	if (GetFileSizeEx(file, &length) && length.QuadPart > 0) {
		/* The view keeps the mapping, which keeps the file */
		if (const HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) {
			view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
			CloseHandle(mapping);
		}
	}
	CloseHandle(file);
	size = view ? static_cast<size_t>(length.QuadPart) : 0;
	return static_cast<const unsigned char *>(view);
#else
	const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
	struct stat about{};
	void *view = MAP_FAILED;

	if (fd < 0)
		return nullptr;
	if (fstat(fd, &about) == 0 && about.st_size > 0)
		view = mmap(nullptr, static_cast<size_t>(about.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (view == MAP_FAILED)
		return nullptr;
	size = static_cast<size_t>(about.st_size);
	return static_cast<const unsigned char *>(view);
#endif
}

void
unmap_file(const unsigned char *data, size_t size)
{
#ifdef _WIN32
	(void)size;
	UnmapViewOfFile(data);
#else
	munmap(const_cast<unsigned char *>(data), size);
#endif
}

} /* namespace */

bool
SystemFonts::readable(const unsigned char *data, size_t size)
{
	size_t start = 0;

	if (size < 12)
		return false;
	/* A collection: its first font */
	if (!std::memcmp(data, "ttcf", 4)) {
		if (size < 16)
			return false;
		start = be32(data + 12);
		if (start > size - 12)
			return false;
	}

	const uint32_t version = be32(data + start);

	if (version != 0x00010000 && std::memcmp(data + start, "true", 4) && std::memcmp(data + start, "OTTO", 4))
		return false;

	const uint16_t tables = be16(data + start + 4);
	std::map<std::string, uint32_t> at;

	if (start + 12 + size_t{tables} * 16 > size)
		return false;
	for (uint16_t i = 0; i < tables; i++) {
		const unsigned char *const entry = data + start + 12 + size_t{i} * 16;
		const uint32_t offset = be32(entry + 8), length = be32(entry + 12);

		if (offset <= size && length <= size - offset)
			at[std::string(reinterpret_cast<const char *>(entry), 4)] = offset;
	}
	for (const char *needed : { "cmap", "head", "hhea", "hmtx" })
		if (!at.count(needed))
			return false;
	if (!(at.count("glyf") && at.count("loca")) && !at.count("CFF "))
		return false;

	/* A table of Unicode letters: Windows (3, 1 or 10), or Unicode (0) */
	const uint32_t cmap = at["cmap"];

	if (cmap + 4 > size)
		return false;
	for (uint16_t i = 0, count = be16(data + cmap + 2); i < count && cmap + 4 + size_t{i} * 8 + 8 <= size; i++) {
		const unsigned char *const record = data + cmap + 4 + size_t{i} * 8;
		const uint16_t platform = be16(record), encoding = be16(record + 2);

		if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))
			return true;
	}
	return false;
}

SystemFonts::SystemFonts()
{
	const std::map<std::string, std::string> found = find_files();

	for (const std::vector<const char *> &line : WANTED) {
		for (const char *name : line) {
			const std::string &path = found.at(name);
			size_t size = 0;

			if (path.empty())
				continue;
			const unsigned char *const data = map_file(path, size);
			if (!data)
				continue;
			if (!readable(data, size)) {
				unmap_file(data, size);
				continue;
			}
			m_fonts.push_back({ path, data, size });
			break;
		}
	}
}

SystemFonts::~SystemFonts()
{
	for (const Font &font : m_fonts)
		unmap_file(font.data, font.size);
}

} /* namespace odin */
