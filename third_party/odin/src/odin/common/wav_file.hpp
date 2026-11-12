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

#ifndef ODIN_WAV_FILE_HPP
#define ODIN_WAV_FILE_HPP

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace odin {

/* Debug (ODIN_WAV): a WAV file of 16-bit samples, written as they come.
 * Its header always tells the samples written so far, so that a run
 * stopped at any time leaves a file a player reads. */
class WavFile {
public:
	WavFile(const std::string &path, uint32_t rate, uint16_t channels);
	~WavFile();
	WavFile(const WavFile &) = delete;
	WavFile &operator=(const WavFile &) = delete;

	bool ok() const { return m_file != nullptr; }
	/* The channels one after the other, as many times as there are */
	void write(std::span<const int16_t> samples);

private:
	void header();

	std::FILE *m_file;
	uint32_t m_rate;
	uint16_t m_channels;
	uint32_t m_bytes;
};

} /* namespace odin */

#endif /* ODIN_WAV_FILE_HPP */
