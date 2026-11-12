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

/* PMX1 scenes, from the region headers and object lists the core builds.
 * Polygon words are PowerVR Series 2: ISP/TSP, TSP and texture control. */

#include "odin/machine/chips/pmx1/pmx1.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace odin {

namespace {

/* A 3D address in video RAM; the start of a strip in an object pointer */
constexpr uint32_t VRAM_MASK = 0x01FFFFFF, START = 0x1FFFFF;

/* The Dreamcast's 4-bit palette format: here 32-bit ARGB, B first in memory */
constexpr unsigned FORMAT_8888 = 5;

/* ISP/TSP instruction word */
constexpr uint32_t ISP_TEXTURE = 1u << 25, ISP_OFFSET = 1u << 24, ISP_GOURAUD = 1u << 23, ISP_UV16 = 1u << 22;
constexpr uint32_t ISP_NO_Z_WRITE = 1u << 26;
/* TSP word: 23-22 the fog (PmxDraw::fog) */
constexpr uint32_t TSP_USE_ALPHA = 1u << 20, TSP_IGNORE_TEXTURE_ALPHA = 1u << 19;

/* Frames a texture stays on the GPU without being drawn */
constexpr unsigned KEEP_IMAGES = 600;
/* Offset of the largest level of a mipmapped texture, by the log2 of its
 * size: 16-bit texels, and VQ indices */
constexpr std::array<uint32_t, 11> MIP_16 = { 0x6, 0x8, 0x10, 0x30, 0xB0, 0x2B0, 0xAB0, 0x2AB0, 0xAAB0, 0x2AAB0, 0xAAAB0 };
constexpr std::array<uint32_t, 11> MIP_VQ = { 0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556 };


constexpr uint32_t SIDE_MAX = 1024;
/* Translucent polygons sorted against each other at most */
constexpr size_t MOST_SORTED = 8192;
/* Pixels two polygons must share to be sorted against each other */
constexpr double LEAST_OVERLAP = 0.25;
/* Polygons this close in 1/W keep the driver's order */
constexpr double SAME_PLANE = 1.00001;

/* Spreads each value's bits to the even bits */
constexpr std::array<uint32_t, SIDE_MAX> SPREAD = [] {
	std::array<uint32_t, SIDE_MAX> spread{};

	for (uint32_t i = 0; i < SIDE_MAX; i++) {
		uint32_t v = i;

		v = (v | (v << 8)) & 0x00FF00FF;
		v = (v | (v << 4)) & 0x0F0F0F0F;
		v = (v | (v << 2)) & 0x33333333;
		spread[i] = (v | (v << 1)) & 0x55555555;
	}
	return spread;
}();

/* x and y bits interleaved, y first: the twiddled order */
uint32_t twiddled(uint32_t x, uint32_t y) { return SPREAD[y] | SPREAD[x] << 1; }

/* A non-square texture: twiddled squares of its smaller side, a power of two,
 * one after another along the larger side */
uint32_t twiddled(uint32_t x, uint32_t y, uint32_t side)
{
	const unsigned shift = static_cast<unsigned>(std::countr_zero(side));

	return twiddled(x & (side - 1), y & (side - 1)) + (((x | y) >> shift) << (2 * shift));
}

unsigned log2_of(uint32_t size)
{
	unsigned n = 0;

	while ((1u << n) < size)
		n++;
	return n;
}

uint32_t rgba_of(uint32_t p, unsigned format)
{
	uint32_t r, g, b, a;

	switch (format) {
	case 1:
		r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3, a = 255;
		break;
	case 2:
		r = ((p >> 8) & 15) * 17, g = ((p >> 4) & 15) * 17, b = (p & 15) * 17, a = (p >> 12) * 17;
		break;
	case 0:
	case 7:
		r = ((p >> 10) & 31) << 3, g = ((p >> 5) & 31) << 3, b = (p & 31) << 3, a = p >> 15 ? 255 : 0;
		break;
	default:
		/* YUV, bump map and format 6 are not done yet */
		r = g = b = 128, a = 255;
		break;
	}
	return r | g << 8 | b << 16 | a << 24;
}

} /* namespace */

