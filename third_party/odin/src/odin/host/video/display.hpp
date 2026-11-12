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

#ifndef ODIN_DISPLAY_HPP
#define ODIN_DISPLAY_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <semaphore>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "odin/host/video/display_options.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/video/picture.hpp"

#include "odin/machine/chips/pmx1/pmx1_scene.hpp"

namespace odin {

class Hikaru;
class Keyboard;
class Menu;
struct BarState;
struct LibraryState;
struct MenuState;
class Renderer;
class Worker;

/* The screen of the cabinet: a window, and in it the pictures the renderer
 * (renderer.hpp) draws from what the graphics chips leave.
 *
 * The frames are drawn on a thread of their own, which holds the OpenGL
 * context: the board runs a frame while the one before is drawn. What a
 * drawing reads of the board is told in renderer.hpp; the board waits for
 * the drawing before it changes any of it (HikaruScreen). Everything else
 * here is called from the thread that runs the board.
 *
 * The environment asks for what is measured on the pictures:
 *
 *  ODIN_SCREENSHOT=<prefix>	<prefix>-<frame>.ppm every 60 frames, or
 *				every ODIN_SCREENSHOT_EVERY frames
 *  ODIN_PICTURE_FP=1		a line for the picture of each frame on
 *				stderr, FNV-1a 64 of its pixels, to compare
 *				two builds
 *  ODIN_WINDOW_FP=1		the same for the whole window each time it
 *				is shown: the game with its bezel or its
 *				sides, the bar, the menu, the library
 *  ODIN_SPEED=1		a line every 60 frames on stderr: the frames
 *				a second over them and the longest of them
 *				in milliseconds
 *  ODIN_STATS=1		at the end, on stderr: the host time of the
 *				drawings and what the board waited for them
 *				(and the time of each part of the board, see
 *				hikaru.cpp)
 *  ODIN_DRAW_TIMES=1		at the end, on stderr: what each part of the
 *				drawings cost, on their thread and for the
 *				graphics card (renderer_times.hpp)
 *  ODIN_LATE_DRAW=1		a test of the thread: the drawing of a frame
 *				starts when the board has run the next one,
 *				or has to wait for it. The pictures must be
 *				the same.
 *  ODIN_WINDOW_SHOT=<prefix>	the whole window as drawn, to
 *				<prefix>-<what>-<frame>.ppm, every
 *				ODIN_SCREENSHOT_EVERY frames
 *
 * and for the looks at what is drawn, see Renderer::Options: HR_LOG,
 * HR_LOG_FRAME, ODIN_LIST_FP, ODIN_LIST_DUMP, ODIN_LISTS_TEST,
 * ODIN_DEBUG_KEYS. */
class Display : public HikaruScreen {
public:
	/* What the user chooses of it (display_options.hpp) */
	using Vsync = DisplayVsync;
	using Sides = DisplaySides;
	using Options = DisplayOptions;

	/* A display that shows fewer frames a second would hold the board
	 * back where each frame waits for it: the game slower than the 60 of
	 * the cabinet, and its sound short of more than the sample a frame
	 * AudioOut makes up (1 in 735). The frames on time alone wait on any
	 * display: the late ones catch up. */
	static constexpr float LEAST_REFRESH = 59.92f;

	/* The window, black until a board is attached; nothing, with the
	 * reason on stderr, if it cannot be opened. The keyboard outlives it. */
	static std::unique_ptr<Display> make(const Keyboard &keys, const Options &options);
	~Display();
	Display(const Display &) = delete;
	Display &operator=(const Display &) = delete;

	/* The window becomes the screen of a board, the programs of its game
	 * prepared behind the loading screen with its title; detach before
	 * the board goes. Then the window is black again. */
	void attach(Hikaru &board, const std::string &title);
	/* As above, for a board whose scenes the GPU draws directly, its
	 * programs kept for the game of that name: call set_scene before the
	 * end_frame of a frame that has one */
	void attach_scenes(const std::string &game, const std::string &title);
	void set_scene(PmxScene scene);
	/* The DAC of that board puts out black: the picture is black, its
	 * scenes drawn all the same (its frame buffer keeps them) */
	void set_blank(bool blank);
	unsigned scale() const { return m_scale; }
	DisplayUpscaleMode upscale_mode() const { return m_upscale_mode; }
	bool draws_native() const { return m_own_native; }
	bool sorts_per_pixel() const { return m_own_sorts; }
	unsigned anisotropy() const { return m_anisotropy; }
	void detach();

	/* A frame of the board: begin_frame before the board runs it,
	 * end_frame once it has, with its number, whether it ended after its
	 * time at 60 frames a second, and the bar over it while it is shown,
	 * built then from its state. */
	void begin_frame();
	void end_frame(unsigned frame, bool late, BarState *bar = nullptr);

