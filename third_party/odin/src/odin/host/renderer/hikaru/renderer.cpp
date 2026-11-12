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

#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/common/host_clock.hpp"
#include "odin/machine/boards/hikaru/hikaru_gpu.hpp"
#include "odin/host/ui/loading_screen.hpp"
#include "odin/host/video/picture.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace odin {

Renderer::Renderer(HostGraphics &graphics, const Memory &memory, const Options &options)
	: Renderer(graphics.make_drawing(), memory, options)
{
}

Renderer::Renderer(std::unique_ptr<Drawing> card, const Memory &memory, const Options &options)
	: m_gpu(nullptr), m_fb(memory.fb), m_fb_writes(memory.fb_writes), m_options(options), m_building(nullptr), m_list(nullptr),
	  m_frame{}, m_card(std::move(card)), m_textures(*m_card, memory.texram),
	  m_times(*m_card, options.times),
	  m_bound_tex_index(~0u), m_draw{}, m_collect(false), m_families_view(false), m_families(false),
	  m_again(false), m_fragments_max(0),
	  m_fragments_full(false), m_scale(1), m_upscale_mode(DisplayUpscaleMode::RAW), m_num_lists(0), m_shown(2), m_next(-1), m_launched(0), m_last{},
	  m_layer_ram{std::vector<uint8_t>(size_t{WIDTH} * HEIGHT * 4),
	              std::vector<uint8_t>(size_t{WIDTH} * HEIGHT * 4)},
	  m_layer_changed{true, true}, m_layer_offs{}, m_layer_date{}, m_layer_pixels(size_t{WIDTH} * HEIGHT),
	  m_layer_format{~0u, ~0u}, m_message{}
{
	for (ListTextures &textures : m_list_textures) {
		textures.list = nullptr;
	}
	init_debug();
	m_card->keep_programs_in(options.programs_made);
	size_lists(room_at_start());
}

Renderer::~Renderer()
{
	m_times.tell();
}

/* Makes ready the mesh programs the lists of the game name, those of Odin
 * then those of the player (Drawing::prepare_program); on the thread
 * that draws the frames. */
void
Renderer::prepare_programs(const std::function<void(unsigned, unsigned)> &progress)
{
	std::vector<uint64_t> keys;

	for (const std::string *list : { &m_options.programs_seed, &m_options.programs_kept }) {
		if (list->empty())
			continue;
		for (const uint64_t key : read_program_list(*list))
			if (!m_card->holds_program(key) && std::find(keys.begin(), keys.end(), key) == keys.end())
				keys.push_back(key);
	}

	const unsigned total = static_cast<unsigned>(keys.size());
	const uint64_t began = host_ns();

	if (total)
		progress(0, total);
	m_card->begin_preparing();
	for (unsigned i = 0; i < total; i++) {
		m_card->prepare_program(keys[i]);
		progress(i + 1, total);
	}
	m_card->end_preparing();
	if (total)
		std::printf("programs: %u ready in %.1f s, %u of them read back, %u compiled\n", total,
		            static_cast<double>(host_ns() - began) / 1e9, m_card->programs_read_back(),
		            m_card->programs_compiled());
	if (!m_options.programs_kept.empty())
		m_card->list_programs_in(m_options.programs_kept);
}

void
Renderer::draw_loading(std::string_view title, unsigned done, unsigned total, unsigned window_w, unsigned window_h)
{
	/* Where the picture would be */
	const PictureRect rect = picture_rect(window_w, window_h);

	m_card->draw_loading(LoadingScreen::placed(title, done, total, rect.x, rect.y, rect.w, rect.h), window_w,
	                     window_h);
}

/* No texture chosen is of the list about to be drawn. */
void
Renderer::forget_held()
{
	m_bound_tex_index = ~0u;
}

void
Renderer::begin_frame()
{
	/* The list drawn last is kept until its translucent fragments are
	 * counted, see check_last_list: the command processor leaves it. */
	m_building = m_gpu->antarctic().begin_list();
}

