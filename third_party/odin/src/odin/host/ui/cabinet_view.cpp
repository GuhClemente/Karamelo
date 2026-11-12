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

/* The six cabinets, as drawn for the menu: the same shapes at the same
 * places, in the units of that drawing. */

#include "odin/host/ui/cabinet_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace odin {

namespace {

constexpr float PI = std::numbers::pi_v<float>;

/* How a shape is drawn: on the body or on a part of the cabinet (filled,
 * outlined), outlined only, a detail, a thick detail or a line of the ink
 * (its width in units of the drawing), the accent of what lights (a line:
 * as wide as told) */
enum class Ink { BODY, PART, LINE, DETAIL, THICK, INK, LIT };

/* Where a text stands from its point, on its base line */
enum class Align { LEFT, CENTRE, RIGHT };

/* The strokes of the outlines and the details on the screen, in points;
 * those of a control worked; how much a button sinks; the room around the
 * drawing in its units */
constexpr float OUTLINE = 1.1f;
constexpr float DETAIL = 0.8f;
constexpr float HOT_OUTLINE = 1.7f;
constexpr float SINK = 0.07f;
constexpr float MARGIN = 6.0f;
/* The width of a line of the ink (a sight), in units of the drawing */
constexpr float INK_WIDTH = 1.8f;
/* The size of a text, of a big one, in units of the drawing */
constexpr float TEXT = 19.0f;
constexpr float BIG = 24.0f;

/* How a control lights: worked now (the accent, its outline thicker), its
 * input caught now (the accent) */
struct Light {
	bool worked = false, chosen = false;

	Light operator|(Light other) const { return { worked || other.worked, chosen || other.chosen }; }
};

/* An affine map of the plane: x' = a x + c y + e, y' = b x + d y + f */
struct Map {
	float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

	ImVec2 operator()(ImVec2 p) const { return { a * p.x + c * p.y + e, b * p.x + d * p.y + f }; }
	Map then(const Map &m) const
	{
		return { a * m.a + c * m.b, b * m.a + d * m.b, a * m.c + c * m.d, b * m.c + d * m.d, a * m.e + c * m.f + e,
		         b * m.e + d * m.f + f };
	}
};

/* What draws a cabinet: first it measures (its extent, in its units), then
 * it is placed in the box and draws; what it draws moves (translated,
 * turned, scaled), and a control lit is in the accent. */
class Pen {
public:
	Pen(const MenuKit &kit) : m_kit(kit), m_list(ImGui::GetWindowDrawList()) {}

	/* The extent measured; then where it goes in the box */
	ImVec4 extent() const { return { m_lo.x, m_lo.y, m_hi.x, m_hi.y }; }
	void place(ImVec2 a, ImVec2 b)
	{
		const float w = m_hi.x - m_lo.x + 2.0f * MARGIN, h = m_hi.y - m_lo.y + 2.0f * MARGIN;

		m_scale = std::max(0.0f, std::min((b.x - a.x) / w, (b.y - a.y) / h));
		m_origin = { (a.x + b.x - w * m_scale) / 2.0f - (m_lo.x - MARGIN) * m_scale,
		             (a.y + b.y - h * m_scale) / 2.0f - (m_lo.y - MARGIN) * m_scale };
		m_measuring = false;
	}

	/* The moves of what follows, until pop */
	void push() { m_stack.push_back(m_map); }
	void pop() { m_map = m_stack.back(), m_stack.pop_back(); }
	void move(float x, float y) { m_map = m_map.then({ 1, 0, 0, 1, x, y }); }
	void turn(float degrees, float x, float y)
	{
		const float r = degrees * PI / 180.0f, c = std::cos(r), s = std::sin(r);

		move(x, y);
		m_map = m_map.then({ c, s, -s, c, 0, 0 });
		move(-x, -y);
	}
	void scale(float sx, float sy, float x, float y)
	{
		move(x, y);
		m_map = m_map.then({ sx, 0, 0, sy, 0, 0 });
		move(-x, -y);
	}
	/* A button sinks when pressed */
	void sink(bool on, float x, float y)
	{
		if (on)
			scale(1.0f - SINK, 1.0f - SINK, x, y);
	}
	/* What follows is of a control that lights so (none: it does not) */
	void light(Light how = {}) { m_light = how; }

	void shape(std::vector<ImVec2> points, bool closed, Ink ink, float width = 0.0f, float alpha = 1.0f);
	void circle(float x, float y, float r, Ink ink, float alpha = 1.0f) { ellipse(x, y, r, r, ink, alpha); }
	void ellipse(float x, float y, float rx, float ry, Ink ink, float alpha = 1.0f);
	void rect(float x, float y, float w, float h, float r, Ink ink, float alpha = 1.0f);
	void ring(float x, float y, float outer, float inner);
	void line(float x1, float y1, float x2, float y2, Ink ink, float width = 0.0f);
	void arc(float x, float y, float r, float from, float to, Ink ink, float width, float alpha = 1.0f);
	void curve(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, Ink ink, float width);
	void dashes(float x1, float y1, float x2, float y2, float on, float off);
	/* A text on its base line, turned with what draws it when `turned` */
	void text(float x, float y, const char *text, Align align, float size = TEXT, bool ink = false,
	          bool turned = false);
	/* A legend: texts and the marks of a trigger (an arc) and of a button
	 * (a dot) between them, as one line */
	void legend(float x, float y, const char *first, const char *second);

private:
	ImVec2 at(ImVec2 p);
	bool lit() const { return m_light.worked || m_light.chosen; }
	void stroke(std::span<const ImVec2> points, bool closed, Ink ink, float width);

	const MenuKit &m_kit;
	ImDrawList *m_list;
	bool m_measuring = true;
	Light m_light;
	ImVec2 m_lo = { 1e9f, 1e9f }, m_hi = { -1e9f, -1e9f };
	ImVec2 m_origin = {};
	float m_scale = 1.0f;
	Map m_map;
	std::vector<Map> m_stack;
	std::vector<ImVec2> m_points;
};

/* A point of the drawing on the screen; while measuring, the extent grows */
ImVec2
Pen::at(ImVec2 p)
{
	const ImVec2 q = m_map(p);

	if (m_measuring) {
		m_lo = { std::min(m_lo.x, q.x), std::min(m_lo.y, q.y) };
		m_hi = { std::max(m_hi.x, q.x), std::max(m_hi.y, q.y) };
	}
	return { m_origin.x + q.x * m_scale, m_origin.y + q.y * m_scale };
}

void
Pen::stroke(std::span<const ImVec2> points, bool closed, Ink ink, float width)
{
	const MenuPalette &palette = m_kit.palette();
	const bool outline = ink == Ink::BODY || ink == Ink::PART || ink == Ink::LINE;
	ImU32 colour = ink == Ink::INK ? palette.ink : outline ? palette.cabinet_stroke : palette.cabinet_detail;
	float w = outline             ? m_kit.px(m_light.worked ? HOT_OUTLINE : OUTLINE) :
	          ink == Ink::DETAIL ? m_kit.px(DETAIL) :
	          ink == Ink::INK    ? (width > 0.0f ? width : INK_WIDTH) * m_scale :
	                               width * m_scale;

	if (lit() || ink == Ink::LIT)
		colour = palette.accent;
	if (ink == Ink::DETAIL && width > 0.0f)
		w = width * m_scale;
	m_list->AddPolyline(points.data(), static_cast<int>(points.size()), colour,
	                    closed ? ImDrawFlags_Closed : ImDrawFlags_None, std::max(w, 1.0f));
}

void
Pen::shape(std::vector<ImVec2> points, bool closed, Ink ink, float width, float alpha)
{
	for (ImVec2 &p : points)
		p = at(p);
	if (m_measuring || points.size() < 2)
		return;

	const MenuPalette &palette = m_kit.palette();

	if (closed && points.size() > 2 && (ink == Ink::BODY || ink == Ink::PART || ink == Ink::LIT)) {
		ImU32 fill = ink == Ink::BODY ? palette.body : ink == Ink::PART ? palette.part : palette.accent;

		if (alpha < 1.0f)
			fill = MenuKit::alpha(fill, alpha);
		m_list->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), fill);
	}
	if (ink != Ink::LIT || !closed)
		stroke(points, closed, ink, width);
}

