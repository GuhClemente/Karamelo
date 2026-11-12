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

#ifndef ODIN_SYSTEM_FONTS_HPP
#define ODIN_SYSTEM_FONTS_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace odin {

/* The fonts most computers already have, for the letters the font of the
 * menu lacks: those of most languages (a folder named in Russian, in
 * Japanese...). On Windows, those it comes with in all its languages; on
 * Linux, the most common ones, wherever its folders of fonts keep them.
 * Each is mapped, not read: some are 20 MB, and only the few letters
 * drawn are ever read. A file Dear ImGui could not read is left out. */
class SystemFonts {
public:
	struct Font {
		std::string path;
		const unsigned char *data;
		size_t size;
	};

	SystemFonts();
	~SystemFonts();
	SystemFonts(const SystemFonts &) = delete;
	SystemFonts &operator=(const SystemFonts &) = delete;

	/* In the order they are looked into */
	const std::vector<Font> &fonts() const { return m_fonts; }

private:
	/* Whether Dear ImGui reads the first font of the file: a font or a
	 * collection of them, with the tables it needs, TrueType or CFF
	 * outlines, and Unicode */
	static bool readable(const unsigned char *data, size_t size);

	std::vector<Font> m_fonts;
};

} /* namespace odin */

#endif /* ODIN_SYSTEM_FONTS_HPP */