void
Renderer::take_frame(unsigned frame, unsigned window_w, unsigned window_h, const PictureHole &hole)
{
	/* The command processor may still be in the middle of a mesh: draw
	 * what it pushed so far and let it start a new mesh on the next
	 * vertex. */
	m_gpu->antarctic().end_open_mesh();

	m_list = m_building;
	m_frame.number = frame;
	m_frame.launched = m_launched != 0;
	m_launched = 0;
	m_frame.fb = m_gpu->antarctic().fb_config();
	m_frame.layers = m_gpu->eurasia().layers();
	m_frame.window_w = window_w;
	m_frame.window_h = window_h;
	m_frame.hole = hole;

	update_debug();
	for (unsigned i = 0; i < 2; i++)
		if (shows_layer(i))
			take_layer(i);

	/* With the fingerprints only: each frame on the output costs, and
	 * fills the log */
	if (m_options.list_fp)
		std::printf("FRAMEBUFFER 781=%08X 181=%08X\n", m_frame.fb.w781, m_frame.fb.w181);
	if (m_frame.launched && (m_options.list_fp || m_options.list_dump >= 0))
		print_list_fingerprint();
}

void
Renderer::draw_frame()
{
	m_times.begin();
	forget_held();
	show();
	m_times.part(DrawTimes::PART_PRESENT);
	/* In the window: the largest rectangle of the shape of the screen of
	 * the hardware that fits, in the middle or in the hole of the bezel
	 * (picture_rect, which the mouse reads too) */
	m_card->present(picture_rect(m_frame.window_w, m_frame.window_h, m_frame.hole), m_frame.window_w,
	                m_frame.window_h);
	m_times.part(DrawTimes::PART_SHOWN);

	if (logging()) {
		log(" ==== RENDSTATE STATISTICS ==== ");
		log("  vp  : %u", m_list->num_vps);
		log("  mv  : %u", m_list->num_mvs);
		log("  mat : %u", m_list->num_mats);
		log("  tex : %u", m_list->num_texs);
		log("  ls  : %u", m_list->num_lss);
	}
}

/* Draws the list and blends it into its picture, as m_last tells: the
 * blender output is what the 3D buffer of the list keeps. */
void
Renderer::draw_list()
{
	draw_scene();
	m_times.part(DrawTimes::PART_BLENDER);
	if (m_last.back >= 0)
		m_card->blend_scene_to_pipe(m_last.slot, static_cast<unsigned>(m_last.back), m_last.rate);
	else
		m_card->scene_to_pipe(m_last.slot);
}

/* The translucent fragments of a list are collected without waiting for the
 * graphics card to count them: the processor goes on with the next frame
 * while the card draws. The counts are read a frame later, once the next
 * list is sent (see show): the card has that one to draw meanwhile. True if
 * they did not fit, and the buffer of the fragments is larger now. */
bool
Renderer::too_many(Last &last)
{
	m_times.part(DrawTimes::PART_LAST_LIST);
	const uint32_t used = m_card->counted_fragments(last.slot, last.views);

	m_times.fragments(used);
	return used > m_fragments_max && grow_lists(used);
}

/* Draws a list again, from what it was drawn from. */
void
Renderer::draw_again(const Last &last)
{
	odin_display_list_t *const current = m_list;
	const Last drawn = m_last;

	m_list = last.list;
	m_last = last;
	m_again = true;
	forget_held();
	draw_list();
	forget_held();
	m_again = false;
	m_list = current;
	m_last = drawn;
	m_times.part(DrawTimes::PART_LAST_LIST);
}

/* The list drawn last, counted now: drawn again if its fragments did not
 * fit, before its picture is shown or blended into the next one. */
void
Renderer::check_last_list()
{
	Last last = m_last;

	if (!last.list)
		return;
	m_last.list = nullptr;
	if (too_many(last))
		draw_again(last);
	m_card->keep_picture(last.slot, last.buffer);
}

