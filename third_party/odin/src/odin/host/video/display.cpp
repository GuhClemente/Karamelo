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
 * Copyright (C) 2011-2013, Stefano Teso
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

#include "odin/host/video/display.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#include "odin/common/bytes.hpp"
#include "odin/common/env.hpp"
#include "odin/common/host_clock.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/host/input/keyboard.hpp"
#include "odin/host/ui/logo.hpp"
#include "odin/host/ui/menu.hpp"
#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/machine/core/state_file.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/machine/core/worker.hpp"

namespace odin {

namespace {

/* A file of the data of Odin, as "shaders/<game>.txt": where the list of
 * the games is, else with the program, else in the data of Odin in the home
 * of the user; empty when it is in none */
std::string
find_data_file(const std::string &name)
{
	const std::string home = home_dir();
	std::error_code error;
	std::vector<std::string> paths = { name };

	for (const std::string &folder : program_data_folders())
		paths.push_back(folder + "/" + name);
	if (!home.empty())
		paths.push_back(home + "/.local/share/odin/" + name);
	for (const std::string &path : paths)
		if (std::filesystem::exists(path, error))
			return path;
	return {};
}

/* The lists of the mesh programs of a game (read_program_list): the one that
 * comes with Odin, in shaders/ where the list of the games is, and the one
 * kept for the user, in the cache of their home (in cache/ beside a portable
 * Odin), with the folder of the programs the driver made beside it. A path
 * is left empty when there is no such file or no place for it. */
void
find_program_lists(const std::string &game, std::string &seed, std::string &kept, std::string &made)
{
	const std::string home = home_dir();
	const std::string portable = portable_folder();
	const std::string cache = env_text("XDG_CACHE_HOME");
	std::error_code error;
	std::string dir;

	kept.clear();
	made.clear();
	seed = find_data_file("shaders/" + game + ".txt");

	if (!portable.empty())
		dir = portable + "/cache";
	else if (!cache.empty())
		dir = cache + "/odin";
	else if (!home.empty())
		dir = home + "/.cache/odin";
	else
		return;
	std::filesystem::create_directories(dir + "/shaders", error);
	kept = dir + "/shaders/" + game + ".txt";
	made = dir + "/programs";
}

/* The preset of a filter of the picture by its name, filters/<name>.glslp
 * (find_data_file); empty, said on stderr, when it is in none */
std::string
find_filter(const std::string &name)
{
	if (name.empty())
		return {};

	const std::string path = find_data_file("filters/" + name + ".glslp");

	if (path.empty())
		std::fprintf(stderr, "ERROR: filter: '%s' not found (filters/%s.glslp)\n", name.c_str(), name.c_str());
	return path;
}

/* Whether the point x, y of the square of 24 is inside the bold rune:
 * a line from it to the right crosses its outline an odd number of times */
bool
in_rune(float x, float y)
{
	constexpr size_t N = std::size(RUNE_OUTLINE_BOLD);
	bool in = false;

	for (size_t i = 0, j = N - 1; i < N; j = i++) {
		const auto &[xi, yi] = RUNE_OUTLINE_BOLD[i];
		const auto &[xj, yj] = RUNE_OUTLINE_BOLD[j];

		if ((yi > y) != (yj > y) && x < xi + (y - yi) * (xj - xi) / (yj - yi))
			in = !in;
	}
	return in;
}

/* The logo in the bar of the window and in the task bar: the rune in its
 * gold, bold, nearly as high as the picture; each pixel as opaque as the
 * rune covers it, tried at SAMPLES by SAMPLES points */
void
set_icon(SDL_Window *window)
{
	constexpr int SIDE = 64, SAMPLES = 8;
	constexpr float HEIGHT = 0.92f;
	/* The rune is 20 of the square of 24 high; the middle of the square
	 * in the middle of the picture */
	constexpr float K = SIDE * HEIGHT / 20.0f, FROM = SIDE / 2.0f - 12.0f * K;
	SDL_Surface *const icon = SDL_CreateSurface(SIDE, SIDE, SDL_PIXELFORMAT_RGBA32);

	if (!icon)
		return;
	for (int y = 0; y < SIDE; y++) {
		auto *const row = static_cast<uint8_t *>(icon->pixels) + static_cast<size_t>(y) * static_cast<size_t>(icon->pitch);

		for (int x = 0; x < SIDE; x++) {
			int covered = 0;

			for (int sy = 0; sy < SAMPLES; sy++)
				for (int sx = 0; sx < SAMPLES; sx++) {
					const float px = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / SAMPLES;
					const float py = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / SAMPLES;

					covered += in_rune((px - FROM) / K, (py - FROM) / K);
				}
			for (int c = 0; c < 3; c++)
				row[x * 4 + c] = RUNE_GOLD[c];
			row[x * 4 + 3] = static_cast<uint8_t>((covered * 255 + SAMPLES * SAMPLES / 2) / (SAMPLES * SAMPLES));
		}
	}
	if (!SDL_SetWindowIcon(window, icon))
		std::fprintf(stderr, "ERROR: no icon for the window: '%s'\n", SDL_GetError());
	SDL_DestroySurface(icon);
}

/* The window, made for the graphics API of those flags */
SDL_Window *
make_window(uint64_t flags)
{
	SDL_Window *const window = SDL_CreateWindow("Odin", Renderer::WIDTH, Renderer::HEIGHT, flags | SDL_WINDOW_RESIZABLE);

	if (!window) {
		std::fprintf(stderr, "ERROR: could not create SDL window: '%s'\n", SDL_GetError());
		return nullptr;
	}
	/* Never smaller than the screen of the board: the menu is made for it */
	SDL_SetWindowMinimumSize(window, Renderer::WIDTH, Renderer::HEIGHT);
	set_icon(window);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	return window;
}

/* The window and the card that draws in it (host_graphics.hpp), taken by
 * the calling thread: with Vulkan when odin.ini asks for it and the card
 * has what Odin needs, else with OpenGL */
bool
open_window(DisplayApi api, const std::string &gpu, SDL_Window *&window, std::unique_ptr<HostGraphics> &graphics)
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "ERROR: could not initialize SDL: '%s'\n", SDL_GetError());
		return false;
	}

	if (api == DisplayApi::VULKAN) {
		window = make_window(HostGraphics::window_flags_vk());
		if (window)
			graphics = HostGraphics::make_vk(window, Renderer::WIDTH, Renderer::HEIGHT, gpu);
		if (graphics)
			return true;
		std::fprintf(stderr, "ERROR: Vulkan does not work on this computer: using OpenGL\n");
		if (window)
			SDL_DestroyWindow(window);
	}

	HostGraphics::prepare_gl();
	window = make_window(HostGraphics::window_flags_gl());
	if (!window)
		return false;
	graphics = HostGraphics::make_gl(window, Renderer::WIDTH, Renderer::HEIGHT);
	return graphics != nullptr;
}

} /* namespace */