/* The middle of what two polygons share on the screen; x below 0 if little */
Pmx1::Point Pmx1::overlap_of(const Triangle &l, const Triangle &r)
{
	struct Outline {
		std::array<Point, 10> at;
		unsigned count;
	};
	/* Keeps what lies to the left of the line from `a` to `b` */
	const auto cut = [](const Outline &in, Point a, Point b) {
		Outline out{};
		const auto side = [&](Point p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };

		for (unsigned i = 0; i < in.count; i++) {
			const Point p = in.at[i], q = in.at[(i + 1) % in.count];
			const double from_p = side(p), from_q = side(q);

			if (from_p >= 0.0)
				out.at[out.count++] = p;
			if ((from_p < 0.0) != (from_q < 0.0)) {
				const double along = from_p / (from_p - from_q);

				out.at[out.count++] = { p.x + (q.x - p.x) * along, p.y + (q.y - p.y) * along };
			}
		}
		return out;
	};
	const auto corner = [](const Vertex &v) { return Point{ v.x, v.y }; };
	const double w = WIDTH, h = HEIGHT;
	Point a = corner(r.a), b = corner(r.b);
	const Point c = corner(r.c);
	Outline shared{ { corner(l.a), corner(l.b), corner(l.c) }, 3 };

	if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) < 0.0)
		std::swap(a, b);
	shared = cut(cut(cut(shared, a, b), b, c), c, a);
	shared = cut(cut(cut(cut(shared, { 0.0, 0.0 }, { w, 0.0 }), { w, 0.0 }, { w, h }), { w, h }, { 0.0, h }), { 0.0, h }, { 0.0, 0.0 });

	Point middle{ 0.0, 0.0 };
	double area = 0.0;

	for (unsigned i = 0; i < shared.count; i++) {
		const Point p = shared.at[i], q = shared.at[(i + 1) % shared.count];

		area += p.x * q.y - q.x * p.y;
		middle.x += p.x;
		middle.y += p.y;
	}
	if (std::abs(area) * 0.5 < LEAST_OVERLAP)
		return { -1.0, 0.0 };
	return { middle.x / shared.count, middle.y / shared.count };
}

/* The chip sorts translucency per pixel, and games lay patches a hair apart
 * on one surface. HEURISTIC: here it is per polygon, farthest first, then
 * each behind those it overlaps: their planes are compared where they
 * meet on the screen, as a mean depth misorders polygons of different
 * sizes. */
