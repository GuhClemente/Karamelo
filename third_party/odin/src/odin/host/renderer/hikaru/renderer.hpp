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

#ifndef ODIN_RENDERER_HPP
#define ODIN_RENDERER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "odin/machine/chips/antarctic/antarctic_list.hpp"
#include "odin/host/renderer/hikaru/drawing.hpp"
#include "odin/host/video/display_options.hpp"
#include "odin/machine/chips/eurasia/eurasia_layers.hpp"
#include "odin/machine/boards/hikaru/hikaru_draw.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/video/picture.hpp"
#include "odin/host/renderer/hikaru/renderer_programs.hpp"
#include "odin/host/renderer/hikaru/renderer_textures.hpp"
#include "odin/host/renderer/hikaru/renderer_times.hpp"

namespace odin {

class FbWrites;

class HikaruGpu;

/* Draws what the graphics chips of the Hikaru hand over, through the card of
 * the host (Drawing, drawing.hpp): it stands for the chips that
 * plot the polygons (see hikaru_gpu.hpp), and keeps their rules.
 *
 * The command processor leaves the display list of a frame, the display chip
 * the two 2D layers. The picture comes out in three steps:
 *
 *  - the list launched during a frame is drawn at the end of that frame,
 *    viewport by viewport and polygon type by polygon type
 *    (renderer_scene.cpp, renderer_translucent.cpp);
 *  - its picture is blended into a 3D buffer of the hardware and waits: it
 *    reaches the screen at the next launch (renderer.cpp);
 *  - the picture on the screen is composed with the layers at every frame
 *    (renderer.cpp, renderer_layers.cpp), then brought to the size of the
 *    window.
 *
 * Two sizes are told apart. The screen of the hardware, WIDTH by HEIGHT, is
 * what the games address: the centre of a view, its clip window, the 2D
 * layers, the pictures of a save state. The buffers the 3D
 * is drawn in are `scale` times as wide and as high, and so is the picture
 * on its way to the screen: a pixel of the hardware is scale x scale pixels
 * there. The window has a size of its own.
 *
 * Every call is made on the thread that has taken the card
 * (HostGraphics::take_thread), the constructor included. */
class Renderer {
public:
	/* The screen of the hardware, in pixels */
	static constexpr unsigned WIDTH = SCREEN_WIDTH;
	static constexpr unsigned HEIGHT = SCREEN_HEIGHT;

	/* The memories of the board the renderer reads */
	struct Memory {
		std::span<const uint8_t> fb;		/* framebuffer RAM, 8 MB */
		std::span<const uint8_t> texram[2];	/* texture RAM, two banks of 4 MB */
		FbWrites *fb_writes = nullptr;		/* the dates of its writes */
	};
	/* Debug: for looking at what is drawn (HR_LOG, HR_LOG_FRAME,
	 * ODIN_LIST_FP, ODIN_LIST_DUMP, ODIN_DRAW_TIMES, ODIN_DEBUG_KEYS;
	 * ODIN_LISTS_TEST for the tests); everything is off at zero. And the
	 * lists of the mesh programs of the game, see read_program_list: the
	 * one that comes with Odin, and the one kept on this computer,
	 * which takes the programs met later; the folder the programs the
	 * driver made are kept in. Empty for none. */
	struct Options {
		bool log;		/* the objects of every mesh, on stdout */
		int log_frame;		/* that log for this frame only */
		bool list_fp;		/* a fingerprint of each list, on stderr, and
					 * the framebuffer of each frame, on stdout */
		int list_dump;		/* with a line for each mesh of this frame, or -1 */
		bool lists_test;	/* no room for the translucent fragments at first */
		bool times;		/* what each part of the drawings cost, on stderr at the end */
		bool debug_keys;	/* the keys change the debug switches */
		bool (*key_down)(void *ctx, char key);
		void *ctx;
		std::string programs_seed, programs_kept, programs_made;
	};

	/* graphics: the card it draws with, which outlives it */
	Renderer(HostGraphics &graphics, const Memory &memory, const Options &options);
	Renderer(std::unique_ptr<Drawing> card, const Memory &memory, const Options &options);
	~Renderer();
	Renderer(const Renderer &) = delete;
	Renderer &operator=(const Renderer &) = delete;

	Drawing &card() { return *m_card; }
	void set_gpu(HikaruGpu *gpu) { m_gpu = gpu; }

	/* Makes ready the mesh programs the lists of the options name, so
	 * that the game does not stutter when it first draws with one. To
	 * be called on the thread draw_frame runs on. When there are some,
	 * `progress` is told how many of how many are ready: none, then after
	 * each one. */
	void prepare_programs(const std::function<void(unsigned, unsigned)> &progress);
	/* The loading screen (loading_screen.hpp) of a game in the window,
	 * placed as the picture of a frame is, while the programs are made
	 * ready. */
	void draw_loading(std::string_view title, unsigned done, unsigned total, unsigned window_w, unsigned window_h);