std::unique_ptr<Display>
Display::make(const Keyboard &keys, const Options &options)
{
	SDL_Window *window = nullptr;
	std::unique_ptr<HostGraphics> graphics;

	if (!open_window(options.api, options.gpu, window, graphics))
		return nullptr;
	return std::unique_ptr<Display>(new Display(keys, options, window, std::move(graphics)));
}

Display::Display(const Keyboard &keys, const Options &options, SDL_Window *window,
                 std::unique_ptr<HostGraphics> graphics)
	: m_board(nullptr), m_keys(keys), m_window(window), m_graphics(std::move(graphics)), m_menu_shown(false),
	  m_show_speed(options.show_speed), m_vsync(options.vsync),
	  m_wait_on_time(false), m_wait_late(false), m_waits(false),
	  m_frame(0), m_scale(options.scale), m_anisotropy(options.anisotropy), m_upscale_mode(options.upscale_mode),
	  m_filter(find_filter(options.filter)), m_logged(false),
	  m_picture{ 0, Renderer::WIDTH, Renderer::HEIGHT },
	  m_late(env_flag("ODIN_LATE_DRAW")), m_held(false), m_late_draw(0),
	  m_shot_prefix(std::getenv("ODIN_SCREENSHOT")), m_window_shot(std::getenv("ODIN_WINDOW_SHOT")), m_shot_every(60),
	  m_fingerprint(env_flag("ODIN_PICTURE_FP")), m_window_fp(env_flag("ODIN_WINDOW_FP")), m_speed(env_flag("ODIN_SPEED")),
	  m_speed_since(0), m_speed_last(0), m_speed_longest(0),
	  m_stats(env_flag("ODIN_STATS")), m_draw_ns(0), m_wait_frame_ns(0), m_wait_write_ns(0), m_drawn(0),
	  m_title_since(0), m_title_frames(0)
{
	m_gpu = m_graphics->name();
	m_given_w = static_cast<int>(Renderer::WIDTH);
	m_given_h = static_cast<int>(Renderer::HEIGHT);
	if (const int every = env_int("ODIN_SCREENSHOT_EVERY", 0); every > 0)
		m_shot_every = static_cast<unsigned>(every);

	/* The card goes to the drawing thread for good. */
	m_graphics->leave_thread();
	m_drawing = std::make_unique<Worker>();
	m_drawing->post([this] {
		m_graphics->take_thread();
		m_graphics->set_waits(false);
	});
	wait_drawing();
	if (options.window_w && options.window_h)
		set_window_size(static_cast<int>(options.window_w), static_cast<int>(options.window_h), options.fullscreen);
	else if (options.window >= 2 || options.fullscreen)
		set_window(options.window ? options.window : 1, options.fullscreen);
	show_black();
}