void Pmx1::sort_translucent(size_t from)
{
	struct Plane {
		float left, top, right, bottom, least_w, most_w;
		double at_x, at_y;
	};
	const auto begin = m_triangles.begin() + static_cast<std::ptrdiff_t>(from);

	std::for_each(begin, m_triangles.end(), [](Triangle &t) { t.sorted = true; });
	std::stable_sort(begin, m_triangles.end(), [](const Triangle &l, const Triangle &r) {
		return l.a.w + l.b.w + l.c.w < r.a.w + r.b.w + r.c.w;
	});

	const size_t count = m_triangles.size() - from;

	if (count < 2 || count > MOST_SORTED)
		return;

	std::vector<Plane> planes(count);
	std::vector<std::vector<uint32_t>> after(count);
	std::vector<uint32_t> waits(count);

	for (size_t i = 0; i < count; i++) {
		const Triangle &t = m_triangles[from + i];
		const double ux = t.b.x - t.a.x, uy = t.b.y - t.a.y, uw = t.b.w - t.a.w;
		const double vx = t.c.x - t.a.x, vy = t.c.y - t.a.y, vw = t.c.w - t.a.w;
		const double area = ux * vy - vx * uy;

		planes[i] = { std::max(std::min({ t.a.x, t.b.x, t.c.x }), 0.0f),
			      std::max(std::min({ t.a.y, t.b.y, t.c.y }), 0.0f),
			      std::min(std::max({ t.a.x, t.b.x, t.c.x }), static_cast<float>(WIDTH)),
			      std::min(std::max({ t.a.y, t.b.y, t.c.y }), static_cast<float>(HEIGHT)),
			      std::min({ t.a.w, t.b.w, t.c.w }), std::max({ t.a.w, t.b.w, t.c.w }),
			      area != 0.0 ? (uw * vy - vw * uy) / area : 0.0,
			      area != 0.0 ? (ux * vw - vx * uw) / area : 0.0 };
	}

	const auto w_at = [&](size_t i, Point at) {
		const Vertex &a = m_triangles[from + i].a;

		return a.w + planes[i].at_x * (at.x - a.x) + planes[i].at_y * (at.y - a.y);
	};

	/* A pair, i before j in depth, whose boxes share some of the screen */
	const auto pair = [&](size_t i, size_t j) {
		const Plane &l = planes[i], &r = planes[j];

		if (std::max(l.left, r.left) >= std::min(l.right, r.right) ||
		    std::max(l.top, r.top) >= std::min(l.bottom, r.bottom))
			return;

		bool swapped = false;

		/* Overlapping or not, the order of those apart in depth holds */
		if (l.most_w > r.least_w) {
			const Point at = overlap_of(m_triangles[from + i], m_triangles[from + j]);

			if (at.x < 0.0)
				return;
			swapped = w_at(i, at) > w_at(j, at) * SAME_PLANE;
		}
		after[swapped ? j : i].push_back(static_cast<uint32_t>(swapped ? i : j));
		waits[swapped ? i : j]++;
	};
	/* The pairs found from the left, not each pair tried: the boxes in the
	 * order of their left side, each against those that start before its
	 * right side. A box with a NaN side does not sort: it meets every other
	 * as the test takes it. The order below depends on the pairs alone */
	std::vector<uint32_t> by_left, odd;
	std::vector<bool> is_odd(count);

	for (uint32_t i = 0; i < count; i++) {
		const Plane &p = planes[i];

		is_odd[i] = std::isnan(p.left) || std::isnan(p.right) || std::isnan(p.top) || std::isnan(p.bottom);
		(is_odd[i] ? odd : by_left).push_back(i);
	}
	std::sort(by_left.begin(), by_left.end(), [&](uint32_t a, uint32_t b) {
		return planes[a].left < planes[b].left || (planes[a].left == planes[b].left && a < b);
	});
	for (size_t p = 0; p < by_left.size(); p++)
		for (size_t q = p + 1; q < by_left.size() && planes[by_left[q]].left < planes[by_left[p]].right; q++)
			pair(std::min(by_left[p], by_left[q]), std::max(by_left[p], by_left[q]));
	for (const uint32_t o : odd)
		for (uint32_t k = 0; k < count; k++)
			if (k != o && (!is_odd[k] || k > o))
				pair(std::min(o, k), std::max(o, k));

	/* Those with nothing left behind them, in mean depth order. Polygons
	 * that overlap in a ring take that order too. */
	std::vector<Triangle> sorted;
	std::vector<bool> done(count);
	std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
	size_t next = 0;

	sorted.reserve(count);
	for (uint32_t i = 0; i < count; i++)
		if (!waits[i])
			ready.push(i);
	while (sorted.size() < count) {
		while (next < count && done[next])
			next++;

		const uint32_t i = ready.empty() ? static_cast<uint32_t>(next) : ready.top();

		if (!ready.empty())
			ready.pop();
		if (done[i])
			continue;
		done[i] = true;
		sorted.push_back(m_triangles[from + i]);
		for (const uint32_t other : after[i])
			if (!--waits[other])
				ready.push(other);
	}
	std::copy(sorted.begin(), sorted.end(), begin);
}

/* The bytes of the frame buffer a texture reads */
void Pmx1::extent(const Texture &t, uint32_t &from, uint32_t &to) const
{
	const uint32_t bytes_a_texel = t.format == FORMAT_8888 ? 4 : 2, log2_w = log2_of(t.width);
	uint64_t size;

	if (t.vq && t.linear)
		size = uint64_t{ 2048 } + uint64_t{ t.stride } * t.height;
	else if (t.palettised)
		size = uint64_t{ 2048 } + (t.mipmap ? MIP_16[log2_w] / 2 : 0) + uint64_t{ t.width } * t.height;
	else if (t.vq)
		size = uint64_t{ 2048 } + (t.mipmap ? MIP_VQ[log2_w] : 0) + uint64_t{ t.width } * t.height / 4;
	else if (t.linear)
		size = uint64_t{ t.stride } * t.height * bytes_a_texel;
	else
		size = (t.mipmap ? uint64_t{ MIP_16[log2_w] } * bytes_a_texel / 2 : 0) + uint64_t{ t.width } * t.height * bytes_a_texel;

	from = std::min<uint32_t>(t.codebook, static_cast<uint32_t>(m_vram.size()));
	to = static_cast<uint32_t>(std::min<uint64_t>(uint64_t{ t.codebook } + size, m_vram.size()));
}