void
Pen::ellipse(float x, float y, float rx, float ry, Ink ink, float alpha)
{
	const int n = std::clamp(static_cast<int>(std::max(rx, ry) / 2.0f), 16, 64);
	std::vector<ImVec2> points;

	for (int k = 0; k < n; k++) {
		const float t = 2.0f * PI * static_cast<float>(k) / static_cast<float>(n);

		points.push_back({ x + rx * std::cos(t), y + ry * std::sin(t) });
	}
	shape(std::move(points), true, ink, 0.0f, alpha);
}

/* A rectangle, its corners rounded */
void
Pen::rect(float x, float y, float w, float h, float r, Ink ink, float alpha)
{
	std::vector<ImVec2> points;
	const std::array<std::array<float, 3>, 4> corners = { { { x + w - r, y + r, -PI / 2 }, { x + w - r, y + h - r, 0 },
	                                                       { x + r, y + h - r, PI / 2 }, { x + r, y + r, PI } } };

	for (const auto &[cx, cy, start] : corners)
		for (int k = 0; k <= 6; k++) {
			const float t = start + PI / 2.0f * static_cast<float>(k) / 6.0f;

			points.push_back({ cx + r * std::cos(t), cy + r * std::sin(t) });
		}
	shape(std::move(points), true, ink, 0.0f, alpha);
}

/* The rim of a wheel: filled between its two edges, both outlined */
void
Pen::ring(float x, float y, float outer, float inner)
{
	constexpr int N = 72;
	std::vector<ImVec2> out, in;

	for (int k = 0; k < N; k++) {
		const float t = 2.0f * PI * static_cast<float>(k) / N;

		out.push_back(at({ x + outer * std::cos(t), y + outer * std::sin(t) }));
		in.push_back(at({ x + inner * std::cos(t), y + inner * std::sin(t) }));
	}
	if (m_measuring)
		return;
	for (int k = 0; k < N; k++) {
		const int j = (k + 1) % N;

		m_list->AddQuadFilled(out[static_cast<size_t>(k)], out[static_cast<size_t>(j)], in[static_cast<size_t>(j)],
		                      in[static_cast<size_t>(k)], m_kit.palette().body);
	}
	stroke(out, true, Ink::BODY, 0.0f);
	stroke(in, true, Ink::BODY, 0.0f);
}

void
Pen::line(float x1, float y1, float x2, float y2, Ink ink, float width)
{
	shape({ { x1, y1 }, { x2, y2 } }, false, ink, width);
}

void
Pen::arc(float x, float y, float r, float from, float to, Ink ink, float width, float alpha)
{
	std::vector<ImVec2> points;

	for (int k = 0; k <= 16; k++) {
		const float t = from + (to - from) * static_cast<float>(k) / 16.0f;

		points.push_back(at({ x + r * std::cos(t), y + r * std::sin(t) }));
	}
	if (m_measuring)
		return;

	const ImU32 colour =
		MenuKit::alpha(ink == Ink::LIT || lit() ? m_kit.palette().accent : m_kit.palette().cabinet_detail, alpha);
	m_list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, ImDrawFlags_None, width * m_scale);
}

void
Pen::curve(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, Ink ink, float width)
{
	std::vector<ImVec2> points;

	for (int k = 0; k <= 16; k++) {
		const float t = static_cast<float>(k) / 16.0f, u = 1.0f - t;

		points.push_back({ u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
		                   u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y });
	}
	shape(std::move(points), false, ink, width);
}

void
Pen::dashes(float x1, float y1, float x2, float y2, float on, float off)
{
	const ImVec2 p = at({ x1, y1 }), q = at({ x2, y2 });
	const ImVec2 points[2] = { p, q };

	if (!m_measuring)
		MenuKit::dashes(m_list, points, false, lit() ? m_kit.palette().accent : m_kit.palette().cabinet_detail,
		                m_kit.px(DETAIL), on * m_scale, off * m_scale);
}

void
Pen::text(float x, float y, const char *text, Align align, float size, bool ink, bool turned)
{
	ImFont *const font = ImGui::GetFont();
	const ImVec2 measure = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
	const float shift = align == Align::LEFT ? 0.0f : align == Align::CENTRE ? measure.x / 2.0f : measure.x;
	/* The base line about four fifths of the way down */
	const ImVec2 corner = { x - shift, y - measure.y * 0.8f };

	if (m_measuring) {
		if (turned) {
			at(corner), at({ corner.x + measure.x, corner.y + measure.y });
		} else {
			const ImVec2 o = m_map({ x, y });
			const Map was = m_map;

			m_map = {};
			at({ o.x - shift, o.y - measure.y * 0.8f }), at({ o.x - shift + measure.x, o.y + measure.y * 0.2f });
			m_map = was;
		}
		return;
	}

	const MenuPalette &palette = m_kit.palette();
	const ImU32 colour = lit() ? palette.accent : ink ? palette.ink : palette.cabinet;
	/* Drawn at the size of the font that is nearest, the place kept */
	const float screen_size = MenuKit::text_size(size * m_scale);
	const ImVec2 shown = font->CalcTextSizeA(screen_size, 1e9f, 0.0f, text);
	const float shown_shift = align == Align::LEFT ? 0.0f : align == Align::CENTRE ? shown.x / 2.0f : shown.x;

	if (!turned) {
		const ImVec2 o = m_map({ x, y });
		const ImVec2 p = { m_origin.x + o.x * m_scale - shown_shift, m_origin.y + o.y * m_scale - shown.y * 0.8f };

		m_list->AddText(font, screen_size, p, colour, text);
		return;
	}

	/* Turned: drawn straight from its point where it goes (not cut by the
	 * box while it lies), then turned about that point as what draws it
	 * turns */
	const ImVec2 o = m_map({ x, y });
	const ImVec2 pivot = { m_origin.x + o.x * m_scale, m_origin.y + o.y * m_scale };
	const float length = std::hypot(m_map.a, m_map.b), c = m_map.a / length, s = m_map.b / length;
	const int first = m_list->VtxBuffer.Size;

	m_list->PushClipRectFullScreen();
	m_list->AddText(font, screen_size, { pivot.x - shown_shift, pivot.y - shown.y * 0.8f }, colour, text);
	m_list->PopClipRect();
	for (int v = first; v < m_list->VtxBuffer.Size; v++) {
		ImVec2 &q = m_list->VtxBuffer[v].pos;
		const ImVec2 d = { q.x - pivot.x, q.y - pivot.y };

		q = { pivot.x + c * d.x - s * d.y, pivot.y + s * d.x + c * d.y };
	}
}