/* The renderer is made on the drawing thread, where the context is; the
 * loading screen does not wait for the display. */
void
Display::attach(Hikaru &board, const std::string &title)
{
	Renderer::Memory memory;
	Renderer::Options looks;

	memory.fb = board.framebuffer_ram();
	memory.fb_writes = &board.framebuffer_writes();
	memory.texram[0] = board.texture_ram(0);
	memory.texram[1] = board.texture_ram(1);

	looks.log = env_flag("HR_LOG");
	looks.log_frame = env_int("HR_LOG_FRAME", 0);
	looks.list_fp = env_flag("ODIN_LIST_FP");
	looks.list_dump = env_int("ODIN_LIST_DUMP", -1);
	looks.lists_test = env_int("ODIN_LISTS_TEST", 0) != 0;
	looks.times = env_flag("ODIN_DRAW_TIMES");
	looks.debug_keys = env_flag("ODIN_DEBUG_KEYS");
	looks.key_down = [](void *ctx, char key) {
		return static_cast<const Keyboard *>(ctx)->down(static_cast<SDL_Keycode>(key));
	};
	looks.ctx = const_cast<Keyboard *>(&m_keys);
	find_program_lists(board.game_name(), looks.programs_seed, looks.programs_kept, looks.programs_made);
	m_program_list = looks.programs_kept;

	run([&] {
		m_renderer = std::make_unique<Renderer>(*m_graphics, memory, looks);
		m_renderer->set_gpu(&board.gpu());
		m_graphics->set_waits(false);
		m_waits = false;
	});
	m_board = &board;
	m_frame = 0;
	m_picture = { 0, Renderer::WIDTH, Renderer::HEIGHT };
	m_speed_since = m_speed_last = m_speed_longest = 0;
	m_title_since = 0;
	m_fps = 0.0f;
	m_title = std::string("Odin - ") + title + " [" + m_graphics->api() + "]";
	SDL_SetWindowTitle(m_window, m_title.c_str());
	prepare_programs(title);
	set_vsync(m_vsync);
	board.set_screen(this);
}

void
Display::attach_scenes(const std::string &game, const std::string &title)
{
	std::string seed, kept, made;
	std::vector<uint64_t> keys;

	find_program_lists(game, seed, kept, made);
	for (const std::string *list : { &seed, &kept })
		if (!list->empty())
			for (const uint64_t key : read_pmx_list(*list))
				if (std::find(keys.begin(), keys.end(), key) == keys.end())
					keys.push_back(key);
	run([this, &made] {
		m_own = m_graphics->make_drawing();
		m_own->keep_programs_in(made);
		m_own_built = 1;
		m_own_native = m_own->draws_native();
		m_own_sorts = m_own->sorts_per_pixel();
		m_graphics->set_waits(false);
		m_waits = false;
	});
	m_own_fresh = false;
	m_frame = 0;
	m_speed_since = m_speed_last = m_speed_longest = 0;
	m_title_since = 0;
	m_fps = 0.0f;
	m_title = std::string("Odin - ") + title + " [" + m_graphics->api() + "]";
	SDL_SetWindowTitle(m_window, m_title.c_str());
	prepare_scenes(title, keys);
	if (!kept.empty())
		run([this, &kept] { m_own->list_pmx_in(kept); });
	set_vsync(m_vsync);
}