uint64_t Pmx1::hash_of(uint32_t from, uint32_t to) const
{
	const auto mix = [](uint64_t &hash, uint64_t word_of_8) {
		hash = (hash ^ word_of_8) * 0x100000001B3ull;
		hash ^= hash >> 29;
	};
	/* Four lanes: a scene hashes megabytes */
	std::array<uint64_t, 4> hash = { 0xCBF29CE484222325ull, 0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull };
	uint64_t at = from;

	for (; at + 32 <= to; at += 32) {
		std::array<uint64_t, 4> words;

		std::memcpy(words.data(), m_vram.data() + at, 32);
		for (unsigned i = 0; i < 4; i++)
			mix(hash[i], words[i]);
	}
	for (; at + 8 <= to; at += 8) {
		uint64_t word_of_8;

		std::memcpy(&word_of_8, m_vram.data() + at, 8);
		mix(hash[0], word_of_8);
	}
	for (unsigned i = 1; i < 4; i++)
		mix(hash[0], hash[i]);
	return hash[0];
}

/* A texture's image for the GPU, reloaded when its bytes change */
uint32_t Pmx1::image_of(uint32_t tsp, uint32_t tcw)
{
	const uint64_t key = uint64_t{ tcw } << 32 | ((tcw >> 25) & 1 ? (m_stride >> 5) << 6 : 0) | (tsp & 0x3F);
	auto found = m_card_images.find(key);

	/* Already checked: nothing writes while a scene is built */
	if (found != m_card_images.end() && found->second.used == m_rendered)
		return found->second.id;

	const Texture t = texture_of(tsp, tcw);
	uint32_t from = 0, to = 0;

	extent(t, from, to);
	/* Nothing wrote its bytes since they were read */
	if (found != m_card_images.end() && m_kept && m_kept(from, to, found->second.read)) {
		found->second.used = m_rendered;
		return found->second.id;
	}

	const uint64_t read = m_keep ? m_keep(from, to) : 0, hash = hash_of(from, to);

	if (found != m_card_images.end()) {
		found->second.used = m_rendered;
		if (found->second.hash == hash) {
			found->second.read = read;
			return found->second.id;
		}
		m_scene.gone_images.push_back(found->second.id);
		m_card_images.erase(found);
	}
	auto image = std::make_shared<PmxImage>();

	image->id = m_next_image++;
	image->width = t.width;
	image->height = t.height;
	for (unsigned level = 0; level <= t.levels; level++) {
		const uint32_t w = std::max(t.width >> level, 1u), h = std::max(t.height >> level, 1u);
		const uint32_t mip = mip_of(t, w);
		std::vector<uint32_t> &pixels = image->levels.emplace_back(size_t{ w } * h);

		for (uint32_t y = 0; y < h; y++)
			for (uint32_t x = 0; x < w; x++)
				pixels[size_t{ y } * w + x] = fetch(t, w, h, mip, x, y);
	}
	m_card_images.emplace(key, CardImage{ image->id, hash, m_rendered, read });
	m_scene.new_images.push_back(std::move(image));
	return m_next_image - 1;
}

