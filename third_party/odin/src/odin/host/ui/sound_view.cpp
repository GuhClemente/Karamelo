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


/* The drawing of the page Audio: one generic cabinet for every game, the
 * parts of its sound at seven places (3/4 view, silhouette). */

#include "odin/host/ui/sound_view.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>

namespace odin {

namespace {

using Kind = SoundPart::Kind;
using Place = SoundPart::Place;

struct Vec3 {
	float x, y, z;
};

float
dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* The view, from behind the player on the right (36 degrees) and from
 * above (30): `c` toward the eye, `r` to the right of the picture, `u` up
 * in it */
struct Camera {
	Vec3 c, r, u;
};

Camera
camera()
{
	constexpr float A = 36.0f * std::numbers::pi_v<float> / 180.0f, B = 30.0f * std::numbers::pi_v<float> / 180.0f;

	return { { std::sin(A) * std::cos(B), std::cos(A) * std::cos(B), std::sin(B) }, { std::cos(A), -std::sin(A), 0.0f },
	         { -std::sin(A) * std::sin(B), -std::cos(A) * std::sin(B), std::cos(B) } };
}

/* A block of the cabinet in centimetres: x from the player's left to his
 * right, y from the screen back to the seat, z up */
enum class Shade : uint8_t { BODY, SEAT, FLOOR, SCREEN };

struct Block {
	float x0, x1, y0, y1, z0, z1;
	Shade shade;
};

/* The top, the screen, the panel and the body; the floor and the seat
 * (cushion, back, head rest) for a game played seated. Standing, the
 * cabinet stands 12 cm higher. */
std::vector<Block>
blocks(bool seated)
{
	const float up = seated ? 0.0f : 12.0f;
	std::vector<Block> out;

	if (seated)
		out.push_back({ -50, 50, 55, 248, 0, 5, Shade::FLOOR });
	out.push_back({ -52, 52, 0, 55, 0, 168 + up, Shade::BODY });
	out.push_back({ -55, 55, -2, 61, 168 + up, 186 + up, Shade::BODY });
	out.push_back({ -33, 33, 55, 56.5f, 104 + up, 156 + up, Shade::SCREEN });
	out.push_back({ -57, 57, 55, 92, 78 + up, 92 + up, Shade::BODY });
	if (seated) {
		out.push_back({ -32, 32, 182, 230, 5, 46, Shade::SEAT });
		out.push_back({ -32, 32, 226, 244, 46, 116, Shade::SEAT });
		out.push_back({ -14, 14, 228, 242, 116, 132, Shade::SEAT });
	}
	return out;
}

/* The seven places, on the faces the player sees */
Vec3
spot(Place at, bool seated)
{
	const float up = seated ? 0.0f : 12.0f;

	switch (at) {
	case Place::FRONT_LEFT:
		return { -43, 57, 130 + up };
	case Place::FRONT_RIGHT:
		return { 43, 57, 130 + up };
	case Place::BASE:
		return { 0, 57, 26 };
	case Place::PANEL:
		return { 0, 78, 92 + up };
	case Place::SEAT_LEFT:
		return { -28, 245, 100 };
	case Place::SEAT_RIGHT:
		return { 28, 245, 100 };
	case Place::SEAT:
		return { 0, 245, 66 };
	case Place::NONE:
		break;
	}
	return { 0, 0, 0 };
}

struct Face {
	Vec3 n;
	std::array<Vec3, 4> q;
};

std::array<Face, 6>
faces(const Block &b)
{
	return { {
		{ { 0, 0, 1 }, { { { b.x0, b.y0, b.z1 }, { b.x1, b.y0, b.z1 }, { b.x1, b.y1, b.z1 }, { b.x0, b.y1, b.z1 } } } },
		{ { 0, 0, -1 }, { { { b.x0, b.y0, b.z0 }, { b.x1, b.y0, b.z0 }, { b.x1, b.y1, b.z0 }, { b.x0, b.y1, b.z0 } } } },
		{ { 0, -1, 0 }, { { { b.x0, b.y0, b.z0 }, { b.x1, b.y0, b.z0 }, { b.x1, b.y0, b.z1 }, { b.x0, b.y0, b.z1 } } } },
		{ { 0, 1, 0 }, { { { b.x0, b.y1, b.z0 }, { b.x1, b.y1, b.z0 }, { b.x1, b.y1, b.z1 }, { b.x0, b.y1, b.z1 } } } },
		{ { -1, 0, 0 }, { { { b.x0, b.y0, b.z0 }, { b.x0, b.y1, b.z0 }, { b.x0, b.y1, b.z1 }, { b.x0, b.y0, b.z1 } } } },
		{ { 1, 0, 0 }, { { { b.x1, b.y0, b.z0 }, { b.x1, b.y1, b.z0 }, { b.x1, b.y1, b.z1 }, { b.x1, b.y0, b.z1 } } } },
	} };
}

/* A colour toward another, that share of the way, opaque; a colour that
 * lets that share through */
ImU32
mix(ImU32 from, ImU32 to, float k)
{
	const auto channel = [&](int shift) {
		const float a = static_cast<float>((from >> shift) & 0xFF), b = static_cast<float>((to >> shift) & 0xFF);

		return static_cast<ImU32>(std::lround(a + (b - a) * k)) << shift;
	};
	return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | IM_COL32_A_MASK;
}

ImU32
faded(ImU32 colour, float share)
{
	const float a = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF) * share;