/* The last one may still be drawing */
void
Display::set_scene(PmxScene scene)
{
	finish();
	m_own_scene = std::move(scene);
	m_own_fresh = true;
}

void
Display::set_blank(bool blank)
{
	if (blank == m_own_blank)
		return;
	finish();
	m_own_blank = blank;
}

/* On the drawing thread */
void
Display::draw_own(bool late, unsigned w, unsigned h, bool bar)
{
	m_own->set_filter(m_filter);
	if (m_own_fresh) {
		/* It rebuilds its pictures each time */
		if (m_scale != m_own_built) {
			m_own->set_scale(m_scale);
			m_own_built = m_scale;
		}
		m_own->draw_pmx(m_own_scene, m_anisotropy);
		m_own_fresh = false;
	}
	if (m_own_blank) {
		/* Black where the picture goes, the bezel and the bar over it; no
		 * blur of it at the sides */
		const HostPicture drawn = m_own->frame_picture();

		m_graphics->clear_window(w, h);
		m_picture = HostPicture{ 0, drawn.w, drawn.h };
	} else {
		m_own->present(picture_rect(w, h, m_hole), w, h);
		m_picture = m_own->frame_picture();
	}
	screenshot();
	print_fingerprint();
	draw_over(w, h, bar);
	if (const bool wait = late ? m_wait_late : m_wait_on_time; wait != m_waits) {
		m_graphics->set_waits(wait);
		m_waits = wait;
	}
	present(w, h, "game");
}

void
Display::detach()
{
	if (m_own) {
		run([this] { m_own.reset(); });
		m_own_scene = {};
		m_own_fresh = m_own_blank = false;
		m_picture.id = 0;
		m_title = "Odin";
		SDL_SetWindowTitle(m_window, "Odin");
		show_black();
		return;
	}
	if (!m_board)
		return;
	m_board->set_screen(nullptr);
	run([this] { m_renderer.reset(); });
	m_board = nullptr;
	m_picture.id = 0;
	m_sinden_border = {};
	m_program_list.clear();
	m_title = "Odin";
	SDL_SetWindowTitle(m_window, "Odin");
	show_black();
}

void
Display::show_black()
{
	int w, h;

	if (m_board)
		return;
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	run([this, w, h] {
		m_graphics->clear_window(static_cast<unsigned>(w), static_cast<unsigned>(h));
		present(static_cast<unsigned>(w), static_cast<unsigned>(h), "black");
	});
}

/* The programs of the lists of the game are compiled before its first
 * frame (load) */
void
Display::prepare_programs(const std::string &title)
{
	load([this](const Progress &progress) { m_renderer->prepare_programs(progress); },
	     [this, &title](unsigned done, unsigned total, unsigned w, unsigned h) {
		     m_renderer->draw_loading(title, done, total, w, h);
	     });
}

/* The programs of the Neon 250 are made before the first scene, with those
 * of the keys of the lists of the game (load), the loading screen where the
 * picture will be */
void
Display::prepare_scenes(const std::string &title, const std::vector<uint64_t> &keys)
{
	load([this, &keys](const Progress &progress) {
		     const uint64_t began = host_ns();

		     m_own->prepare_pmx(keys, progress);

		     const unsigned read = m_own->programs_read_back(), compiled = m_own->programs_compiled();

		     std::printf("programs: %u ready in %.1f s, %u of them read back, %u compiled\n", read + compiled,
		                 static_cast<double>(host_ns() - began) / 1e9, read, compiled);
	     },
	     [this, &title](unsigned done, unsigned total, unsigned w, unsigned h) {
		     const PictureRect rect = picture_rect(w, h);

		     m_own->draw_loading(LoadingScreen::placed(title, done, total, rect.x, rect.y, rect.w, rect.h), w,
		                         h);
	     });
}

/* `make` runs on the drawing thread, its progress drawn by `draw` at most 60
 * times a second, with the size of the window. The window answers
 * meanwhile; its events wait in their queue for the game. */