/* Triangles in order, one draw per run of equal state */
void Pmx1::build_scene()
{
	m_scene.vertices.clear();
	m_scene.vertices.reserve(m_triangles.size() * 3);
	m_scene.draws.clear();
	m_scene.most_w = 1.0f;
	m_scene.rendered = true;
	for (const Triangle &tri : m_triangles) {
		const State &state = tri.state;
		const bool gouraud = state.isp & ISP_GOURAUD;

		for (const Vertex *const v : { &tri.a, &tri.b, &tri.c }) {
			m_scene.vertices.push_back({ v->x, v->y, v->w, v->u, v->v, gouraud ? v->argb : tri.c.argb,
			                             gouraud ? v->offset : tri.c.offset });
			if (v->w > m_scene.most_w)
				m_scene.most_w = v->w;
		}

		const uint32_t first = static_cast<uint32_t>(m_scene.vertices.size()) - 3;
		/* Sorted polygons hide nothing: the clear parts of one would */
		const bool write_z = !(state.isp & ISP_NO_Z_WRITE) && !tri.sorted;
		const bool same = !m_scene.draws.empty() && first > 0 && tri.translucent == m_scene.draws.back().blend &&
		                  tri.native == m_scene.draws.back().native && write_z == m_scene.draws.back().write_z &&
		                  tri.per_pixel == m_scene.draws.back().per_pixel &&
		                  state.isp == m_last_isp && state.tsp == m_last_tsp && state.tcw == m_last_tcw;

		if (same) {
			m_scene.draws.back().count += 3;
			continue;
		}
		m_last_isp = state.isp;
		m_last_tsp = state.tsp;
		m_last_tcw = state.tcw;

		PmxDraw draw{};
		const bool textured = state.isp & ISP_TEXTURE;

		draw.first = first;
		draw.count = 3;
		draw.compare = static_cast<uint8_t>(state.isp >> 29);
		draw.src = static_cast<uint8_t>(state.tsp >> 29);
		draw.dst = static_cast<uint8_t>((state.tsp >> 26) & 7);
		draw.shading = static_cast<uint8_t>((state.tsp >> 6) & 3);
		draw.fog = static_cast<uint8_t>((state.tsp >> 22) & 3);
		draw.blend = tri.translucent;
		draw.native = tri.native;
		draw.per_pixel = tri.per_pixel;
		draw.write_z = write_z;
		draw.use_alpha = state.tsp & TSP_USE_ALPHA;
		draw.ignore_texture_alpha = state.tsp & TSP_IGNORE_TEXTURE_ALPHA;
		draw.offset = state.isp & ISP_OFFSET;
		if (textured) {
			const Texture t = texture_of(state.tsp, state.tcw);
			const auto wrap = [](bool clamp, bool flip) {
				return clamp ? PmxWrap::CLAMP : flip ? PmxWrap::MIRROR : PmxWrap::REPEAT;
			};

			draw.image = image_of(state.tsp, state.tcw);
			draw.lod_bias = t.mipmap ? std::log2(t.lod_scale) : 0.0f;
			draw.wrap_u = wrap(t.clamp_u, t.flip_u);
			draw.wrap_v = wrap(t.clamp_v, t.flip_v);
			draw.smooth = t.bilinear;
			draw.mipmap = t.mipmap;
		}
		m_scene.draws.push_back(draw);
	}
	for (auto it = m_card_images.begin(); it != m_card_images.end();) {
		if (m_rendered - it->second.used > KEEP_IMAGES) {
			m_scene.gone_images.push_back(it->second.id);
			it = m_card_images.erase(it);
		} else {
			++it;
		}
	}
}

PmxScene Pmx1::take_scene()
{
	PmxScene scene;

	scene.most_w = m_scene.most_w;
	scene.rendered = std::exchange(m_scene.rendered, false);
	scene.fog_table = m_scene.fog_table;
	scene.fog_density = m_scene.fog_density;
	scene.fog_color = m_scene.fog_color;
	scene.fog_vertex_color = m_scene.fog_vertex_color;
	std::swap(scene.vertices, m_scene.vertices);
	std::swap(scene.draws, m_scene.draws);
	std::swap(scene.new_images, m_scene.new_images);
	std::swap(scene.gone_images, m_scene.gone_images);
	return scene;
}

/* A triangle with equal 1/W at all corners is flat on the screen, and is
 * drawn as set_look() asks */
void Pmx1::push_triangle(const State &state, Vertex a, Vertex b, Vertex c, bool translucent)
{
	const bool flat = m_fine > 1 && m_flat_mode && a.w == b.w && b.w == c.w;
	const bool native = flat && m_flat_mode == 2;

	if (flat && !native)
		for (Vertex *const v : { &a, &b, &c }) {
			v->x = std::round(v->x);
			v->y = std::round(v->y);
		}
	m_triangles.push_back({ state, a, b, c, translucent, native, false, false });
}

/* The triangles of `mask` of a strip whose vertices of `size` words start
 * at `first` */
