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

#include "odin/machine/chips/pmx1/pmx1_ta.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "odin/common/bytes.hpp"

/* Instructions: bits 31-30 of the low word. 0: with bit 31 of the high
 * word, start tiling the control list's DWORDs - 1 (11-0); else its pages
 * (address bits 31-12), 0 and 1, or 2 and 3 with bit 29. 1: the
 * copythrough area, DWORDs - 1 (20-0) and its address (high). 2: screen
 * width and height - 1 in tiles (29-24, 22-16), the flags (7-0, 0x20
 * copythrough), and the macrotile (high: min and max X 29-24 and 21-16,
 * min and max Y 14-8 and 6-0). 3: the user clip (high).
 *
 * The control list: a strip pointer (mask of its triangles 30-25, shadow
 * 24, vertex size - 3 at 23-21, its start in words 20-0), or one with bit
 * 31, followed by the vertices' page, the next page and 3 ISP/TSP words (5
 * with the shadow) for it and the strips after it. A strip has the
 * vertices of its last triangle + 2, the first of them at its start in
 * the page, running on into the next page. */

namespace odin {

namespace {

constexpr uint32_t COPYTHROUGH = 0x20;
constexpr uint32_t START = 0x80000000, PAGE_23 = 0x20000000;
constexpr uint32_t END_OF_LIST = 0x80000000, COPY_OVERFLOW = 0x40000000;
constexpr int32_t TILE_W = 32, TILE_H = 16;
/* Things said once */
enum : unsigned { SAID_READ = 1, SAID_WRITE = 2, SAID_MODE = 4, SAID_CLIP = 8, SAID_SHADOW = 16 };

/* A coordinate's tile, clamped while a float: no screen has 64 tiles
 * across nor 128 down */
int32_t
tile(float at, int32_t size, int32_t most)
{
	const float t = std::floor(at / static_cast<float>(size));

	return t < -1.0f ? -1 : t > static_cast<float>(most) ? most : static_cast<int32_t>(t);
}

} /* namespace */

Pmx1Ta::Pmx1Ta(std::span<uint8_t> vram, State &state, const uint32_t &top,
               std::function<void(uint32_t offset, uint32_t size)> written)
	: m_vram(vram), m_s(state), m_top(top), m_written(std::move(written))
{
}

void Pmx1Ta::say(unsigned what, const char *text)
{
	if (!(m_said & what))
		std::fprintf(stderr, "pmx1 TA: %s\n", text);
	m_said |= what;
}

void Pmx1Ta::instruction(uint32_t low, uint32_t high)
{
	switch (low >> 30) {
	case 0:
		if (high & START) {
			m_s.words = (low & 0xFFF) + 1;
			m_s.read = 0;
			m_s.binning = 0;
			m_s.running = 1;
			if ((m_s.screen[0] & 0xFF) != COPYTHROUGH)
				say(SAID_MODE, "a mode but copythrough alone is not emulated, tiled as copythrough");
			if (m_s.clip[1] >> 30)
				say(SAID_CLIP, "the user clip is not emulated");
		} else if (low & PAGE_23) {
			m_s.pages[2] = low & 0xFFFFF;
			m_s.pages[3] = high & 0xFFFFF;
		} else {
			m_s.pages[0] = low & 0xFFFFF;
			m_s.pages[1] = high & 0xFFFFF;
		}
		break;
	case 1:
		/* INFERRED: the copies run on across the starts until the next
		 * Inst 1. The core sends one per scene and starts the TA for
		 * each list; the lists it builds point at the copies so only */
		m_s.copy_size = (low & 0x1FFFFF) + 1;
		m_s.copy_to = high & ~3u;
		m_s.copied = 0;
		break;
	case 2:
		m_s.screen[0] = low;
		m_s.screen[1] = high;
		break;
	default:
		m_s.clip[0] = low;
		m_s.clip[1] = high;
		break;
	}
}

bool Pmx1Ta::read(uint32_t address, uint32_t &value)
{
	const uint32_t offset = address & 0x03FFFFFF;

	if (address >> 26 != (m_top & 0x3F) || (address & 3) || offset > m_vram.size() - 4) {
		say(SAID_READ, "a read outside the frame buffer is not emulated, end of list");
		return false;
	}
	value = le32(m_vram.data() + offset);
	return true;
}

bool Pmx1Ta::write(uint32_t address, uint32_t value)
{
	const uint32_t offset = address & 0x03FFFFFF;

	if (address >> 26 != (m_top & 0x3F) || (address & 3) || offset > m_vram.size() - 4) {
		say(SAID_WRITE, "a copy outside the frame buffer is not emulated, end of list");
		return false;
	}
	put_le32(m_vram.data() + offset, value);
	return true;
}

/* The `count` words at `address`, all in the frame buffer as read() and
 * write() take them; nullptr when one is not */
uint8_t *Pmx1Ta::words_at(uint32_t address, uint32_t count)
{
	const uint32_t offset = address & 0x03FFFFFF;

	if (address >> 26 != (m_top & 0x3F) || (address & 3) || offset > m_vram.size() ||
	    4 * count > m_vram.size() - offset)
		return nullptr;
	return m_vram.data() + offset;
}

/* The next word of the control list */
bool Pmx1Ta::control(uint32_t &value)
{
	if (m_s.read == m_s.words)
		return false;

	const uint32_t at = m_s.read++;

	return read(m_s.pages[(at >> 10) & 3] << 12 | (at & 0x3FF) << 2, value);
}

void Pmx1Ta::end(uint32_t &low, uint32_t &high, bool overflow)
{
	low = END_OF_LIST | (overflow ? COPY_OVERFLOW : 0);
	high = 0;
	m_s.running = m_s.binning = 0;
}

bool Pmx1Ta::status(uint32_t &low, uint32_t &high)
{
	if (!m_s.running)
		return false;
	while (!m_s.binning || !next_tile(low, high)) {
		m_s.binning = 0;
		if (!next_strip(low, high))
			return true;
	}
	return true;
}

/* Reads the control list on to the next strip that touches a tile, and
 * copies it; false at the end of the list, then given as the status */
bool Pmx1Ta::next_strip(uint32_t &low, uint32_t &high)
{
	const uint32_t screen = m_s.screen[0], macrotile = m_s.screen[1];
	const int32_t right = std::min<int32_t>((macrotile >> 16) & 0x3F, (screen >> 24) & 0x3F);
	const int32_t bottom = std::min<int32_t>(macrotile & 0x7F, (screen >> 16) & 0x7F);
	const int32_t left = (macrotile >> 24) & 0x3F, top = (macrotile >> 8) & 0x7F;

	for (;;) {
		uint32_t pointer = 0;

		if (m_s.read == m_s.words) {
			end(low, high, false);
			return false;
		}
		if (!control(pointer)) {
			end(low, high, false);
			return false;
		}
		if (pointer & 0x80000000) {
			m_s.isp_words = pointer & 0x01000000 ? 5 : 3;
			if (pointer & 0x01000000)
				say(SAID_SHADOW, "a strip with the shadow bit, its vertices taken as without");
			bool ok = control(m_s.current_page) && control(m_s.next_page);

			for (uint32_t i = 0; i < m_s.isp_words && ok; i++)
				ok = control(m_s.isp[i]);
			if (!ok) {
				end(low, high, false);
				return false;
			}
		}

		const uint32_t mask = (pointer >> 25) & 0x3F;

		if (!mask)
			continue;

		unsigned last = 0;

		for (unsigned t = 0; t < 6; t++)
			if (mask & (0x20u >> t))
				last = t;

		const uint32_t vertices = last + 3, size = ((pointer >> 21) & 7) + 3;
		const uint32_t words = m_s.isp_words + vertices * size;

		if (m_s.copied + words > m_s.copy_size) {
			end(low, high, true);
			return false;
		}

		/* Copy it: its ISP/TSP words, then its vertices, which run on into
		 * the next page; the words checked a page at a time, else one by
		 * one, as far as the one that fails */
		const uint32_t page = m_s.current_page & 0xFFFFF000, from = page | ((pointer << 2) & 0xFFF);
		const uint32_t to = m_s.copy_to + 4 * m_s.copied, count = vertices * size;
		const uint32_t here = std::min(count, (0x1000 - (from & 0xFFF)) / 4);
		uint8_t *const out = words_at(to, words);
		const uint8_t *const in = words_at(from, here);
		const uint8_t *const on = count > here ? words_at(m_s.next_page & 0xFFFFF000, count - here) : in;
		float x[8] = {}, y[8] = {};
		bool ok = true;

		if (out && in && on) {
			for (uint32_t i = 0; i < m_s.isp_words; i++)
				put_le32(out + 4 * i, m_s.isp[i]);
			for (uint32_t i = 0; i < count; i++)
				put_le32(out + 4 * (m_s.isp_words + i), le32(i < here ? in + 4 * i : on + 4 * (i - here)));
			for (uint32_t v = 0; v < vertices; v++) {
				x[v] = f32(le32(out + 4 * (m_s.isp_words + v * size)));
				y[v] = f32(le32(out + 4 * (m_s.isp_words + v * size + 1)));
			}
		} else {
			for (uint32_t i = 0; i < m_s.isp_words && ok; i++)
				ok = write(to + 4 * i, m_s.isp[i]);
			for (uint32_t i = 0, v = 0, w = 0; i < count && ok; i++) {
				uint32_t at = from + 4 * i, value = 0;

				if ((at & 0xFFFFF000) != page)
					at = (m_s.next_page & 0xFFFFF000) | (at & 0xFFF);
				ok = read(at, value) && write(to + 4 * (m_s.isp_words + i), value);
				/* v: the vertex, w: the word in it */
				if (w == 0)
					x[v] = f32(value);
				else if (w == 1)
					y[v] = f32(value);
				if (++w == size) {
					w = 0;
					v++;
				}
			}
		}
		if (!ok) {
			end(low, high, false);
			return false;
		}
		m_written(to & 0x03FFFFFF, 4 * words);

		/* Its pointer to the copy: the copy's offset in its megabyte, in
		 * words (the 3D core adds Object Base, bits 27-20), with the
		 * strip's mask, shadow and size. The PG gives the strip start
		 * as address bits 22-2 over a 1 MB base, the TA specification
		 * a range of 8 MB less the offset in the first, and the games'
		 * copythrough area is at 0x182C0000 */
		m_s.pointer = (pointer & 0x7FE00000) | ((((m_s.copy_to & 0xFFFFC) >> 2) + m_s.copied) & 0x1FFFFF);
		m_s.copied += words;

		/* The tiles of each triangle's bounding box (INFERRED: the
		 * documents leave the rule to the hardware) */
		m_s.x0 = m_s.y0 = INT32_MAX;
		m_s.x1 = m_s.y1 = INT32_MIN;
		for (unsigned t = 0; t < 6; t++) {
			int32_t *const box = m_s.boxes[t];
			bool real = (mask & (0x20u >> t)) != 0;

			for (unsigned v = t; v < t + 3 && real; v++)
				real = std::isfinite(x[v]) && std::isfinite(y[v]);
			box[0] = 1;
			box[1] = 0;
			if (!real)
				continue;
			box[0] = std::max(left, tile(std::min({ x[t], x[t + 1], x[t + 2] }), TILE_W, 63));
			box[1] = std::min(right, tile(std::max({ x[t], x[t + 1], x[t + 2] }), TILE_W, 63));
			box[2] = std::max(top, tile(std::min({ y[t], y[t + 1], y[t + 2] }), TILE_H, 127));
			box[3] = std::min(bottom, tile(std::max({ y[t], y[t + 1], y[t + 2] }), TILE_H, 127));
			if (box[0] > box[1] || box[2] > box[3]) {
				box[0] = 1;
				box[1] = 0;
				continue;
			}
			m_s.x0 = std::min(m_s.x0, box[0]);
			m_s.x1 = std::max(m_s.x1, box[1]);
			m_s.y0 = std::min(m_s.y0, box[2]);
			m_s.y1 = std::max(m_s.y1, box[3]);
		}
		if (m_s.x0 > m_s.x1)
			continue;
		m_s.x = m_s.x0;
		m_s.y = m_s.y0;
		m_s.binning = 1;
		return true;
	}
}

/* The strip's next tile, row by row: the tile number in the macrotile,
 * the pointer with the triangles that touch it */
bool Pmx1Ta::next_tile(uint32_t &low, uint32_t &high)
{
	const uint32_t macrotile = m_s.screen[1];
	const int32_t left = (macrotile >> 24) & 0x3F, top = (macrotile >> 8) & 0x7F;
	const int32_t across = static_cast<int32_t>((macrotile >> 16) & 0x3F) + 1 - left;

	while (m_s.y <= m_s.y1) {
		const int32_t x = m_s.x, y = m_s.y;
		uint32_t mask = 0;

		if (++m_s.x > m_s.x1) {
			m_s.x = m_s.x0;
			m_s.y++;
		}
		for (unsigned t = 0; t < 6; t++) {
			const int32_t *const box = m_s.boxes[t];

			if (x >= box[0] && x <= box[1] && y >= box[2] && y <= box[3])
				mask |= 0x20u >> t;
		}
		if (mask) {
			low = static_cast<uint32_t>((x - left) + (y - top) * across) & 0xFFF;
			high = (m_s.pointer & ~(0x3Fu << 25)) | mask << 25;
			return true;
		}
	}
	return false;
}

} /* namespace odin */