void
Display::load(const std::function<void(const Progress &)> &make,
              const std::function<void(unsigned, unsigned, unsigned, unsigned)> &draw)
{
	constexpr uint64_t EVERY_NS = 1000000000 / 60;
	const auto window_size = [this] {
		int w, h;

		SDL_GetWindowSizeInPixels(m_window, &w, &h);
		return static_cast<uint64_t>(static_cast<uint32_t>(w)) << 32 | static_cast<uint32_t>(h);
	};
	std::atomic<uint64_t> size(window_size());
	uint64_t shown = 0;

	finish();
	m_drawing->post([&] {
		make([&](unsigned done, unsigned total) {
			const uint64_t now = host_ns();

			if (done && done < total && now - shown < EVERY_NS)
				return;
			shown = now;
			const uint64_t wh = size;
			draw(done, total, static_cast<unsigned>(wh >> 32), static_cast<unsigned>(wh));
			present(static_cast<unsigned>(wh >> 32), static_cast<unsigned>(wh), "loading");
		});
	});
	while (!m_drawing->wait_for(std::chrono::milliseconds(16))) {
		SDL_PumpEvents();
		size = window_size();
	}
}

Display::~Display()
{
	if (m_board)
		m_board->set_screen(nullptr);

	/* The card comes back: the renderer is destroyed with it. */
	run([this] { m_graphics->leave_thread(); });
	m_drawing.reset();
	m_graphics->take_thread();
	if (m_menu) {
		m_menu->release();
		m_menu.reset();
	}
	m_renderer.reset();

	if (m_stats)
		std::fprintf(stderr, "screen: %u frames drawn in %.3f s on their thread; the board waited for them "
		             "%.3f s at the end of its frames and %.3f s before it wrote what they read\n", m_drawn,
		             static_cast<double>(m_draw_ns) / 1e9, static_cast<double>(m_wait_frame_ns) / 1e9,
		             static_cast<double>(m_wait_write_ns) / 1e9);

	m_graphics.reset();
	SDL_DestroyWindow(m_window);
	SDL_Quit();
}

void
Display::finish()
{
	const uint64_t begin = m_stats ? host_ns() : 0;

	if (m_held) {
		m_held = false;
		m_late_draw.release();
	}
	wait_drawing();
	if (m_stats)
		m_wait_write_ns += host_ns() - begin;
}

void
Display::wait_drawing()
{
#if defined(__APPLE__)
	while (!m_drawing->wait_for(std::chrono::milliseconds(1)))
		SDL_PumpEvents();
#else
	m_drawing->wait();
#endif
}

void
Display::run(std::function<void()> job)
{
	finish();
	m_drawing->post(std::move(job));
	wait_drawing();
}

void
Display::open_menu(int page)
{
	if (!m_menu)
		m_menu = std::make_unique<Menu>(m_window, *m_graphics);
	/* The menus of the bar closed, that of the library too */
	show_bar(false);
	m_menu->hide_bar();
	m_menu->open(page);
	/* Behind it, the last picture of the game, or the library as it was
	 * drawn last */
	if (m_board || m_own) {
		run([this] { m_menu->freeze(m_picture); });
	} else {
		int w, h;

		SDL_GetWindowSizeInPixels(m_window, &w, &h);
		run([this, w, h] { m_menu->freeze_window(static_cast<unsigned>(w), static_cast<unsigned>(h)); });
	}
	m_menu_shown = true;
	retitle();
}

void
Display::close_menu()
{
	m_menu->close();
	m_menu_shown = false;
	retitle();
}

void
Display::set_paused(bool paused)
{
	m_paused = paused;
	retitle();
}

/* The title of a game: "(paused)" while it waits, its speed again a second
 * after (show_speed) */
void
Display::retitle()
{
	if (!m_board && !m_own)
		return;
	m_title_since = 0;
	SDL_SetWindowTitle(m_window, m_menu_shown || m_paused ? (m_title + " (paused)").c_str() : m_title.c_str());
}

void
Display::menu_event(const SDL_Event &event)
{
	if (m_menu)
		m_menu->event(event);
}

bool
Display::menu_popup_open() const
{
	return m_menu && m_menu->popup_open();
}

bool
Display::menu_item_at(MenuState &state, float x, float y)
{
	return m_menu && m_menu->item_at(state, x, y);
}

bool
Display::menu_typing() const
{
	return m_menu && m_menu->typing();
}