void Pmx1::draw_triangles(const State &state, uint32_t first, uint32_t size, uint32_t mask, bool translucent)
{
	std::array<Vertex, 8> vs;
	unsigned count = 0;

	for (unsigned t = 0; t < 6; t++)
		if (mask & (0x20u >> t))
			count = t + 3;
	if (!count)
		return;

	/* The words read, in place when all are in video RAM (X, Y, 1/W, U and
	 * V are read for every vertex), else one by one */
	const uint32_t span = 4 * ((count - 1) * size + std::max(size, 5u));
	const bool inside = first <= m_vram.size() && span <= m_vram.size() - first;
	const uint8_t *const base = inside ? m_vram.data() + first : nullptr;
	const auto at = [&](uint32_t offset) {
		uint32_t value;

		if (!base)
			return word(first + offset);
		std::memcpy(&value, base + offset, 4);
		return value;
	};
	const auto put = [&](float &f, uint32_t offset) {
		const uint32_t bits = at(offset);

		std::memcpy(&f, &bits, 4);
	};

	for (unsigned k = 0; k < count; k++) {
		const uint32_t from = k * size * 4;

		put(vs[k].x, from);
		put(vs[k].y, from + 4);
		put(vs[k].w, from + 8);
		put(vs[k].u, from + 12);
		put(vs[k].v, from + 16);
		/* Texture and colour words follow X, Y and 1/W */
		uint32_t next = from + 12;

		if (state.isp & ISP_TEXTURE) {
			if (state.isp & ISP_UV16) {
				const uint32_t uv = at(next);
				const uint32_t u_bits = uv & 0xFFFF0000u, v_bits = uv << 16;

				std::memcpy(&vs[k].u, &u_bits, 4);
				std::memcpy(&vs[k].v, &v_bits, 4);
				next += 4;
			} else {
				next += 8;
			}
		}
		vs[k].argb = at(next);
		vs[k].offset = state.isp & ISP_OFFSET ? at(next + 4) : 0;
	}
	/* The TA tiles only the triangles whose X and Y are numbers. HEURISTIC:
	 * one whose 1/W is not is not drawn either */
	const auto real = [](const Vertex &v) {
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.w);
	};

	for (unsigned t = 0; t < 6; t++)
		if ((mask & (0x20u >> t)) && real(vs[t]) && real(vs[t + 1]) && real(vs[t + 2]))
			push_triangle(state, vs[t], vs[t + 1], vs[t + 2], translucent);
}

/* The scene the core built from the TA's output: a region
 * header of 5 words for each tile and pass, a tile after another, then the
 * opaque and translucent lists, in 64-byte blocks of 15 object pointers and
 * a link. A pointer names a strip the TA copied at Object Base + start * 4
 * and the triangles of it that touch the tile. Each tile's list keeps the
 * TA's order and the copies run on through the scene, so the strips sorted
 * by their start are in the TA's order, and each draws the triangles of all
 * its tiles: the whole screen at once gives the pixels of the tiles. */
void Pmx1::render_tiles(uint32_t region_base, uint32_t object_base)
{
	constexpr uint32_t LAST = 1u << 31, KEEP_Z = 1u << 30, PRESORT = 1u << 29, EMPTY = 1u << 31;
	constexpr uint32_t MOST_HEADERS = 1u << 16;
	std::array<uint32_t, MOST_PASSES> flags{};
	std::array<bool, MOST_PASSES> seen{};
	uint32_t passes = 0, pass = 0, last_at = ~0u;

	for (uint32_t n = 0, at = region_base & VRAM_MASK; n < MOST_HEADERS; n++, at += 20) {
		const uint32_t head = word(at), place = head & 0x7FFC;

		/* The passes of a tile follow each other */
		pass = place == last_at ? pass + 1 : 0;
		last_at = place;
		if (pass >= MOST_PASSES) {
			say(1, "more passes than drawn");
			break;
		}
		passes = std::max(passes, pass + 1);
		if (seen[pass] && (head ^ flags[pass]) & (KEEP_Z | PRESORT))
			say(2, "the tiles of a pass differ in keep Z or presort; those of the first are taken");
		if (!seen[pass]) {
			flags[pass] = head;
			seen[pass] = true;
		}
		for (unsigned slot = 0; slot < 4; slot++) {
			const uint32_t list = word(at + 4 + 4 * slot);

			if (list & EMPTY)
				continue;
			if (slot & 1)
				say(4, "a modifier volume list is not drawn");
			else
				take_list(list & VRAM_MASK, 2 * pass + slot / 2);
		}
		if (head & LAST)
			break;
	}
	m_triangles.clear();
	for (uint32_t p = 0; p < passes; p++) {
		if (p > 0 && !(flags[p] & KEEP_Z))
			say(8, "a pass that clears Z after another is drawn over it");
		for (unsigned slot = 0; slot < 2; slot++) {
			std::vector<uint32_t> &starts = m_lists[2 * p + slot];
			const size_t from = m_triangles.size();

			std::sort(starts.begin(), starts.end());
			for (const uint32_t start : starts) {
				uint16_t &strip = m_strips[start];

				draw_object(object_base, start, (strip >> 6) & 0xF, strip & 0x3F, slot == 1);
				strip = 0;
			}
			starts.clear();
			/* A pass whose headers leave Presort clear has its translucent
			 * objects sorted by the chip, pixel by pixel (PG, the chip's programming guide, p. 60, 82), as
			 * the host's GPU does when it can. Else HEURISTIC: the first
			 * pass's translucent list sorted polygon by polygon, the others
			 * in their order (sorting the second of Smashing Drive so costs
			 * it 6.6 %). Presort: in their order */
			if (slot == 1 && !(flags[p] & PRESORT) && m_card_sorts)
				for (size_t i = from; i < m_triangles.size(); i++)
					m_triangles[i].sorted = m_triangles[i].per_pixel = true;
			else if (slot == 1 && p == 0 && !(flags[p] & PRESORT))
				sort_translucent(from);
		}
	}
	build_scene();
	m_rendered++;
}