	/* Between the window and the whole display */
	void toggle_fullscreen();

	/* What the menu sets, the board waiting meanwhile. The window takes
	 * `times` the screen unless it is on the whole display. */
	void set_scale(unsigned scale);
	void set_upscale_mode(DisplayUpscaleMode mode);
	/* The filter of the picture by its name; empty: none */
	void set_filter(const std::string &name);
	void set_anisotropy(unsigned level);
	bool fullscreen() const;
	void set_fullscreen(bool on);
	void resize(unsigned times);
	/* The window of that size, as the display has room for it, unless it
	 * is on the whole display */
	void resize_to(unsigned w, unsigned h);
	/* The size the user gave the window by hand, in points: a window, not
	 * maximized, of another size than Odin gave it last (false: none). It
	 * is then the size Odin gives it back after the whole display. */
	bool hand_size(unsigned &w, unsigned &h);
	void set_show_speed(bool on);
	/* The game paused (Session): the title says so, as while the menu is
	 * open, instead of its last speed */
	void set_paused(bool paused);
	/* Each frame waits only where the display keeps up (LEAST_REFRESH);
	 * again when the window goes to another display */
	void set_vsync(Vsync vsync);
	void display_changed() { set_vsync(m_vsync); }
	/* The frames a second of the display the window is on (0: not
	 * known) */
	float refresh() const;
	/* The graphics card, whether Vulkan draws on it (else OpenGL), and
	 * the size in pixels of the display the window is on */
	const std::string &gpu() const { return m_gpu; }
	bool vulkan() const { return m_graphics->vulkan(); }
	void display_size(unsigned &w, unsigned &h) const;
	/* The window, for the dialogs of the system; the list of the 3D
	 * programs of the game kept for the user (empty: none) */
	SDL_Window *window() const { return m_window; }
	const std::string &program_list() const { return m_program_list; }

	/* The menu (menu.hpp), over the picture of the last frame while the
	 * board waits: open_menu freezes that picture, draw_menu builds the
	 * menu from the state and shows it once. */
	void open_menu(int page = -1);
	void close_menu();
	bool menu_shown() const { return m_menu_shown; }
	void menu_event(const SDL_Event &event);
	void draw_menu(MenuState &state);
	/* The library over the whole window, the bar over it, no board
	 * attached: opened, then built from their states and shown once a
	 * picture */
	void open_library();
	void draw_library(LibraryState &state, BarState &bar);
	/* A picture of a game for the library, RGBA (the menu takes it; none:
	 * the one it had forgotten), after it is opened */
	void add_library_picture(const std::string &game, SDL_Surface *rgba);
	/* What is around the picture; with BEZEL, a PNG laid over the window
	 * where it is wider than the screen of the hardware, as large as the
	 * window holds it, the picture in its hole (empty: none) */
	void set_sides(Sides sides, const std::string &bezel);
	/* The hole of the bezel, where the picture goes (none: no bezel, or
	 * one with no see-through middle) */
	const PictureHole &picture_hole() const { return m_hole; }
	/* The white border of a light gun that finds the screen by it, over
	 * the game and its menu, as the game that runs has it (none when it
	 * stops) */
	void set_sinden_border(const SindenBorder &border);
	bool menu_popup_open() const;
	/* A click at that point of the window falls on something of the menu
	 * (Menu::item_at) */
	bool menu_item_at(MenuState &state, float x, float y);
	/* The keyboard writes in a field of the menu */
	bool menu_typing() const;
	/* The bar at the top of the window (menu.hpp: BarState), over the
	 * frames of the game that goes on: shown or hidden, the events of the
	 * mouse for it, kept until it is built at the end of the next frame.
	 * The last picture shown again with the bar over it (none: without),
	 * while the board waits. */
	void show_bar(bool on);
	bool bar_shown() const { return m_bar_shown; }
	void bar_event(const SDL_Event &event);
	void show_again(BarState *bar);
	/* The frames shown in the last second (0: not known yet) */
	float fps() const { return m_fps; }

	/* HikaruScreen */
	void list_launched() override;
	void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
	void before_memory_changes() override { finish(); }
	void memory_changed() override;
	bool save_pictures(StateFile &state) override;
	bool load_pictures(StateFile &state) override;

private:
	Display(const Keyboard &keys, const Options &options, SDL_Window *window, std::unique_ptr<HostGraphics> graphics);