/* The menu made as the library makes it, its renderer first; the bar takes
 * the pointer and the hints of the mouse as the menu does */
void
Display::show_bar(bool on)
{
	if (on == m_bar_shown)
		return;
	if (on)
		make_menu();
	m_bar_shown = on;
	m_bar_events.clear();
	if (on) {
		m_menu->forget_input();
		m_menu->open();
	} else {
		m_menu->hide_bar();
		m_menu->close();
	}
}

void
Display::bar_event(const SDL_Event &event)
{
	if (m_bar_shown)
		m_bar_events.push_back(event);
}

/* The bar given the events of the mouse kept for it and built, when it is
 * shown, or its note alone: whether anything is */
bool
Display::build_bar(BarState *bar)
{
	if (!bar)
		return false;
	if (m_bar_shown) {
		for (const SDL_Event &event : m_bar_events)
			m_menu->event(event);
		m_bar_events.clear();
		m_menu->build_bar(*bar);
		return true;
	}
	if (bar->note.empty())
		return false;
	make_menu();
	m_menu->build_note(*bar);
	return true;
}

/* Over the picture of the game: the bezel, the game blurred at its sides,
 * the bar or a note, the white border of a light gun */
void
Display::draw_over(unsigned w, unsigned h, bool bar)
{
	draw_bezel(w, h);
	draw_blur(w, h);
	if (bar)
		m_menu->draw_bar(w, h);
	draw_sinden_border(w, h);
}

void
Display::show_again(BarState *bar)
{
	int w, h;

	if (!m_renderer && !m_own)
		return;
	finish();

	const bool over = build_bar(bar);

	if (m_own) {
		SDL_GetWindowSizeInPixels(m_window, &w, &h);
		run([this, w, h, over] { draw_own(false, static_cast<unsigned>(w), static_cast<unsigned>(h), over); });
		return;
	}

	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	m_drawing->post([this, w, h, over] {
		const auto ww = static_cast<unsigned>(w), wh = static_cast<unsigned>(h);

		m_renderer->show_again(ww, wh, m_hole);
		draw_over(ww, wh, over);
		present(ww, wh, "again");
	});
	wait_drawing();
}

void
Display::make_menu()
{
	if (m_menu)
		return;
	m_menu = std::make_unique<Menu>(m_window, *m_graphics);
	run([this] { m_menu->freeze({}); });
}

/* The library draws with the menu: made here if it is not yet */
void
Display::open_library()
{
	/* Its renderer first: Dear ImGui builds on it */
	make_menu();
	m_menu->forget_input();
}

void
Display::add_library_picture(const std::string &game, SDL_Surface *rgba)
{
	m_menu->add_library_picture(game, rgba);
}

void
Display::draw_library(LibraryState &state, BarState &bar)
{
	int w, h;

	m_menu->build_library(state, bar);
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	run([this, w, h] {
		/* No picture of a game behind it */
		m_menu->freeze({});
		m_menu->draw(static_cast<unsigned>(w), static_cast<unsigned>(h));
		present(static_cast<unsigned>(w), static_cast<unsigned>(h), "library");
	});
}

void
Display::draw_menu(MenuState &state)
{
	int w, h;

	m_menu->build(state);
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	run([this, w, h] {
		m_menu->draw(static_cast<unsigned>(w), static_cast<unsigned>(h));
		draw_sinden_border(static_cast<unsigned>(w), static_cast<unsigned>(h));
		present(static_cast<unsigned>(w), static_cast<unsigned>(h), "menu");
	});
}

void
Display::begin_frame()
{
	if (m_own)
		return;
	m_renderer->begin_frame();
}

/* The frame before is drawn first: this one is taken with the drawing
 * thread at rest, then drawn while the board goes on. */