void
Pen::legend(float x, float y, const char *first, const char *second)
{
	ImFont *const font = ImGui::GetFont();
	const char *const between = "  ·  ";
	const float a = font->CalcTextSizeA(TEXT, 1e9f, 0.0f, first).x, mark = TEXT * 0.8f;
	const float b = font->CalcTextSizeA(TEXT, 1e9f, 0.0f, between).x, c = font->CalcTextSizeA(TEXT, 1e9f, 0.0f, second).x;
	const float start = x - (a + mark + b + c + mark) / 2.0f, mid = y - TEXT * 0.3f;

	text(start, y, first, Align::LEFT);
	arc(start + a + mark / 2.0f, mid + TEXT * 0.25f, mark * 0.35f, PI * 1.15f, PI * 1.85f, Ink::THICK, 2.0f);
	text(start + a + mark, y, between, Align::LEFT);
	text(start + a + mark + b, y, second, Align::LEFT);
	if (!m_measuring) {
		const ImVec2 dot = at({ start + a + mark + b + c + mark / 2.0f, mid });

		m_list->AddCircleFilled(dot, mark * 0.25f * m_scale, lit() ? m_kit.palette().accent : m_kit.palette().cabinet);
	}
}

/* The controls of the cabinet by their names in the list of games */
class Controls {
public:
	explicit Controls(const CabinetNow &now) : m_now(now) {}

	bool has(const char *id) const { return control(id) != nullptr; }
	bool on(const char *id) const
	{
		const std::string_view name = id;

		if (name == "coin")
			return m_now.coin;
		if (name == "service")
			return m_now.service;
		if (name == "test")
			return m_now.test;

		const ControlNow *const c = find(id);

		return c && c->on;
	}
	Light light(const char *id) const { return { on(id), !m_now.chosen.empty() && m_now.chosen == id }; }
	float value(const char *id) const
	{
		const ControlNow *const c = find(id);

		return c ? std::clamp(c->value, -1.0f, 1.0f) : 0.0f;
	}
	const GameControl *control(const char *id) const
	{
		for (const GameControl &c : m_now.controls)
			if (c.id == id)
				return &c;
		return nullptr;
	}
	size_t position(const char *id) const
	{
		const ControlNow *const c = find(id);

		return c ? c->position : 0;
	}

private:
	const ControlNow *find(const char *id) const
	{
		for (size_t c = 0; c < m_now.controls.size() && c < m_now.now.size(); c++)
			if (m_now.controls[c].id == id)
				return &m_now.now[c];
		return nullptr;
	}

	const CabinetNow &m_now;
};

/* A round button: its rim, its cap (lit, sinking when pressed), its name
 * beside it ('r', 'l', 'b' below, 't' above) */
void
push_button(Pen &pen, const Controls &c, const char *id, float x, float y, float r, const char *label, char side)
{
	if (!c.has(id))
		return;

	const bool on = c.on(id);

	r = std::round(r * 1.4f);
	pen.light(c.light(id));
	pen.circle(x, y, r + 5.0f, Ink::DETAIL);
	pen.push();
	pen.sink(on, x, y);
	pen.circle(x, y, r, Ink::PART);
	if (on)
		pen.circle(x, y, r - 1.0f, Ink::LIT);
	pen.pop();
	if (side == 'r')
		pen.text(x + r + 12.0f, y + 4.0f, label, Align::LEFT);
	else if (side == 'l')
		pen.text(x - r - 12.0f, y + 4.0f, label, Align::RIGHT);
	else if (side == 'b')
		pen.text(x, y + r + 20.0f, label, Align::CENTRE);
	else
		pen.text(x, y - r - 12.0f, label, Align::CENTRE);
	pen.light();
}

/* COIN, SERVICE and TEST in a row from there; then the VOLUME rocker of
 * the cabinets that have one by them (the Gaelco ones: behind the coin
 * door, ATV-EN p. 19, SD-SIT p. 32, GF-EN p. 20: the English manual of ATV
 * Track, the sit-down one of Smashing Drive, the English one of Gaelco
 * Football) */
void
cabinet_row(Pen &pen, const Controls &c, float x, float y)
{
	const std::array<std::tuple<const char *, float, const char *>, 3> buttons = { {
		{ "coin", 0.0f, "COIN" }, { "service", 112.0f, "SERVICE" }, { "test", 250.0f, "TEST" } } };

	for (const auto &[id, dx, label] : buttons) {
		pen.light(c.light(id));
		pen.circle(x + dx, y, 12.0f, Ink::PART);
		if (c.on(id))
			pen.circle(x + dx, y, 11.0f, Ink::LIT);
		pen.text(x + dx + 19.0f, y + 6.0f, label, Align::LEFT);
		pen.light();
	}
	if (!c.has("volume_up"))
		return;

	const float vx = x + 340.0f;

	pen.rect(vx, y - 13.0f, 56.0f, 26.0f, 6.0f, Ink::LINE);
	for (const auto &[id, dx, sign] : { std::tuple{ "volume_down", 0.0f, "-" }, std::tuple{ "volume_up", 28.0f, "+" } }) {
		pen.light(c.light(id));
		pen.rect(vx + dx + 3.0f, y - 10.0f, 22.0f, 20.0f, 4.0f, c.on(id) ? Ink::LIT : Ink::PART);
		/* Its sign dark on the lit key */
		pen.light();
		pen.text(vx + dx + 14.0f, y + 6.0f, sign, Align::CENTRE, TEXT, c.on(id));
	}
	pen.text(vx + 66.0f, y + 6.0f, "VOLUME", Align::LEFT);
}

/* How wide that row is */
float
cabinet_row_width(const Controls &c)
{
	return c.has("volume_up") ? 470.0f : 316.0f;
}

/* A pedal: its arm, the pedal going down as far as it is pushed */
void
pedal(Pen &pen, float x, float y, float w, float h, float v, const char *label)
{
	v = std::clamp(v, 0.0f, 1.0f);
	pen.push();
	if (v > 0.005f) {
		pen.move(0.0f, v * 12.0f);
		pen.scale(1.0f - 0.05f * v, 1.0f - 0.3f * v, x + w / 2.0f, y + h);
	}
	pen.line(x + w / 2.0f, y - 22.0f, x + w / 2.0f, y + 6.0f, Ink::DETAIL, 3.0f);
	pen.rect(x, y, w, h, 8.0f, Ink::PART);
	if (v > 0.0f)
		pen.rect(x, y, w, h, 8.0f, Ink::LIT, v * 0.9f);
	pen.pop();
	pen.text(x + w / 2.0f, y + h + 38.0f, label, Align::CENTRE);
}