/* The object pointers of a list, block after block: a strip seen first
 * goes to list `list`, and m_strips gathers the triangles of all its tiles
 * (bits 5-0) with its form (bits 9-6, pointer bits 24-21) and its list + 1
 * (bits 14-10) */
void Pmx1::take_list(uint32_t address, unsigned list)
{
	constexpr uint32_t LINK = 0xE, END = 0xF, MOST_BLOCKS = 1u << 16;

	for (uint32_t blocks = 0; blocks < MOST_BLOCKS; blocks++) {
		uint32_t k = 0;

		const bool inside = address <= m_vram.size() - 64;

		for (; k < 16; k++) {
			uint32_t pointer;

			if (inside)
				std::memcpy(&pointer, m_vram.data() + address + 4 * k, 4);
			else
				pointer = word(address + 4 * k);

			if (pointer >> 28 == END)
				return;
			if (pointer >> 28 == LINK) {
				address = pointer & VRAM_MASK;
				break;
			}
			uint16_t &strip = m_strips[pointer & START];

			if (!strip) {
				strip = static_cast<uint16_t>((list + 1) << 10 | ((pointer >> 21) & 0xF) << 6);
				m_lists[list].push_back(pointer & START);
			} else if (strip >> 10 != list + 1) {
				say(16, "a strip in two lists is drawn with the first");
			}
			strip |= static_cast<uint16_t>((pointer >> 25) & 0x3F);
		}
		if (k == 16)
			return;
	}
}

/* The strip at Object Base + `start` words: `form` bit 3 the shadow form
 * (5 words of state), bits 2-0 words per vertex past X, Y and 1/W */
void Pmx1::draw_object(uint32_t object_base, uint32_t start, uint32_t form, uint32_t mask, bool translucent)
{
	const uint32_t at = (object_base & 0x0FF00000 & VRAM_MASK) + 4 * start;
	const uint32_t state_words = form & 8 ? 5 : 3;
	const State state{ word(at), word(at + 4), word(at + 8) };

	draw_triangles(state, at + 4 * state_words, 3 + (form & 7), mask, translucent);
}

void Pmx1::say(unsigned what, const char *text)
{
	if (!(m_said & what))
		std::fprintf(stderr, "pmx1 3D: %s\n", text);
	m_said |= what;
}

/* Texture word: 31 mipmapped, 30 VQ (2 KB of 2x2 blocks, then an index per
 * block), 29-27 format, 26 not twiddled, then 25 stride: its rows as long
 * as TEX_STRIDE says, 21 with VQ palettised: 8 bits a texel into a palette
 * held as a VQ codebook (PG p. 78, appendix A). TSP: 18-17 flip, 16-15
 * clamp (U, V), 14-13 filter (0 nearest), 5-0 size. */
