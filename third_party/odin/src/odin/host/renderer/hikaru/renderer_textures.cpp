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

#include "odin/host/renderer/hikaru/renderer_textures.hpp"
#include "odin/machine/boards/hikaru/hikaru_draw.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace odin {

namespace {

/* A bank of the texture RAM is 2048 x 1024 texels of 16 bits. */
constexpr uint32_t BANK_WIDTH = 2048;
constexpr uint32_t BANK_HEIGHT = 1024;

bool
same_texture(const odin_texhead_t &a, const odin_texhead_t &b)
{
	return odin_tex_format(&a) == odin_tex_format(&b) &&
	       odin_tex_logw(&a) == odin_tex_logw(&b) &&
	       odin_tex_logh(&a) == odin_tex_logh(&b) &&
	       odin_tex_bank(&a) == odin_tex_bank(&b) &&
	       odin_tex_slotx(&a) == odin_tex_slotx(&b) &&
	       odin_tex_sloty(&a) == odin_tex_sloty(&b);
}

/* A texel of the format 2 is one 4-bit value: the ramp PHARRIER draws its
 * red halo with goes 0, 2, 4, 6, 9, B, D, F across. The four channels get
 * it (HEURISTIC: the halo takes its alpha, the BOOTROM font, 0 or F, its
 * colour weight and its alpha). */
uint16_t
i4_to_rgba4444(uint32_t texel)
{
	return static_cast<uint16_t>((texel & 15) * 0x1111);
}

} /* namespace */

TextureCache::TextureCache(Drawing &card, const std::span<const uint8_t> texram[2])
	: m_card(card), m_texram{texram[0], texram[1]}, m_no_mipmaps(false), m_anisotropy(1),
	  m_log(nullptr)
{
	for (auto &bank : m_slots)
		bank.resize(SLOTS_X * SLOTS_Y);
}

TextureCache::~TextureCache()
{
	invalidate(-1);
	delete_dead();
	for (const auto &[key, sampler] : m_samplers)
		m_card.forget_sampler(sampler);
}

void
TextureCache::bury(HostTexture texture)
{
	if (texture.id)
		m_dead.push_back(texture);
}

void
TextureCache::delete_dead()
{
	if (m_dead.empty())
		return;
	m_card.forget_textures(m_dead);
	m_dead.clear();
}

/* The sampling belongs to the texhead, not to the texels: two texheads may
 * share a slot and differ only there, so the sampler is bound on every
 * bind, as the X360 port sets them (825C6C98, 825C6650). */
HostSampler
TextureCache::sampler(const odin_texhead_t &th)
{
	const uint32_t key = (odin_tex_mirroru(&th) << 0) | (odin_tex_repeatu(&th) << 1) |
	                     (odin_tex_mirrorv(&th) << 2) | (odin_tex_repeatv(&th) << 3) |
	                     (odin_tex_filter(&th) << 4) |
	                     (static_cast<uint32_t>(static_cast<uint8_t>(odin_tex_bias(&th))) << 6);
	const auto found = m_samplers.find(key);

	if (found != m_samplers.end())
		return found->second;

	/* The board has trilinear filtering alone (its patents); the
	 * anisotropy is an option of ours. */
	const HostSampler sampler = m_card.make_sampler(sampling_of(th), m_anisotropy);

	m_samplers.emplace(key, sampler);
	return sampler;
}

/* A word holds a block of two by two texels: a texhead of w x h words is a
 * picture of 2w x 2h texels. The font of the BOOTROM menu is one of 64 x 64
 * words, its glyphs 8 texels apart across and 16 down. */
std::vector<uint16_t>
TextureCache::decode_i4(uint32_t bank, uint32_t w, uint32_t h, uint32_t x0, uint32_t y0) const
{
	std::vector<uint16_t> data(size_t{w} * 2 * h * 2);

	for (uint32_t y = 0; y < h; y++) {
		uint16_t *top = &data[size_t{y} * 2 * (w * 2)];
		uint16_t *bottom = top + w * 2;

		for (uint32_t x = 0; x < w; x++) {
			const size_t offs = size_t{y0 + y} * (BANK_WIDTH * 2) + (x0 + x) * 2;
			uint16_t texels;

			std::memcpy(&texels, &m_texram[bank][offs], 2);
			top[x * 2 + 0] = i4_to_rgba4444(texels >> 12);
			top[x * 2 + 1] = i4_to_rgba4444(texels >> 8);
			bottom[x * 2 + 0] = i4_to_rgba4444(texels >> 4);
			bottom[x * 2 + 1] = i4_to_rgba4444(texels >> 0);
		}
	}
	return data;
}

/* Where each level of a texhead lies, as many as it has with mipmaps. The
 * levels alternate between the two banks (see Antarctic::copy_texture),
 * down to the level where the smaller side reaches one texel: the games
 * upload no further (the sky dome of von4, 256x128, has a zero 1x1 level
 * that the port would read, 825C6A00; HEURISTIC). */
unsigned
TextureCache::levels_of(const odin_texhead_t &th, Level (&levels)[MAX_LEVELS])
{
	const unsigned n = std::min(odin_tex_logw(&th), odin_tex_logh(&th)) + 5;
	Level level = { odin_tex_bank(&th), 0, 0, 16u << odin_tex_logw(&th), 16u << odin_tex_logh(&th) };

	odin_tex_coords(&th, &level.x, &level.y);
	for (unsigned i = 0; i < n; i++) {
		levels[i] = level;
		level.x += (BANK_WIDTH - level.x) / 2;
		level.y += (BANK_HEIGHT - level.y) / 2;
		level.w = std::max(level.w >> 1, 1u);
		level.h = std::max(level.h >> 1, 1u);
		level.bank ^= 1;
	}
	return n;
}