void
nascar(Pen &pen, const Controls &c)
{
	pen.curve({ 30, 86 }, { 210, 71.3f }, { 390, 71.3f }, { 570, 86 }, Ink::DETAIL, 0.0f);
	push_button(pen, c, "start", 120, 44, 14, "START", 'r');
	push_button(pen, c, "view_change", 300, 44, 14, "VIEW CHANGE", 'r');

	/* The wheel turns 135 degrees each way */
	const float cx = 220, cy = 244, R = 144, r = 118;

	pen.light(c.light("steering_wheel"));
	pen.push();
	pen.turn(c.value("steering_wheel") * 135.0f, cx, cy);
	pen.ring(cx, cy, R, r);
	pen.line(cx, cy - R + 3, cx, cy - r - 3, Ink::INK, 4.0f);
	pen.shape({ { cx - r, cy - 12 }, { cx - 36, cy - 18 }, { cx - 36, cy + 16 }, { cx - r, cy + 12 } }, true, Ink::PART);
	pen.shape({ { cx + r, cy - 12 }, { cx + 36, cy - 18 }, { cx + 36, cy + 16 }, { cx + r, cy + 12 } }, true, Ink::PART);
	pen.shape({ { cx - 16, cy + 34 }, { cx + 16, cy + 34 }, { cx + 10, cy + r }, { cx - 10, cy + r } }, true, Ink::PART);
	pen.circle(cx, cy, 40, Ink::PART);
	pen.circle(cx, cy, 20, Ink::DETAIL);
	pen.pop();

	/* The shifter: N in the middle, 1 and 3 up, 2 and 4 down */
	const float sx = 486, sy = 250;
	const std::array<ImVec2, 5> places = { { { 0, 0 }, { -26, -50 }, { -26, 50 }, { 26, -50 }, { 26, 50 } } };
	const GameControl *const gear = c.control("gear");
	const size_t position = std::min<size_t>(c.position("gear"), places.size() - 1);

	pen.light(c.light("gear"));
	pen.rect(sx - 60, sy - 82, 120, 164, 14, Ink::BODY);
	pen.line(sx - 26, sy - 50, sx - 26, sy + 50, Ink::THICK, 7.0f);
	pen.line(sx + 26, sy - 50, sx + 26, sy + 50, Ink::THICK, 7.0f);
	pen.line(sx - 26, sy, sx + 26, sy, Ink::THICK, 7.0f);
	for (size_t k = 1; k < places.size(); k++) {
		const char digit[2] = { static_cast<char>('0' + k), 0 };

		pen.text(sx + places[k].x + (places[k].x < 0 ? -17.0f : 17.0f), sy + places[k].y + 4, digit, Align::CENTRE);
	}
	pen.circle(sx + places[position].x, sy + places[position].y, 15, Ink::PART);
	if (c.on("gear"))
		pen.circle(sx + places[position].x, sy + places[position].y, 14, Ink::LIT);

	const std::string name = gear && position < gear->positions.size() ? gear->positions[position].name : "N";

	pen.text(sx, sy + 108, ("GEAR " + name).c_str(), Align::CENTRE, BIG);
	pen.text(sx, sy - 96, "SHIFT LEVER", Align::CENTRE);
	pen.light(c.light("brake_pedal"));
	pedal(pen, 140, 440, 64, 74, c.value("brake_pedal"), "BRAKE");
	pen.light(c.light("gas_pedal"));
	pedal(pen, 242, 426, 48, 88, c.value("gas_pedal"), "GAS");
	pen.light();
}

/* A nozzle of Brave, drawn lying, then stood up: its hose, its body, FOG STREAM SW (the grip that turns) and
 * DRAINAGE (the button on top) */
void
nozzle(Pen &pen, const Controls &c, unsigned p)
{
	const std::string fog = std::to_string(p) + "p_fog", water = std::to_string(p) + "p_water";
	const bool fog_on = c.on(fog.c_str()), water_on = c.on(water.c_str());

	pen.curve({ 64, 12 }, { 86, 4 }, { 104, 22 }, { 128, 6 }, Ink::THICK, 10.0f);
	pen.rect(128, -20, 24, 40, 6, Ink::PART);
	pen.rect(150, -15, 300, 30, 11, Ink::BODY);
	pen.rect(448, -18, 14, 36, 5, Ink::PART);
	pen.light(c.light(fog.c_str()));
	pen.rect(462, -24, 90, 48, 11, Ink::PART);
	if (fog_on)
		pen.rect(463, -23, 88, 46, 10, Ink::LIT, 0.3f);
	for (int k = 0; k < 7; k++)
		pen.line(476.0f + static_cast<float>(k) * 11.0f, -18, 476.0f + static_cast<float>(k) * 11.0f, 18, Ink::DETAIL);
	pen.light();
	pen.shape({ { 552, -14 }, { 640, -24 }, { 646.7f, -24 }, { 650, -20.7f }, { 650, -14 }, { 650, 14 }, { 650, 20.7f },
	            { 646.7f, 24 }, { 640, 24 }, { 552, 14 } },
	          true, Ink::BODY);
	pen.ellipse(650, 0, 6, 22, Ink::PART);
	pen.light(c.light(water.c_str()));
	pen.push();
	if (water_on)
		pen.move(0, 2);
	pen.ellipse(310, -15, 16, 7, Ink::PART);
	if (water_on)
		pen.ellipse(310, -15, 15, 6, Ink::LIT);
	pen.pop();
	pen.light();
	if (p == 1) {
		pen.text(310, -32, "DRAINAGE", Align::CENTRE, TEXT, false, true);
		pen.text(507, -34, "FOG STREAM SW", Align::CENTRE, TEXT, false, true);
	}
}