Pmx1::Texture Pmx1::texture_of(uint32_t tsp, uint32_t tcw) const
{
	Texture t{};

	t.width = 8u << ((tsp >> 3) & 7);
	t.height = 8u << (tsp & 7);
	t.format = (tcw >> 27) & 7;
	t.linear = (tcw >> 26) & 1;
	t.vq = (tcw >> 30) & 1;
	t.palettised = t.vq && (tcw >> 21) & 1;
	t.stride = (tcw >> 25) & 1 ? m_stride : t.width;
	t.side = std::min(t.width, t.height);
	t.flip_u = (tsp >> 18) & 1, t.flip_v = (tsp >> 17) & 1;
	t.clamp_u = (tsp >> 16) & 1, t.clamp_v = (tsp >> 15) & 1;
	t.bilinear = ((tsp >> 13) & 3) != 0;
	/* Bits 20-0: bits 25-5 of the address (PG p. 79), 8 bytes a step on
	 * the Dreamcast */
	t.codebook = (tcw & 0x1FFFFF) << 5;
	/* VQ: indices follow the 2 KB of blocks, moved by the mipmap */
	t.base = t.vq ? t.codebook + 2048 : t.codebook;
	t.mipmap = (tcw >> 31) & 1 && !t.linear && t.width == t.height;
	t.levels = t.mipmap ? log2_of(t.width) : 0;
	/* Bits 11-8: how soon the next level starts, in quarters */
	const uint32_t adjust = (tsp >> 8) & 15;

	t.lod_scale = adjust ? static_cast<float>(adjust) / 4.0f : 1.0f;
	return t;
}

/* Offset of the level `size_x` texels wide, from the start of a mipmap */
uint32_t Pmx1::mip_of(const Texture &t, uint32_t size_x)
{
	if (!t.mipmap)
		return 0;
	/* A palettised texture's levels: half the 16-bit offsets, in bytes
	 * (PG appendix A; INFERRED: after the palette, as VQ's) */
	if (t.palettised)
		return MIP_16[log2_of(size_x)] / 2;
	return t.vq ? MIP_VQ[log2_of(size_x)] : MIP_16[log2_of(size_x)] * (t.format == FORMAT_8888 ? 2 : 1);
}

/* A texel of the level `size` texels wide, with `mip` from mip_of() */
uint32_t Pmx1::fetch(const Texture &t, uint32_t size_x, uint32_t size_y, uint32_t mip, uint32_t x, uint32_t y) const
{
	const uint32_t side = std::min(size_x, size_y);
	uint32_t at;

	if (t.vq && t.linear) {
		/* One index per texel, in rows, into a palette held in the
		 * first texel of each entry */
		const uint32_t index_at = t.base + y * t.stride + x;
		const uint32_t block = index_at < m_vram.size() ? m_vram[index_at] : 0;

		at = t.codebook + block * 8;
	} else if (t.palettised) {
		/* An index a texel, twiddled as VQ's, into the same palette */
		const uint32_t index_at = t.base + mip + twiddled(x, y, side);
		const uint32_t block = index_at < m_vram.size() ? m_vram[index_at] : 0;

		at = t.codebook + block * 8;
	} else if (t.vq) {
		const uint32_t index_at = t.base + mip + twiddled(x / 2, y / 2, std::max(side / 2, 1u));
		const uint32_t block = index_at < m_vram.size() ? m_vram[index_at] : 0;

		at = t.codebook + block * 8 + twiddled(x & 1, y & 1) * 2;
	} else if (t.linear && t.format != FORMAT_8888) {
		at = t.base + (y * t.stride + x) * 2;
	} else if (t.format == FORMAT_8888 && t.linear) {
		at = t.base + (y * t.stride + x) * 4;
		if (at + 4 > m_vram.size())
			return 0;
		return static_cast<uint32_t>(m_vram[at + 2]) | static_cast<uint32_t>(m_vram[at + 1]) << 8 |
		       static_cast<uint32_t>(m_vram[at]) << 16 | static_cast<uint32_t>(m_vram[at + 3]) << 24;
	} else if (t.format == FORMAT_8888) {
		at = t.base + mip + twiddled(x, y, side) * 4;
		if (at + 4 > m_vram.size())
			return 0;
		return static_cast<uint32_t>(m_vram[at + 2]) | static_cast<uint32_t>(m_vram[at + 1]) << 8 |
		       static_cast<uint32_t>(m_vram[at]) << 16 | static_cast<uint32_t>(m_vram[at + 3]) << 24;
	} else {
		at = t.base + mip + twiddled(x, y, side) * 2;
	}
	if (at + 2 > m_vram.size())
		return 0;
	return rgba_of(m_vram[at] | static_cast<uint32_t>(m_vram[at + 1]) << 8, t.format);
}

} /* namespace odin */
