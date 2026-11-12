/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/chips/pmx1/pmx1.hpp"

#include <cmath>
#include <cstring>

namespace odin {

Pmx1::Pmx1(std::span<uint8_t> vram)
	: m_vram(vram), m_strips(size_t{1} << 21)
{
}

uint32_t Pmx1::word(uint32_t offset) const
{
	uint32_t value = 0;

	if (offset + 4 <= m_vram.size())
		std::memcpy(&value, m_vram.data() + offset, 4);
	return value;
}

/* TEX_STRIDE, bits 5-0 of Texture Control (PG, the chip's programming guide, p. 92): 32 texels a step
 * (p. 117: "Index = V*StrideRegister*32 + U"). The fog registers are the
 * Dreamcast CLX2's (INFERRED: the guide does not give them) */
void Pmx1::draw(std::span<const uint32_t, 0x200> regs_3d)
{
	constexpr uint32_t OBJECT_BASE = 0x20 / 4, REGION_HEADER_BASE = 0x2C / 4, TEXTURE_CONTROL = 0xE4 / 4;
	constexpr uint32_t FOG_COLOR = 0xB0 / 4, FOG_VERTEX_COLOR = 0xB4 / 4, FOG_DENSITY = 0xB8 / 4, FOG_TABLE = 0x200 / 4;
	const uint32_t density = regs_3d[FOG_DENSITY];

	m_stride = (regs_3d[TEXTURE_CONTROL] & 0x3F) * 32;
	m_scene.fog_color = regs_3d[FOG_COLOR];
	m_scene.fog_vertex_color = regs_3d[FOG_VERTEX_COLOR];
	m_scene.fog_density = static_cast<float>((density >> 8) & 0xFF) / 128.0f *
	                      std::ldexp(1.0f, static_cast<int8_t>(density & 0xFF));
	for (unsigned k = 0; k < 128; k++) {
		m_scene.fog_table[2 * k] = static_cast<uint8_t>(regs_3d[FOG_TABLE + k] >> 8);
		m_scene.fog_table[2 * k + 1] = static_cast<uint8_t>(regs_3d[FOG_TABLE + k]);
	}
	render_tiles(regs_3d[REGION_HEADER_BASE], regs_3d[OBJECT_BASE]);
}

} /* namespace odin */