	/* Draws the 3D `scale` times finer than the screen of the hardware,
	 * 1 at first. The pictures kept are carried over, pixel for pixel. */
	void set_scale(unsigned scale);
	/* Anisotropic filtering of the textures, 1 for none as on the board */
	void set_anisotropy(unsigned level) { m_textures.set_anisotropy(level); }
	/* How the meshes flat to the screen are drawn at a scale above 1:
	 * finer as the rest, or as the hardware's screen has them (draw_mesh) */
	void set_upscale_mode(DisplayUpscaleMode mode) { m_upscale_mode = mode; }
	/* The picture through the filter of that preset (picture_filter_gl.hpp)
	 * into the window; none with an empty path, or when it does not
	 * build (said once on stderr). Built when the path changes. */
	void set_filter(const std::string &preset) { m_card->set_filter(preset); }

	/* A display list was launched. */
	void launch() { m_launched = 1; }
	/* The texture RAM of a bank is about to be written; every bank if
	 * negative. */
	void invalidate_textures(int bank) { m_textures.invalidate(bank); }
	/* A rectangle of texels of a bank is about to be written. */
	void invalidate_textures(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
	{
		m_textures.invalidate(bank, x, y, w, h);
	}

	/* A frame of the machine, in three calls:
	 *  - begin_frame, before the machine runs it;
	 *  - take_frame, once it has, with the number of the frame and the
	 *    size of the window in pixels: what the frame leaves to be drawn
	 *    is taken from the machine;
	 *  - draw_frame draws it. Its picture is shown in the window: as
	 *    large as fits, in the shape of the screen of the hardware,
	 *    black around.
	 *
	 * draw_frame reads nothing of the machine but the texture RAM and
	 * the display lists of the last two frames: the machine may run the
	 * next frame meanwhile. It must be over before the texture RAM is
	 * written, and before any call but launch and begin_frame. */
	void begin_frame();
	void take_frame(unsigned frame, unsigned window_w, unsigned window_h, const PictureHole &hole = {});
	void draw_frame();
	/* The window has taken the picture of the frame: the times of its
	 * drawing end there. */
	void frame_shown() { m_times.end(); }
	/* The drawing of the frame taken last writes a log. */
	bool frame_is_logged() const { return logging(); }
	/* The picture of the last frame in the window again, as it was shown
	 * (the board waits) */
	void show_again(unsigned window_w, unsigned window_h, const PictureHole &hole)
	{
		m_card->present(picture_rect(window_w, window_h, hole), window_w, window_h);
	}

	/* The picture of the last frame, whatever the size of the window */
	HostPicture frame_picture() const { return m_card->frame_picture(); }

	/* The debug switches as text, for the title of the window; empty
	 * without the debug keys. */
	const char *message() const { return m_message; }

	/* Save state. The renderer keeps from one frame to the next the two
	 * 3D buffers, which the blender feeds back, and the pictures on their
	 * way to the screen: PICTURES pictures of PICTURE_SIZE bytes, then
	 * the counters. A picture of a state has the size of the screen of
	 * the hardware whatever the scale: one pixel of each scale x scale is
	 * saved, and comes back as all of them. The textures are decoded
	 * again from the texture RAM. */
	static constexpr unsigned PICTURES = Drawing::KEPT;
	static constexpr size_t PICTURE_SIZE = Drawing::PICTURE_SIZE;
	struct Counters {
		uint32_t num_lists;
		uint32_t shown;
		int32_t next;
		uint32_t launched;
	};
	void save(std::span<uint8_t> pictures, Counters &counters);
	void load(std::span<const uint8_t> pictures, const Counters &counters);

private:
	/* The switches of the debug keys, see renderer_debug.cpp */
	enum Debug : unsigned {
		DEBUG_LOG,
		DEBUG_NO_LAYER1,
		DEBUG_NO_LAYER2,
		DEBUG_NO_3D,
		DEBUG_NO_TEXTURES,
		DEBUG_NO_MIPMAPS,
		DEBUG_SELECT_POLYTYPE,
		DEBUG_NO_LIGHTING,
		DEBUG_NO_AMBIENT,
		DEBUG_NO_DIFFUSE,
		DEBUG_NO_FOG,

		NUM_DEBUG
	};
	/* The list drawn last, until its translucent fragments are counted:
	 * see check_last_list */
	struct Last {
		odin_display_list_t *list;	/* nullptr once counted */
		unsigned buffer;	/* its 3D buffer */
		int back;		/* the one blended into it, or -1 */
		uint32_t rate;		/* 181 */
		uint32_t slot;		/* its picture in the pipe, and its counts */
		uint32_t views;		/* that have such fragments */
	};
	/* The texture each texhead of a list was drawn with, see tex_ids */
	struct ListTextures {
		const odin_display_list_t *list;
		std::vector<HostTexture> ids;
	};
	/* What take_frame keeps of the machine for draw_frame, with the
	 * list and the layers */
	struct Frame {
		unsigned number;
		bool launched;		/* a list was launched during it */
		odin_fb_config_t fb;
		odin_layers_t layers;
		unsigned window_w, window_h;
		PictureHole hole;	/* of the bezel, where the picture goes */
	};

	/* renderer.cpp */
	/* Says what went wrong on stderr and aborts. */
	[[noreturn]] static void fatal(const char *what);
	void forget_held();
	void show();
	void draw_list();
	void check_last_list();
	bool too_many(Last &last);
	void draw_again(const Last &last);
	void compose();
	/* The size of the buffers the lists are drawn in */
	unsigned width() const { return WIDTH * m_scale; }
	unsigned height() const { return HEIGHT * m_scale; }

	/* renderer_scene.cpp */
	void draw_scene();
	bool has_flagged(unsigned vpi) const;
	void draw_polytype(unsigned vpi, unsigned polytype);
	void draw_mesh(const odin_mesh_t &mesh);
	MeshVariant variant_of(const odin_mesh_t &mesh) const;
	bool is_faded_by_depth(const odin_mesh_t &mesh) const;
	void choose_texture(const odin_mesh_t &mesh);
	void set_material_blend(const odin_mesh_t &mesh);
	void set_opaque_blend(const odin_mesh_t &mesh);
	std::vector<HostTexture> &tex_ids();

	/* renderer_translucent.cpp */
	void size_lists(uint32_t max);
	bool grow_lists(uint32_t used);
	uint32_t room_at_start() const;
	void draw_translucent(unsigned vpi, const odin_mesh_t *meshes, unsigned num);

	/* renderer_layers.cpp */
	bool shows_layer(unsigned i) const;
	void take_layer(unsigned i);
	void send_layer(unsigned i);

	/* renderer_debug.cpp */
	void init_debug();
	void update_debug();
	bool logging() const { return m_debug[DEBUG_LOG] != 0; }
	void log(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
	void log_mesh(const odin_mesh_t &mesh) const;
	void print_list_fingerprint() const;

	HikaruGpu *m_gpu;
	std::span<const uint8_t> m_fb;
	FbWrites *m_fb_writes;
	Options m_options;

	/* The list the command processor builds, and the one of the frame
	 * taken last */
	odin_display_list_t *m_building;
	odin_display_list_t *m_list;
	Frame m_frame;
	/* The command processor builds its lists in three buffers, the picture
	 * of each in its place of the pipe. */
	static constexpr unsigned LIST_BUFFERS = Drawing::PIPE;
	ListTextures m_list_textures[LIST_BUFFERS];

	/* What the card of the host does for it */
	std::unique_ptr<Drawing> m_card;
	TextureCache m_textures;
	DrawTimes m_times;

	/* The texhead of the list being drawn whose texture m_draw has, and
	 * how the next mesh is drawn */
	uint32_t m_bound_tex_index;
	MeshDraw m_draw;
	/* The translucent fragments are collected (draw_translucent); the
	 * view has meshes of both families of the 101 (depth_bias.hpp), and
	 * the pass writes the depth */
	bool m_collect;
	bool m_families_view, m_families;
	/* The list is drawn again, see check_last_list */
	bool m_again;
	/* Room for that many translucent fragments; a list did not fit, and
	 * it was told */
	uint32_t m_fragments_max;
	bool m_fragments_full;

	/* How many times finer than the screen of the hardware the buffers
	 * are */
	unsigned m_scale;
	DisplayUpscaleMode m_upscale_mode;
	/* The pictures of the last three lists, in the pipe of m_card: see
	 * show */
	uint32_t m_num_lists;		/* lists drawn so far */
	uint32_t m_shown;		/* the picture on the screen */
	int32_t m_next;			/* the one it takes at the next frame, or -1 */
	uint32_t m_launched;		/* a list was launched since the last frame taken */
	Last m_last;

	/* The framebuffer RAM of the two layers as the frame left it,
	 * WIDTH words a line, whether it changed since it was last sent to
	 * m_card, and the pixels of a layer on their way there */
	std::vector<uint8_t> m_layer_ram[2];
	bool m_layer_changed[2];
	/* Where m_layer_ram was taken from, and the date after which a write
	 * there may have changed it (0: any) */
	size_t m_layer_offs[2];
	uint64_t m_layer_date[2];
	std::vector<uint16_t> m_layer_pixels;
	/* The format of each layer last sent (~0u before the first) */
	uint32_t m_layer_format[2];

	int m_debug[NUM_DEBUG];
	bool m_debug_held[NUM_DEBUG];
	char m_message[256];
};

} /* namespace odin */

#endif /* ODIN_RENDERER_HPP */