void
Display::end_frame(unsigned frame, bool late, BarState *bar)
{
	const uint64_t waited = m_wait_write_ns;
	int w, h;

	finish();
	/* That wait was for the end of the frame. */
	m_wait_frame_ns += m_wait_write_ns - waited;
	m_wait_write_ns = waited;
	m_frame = frame;
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	if (m_own) {
		const bool over = build_bar(bar);

		/* Drawn while the board keeps running */
		m_drawing->post([this, late, w, h, over] {
			draw_own(late, static_cast<unsigned>(w), static_cast<unsigned>(h), over);
		});
		show_speed();
		return;
	}
	m_renderer->take_frame(frame, static_cast<unsigned>(w), static_cast<unsigned>(h), m_hole);
	m_logged = m_renderer->frame_is_logged();
	m_message = m_renderer->message();
	/* Built while the drawing thread rests, drawn over the frame */
	const bool over = build_bar(bar);

	m_drawing->post([this, late, w, h, over] { draw(late, static_cast<unsigned>(w), static_cast<unsigned>(h), over); });
	m_held = m_late;
	/* A log is written whole before the board writes on. */
	if (m_logged)
		finish();

	show_speed();
	print_speed();
}

void
Display::draw(bool late, unsigned window_w, unsigned window_h, bool bar)
{
	if (m_late)
		m_late_draw.acquire();

	const uint64_t begin = m_stats ? host_ns() : 0;

	m_renderer->set_scale(m_scale);
	m_renderer->set_upscale_mode(m_upscale_mode);
	m_renderer->set_anisotropy(m_anisotropy);
	m_renderer->set_filter(m_filter);
	m_renderer->draw_frame();
	m_picture = m_renderer->frame_picture();
	draw_over(window_w, window_h, bar);

	screenshot();
	print_fingerprint();
	if (const bool wait = late ? m_wait_late : m_wait_on_time; wait != m_waits) {
		m_graphics->set_waits(wait);
		m_waits = wait;
	}
	present(window_w, window_h, "game");
	m_renderer->frame_shown();
	if (m_stats) {
		m_draw_ns += host_ns() - begin;
		m_drawn++;
	}
}

/* The picture of the frame as bytes of red, green and blue, the bottom line
 * first. */
void
Display::read_picture(uint8_t *pixels)
{
	m_graphics->read(m_picture, m_picture.w, m_picture.h, pixels);
}

void
Display::present(unsigned window_w, unsigned window_h, const char *what)
{
	const bool shot = m_window_shot && (m_frame % m_shot_every) == 0;

	if (m_window_fp || shot) {
		m_window_pixels.resize(size_t{window_w} * window_h * 3);
		m_graphics->read({}, window_w, window_h, m_window_pixels.data());
	}
	if (m_window_fp)
		std::fprintf(stderr, "WINFP %s frame=%u size=%ux%u fp=%016" PRIx64 "\n", what, m_frame, window_w, window_h,
		             fnv1a(FNV_START, m_window_pixels.data(), m_window_pixels.size()));
	if (shot) {
		char path[512];

		std::snprintf(path, sizeof(path), "%s-%s-%06u.ppm", m_window_shot, what, m_frame);
		write_ppm(path, window_w, window_h, m_window_pixels.data());
	}
	m_graphics->show_window();
}

/* Bytes of red, green and blue, the bottom line first, as a PPM file */
void
Display::write_ppm(const char *path, size_t w, size_t h, const uint8_t *pixels)
{
	std::FILE *const file = std::fopen(path, "wb");

	if (!file)
		return;
	std::fprintf(file, "P6\n%zu %zu\n255\n", w, h);
	for (size_t y = 0; y < h; y++)
		std::fwrite(&pixels[(h - 1 - y) * w * 3], 1, w * 3, file);
	std::fclose(file);
}

void
Display::screenshot()
{
	const size_t w = m_picture.w, h = m_picture.h;
	char path[512];

	if (!m_shot_prefix || (m_frame % m_shot_every) != 0)
		return;

	std::vector<uint8_t> pixels(w * h * 3);

	/* None: a board's DAC puts out black */
	if (m_picture.id)
		read_picture(pixels.data());
	std::snprintf(path, sizeof(path), "%s-%06u.ppm", m_shot_prefix, m_frame);
	write_ppm(path, w, h, pixels.data());
}

void
Display::print_fingerprint()
{
	if (!m_fingerprint)
		return;

	m_pixels.assign(size_t{m_picture.w} * m_picture.h * 3, 0);
	if (m_picture.id)
		read_picture(m_pixels.data());
	std::fprintf(stderr, "PICFP frame=%u fp=%016" PRIx64 "\n", m_frame, fnv1a(FNV_START, m_pixels.data(), m_pixels.size()));
}

