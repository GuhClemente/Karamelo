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

#include "odin/common/wav_file.hpp"

#include "odin/common/bytes.hpp"

namespace odin {

WavFile::WavFile(const std::string &path, uint32_t rate, uint16_t channels)
	: m_file(std::fopen(path.c_str(), "wb")), m_rate(rate), m_channels(channels), m_bytes(0)
{
	if (m_file)
		header();
}

WavFile::~WavFile()
{
	if (m_file)
		std::fclose(m_file);
}

void
WavFile::write(std::span<const int16_t> samples)
{
	if (!m_file || samples.empty())
		return;

	/* Least significant byte first, whatever the host */
	uint8_t bytes[4096];
	size_t n = 0;

	for (const int16_t sample : samples) {
		put_le16(bytes + n, static_cast<uint16_t>(sample));
		n += 2;
		if (n == sizeof(bytes)) {
			std::fwrite(bytes, 1, n, m_file);
			n = 0;
		}
	}
	std::fwrite(bytes, 1, n, m_file);
	m_bytes += static_cast<uint32_t>(samples.size() * 2);
	header();
	std::fseek(m_file, 0, SEEK_END);
}

void
WavFile::header()
{
	uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };

	put_le32(h + 4, 36 + m_bytes);
	put_le32(h + 16, 16);
	put_le16(h + 20, 1);	/* PCM */
	put_le16(h + 22, m_channels);
	put_le32(h + 24, m_rate);
	put_le32(h + 28, m_rate * m_channels * 2);
	put_le16(h + 32, m_channels * 2);
	put_le16(h + 34, 16);
	h[36] = 'd';
	h[37] = 'a';
	h[38] = 't';
	h[39] = 'a';
	put_le32(h + 40, m_bytes);
	std::fseek(m_file, 0, SEEK_SET);
	std::fwrite(h, 1, sizeof(h), m_file);
}

} /* namespace odin */