	return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::lround(a)) << IM_COL32_A_SHIFT);
}

/* The tones of the drawing, from the palette: the cabinet a little of its
 * lavender over the page, the seat more, the floor less */
struct Tones {
	ImU32 body, seat, floor, screen, ink, accent, silent;
};

Tones
tones(const MenuPalette &palette)
{
	return { mix(palette.screen, palette.cabinet, 0.12f), mix(palette.screen, palette.cabinet, 0.22f),
	         mix(palette.screen, palette.cabinet, 0.06f), palette.tile, palette.ink, palette.accent, palette.faint };
}

enum class Lit : uint8_t { OFF, ON, POINTED };

/* A rectangle with its corners cut */
std::array<ImVec2, 8>
cut_rect(ImVec2 a, ImVec2 b, float c)
{
	return { { { a.x + c, a.y }, { b.x - c, a.y }, { b.x, a.y + c }, { b.x, b.y - c },
	           { b.x - c, b.y }, { a.x + c, b.y }, { a.x, b.y - c }, { a.x, a.y + c } } };
}

void
dashed_ring(ImDrawList *list, ImVec2 c, float r, ImU32 colour, float width, float on, float off)
{
	std::array<ImVec2, 28> points;

	for (size_t i = 0; i < points.size(); i++) {
		const float t = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(points.size());

		points[i] = { c.x + r * std::cos(t), c.y + r * std::sin(t) };
	}
	MenuKit::dashes(list, points, true, colour, width, on, off);
}

/* A speaker, `h` its half size: a box and its cone; heard, filled and in
 * a halo; silent, dotted */
void
speaker_mark(const MenuKit &kit, ImDrawList *list, ImVec2 c, float h, bool woofer, Lit lit, const Tones &tones)
{
	const auto box = cut_rect({ c.x - h, c.y - h }, { c.x + h, c.y + h }, h * (woofer ? 0.29f : 0.3f));
	const float cone = h * 0.66f, on = kit.px(3.0f), off = kit.px(2.0f);

	if (lit == Lit::OFF) {
		MenuKit::dashes(list, box, true, tones.silent, kit.px(1.2f), on, off);
		dashed_ring(list, c, cone, tones.silent, kit.px(1.2f), on, off);
		if (woofer)
			dashed_ring(list, c, h * 0.28f, tones.silent, kit.px(1.1f), on, off);
		else
			list->AddCircleFilled(c, h * 0.18f, tones.silent);
		return;
	}

	const ImU32 colour = lit == Lit::POINTED ? tones.accent : tones.ink;

	list->AddCircleFilled(c, h * 1.6f, faded(colour, 0.07f));
	list->AddCircleFilled(c, h * 1.3f, faded(colour, 0.12f));
	list->AddConvexPolyFilled(box.data(), static_cast<int>(box.size()), colour);
	list->AddCircle(c, cone, tones.body, 0, kit.px(1.6f));
	if (woofer)
		list->AddCircle(c, h * 0.28f, tones.body, 0, kit.px(1.4f));
	else
		list->AddCircleFilled(c, h * 0.2f, tones.body);
}

/* A shaker: a puck between two pairs of zigzags, always there, bright when
 * it gets a signal */
void
shaker_mark(const MenuKit &kit, ImDrawList *list, ImVec2 c, Lit lit, const Tones &tones)
{
	const bool on = lit != Lit::OFF;
	const ImU32 colour = lit == Lit::POINTED ? tones.accent : on ? tones.ink : tones.silent;
	const auto p = [&kit, c](float x, float y) { return ImVec2{ c.x + kit.px(x), c.y + kit.px(y) }; };
	const auto puck = cut_rect(p(-11, -6), p(11, 6), kit.px(2.5f));

	if (on) {
		list->AddConvexPolyFilled(puck.data(), static_cast<int>(puck.size()), colour);
		list->AddLine(p(-6, 0), p(6, 0), tones.body, kit.px(1.4f));
	} else {
		const std::array<ImVec2, 2> mid = { p(-6, 0), p(6, 0) };

		MenuKit::dashes(list, puck, true, colour, kit.px(1.2f), kit.px(3.0f), kit.px(2.0f));
		MenuKit::dashes(list, mid, false, colour, kit.px(1.1f), kit.px(3.0f), kit.px(2.0f));
	}
	for (const float x : { -16.0f, -21.0f, 13.0f, 18.0f }) {
		const std::array<ImVec2, 5> zig = { p(x, -8), p(x + 3, -4), p(x, 0), p(x + 3, 4), p(x, 8) };

		list->AddPolyline(zig.data(), static_cast<int>(zig.size()), on ? colour : faded(tones.silent, 0.7f),
		                  ImDrawFlags_None, kit.px(on ? 1.5f : 1.1f));
	}
}