/* The polygons reach the screen in two steps. The game libraries launch a
 * list only once the command processor has analysed the last one and the
 * display chip has plotted the one before (RACER @0C0118C0): a launch hands
 * the last list over to the display chip, which plots it; it shows at the
 * next frame. Nothing moves without a launch. The 781, the 181 and the clear
 * color act at once.
 *
 * On the RACER cabinet the SEGA logo stays two frames on the black of the
 * next scene, and the stars leave one frame before the text of the layers;
 * SGNASCAR keeps its white picture while it loads, with no list launched.
 *
 * The list before is counted once this one is sent. Its picture goes to its
 * 3D buffer first, as when it was counted first: this one may blend with
 * it. If its fragments did not fit it is drawn again, and so is this one,
 * which blended with what it left. */
void
Renderer::show()
{
	const odin_fb_config_t &fb = m_frame.fb;
	Last before = m_last;

	m_times.part(DrawTimes::PART_LAST_LIST);
	m_last.list = nullptr;
	/* The 3D buffer of a list takes its picture. */
	if (before.list)
		m_card->keep_picture(before.slot, before.buffer);

	/* The picture plotted during the last frame. */
	if (m_next >= 0) {
		m_shown = static_cast<uint32_t>(m_next);
		m_next = -1;
	}

	/* Draw the list to the 3D buffer its 781 names and keep the picture;
	 * the one of the list before shows next. */
	if (m_frame.launched) {
		const uint32_t n1 = odin_fb_num_1(&fb), n2 = odin_fb_num_2(&fb);
		const uint32_t rate = odin_fb_blend(&fb) ? odin_fb_alpha(&fb) : 0;
		const uint32_t num = m_num_lists;

		m_last.list = m_list;
		m_last.buffer = (n1 < 2) ? n1 : (n2 < 2) ? n2 : 0;
		/* A 3D buffer can be blended with what it holds itself, "two
		 * frames previous" in the patent: BRAVEFF does at rate FF
		 * (781 = 0505) and its screen stays black until the city
		 * fades in, as on the cabinet. */
		m_last.back = (rate > 0 && n1 < 2 && n2 < 2) ? static_cast<int>(n2) : -1;
		m_last.rate = rate;
		m_last.slot = num % LIST_BUFFERS;
		m_last.views = 0;

		/* Every list that has translucent fragments is then drawn
		 * again, and must come out the same. */
		if (m_options.lists_test)
			size_lists(1);

		std::fill(tex_ids().begin(), tex_ids().end(), HostTexture{});
		draw_list();

		if (num > 0)
			m_next = static_cast<int32_t>((num - 1) % LIST_BUFFERS);
		m_num_lists = num + 1;
	}

	if (before.list && too_many(before)) {
		draw_again(before);
		m_card->keep_picture(before.slot, before.buffer);
		/* Drawn again, this one has waited for its own counts. */
		if (m_last.list) {
			m_card->forget_counts(m_last.slot);
			draw_again(m_last);
			m_last.views = 0;
		}
	}
	/* What the list before was drawn with may go now. */
	m_textures.delete_dead();

	/* A picture that is on the screen already is checked now. */
	if (m_last.list && m_last.slot == m_shown)
		check_last_list();

	compose();
}

/* The picture shown and the layers, composed into the picture of the frame
 * (Compose, hikaru_draw.hpp). The registers of the display chip that set
 * up the framebuffer are not used. */
void
Renderer::compose()
{
	bool shown[2] = { false, false };

	m_times.part(DrawTimes::PART_LAYERS);
	for (unsigned i = 0; i < 2; i++)
		if ((shown[i] = shows_layer(i)))
			send_layer(i);
	m_times.part(DrawTimes::PART_COMPOSE);
	m_card->compose(compose_of(m_frame.fb, m_frame.layers, shown), m_shown, shown);
}

void
Renderer::set_scale(unsigned scale)
{
	if (scale == m_scale)
		return;
	if (scale < 1)
		fatal("a scale of zero");

	/* The pictures are whole once the last list is counted. */
	check_last_list();

	m_card->set_scale(scale);
	m_scale = scale;
	size_lists(room_at_start());
	forget_held();
}

void
Renderer::fatal(const char *what)
{
	std::fprintf(stderr, "renderer: %s\n", what);
	std::abort();
}

} /* namespace odin */