void
brave(Pen &pen, const Controls &c)
{
	const float X = 112, Y = 196, W = 476, H = 357;

	pen.light(c.light("1p_sight_x") | c.light("1p_sight_y") | c.light("2p_sight_x") | c.light("2p_sight_y"));
	pen.rect(X, Y, W, H, 8, Ink::BODY);
	for (int k = 1; k < 4; k++) {
		const float fx = X + static_cast<float>(k) * W / 4.0f, fy = Y + static_cast<float>(k) * H / 4.0f;

		pen.line(fx, Y + 6, fx, Y + H - 6, Ink::DETAIL, 1.0f);
		pen.line(X + 6, fy, X + W - 6, fy, Ink::DETAIL, 1.0f);
	}
	pen.light();

	/* The nozzles stand on each side of the screen, START under each */
	for (const auto &[p, tx] : { std::pair{ 1u, 56.0f }, std::pair{ 2u, 644.0f } }) {
		const std::string start = std::to_string(p) + "p_start";

		pen.push();
		pen.move(tx, 762);
		pen.turn(-90, 0, 0);
		nozzle(pen, c, p);
		pen.pop();
		pen.text(tx + 4, 722, p == 1 ? "1P" : "2P", Align::CENTRE, BIG);
		push_button(pen, c, start.c_str(), tx, 788, 13, "", 'b');
		pen.light(c.light(start.c_str()));
		pen.text(p == 1 ? tx + 30 : tx - 30, 794, "START", p == 1 ? Align::LEFT : Align::RIGHT);
		pen.light();
	}

	/* Where each nozzle aims, and when it is off the screen */
	for (const unsigned p : { 1u, 2u }) {
		const std::string x = std::to_string(p) + "p_sight_x", y = std::to_string(p) + "p_sight_y";
		const std::string off = std::to_string(p) + "p_out_of_screen";
		const bool away = c.on(off.c_str());
		const float px = X + (c.value(x.c_str()) + 1.0f) / 2.0f * W, py = Y + (c.value(y.c_str()) + 1.0f) / 2.0f * H;
		const char number[2] = { static_cast<char>('0' + p), 0 };

		pen.light(c.light(x.c_str()) | c.light(y.c_str()));
		if (!away) {
			pen.ellipse(px, py, 14, 14, Ink::INK);
			pen.line(px - 22, py, px - 8, py, Ink::INK, 1.8f);
			pen.line(px + 8, py, px + 22, py, Ink::INK, 1.8f);
			pen.line(px, py - 22, px, py - 8, Ink::INK, 1.8f);
			pen.line(px, py + 8, px, py + 22, Ink::INK, 1.8f);
			pen.text(px + 18, py - 14, number, Align::LEFT, TEXT, true);
		}
		pen.light(c.light(off.c_str()));
		pen.text(X + W, Y + H + (p == 1 ? 24.0f : 44.0f), away ? (p == 1 ? "1P OUT OF SCREEN" : "2P OUT OF SCREEN") : "",
		         Align::RIGHT);
		pen.light();
	}
	pen.text(X, Y - 12, "AIM", Align::LEFT);
}

void
harriers(Pen &pen, const Controls &c)
{
	const float cx = 250, cy = 340, R = 130;
	const float x = cx + c.value("stick_h") * (R - 44), y = cy + c.value("stick_v") * (R - 44);

	pen.light(c.light("stick_h") | c.light("stick_v"));
	pen.circle(cx, cy, R, Ink::BODY);
	pen.circle(cx, cy, R * 0.5f, Ink::DETAIL);
	pen.dashes(cx - R + 10, cy, cx + R - 10, cy, 3, 6);
	pen.dashes(cx, cy - R + 10, cx, cy + R - 10, 3, 6);
	pen.light();
	pen.line(cx, cy, x, y, Ink::DETAIL, 3.0f);
	pen.push();
	pen.move(x, y);
	pen.circle(0, 0, 40, Ink::PART);
	pen.light(c.light("bullet"));
	pen.arc(0, 0, 47, -PI / 2 - 0.55f, -PI / 2 + 0.55f, Ink::THICK, 7.0f);
	if (c.on("bullet"))
		pen.arc(0, 0, 47, -PI / 2 - 0.55f, -PI / 2 + 0.55f, Ink::LIT, 7.0f);
	pen.light(c.light("missile"));
	pen.circle(0, 8, 12, Ink::PART);
	if (c.on("missile"))
		pen.circle(0, 8, 11, Ink::LIT);
	pen.light();
	pen.pop();
	pen.legend(cx, cy - R - 14, "BULLET", "MISSILE");
	pen.text(cx, cy + R + 26, "STICK H · STICK V", Align::CENTRE);
	push_button(pen, c, "bomb", 130, 104, 26, "BOMB", 't');
	for (const auto &[id, bx, label] : { std::tuple{ "vr_l", 240.0f, "VR L" }, std::tuple{ "vr_r", 310.0f, "VR R" } }) {
		const bool on = c.on(id);

		pen.light(c.light(id));
		pen.push();
		pen.sink(on, bx, 104);
		pen.rect(bx - 17, 90, 34, 28, 6, Ink::PART);
		if (on)
			pen.rect(bx - 16, 91, 32, 26, 5, Ink::LIT);
		pen.pop();
		pen.text(bx, 136, label, Align::CENTRE);
		pen.light();
	}
	push_button(pen, c, "start", 390, 104, 13, "START", 'b');
}

void
racer(Pen &pen, const Controls &c)
{
	const float dy = 230;

	pen.rect(20, 20, 480, 580, 30, Ink::BODY);
	for (const auto &[id, x, brake] : { std::tuple{ "left_lever", 130.0f, false }, std::tuple{ "right_lever", 390.0f, true } }) {
		pen.rect(x - 12, 78 + dy, 24, 268, 12, Ink::PART);
		pen.light(c.light(id));
		pen.push();
		pen.move(0, c.value(id) * 110.0f);
		pen.rect(x - 74, 200 + dy, 148, 38, 19, Ink::PART);
		for (int k = 0; k < 3; k++)
			pen.line(x - 30 + static_cast<float>(k) * 30, 208 + dy, x - 30 + static_cast<float>(k) * 30, 230 + dy, Ink::DETAIL);
		if (brake && c.has("brake")) {
			const bool on = c.on("brake");

			pen.light(c.light("brake"));
			pen.push();
			if (on)
				pen.move(0, -9);
			pen.curve({ x - 60, 254 + dy }, { x - 20, 264.7f + dy }, { x + 20, 264.7f + dy }, { x + 60, 254 + dy },
			          on ? Ink::LIT : Ink::THICK, 9.0f);
			pen.pop();
			pen.text(x - 82, 268 + dy, "BRAKE", Align::RIGHT);
		}
		pen.pop();
		pen.light();
	}
	pen.text(130, 64 + dy, "LEFT LEVER", Align::CENTRE);
	pen.text(390, 64 + dy, "RIGHT LEVER", Align::CENTRE);
	push_button(pen, c, "boost", 260, 112, 42, "BOOST", 't');
	push_button(pen, c, "left_button", 160, 206, 13, "SELECT", 'b');
	push_button(pen, c, "right_button", 360, 206, 13, "SELECT", 'b');
	push_button(pen, c, "start", 260, 210, 15, "START", 'b');
}