/* A part in the drawing: where it is, its half size, how far from the eye */
struct Mark {
	int part;
	ImVec2 at;
	float hx, hy, depth;
};

/* The marks pushed apart until none overlaps another, `gap` between */
void
spread(std::vector<Mark> &marks, float gap)
{
	for (int round = 0; round < 40; round++) {
		bool moved = false;

		for (size_t i = 0; i < marks.size(); i++)
			for (size_t j = i + 1; j < marks.size(); j++) {
				Mark &a = marks[i], &b = marks[j];
				const float ox = a.hx + b.hx + gap - std::fabs(a.at.x - b.at.x);
				const float oy = a.hy + b.hy + gap - std::fabs(a.at.y - b.at.y);

				if (ox <= 0.0f || oy <= 0.0f)
					continue;
				moved = true;
				if (ox < oy) {
					const float d = (a.at.x < b.at.x ? -1.0f : 1.0f) * ox / 2.0f;

					a.at.x += d;
					b.at.x -= d;
				} else {
					const float d = (a.at.y < b.at.y ? -1.0f : 1.0f) * oy / 2.0f;

					a.at.y += d;
					b.at.y -= d;
				}
			}
		if (!moved)
			break;
	}
}

} /* namespace */

int
draw_sound_cabinet(const MenuKit &kit, ImVec2 a, ImVec2 b, const SoundScene &scene)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const Camera cam = camera();
	const Tones tone = tones(kit.palette());
	const auto project = [&cam](Vec3 p) { return ImVec2{ dot(p, cam.r), -dot(p, cam.u) }; };
	ImVec2 lo = { FLT_MAX, FLT_MAX }, hi = { -FLT_MAX, -FLT_MAX };
	const auto grow = [&lo, &hi](ImVec2 p) {
		lo = { std::min(lo.x, p.x), std::min(lo.y, p.y) };
		hi = { std::max(hi.x, p.x), std::max(hi.y, p.y) };
	};

	/* The faces the eye sees of each block, the farthest block first */
	struct Drawn {
		std::vector<std::array<ImVec2, 4>> faces;
		Shade shade;
		float depth;
	};
	std::vector<Drawn> drawn;

	for (const Block &block : blocks(scene.seated)) {
		const Vec3 centre = { (block.x0 + block.x1) / 2.0f, (block.y0 + block.y1) / 2.0f, (block.z0 + block.z1) / 2.0f };
		Drawn d = { {}, block.shade, block.shade == Shade::FLOOR ? -FLT_MAX : dot(centre, cam.c) };

		for (const Face &face : faces(block)) {
			if (dot(face.n, cam.c) <= 0.02f)
				continue;

			std::array<ImVec2, 4> q;

			for (size_t i = 0; i < q.size(); i++) {
				q[i] = project(face.q[i]);
				grow(q[i]);
			}
			d.faces.push_back(q);
		}
		drawn.push_back(std::move(d));
	}
	std::stable_sort(drawn.begin(), drawn.end(), [](const Drawn &x, const Drawn &y) { return x.depth < y.depth; });

	std::vector<Mark> marks;

	for (size_t i = 0; i < scene.parts.size(); i++) {
		const SoundPart &part = scene.parts[i];

		if (part.at == Place::NONE)
			continue;

		const Vec3 s = spot(part.at, scene.seated);
		const ImVec2 q = project(s);
		const float hx = part.kind == Kind::SHAKER ? 22.0f : part.kind == Kind::WOOFER ? 15.0f : 11.0f;
		const float hy = part.kind == Kind::SHAKER ? 9.0f : hx;

		grow(q);
		marks.push_back({ static_cast<int>(i), q, kit.px(hx), kit.px(hy), dot(s, cam.c) });
	}

	/* The whole drawing in the room, its middle in the middle */
	const float margin = kit.px(22.0f), w = std::max(hi.x - lo.x, 1.0f), h = std::max(hi.y - lo.y, 1.0f);
	const float scale = std::min({ (b.x - a.x - 2.0f * margin) / w, (b.y - a.y - 2.0f * margin) / h, kit.px(2.4f) });
	const ImVec2 origin = { a.x + (b.x - a.x - w * scale) / 2.0f - lo.x * scale,
	                        a.y + (b.y - a.y - h * scale) / 2.0f - lo.y * scale };
	const auto place = [&origin, scale](ImVec2 p) { return ImVec2{ origin.x + p.x * scale, origin.y + p.y * scale }; };

	for (Drawn &d : drawn) {
		const ImU32 fill = d.shade == Shade::SCREEN ? tone.screen : d.shade == Shade::FLOOR ? tone.floor :
		                   d.shade == Shade::SEAT ? tone.seat : tone.body;

		for (std::array<ImVec2, 4> &q : d.faces) {
			for (ImVec2 &p : q)
				p = place(p);
			if (d.shade != Shade::FLOOR && d.shade != Shade::SCREEN)
				list->AddPolyline(q.data(), 4, kit.palette().screen, ImDrawFlags_Closed, kit.px(3.2f));
			list->AddConvexPolyFilled(q.data(), 4, fill);
			if (d.shade != Shade::SCREEN)
				list->AddPolyline(q.data(), 4, fill, ImDrawFlags_Closed, kit.px(1.0f));
		}
	}

	for (Mark &mark : marks)
		mark.at = place(mark.at);
	spread(marks, kit.px(8.0f));
	std::stable_sort(marks.begin(), marks.end(), [](const Mark &x, const Mark &y) { return x.depth < y.depth; });

	int under = -1;

	for (const Mark &mark : marks) {
		const SoundPart &part = scene.parts[static_cast<size_t>(mark.part)];
		const unsigned feeds = sound_feeds(scene.family, scene.played, part);
		const Lit lit = mark.part == scene.pointed || (feeds & scene.pointed_speakers) ? Lit::POINTED :
		                feeds ? Lit::ON : Lit::OFF;

		const bool woofer = part.kind == Kind::WOOFER;

		if (part.kind == Kind::SHAKER)
			shaker_mark(kit, list, mark.at, lit, tone);
		else
			speaker_mark(kit, list, mark.at, kit.px(woofer ? 14.0f : 10.0f), woofer, lit, tone);

		const float pad = kit.px(4.0f);

		if (ImGui::IsMouseHoveringRect({ mark.at.x - mark.hx - pad, mark.at.y - mark.hy - pad },
		                               { mark.at.x + mark.hx + pad, mark.at.y + mark.hy + pad }))
			under = mark.part;
	}
	return under;
}