void
Display::print_speed()
{
	const double second = static_cast<double>(SDL_GetPerformanceFrequency());

	if (!m_speed)
		return;

	const uint64_t now = SDL_GetPerformanceCounter();

	if (m_speed_last && now - m_speed_last > m_speed_longest)
		m_speed_longest = now - m_speed_last;
	m_speed_last = now;
	if ((m_frame % 60) != 0)
		return;

	if (m_speed_since)
		std::fprintf(stderr, "SPEED frame=%u fps=%.1f longest=%.1f\n", m_frame,
		             60.0 * second / static_cast<double>(now - m_speed_since),
		             1000.0 * static_cast<double>(m_speed_longest) / second);
	m_speed_since = now;
	m_speed_longest = 0;
}

/* The title of the window tells the frames shown in the last second: the
 * time of one frame, in milliseconds, reads 59 or 62 for 60. */
void
Display::show_speed()
{
	const uint64_t second = SDL_GetPerformanceFrequency();
	const uint64_t now = SDL_GetPerformanceCounter();
	char title[512];

	m_title_frames++;
	if (!m_show_speed || (m_title_since && now - m_title_since < second))
		return;

	/* Nothing to tell at the start nor after a pause */
	if (m_title_since && now - m_title_since < 2 * second) {
		const double fps = static_cast<double>(m_title_frames) * static_cast<double>(second) /
		                   static_cast<double>(now - m_title_since);

		m_fps = static_cast<float>(fps);
		if (!m_message.empty())
			std::snprintf(title, sizeof(title), "%s (%4.1f FPS) [%s]", m_title.c_str(), fps, m_message.c_str());
		else
			std::snprintf(title, sizeof(title), "%s (%4.1f FPS)", m_title.c_str(), fps);
		SDL_SetWindowTitle(m_window, title);
	}
	m_title_since = now;
	m_title_frames = 0;
}

void
Display::set_scale(unsigned scale)
{
	finish();
	m_scale = scale;
}

void
Display::set_filter(const std::string &name)
{
	finish();
	m_filter = find_filter(name);
}

void
Display::set_anisotropy(unsigned level)
{
	finish();
	m_anisotropy = level;
}

void
Display::set_upscale_mode(DisplayUpscaleMode mode)
{
	finish();
	m_upscale_mode = mode;
}

void
Display::set_show_speed(bool on)
{
	m_show_speed = on;
	m_title_since = 0;
	if (!on)
		SDL_SetWindowTitle(m_window, m_title.c_str());
}

void
Display::list_launched()
{
	m_renderer->launch();
}

/* The frame being drawn reads the texture RAM as its own frame left it;
 * the decoded textures that read those texels are stale then. */
void
Display::before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	finish();
	m_renderer->invalidate_textures(bank, x, y, w, h);
}

void
Display::memory_changed()
{
	finish();
	m_renderer->invalidate_textures(-1);
}

/* The pictures the renderer keeps, one item each, then its four counters.
 * They are read and written on the drawing thread. */
bool
Display::save_pictures(StateFile &state)
{
	std::vector<uint8_t> pictures(Renderer::PICTURES * Renderer::PICTURE_SIZE);
	Renderer::Counters counters = {};

	run([&] { m_renderer->save(pictures, counters); });
	for (unsigned i = 0; i < Renderer::PICTURES; i++)
		if (!state.put(&pictures[i * Renderer::PICTURE_SIZE], static_cast<uint32_t>(Renderer::PICTURE_SIZE)))
			return false;
	return state.put(counters.num_lists) && state.put(counters.shown) &&
	       state.put(counters.next) && state.put(counters.launched);
}

bool
Display::load_pictures(StateFile &state)
{
	std::vector<uint8_t> pictures(Renderer::PICTURES * Renderer::PICTURE_SIZE);
	Renderer::Counters counters = {};

	for (unsigned i = 0; i < Renderer::PICTURES; i++)
		if (!state.get(&pictures[i * Renderer::PICTURE_SIZE], static_cast<uint32_t>(Renderer::PICTURE_SIZE)))
			return false;
	if (!state.get(counters.num_lists) || !state.get(counters.shown) ||
	    !state.get(counters.next) || !state.get(counters.launched))
		return false;

	run([&] { m_renderer->load(pictures, counters); });
	return true;
}

} /* namespace odin */