/* Air Trix: the board in front of us, seen by a camera low behind it */
void
air_trix(Pen &pen, const Controls &c)
{
	const float cam_y = 2.2f, cam_z = -5.5f, focal = 1700, ox = 286, oy = 214, pitch = 0.35f;
	const float cp = std::cos(pitch), sp = std::sin(pitch);
	const auto project = [&](float x, float y, float z) {
		const float yr = y - cam_y, zr = z - cam_z, yc = yr * cp + zr * sp, zc = -yr * sp + zr * cp;

		return ImVec2{ ox + focal * x / zc, oy - focal * yc / zc };
	};
	/* The outline of the deck, nose and tail rounded, in x and z */
	std::vector<ImVec2> outline;

	for (int k = 0; k <= 16; k++) {
		const float a = PI - static_cast<float>(k) / 16.0f * PI;

		outline.push_back({ 0.3f * std::cos(a), 0.78f + 0.32f * std::sin(a) });
	}
	for (int k = 0; k <= 16; k++) {
		const float a = -static_cast<float>(k) / 16.0f * PI;

		outline.push_back({ 0.3f * std::cos(a), -0.78f + 0.32f * std::sin(a) });
	}

	const auto lift = [](float z) { return std::abs(z) > 0.74f ? 0.6f * (std::abs(z) - 0.74f) * (std::abs(z) - 0.74f) : 0.0f; };
	const bool up = c.on("up"), down = c.on("down");
	const float yaw = c.value("yaw") * 0.45f, roll = c.value("roll") * 0.3f, kick = up ? -0.12f : down ? 0.12f : 0.0f;
	const auto world = [&](float x, float y, float z) {
		const float y1 = y * std::cos(kick) - z * std::sin(kick), z1 = y * std::sin(kick) + z * std::cos(kick);
		const float x2 = x * std::cos(roll) - y1 * std::sin(roll), y2 = x * std::sin(roll) + y1 * std::cos(roll);
		const float x3 = x2 * std::cos(yaw) + z1 * std::sin(yaw), z3 = -x2 * std::sin(yaw) + z1 * std::cos(yaw);

		return project(x3, y2 + 0.22f, z3);
	};
	const auto board = [&](float dx, float dy, float dz, auto keep) {
		std::vector<ImVec2> points;

		for (const ImVec2 &p : outline)
			if (keep(p.y))
				points.push_back(world(p.x * dx, lift(p.y) + dy, p.y * dz));
		return points;
	};
	const auto all = [](float) { return true; };
	const auto circle = [&](float r) {
		std::vector<ImVec2> points;

		for (int k = 0; k < 48; k++) {
			const float a = static_cast<float>(k) / 48.0f * 2.0f * PI;

			points.push_back(project(r * std::cos(a), 0, r * std::sin(a)));
		}
		return points;
	};

	pen.light(c.light("yaw"));
	pen.shape(circle(0.72f), true, Ink::BODY);
	pen.shape(circle(0.56f), true, Ink::DETAIL);
	pen.light();
	pen.shape(board(1, -0.05f, 1, all), true, Ink::DETAIL);
	pen.light(c.light("roll"));
	pen.shape(board(1, 0, 1, all), true, Ink::BODY);
	pen.shape(board(0.8f, 0.002f, 0.84f, all), true, Ink::DETAIL);
	for (const auto &[id, nose] : { std::pair{ "up", true }, std::pair{ "down", false } }) {
		const auto end = board(1, 0.003f, 1, [nose](float z) { return nose ? z > 0.7f : z < -0.7f; });
		const bool on = c.on(id);

		pen.light(c.light(id));
		pen.shape(end, true, Ink::LINE);
		if (on)
			pen.shape(end, true, Ink::LIT, 0.0f, 0.85f);
	}
	pen.light();
	pen.text(290, 425, "YAW · ROLL", Align::CENTRE);

	const ImVec2 nose = world(0, lift(1.1f) + 0.08f, 1.18f), tail = world(0, lift(1.1f) + 0.08f, -1.18f);

	pen.light(c.light("up"));
	pen.text(nose.x, nose.y - 10, "NOSE · UP", Align::CENTRE);
	pen.light(c.light("down"));
	pen.text(tail.x, tail.y + 28, "TAIL · DOWN", Align::CENTRE);
	pen.light();
	pen.rect(110, 456, 360, 112, 20, Ink::BODY);
	push_button(pen, c, "start", 200, 500, 22, "START", 'b');
	push_button(pen, c, "left", 320, 500, 17, "LEFT", 'b');
	push_button(pen, c, "right", 400, 500, 17, "RIGHT", 'b');
}

/* Virtual-On: two levers, each with its trigger and its turbo button */
void
virtual_on(Pen &pen, const Controls &c)
{
	for (const auto &[side, cx, label] : { std::tuple{ "left", 210.0f, "LEFT-LEVER" }, std::tuple{ "right", 450.0f, "RIGHT-LEVER" } }) {
		const std::string lever = std::string(side) + "_lever_";
		const auto on = [&](const char *way) { return c.on((lever + way).c_str()); };
		const auto light = [&](const char *way) { return c.light((lever + way).c_str()); };
		const float cy = 270, R = 104;
		float dx = static_cast<float>(on("right")) - static_cast<float>(on("left"));
		float dy = static_cast<float>(on("down")) - static_cast<float>(on("up"));
		std::vector<ImVec2> outer, inner;

		if (dx != 0.0f && dy != 0.0f)
			dx *= 0.72f, dy *= 0.72f;
		for (int k = 0; k < 8; k++) {
			const float a = PI / 8 + static_cast<float>(k) * PI / 4;

			outer.push_back({ cx + R * std::cos(a), cy + R * std::sin(a) });
			inner.push_back({ cx + 44 * std::cos(a), cy + 44 * std::sin(a) });
		}
		pen.light(light("up") | light("down") | light("left") | light("right"));
		pen.shape(outer, true, Ink::BODY);
		pen.shape(inner, true, Ink::DETAIL);
		pen.light();

		const float x = cx + dx * 60, y = cy + dy * 60;
		const std::string trigger = std::string(side) + "_trigger", turbo = std::string(side) + "_turbo";

		pen.line(cx, cy, x, y, Ink::DETAIL, 3.0f);
		pen.push();
		pen.move(x, y);
		pen.circle(0, 0, 36, Ink::PART);
		pen.light(c.light(trigger.c_str()));
		pen.arc(0, 0, 43, -PI / 2 - 0.5f, -PI / 2 + 0.5f, Ink::THICK, 7.0f);
		if (c.on(trigger.c_str()))
			pen.arc(0, 0, 43, -PI / 2 - 0.5f, -PI / 2 + 0.5f, Ink::LIT, 7.0f);
		pen.light(c.light(turbo.c_str()));
		pen.circle(0, 4, 12, Ink::PART);
		if (c.on(turbo.c_str()))
			pen.circle(0, 4, 11, Ink::LIT);
		pen.light();
		pen.pop();
		pen.text(cx, cy + R + 26, label, Align::CENTRE);
		pen.legend(cx, cy - R - 14, "TRIGGER", "TURBO");
	}
	push_button(pen, c, "start", 270, 60, 15, "START", 'b');
	push_button(pen, c, "pib", 390, 60, 15, "P.I.B.", 'b');
}

/* The points of a cubic curve, after its first one */
void
add_curve(std::vector<ImVec2> &points, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, int steps = 12)
{
	for (int k = 1; k <= steps; k++) {
		const float t = static_cast<float>(k) / static_cast<float>(steps), u = 1.0f - t;
		const float a = u * u * u, b = 3.0f * u * u * t, d = 3.0f * u * t * t, e = t * t * t;

		points.push_back({ a * p0.x + b * p1.x + d * p2.x + e * p3.x, a * p0.y + b * p1.y + d * p2.y + e * p3.y });
	}
}