/* A texture reads a texel of the rectangle. Every level counts, whether
 * the texture was decoded with mipmaps or not. */
bool
TextureCache::reads(const Texture &t, unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	for (unsigned i = 0; i < t.num_levels; i++) {
		const Level &l = t.levels[i];

		if (l.bank == bank && l.x < x + w && x < l.x + l.w && l.y < y + h && y < l.y + l.h)
			return true;
	}
	return false;
}

HostTexture
TextureCache::decode(const odin_texhead_t &th, const Level (&levels)[MAX_LEVELS], uint32_t all_levels) const
{
	const uint32_t format = odin_tex_format(&th);
	const uint32_t num_levels = m_no_mipmaps ? 1 : all_levels;
	Texels texels;

	switch (format) {
	case ODIN_FORMAT_ABGR1555:
		texels = Texels::ABGR1555;
		break;
	case ODIN_FORMAT_ABGR4444:
	case ODIN_FORMAT_I4:
		texels = Texels::ABGR4444;
		break;
	case ODIN_FORMAT_LA8:
		texels = Texels::LA8;
		break;
	default:
		return {};
	}

	/* Each level then lies in its bank too. */
	if (levels[0].x + levels[0].w > BANK_WIDTH || levels[0].y + levels[0].h > BANK_HEIGHT) {
		std::fprintf(stderr, "renderer: a texture outside its bank: %s\n",
		             odin_texhead_str(&th));
		std::abort();
	}

	/* Read where it lies in the bank; I4, made ABGR4444 first, four
	 * texels in a word */
	std::vector<uint16_t> i4[MAX_LEVELS];
	TexelRect rects[MAX_LEVELS];

	for (uint32_t level = 0; level < num_levels; level++) {
		const Level &l = levels[level];

		if (format == ODIN_FORMAT_I4) {
			i4[level] = decode_i4(l.bank, l.w, l.h, l.x, l.y);
			rects[level] = { i4[level].data(), l.w * 2, 0, 0, l.w * 2, l.h * 2 };
		} else {
			rects[level] = { m_texram[l.bank].data(), BANK_WIDTH, l.x, l.y, l.w, l.h };
		}
	}
	return m_card.make_texture(texels, std::span(rects, num_levels));
}

bool
TextureCache::slot_of(const odin_texhead_t &th, uint32_t &bank, size_t &index)
{
	const uint32_t slotx = odin_tex_slotx(&th);
	const uint32_t sloty = odin_tex_sloty(&th);

	if (slotx < FIRST_SLOT_X || sloty < FIRST_SLOT_Y)
		return false;
	bank = odin_tex_bank(&th);
	index = (sloty - FIRST_SLOT_Y) * SLOTS_X + (slotx - FIRST_SLOT_X);
	return true;
}

const TextureCache::Texture *
TextureCache::find(const odin_texhead_t &th, uint32_t bank, size_t index) const
{
	for (const Texture &held : m_slots[bank][index])
		if (same_texture(th, held.th))
			return &held;
	return nullptr;
}

bool
TextureCache::holds(const odin_texhead_t &th) const
{
	uint32_t bank;
	size_t index;

	return !slot_of(th, bank, index) || find(th, bank, index);
}

HostTexture
TextureCache::get(const odin_texhead_t &th)
{
	uint32_t bank;
	size_t index;

	if (!slot_of(th, bank, index))
		return {};

	if (const Texture *const held = find(th, bank, index))
		return held->id;

	Slot &slot = m_slots[bank][index];
	Level levels[MAX_LEVELS]{};
	const unsigned all_levels = levels_of(th, levels);

	if (m_log)
		std::fprintf(m_log, "\tHR: TEXUPLOAD %s\n", odin_texhead_str(&th));
	const HostTexture texture = decode(th, levels, all_levels);
	if (!texture.id)
		return {};

	if (slot.empty()) {
		std::vector<uint32_t> &used = m_used[bank];

		used.insert(std::lower_bound(used.begin(), used.end(), index), static_cast<uint32_t>(index));
	}
	Texture &held = slot.emplace_back(Texture{texture, th, all_levels, {}});

	std::copy(std::begin(levels), std::end(levels), held.levels);
	return texture;
}

void
TextureCache::clear_bank(unsigned bank)
{
	for (const uint32_t index : m_used[bank]) {
		Slot &slot = m_slots[bank][index];

		for (const Texture &held : slot)
			bury(held.id);
		slot.clear();
	}
	m_used[bank].clear();
}

void
TextureCache::set_anisotropy(unsigned level)
{
	if (level == m_anisotropy)
		return;
	/* The samplers alone hold it. */
	m_anisotropy = level;
	for (const auto &[key, sampler] : m_samplers)
		m_card.forget_sampler(sampler);
	m_samplers.clear();
}

void
TextureCache::invalidate(int bank)
{
	if (bank < 0) {
		clear_bank(0);
		clear_bank(1);
	} else {
		clear_bank(static_cast<unsigned>(bank));
	}
}

void
TextureCache::invalidate(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	if (x == 0 && y == 0 && w >= BANK_WIDTH && h >= BANK_HEIGHT) {
		clear_bank(bank);
		return;
	}
	/* A texture of either bank may read this one at some level. */
	for (unsigned b = 0; b < 2; b++) {
		std::erase_if(m_used[b], [&](uint32_t index) {
			Slot &slot = m_slots[b][index];

			std::erase_if(slot, [&](const Texture &held) {
				if (!reads(held, bank, x, y, w, h))
					return false;
				bury(held.id);
				return true;
			});
			return slot.empty();
		});
	}
}

} /* namespace odin */