ImVec2
your_speakers_size(const MenuKit &kit, const SoundScene &scene)
{
	const float side = kit.px(18.0f), gap = kit.px(6.0f);
	const bool four = your_speakers(scene.family, scene.played) & YOUR_REAR_LEFT;

	return { 2.0f * side + gap, four ? 2.0f * side + gap : side };
}

unsigned
draw_your_speakers(const MenuKit &kit, ImVec2 at, const SoundScene &scene)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const Tones tone = tones(kit.palette());
	const float side = kit.px(18.0f), gap = kit.px(6.0f);
	const unsigned all = your_speakers(scene.family, scene.played);
	const bool four = all & YOUR_REAR_LEFT;
	unsigned heard = 0, pointed = scene.pointed_speakers, under = 0;

	for (const SoundPart &part : scene.parts)
		heard |= sound_feeds(scene.family, scene.played, part);
	if (scene.pointed >= 0 && static_cast<size_t>(scene.pointed) < scene.parts.size())
		pointed = sound_feeds(scene.family, scene.played, scene.parts[static_cast<size_t>(scene.pointed)]);

	const unsigned order[4] = { four ? YOUR_FRONT_LEFT : YOUR_LEFT, four ? YOUR_FRONT_RIGHT : YOUR_RIGHT, YOUR_REAR_LEFT,
	                            YOUR_REAR_RIGHT };

	for (unsigned i = 0; i < (four ? 4u : 2u); i++) {
		const float column = static_cast<float>(i % 2), row = static_cast<float>(i / 2);
		const ImVec2 a = { at.x + column * (side + gap), at.y + row * (side + gap) };
		const ImVec2 c = { a.x + side / 2.0f, a.y + side / 2.0f };
		const Lit lit = pointed & order[i] ? Lit::POINTED : heard & order[i] ? Lit::ON : Lit::OFF;

		speaker_mark(kit, list, c, side / 2.0f - kit.px(2.0f), false, lit, tone);
		if (ImGui::IsMouseHoveringRect(a, { a.x + side, a.y + side }))
			under = order[i];
	}
	return under;
}

} /* namespace odin */