/* A tube along a line of points, `half` on each side of it */
void
tube(Pen &pen, std::span<const ImVec2> line, float half, Ink ink)
{
	std::vector<ImVec2> left, right;

	for (size_t k = 0; k < line.size(); k++) {
		const ImVec2 a = line[k == 0 ? 0 : k - 1], b = line[k + 1 < line.size() ? k + 1 : k];
		const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(std::hypot(dx, dy), 1e-3f);

		left.push_back({ line[k].x - dy / len * half, line[k].y + dx / len * half });
		right.push_back({ line[k].x + dy / len * half, line[k].y - dx / len * half });
	}
	left.insert(left.end(), right.rbegin(), right.rend());
	pen.shape(std::move(left), true, ink);
}

/* A grip with its grooves */
void
grip(Pen &pen, float x, float y, float w, float h)
{
	pen.rect(x, y, w, h, 13.0f, Ink::PART);
	for (float gx = x + 12.0f; gx < x + w - 8.0f; gx += 10.0f)
		pen.line(gx, y + 6.0f, gx, y + h - 6.0f, Ink::DETAIL);
}

/* A brake lever in front of its grip, pulled towards it when held */
void
brake_lever(Pen &pen, const Controls &c, const char *id, float x, float y, float far)
{
	const float pulled = c.on(id) ? 9.0f : 0.0f;
	std::vector<ImVec2> line = { { x, y + 4.0f + pulled } };

	add_curve(line, line[0], { x + far * 0.3f, y - 13.0f + pulled }, { x + far * 0.8f, y - 13.0f + pulled },
	          { x + far, y - 12.0f + pulled }, 8);
	pen.light(c.light(id));
	tube(pen, line, 3.5f, c.on(id) ? Ink::LIT : Ink::PART);
	pen.light();
}

/* ATV Track from above (ATV-EN p. 7, 20, 44, 69): the handlebar on the
 * tank, the left brake lever by the left grip, the right one by the twist
 * grip of the THROTTLE, START on the throttle's housing. The handlebar
 * turns 20 degrees each way (HEURISTIC: no manual gives its travel). The
 * emergency stop on the tank is left out: Odin holds it released (pin 1.6
 * grounded), nothing works it */
void
atv_track(Pen &pen, const Controls &c)
{
	const float cx = 290, cy = 196;
	std::vector<ImVec2> tank = { { cx - 48, cy + 30 } };

	add_curve(tank, tank[0], { cx - 66, cy + 64 }, { cx - 70, cy + 124 }, { cx - 52, cy + 154 });
	add_curve(tank, tank.back(), { cx - 36, cy + 176 }, { cx + 36, cy + 176 }, { cx + 52, cy + 154 });
	add_curve(tank, tank.back(), { cx + 70, cy + 124 }, { cx + 66, cy + 64 }, { cx + 48, cy + 30 });
	pen.shape(tank, true, Ink::BODY);
	pen.push();
	pen.scale(0.66f, 0.66f, cx, cy + 104);
	pen.shape(tank, true, Ink::DETAIL);
	pen.pop();
	pen.arc(cx, cy, 118, -PI * 118.0f / 180.0f, -PI * 62.0f / 180.0f, Ink::DETAIL, 0.0f);

	pen.light(c.light("handlebar"));
	pen.text(cx, cy - 142, "HANDLEBAR", Align::CENTRE);
	pen.push();
	pen.turn(c.value("handlebar") * 20.0f, cx, cy);
	std::vector<ImVec2> bar = { { cx - 172, cy + 16 } };

	add_curve(bar, bar[0], { cx - 115, cy - 1 }, { cx - 58, cy - 7 }, { cx, cy - 7 });
	add_curve(bar, bar.back(), { cx + 66, cy - 7 }, { cx + 133, cy - 1 }, { cx + 200, cy + 16 });
	tube(pen, bar, 6.5f, Ink::PART);
	pen.rect(cx - 26, cy - 16, 52, 30, 6, Ink::PART);
	pen.line(cx, cy - 106, cx, cy - 128, Ink::INK, 4.0f);
	pen.light();

	/* Left: the grip, its bracket, the lever */
	grip(pen, cx - 232, cy + 1, 64, 30);
	pen.rect(cx - 168, cy + 2, 16, 28, 4, Ink::PART);
	brake_lever(pen, c, "left_brake", cx - 160, cy, -102);
	pen.light(c.light("left_brake"));
	pen.text(cx - 196, cy - 38, "LEFT BRAKE", Align::CENTRE);
	pen.light();

	/* Right: the bracket, the throttle's housing with START on it, the
	 * twist grip lit as far as it is turned, the lever */
	const float throttle = std::clamp(c.value("throttle"), 0.0f, 1.0f);

	pen.rect(cx + 140, cy + 2, 16, 28, 4, Ink::PART);
	pen.rect(cx + 156, cy - 8, 44, 48, 9, Ink::PART);
	push_button(pen, c, "start", cx + 178, cy + 16, 7.5f, "", 'b');
	pen.light(c.light("throttle"));
	grip(pen, cx + 200, cy + 1, 64, 30);
	if (throttle > 0.0f)
		pen.rect(cx + 201, cy + 2, 62, 28, 12, Ink::LIT, throttle * 0.9f);
	pen.arc(cx + 268, cy + 16, 19, -PI * 62.0f / 180.0f, PI * 62.0f / 180.0f, Ink::THICK, 3.5f);
	pen.text(cx + 236, cy + 92, "THROTTLE", Align::CENTRE);
	pen.light();
	brake_lever(pen, c, "right_brake", cx + 148, cy, 102);
	pen.light(c.light("right_brake"));
	pen.text(cx + 198, cy - 38, "RIGHT BRAKE", Align::CENTRE);
	pen.light(c.light("start"));
	pen.text(cx + 178, cy + 66, "START", Align::CENTRE);
	pen.light();
	pen.pop();
}

/* Smashing Drive face on (SD-SIT p. 7, 15, 18, 45): the wheel under the
 * dashboard, HORN in its middle (the game's START), the pedals under it,
 * which are switches: down or up (SD-SIT p. 18). The wheel is drawn
 * turning 135 degrees each way */
void
smashing_drive(Pen &pen, const Controls &c)
{
	const float cx = 300, cy = 228, R = 150, r = 124;

	pen.curve({ 40, cy - 118 }, { 200, cy - 136 }, { 400, cy - 136 }, { 560, cy - 118 }, Ink::DETAIL, 0.0f);
	pen.light(c.light("wheel"));
	pen.push();
	pen.turn(c.value("wheel") * 135.0f, cx, cy);
	pen.ring(cx, cy, R, r);
	pen.line(cx, cy - R + 3, cx, cy - r - 3, Ink::INK, 4.0f);
	pen.shape({ { cx - r, cy - 12 }, { cx - 40, cy - 18 }, { cx - 40, cy + 16 }, { cx - r, cy + 12 } }, true, Ink::PART);
	pen.shape({ { cx + r, cy - 12 }, { cx + 40, cy - 18 }, { cx + 40, cy + 16 }, { cx + r, cy + 12 } }, true, Ink::PART);
	pen.shape({ { cx - 16, cy + 38 }, { cx + 16, cy + 38 }, { cx + 10, cy + r }, { cx - 10, cy + r } }, true, Ink::PART);
	pen.circle(cx, cy, 44, Ink::PART);
	pen.pop();
	pen.light();
	push_button(pen, c, "horn", cx, cy, 17, "", 'b');
	pen.light(c.light("horn"));
	pen.text(cx, cy + 186, "HORN  ·  START", Align::CENTRE);
	pen.light(c.light("brake_pedal"));
	pedal(pen, 216, 452, 70, 76, c.on("brake_pedal") ? 1.0f : 0.0f, "BRAKE");
	pen.light(c.light("gas_pedal"));
	pedal(pen, 330, 436, 50, 92, c.on("gas_pedal") ? 1.0f : 0.0f, "GAS");
	pen.light();
}

