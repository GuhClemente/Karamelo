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
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_RENDERER_TEXTURES_HPP
#define ODIN_RENDERER_TEXTURES_HPP

#include <cstdint>
#include <cstdio>
#include <span>
#include <unordered_map>
#include <vector>

#include "odin/machine/chips/antarctic/antarctic_list.hpp"
#include "odin/host/renderer/hikaru/drawing.hpp"

namespace odin {

/* The textures of the texture RAM, made by the graphics card of the host.
 *
 * A texture is decoded the first time a texhead is drawn and kept until its
 * bank of the texture RAM is written. The slots of a bank are 16 x 16 texels
 * and start at (80, C0); a slot has a texture for each size and format it
 * is read with: PHARRIER draws the same frames with a 32x64 and a 128x128
 * texture at the slot C8,D0 of the bank 1. */
class TextureCache {
public:
	/* card: what makes the textures; texram: the two banks of the
	 * texture RAM, 4 MB each */
	TextureCache(Drawing &card, const std::span<const uint8_t> texram[2]);
	~TextureCache();

	/* The texture of a texhead, decoded if it is not held; none if it
	 * cannot be. */
	HostTexture get(const odin_texhead_t &th);
	/* get would not decode it. */
	bool holds(const odin_texhead_t &th) const;
	/* The texture RAM of a bank has changed; every bank if negative. */
	void invalidate(int bank);
	/* A rectangle of texels of a bank changes: the textures that read
	 * any of them are dropped. */
	void invalidate(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

	/* A sampler with the sampling of a texhead (sampling_of,
	 * hikaru_draw.hpp) and the anisotropy: made once for each that the
	 * texheads have. A texture is always read through one, which stands for
	 * its own sampling. */
	HostSampler sampler(const odin_texhead_t &th);

	/* A texture the graphics card may still be drawing with is not
	 * deleted at once: the driver would wait for the card, and the list
	 * drawn last may have to be drawn again with it. It goes when
	 * delete_dead is called, at the next frame. */
	void bury(HostTexture texture);
	void delete_dead();

	/* The first level only, for looking at a scene */
	void set_no_mipmaps(bool on) { m_no_mipmaps = on; }
	/* Anisotropic filtering, 1 for none as on the board. The samplers are
	 * made again. */
	void set_anisotropy(unsigned level);
	/* Each texture decoded is told there; nullptr for none. */
	void set_log(FILE *out) { m_log = out; }

private:
	enum : uint32_t {
		FIRST_SLOT_X = 0x80,
		FIRST_SLOT_Y = 0xC0,
		SLOTS_X = 0x80,
		SLOTS_Y = 0x40,
		/* 16 << 7 texels a side at most: 7 + 5 levels */
		MAX_LEVELS = 12
	};

	/* Where a level of a texture lies */
	struct Level {
		uint32_t bank, x, y, w, h;
	};
	struct Texture {
		HostTexture id;
		odin_texhead_t th;
		/* All of them, whether it was decoded with mipmaps or not */
		unsigned num_levels;
		Level levels[MAX_LEVELS];
	};
	/* The textures read at one slot */
	using Slot = std::vector<Texture>;

	/* The slot a texhead reads, if it reads one */
	static bool slot_of(const odin_texhead_t &th, uint32_t &bank, size_t &index);
	/* The texture held at that slot for the texhead; nullptr for none */
	const Texture *find(const odin_texhead_t &th, uint32_t bank, size_t index) const;
	static unsigned levels_of(const odin_texhead_t &th, Level (&levels)[MAX_LEVELS]);
	static bool reads(const Texture &t, unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
	/* Made from its levels (levels_of), all of them or the first alone */
	HostTexture decode(const odin_texhead_t &th, const Level (&levels)[MAX_LEVELS], uint32_t all_levels) const;
	std::vector<uint16_t> decode_i4(uint32_t bank, uint32_t w, uint32_t h,
	                                uint32_t x0, uint32_t y0) const;
	void clear_bank(unsigned bank);

	Drawing &m_card;
	std::span<const uint8_t> m_texram[2];
	std::vector<Slot> m_slots[2];
	/* The slots of each bank that hold a texture, in order: a bank has
	 * 8 192 slots, and a write looks at the textures only. */
	std::vector<uint32_t> m_used[2];
	std::vector<HostTexture> m_dead;
	bool m_no_mipmaps;
	unsigned m_anisotropy;
	/* By wrap, filter and byte of the 0C1 */
	std::unordered_map<uint32_t, HostSampler> m_samplers;
	FILE *m_log;
};

} /* namespace odin */

#endif /* ODIN_RENDERER_TEXTURES_HPP */