	void set_window(unsigned times, bool fullscreen);
	void set_window_size(int w, int h, bool fullscreen);
	bool covers_display(int w, int h) const;
	void retitle();
	/* Black again, the board detached, when the window has to be drawn */
	void show_black();
	/* The menu, made the first time the bar, a note or the library needs it */
	void make_menu();
	/* The programs made before the first frame, with the loading screen:
	 * Hikaru's lists, the Neon 250's (load) */
	void prepare_programs(const std::string &title);
	void prepare_scenes(const std::string &title, const std::vector<uint64_t> &keys);
	using Progress = std::function<void(unsigned, unsigned)>;
	void load(const std::function<void(const Progress &)> &make,
	          const std::function<void(unsigned, unsigned, unsigned, unsigned)> &draw);
	/* Waits for the frame being drawn. */
	void finish();
	void wait_drawing();
	/* Has the drawing thread do a job, and waits for it. */
	void run(std::function<void()> job);
	/* On the drawing thread, the bar over the frame when `bar` */
	void draw(bool late, unsigned window_w, unsigned window_h, bool bar);
	/* On the drawing thread: the bezel made an image of the card (none:
	 * forgotten), then laid over the window */
	void set_bezel(const std::string &path);
	void make_bezel(SDL_Surface *rgba);
	void draw_bezel(unsigned window_w, unsigned window_h);
	void draw_blur(unsigned window_w, unsigned window_h);
	void draw_sinden_border(unsigned window_w, unsigned window_h);
	void draw_over(unsigned w, unsigned h, bool bar);
	/* Draws a frame from a board with its own drawing, in a window of w
	 * by h */
	void draw_own(bool late, unsigned w, unsigned h, bool bar);
	bool build_bar(BarState *bar);
	void read_picture(uint8_t *pixels);
	/* The window drawn, shown; `what` it shows, for ODIN_WINDOW_FP */
	void present(unsigned window_w, unsigned window_h, const char *what);
	void screenshot();
	static void write_ppm(const char *path, size_t w, size_t h, const uint8_t *pixels);
	void print_fingerprint();
	/* After a frame */
	void show_speed();
	void print_speed();

	Hikaru *m_board;	/* none: the window is black */
	const Keyboard &m_keys;
	SDL_Window *m_window;
	/* The size Odin gave the window last, or took from the user, in
	 * points; whether it is the user's */
	int m_given_w = 0, m_given_h = 0;
	bool m_by_hand = false;
	/* Whether Odin asked last for the whole display */
	bool m_fullscreen_asked = false;
	std::string m_program_list;
	std::string m_title = "Odin";	/* without the speed */
	bool m_paused = false;
	/* What it draws with, and the renderer of the board */
	std::unique_ptr<HostGraphics> m_graphics;
	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Worker> m_drawing;
	/* Made when it opens first */
	std::unique_ptr<Menu> m_menu;
	bool m_menu_shown;
	bool m_bar_shown = false;
	std::vector<SDL_Event> m_bar_events;
	std::string m_gpu;
	bool m_show_speed;
	Vsync m_vsync;
	/* On the drawing thread: whether a frame on time and a late one wait
	 * for the display, and whether the frames wait now */
	bool m_wait_on_time, m_wait_late, m_waits;

	/* The frame given to the drawing thread: its number, the options it
	 * is drawn with, whether its drawing writes a log, and the picture
	 * the drawing leaves (none yet: the size of the screen of the board).
	 * Around it: the bezel, or the sides blurred. */
	unsigned m_frame;
	unsigned m_scale, m_anisotropy;
	DisplayUpscaleMode m_upscale_mode;
	std::string m_filter;	/* the path of its preset */
	bool m_logged;
	HostPicture m_picture;
	HostImage m_bezel;
	/* The drawing of a board whose scenes the GPU draws directly, and its
	 * next scene */
	std::unique_ptr<Drawing> m_own;
	PmxScene m_own_scene;
	unsigned m_own_built = 1;
	bool m_own_fresh = false;
	bool m_own_blank = false;
	bool m_own_native = false, m_own_sorts = false;
	std::atomic<Sides> m_sides{ Sides::OFF };
	PictureHole m_hole;
	SindenBorder m_sinden_border;
	/* The debug switches as text, for the title */
	std::string m_message;

	/* ODIN_LATE_DRAW: the drawing given last waits for the semaphore */
	bool m_late;
	bool m_held;
	std::binary_semaphore m_late_draw;

	/* What the environment asks for (the list above) */
	const char *m_shot_prefix, *m_window_shot;
	unsigned m_shot_every;
	bool m_fingerprint, m_window_fp;
	std::vector<uint8_t> m_pixels, m_window_pixels;
	bool m_speed;
	uint64_t m_speed_since, m_speed_last, m_speed_longest;
	/* ODIN_STATS: the host time of the drawings on their thread, and what
	 * the board waited for them: at the end of a frame, and before it
	 * changed what a drawing reads */
	bool m_stats;
	uint64_t m_draw_ns, m_wait_frame_ns, m_wait_write_ns;
	unsigned m_drawn;
	/* The frames shown in the title, and in the last second */
	uint64_t m_title_since;
	unsigned m_title_frames;
	float m_fps = 0.0f;
};

} /* namespace odin */

#endif /* ODIN_DISPLAY_HPP */