/* A football: a pentagon in the middle, its seams to the edge */
void
ball_pattern(Pen &pen, float x, float y, float r)
{
	std::vector<ImVec2> pentagon;

	for (int k = 0; k < 5; k++) {
		const float a = (-90.0f + 72.0f * static_cast<float>(k)) * PI / 180.0f;
		const ImVec2 at = { x + 0.38f * r * std::cos(a), y + 0.38f * r * std::sin(a) };

		pentagon.push_back(at);
		pen.line(at.x, at.y, x + 0.86f * r * std::cos(a), y + 0.86f * r * std::sin(a), Ink::DETAIL);
	}
	pen.shape(std::move(pentagon), true, Ink::DETAIL);
}

/* Gaelco Football from above (GF-EN p. 7, 14, 22, 32, 52-54): two
 * stations under the screen, 1P on the left; on each, the stick, then A,
 * B and C from left to right, A by the stick (the game's own "How to play"
 * picture; INFERRED for the real panel), START alone at the back, and the
 * ball on its arm in front, kicked forward and sideways by the foot */
void
football(Pen &pen, const Controls &c)
{
	pen.rect(150, 26, 300, 52, 6, Ink::BODY);
	pen.line(176, 78, 424, 78, Ink::THICK, 4.0f);
	pen.shape({ { 244, 148 }, { 244, 116 }, { 356, 116 }, { 356, 148 } }, false, Ink::THICK, 6.0f);
	for (const auto &[p, cx] : { std::pair{ 1u, 162.0f }, std::pair{ 2u, 438.0f } }) {
		const std::string player = std::to_string(p) + "p_";
		const auto id = [&](const char *what) { return player + what; };
		const auto on = [&](const char *what) { return c.on(id(what).c_str()) ? 1.0f : 0.0f; };
		const auto light = [&](const char *what) { return c.light(id(what).c_str()); };
		const float cy = 236;

		pen.rect(cx - 100, cy - 88, 200, 176, 40, Ink::BODY);
		pen.rect(cx - 88, cy - 76, 176, 152, 32, Ink::DETAIL);
		pen.text(cx, cy - 104, p == 1 ? "1P" : "2P", Align::CENTRE, BIG);

		/* The stick in its gate of four switches */
		const float jx = cx - 44, jy = cy + 18;
		const float dx = (on("right") - on("left")) * 14.0f, dy = (on("down") - on("up")) * 14.0f;

		pen.light(light("up") | light("down") | light("left") | light("right"));
		pen.circle(jx, jy, 32, Ink::DETAIL);
		for (const auto &[gx, gy] : { std::pair{ 0.0f, -1.0f }, std::pair{ 0.0f, 1.0f }, std::pair{ -1.0f, 0.0f },
		                              std::pair{ 1.0f, 0.0f } })
			pen.line(jx + gx * 8, jy + gy * 8, jx + gx * 26, jy + gy * 26, Ink::DETAIL);
		pen.line(jx, jy, jx + dx, jy + dy, Ink::THICK, 3.0f);
		pen.circle(jx + dx, jy + dy, 17, Ink::PART);
		pen.circle(jx + dx, jy + dy, 8, Ink::DETAIL);
		pen.light();
		push_button(pen, c, id("button1").c_str(), cx + 10, cy + 30, 10, "A", 't');
		push_button(pen, c, id("button2").c_str(), cx + 42, cy + 8, 10, "B", 't');
		push_button(pen, c, id("button3").c_str(), cx + 74, cy - 14, 10, "C", 't');
		push_button(pen, c, id("start").c_str(), cx - 50, cy - 50, 10, "START", 'r');

		/* The ball on its arm: forward is away from the player */
		const float side = c.value(id("kick_x").c_str()), forward = std::fabs(c.value(id("kick_y").c_str()));
		const bool kicked = std::fabs(side) > 0.1f || forward > 0.1f;
		const float by = cy + 152;

		pen.light(light("kick_x") | light("kick_y"));
		pen.line(cx, by - 62, cx, by - 38, Ink::DETAIL);
		pen.push();
		pen.turn(-side * 18.0f, cx, by - 62);
		pen.move(0.0f, -forward * 22.0f);
		pen.line(cx, by - 62, cx, by - 36, Ink::THICK, 6.0f);
		pen.circle(cx, by, 34, Ink::PART);
		if (kicked)
			pen.circle(cx, by, 33, Ink::LIT, 0.85f);
		ball_pattern(pen, cx, by, 34);
		pen.pop();
		pen.text(cx + 48, by + 6, "BALL", Align::LEFT);
		pen.light();
	}
}

using Drawing = void (*)(Pen &, const Controls &);

Drawing
drawing_of(const std::string &game)
{
	static constexpr std::pair<const char *, Drawing> DRAWINGS[] = {
		{ "sgnascar", nascar }, { "sgnascaro", nascar }, { "braveff", brave }, { "pharrier", harriers },
		{ "swracer", racer }, { "airtrix", air_trix }, { "airtrixo", air_trix }, { "von4", virtual_on },
		{ "atvtrack", atv_track }, { "atvtracka", atv_track }, { "smashdrv", smashing_drive },
		{ "smashdrvb", smashing_drive }, { "smashdrvs", smashing_drive },
		{ "gfootbal", football },
	};

	for (const auto &[name, drawing] : DRAWINGS)
		if (game == name)
			return drawing;
	return nullptr;
}

} /* namespace */

void
draw_cabinet(const MenuKit &kit, const std::string &game, const CabinetNow &now, ImVec2 a, ImVec2 b)
{
	const Drawing drawing = drawing_of(game);

	if (!drawing)
		return;

	/* Measured at rest, so that it does not move as its controls do: the
	 * row of COIN, SERVICE and TEST centred under it */
	const CabinetNow rest{ now.controls, {}, false, false, false };
	const Controls at_rest(rest), worked(now);
	Pen pen(kit);

	drawing(pen, at_rest);

	const ImVec4 e = pen.extent();
	const ImVec2 row = { (e.x + e.z) / 2.0f - cabinet_row_width(at_rest) / 2.0f, e.w + 40.0f };

	cabinet_row(pen, at_rest, row.x, row.y);
	pen.place(a, b);
	drawing(pen, worked);
	cabinet_row(pen, worked, row.x, row.y);
}

} /* namespace odin */
