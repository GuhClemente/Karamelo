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

#ifndef ODIN_BOARD_DRAWING_HPP
#define ODIN_BOARD_DRAWING_HPP

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "odin/common/repeats.hpp"
#include "odin/machine/chips/antarctic/antarctic_list.hpp"
#include "odin/machine/chips/pmx1/pmx1_scene.hpp"
#include "odin/machine/boards/hikaru/hikaru_draw.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/ui/loading_screen.hpp"
#include "odin/host/video/picture.hpp"
#include "odin/host/renderer/hikaru/renderer_programs.hpp"
#include "odin/host/renderer/hikaru/depth_bias.hpp"

namespace odin {

namespace vk {
class Context;
}

/* A texture the renderer draws the board with (0: none) */
struct HostTexture {
	uint32_t id = 0;
};

/* A way the renderer reads its textures (0: none) */
struct HostSampler {
	uint32_t id = 0;
};

/* The texels of a texture, 16 bits each as the texture RAM of the board
 * holds them (ODIN_FORMAT_*, antarctic_list.hpp): ABGR1555 and ABGR4444
 * a little-endian word each, LA8 a byte of light then one of alpha. */
enum class Texels : uint8_t { ABGR1555, ABGR4444, LA8 };

/* A level of a texture: the rectangle x, y, w by h of a picture `row`
 * texels wide that starts at `texels` */
struct TexelRect {
	const void *texels;
	unsigned row, x, y, w, h;
};

/* The pixels of a 2D layer: ABGR1555 a little-endian word each, A2BGR10 a
 * little-endian 32-bit word each */
enum class LayerTexels : uint8_t { ABGR1555, A2BGR10 };

/* How the renderer has a mesh drawn, by the rules of the board
 * (hikaru_draw.hpp) */
struct MeshDraw {
	MeshVariant variant;	/* its program */
	MeshBlend blend;
	bool weigh_by_alpha;	/* the colour weighted by its alpha */
	unsigned blend_mode;	/* A81, among the translucent fragments */
	HostTexture texture;	/* with variant.has_texture */
	HostSampler sampler;
	bool snap;		/* Snap 2D: its corners on the pixels of the hardware's screen */
};

/* What the drawing of a list is told besides the list: where the lines of
 * its log go (HR_LOG; nullptr: none) and the switches of the debug keys */
struct SceneDebug {
	std::FILE *log;
	bool no_ambient, no_diffuse;
};

/* A line of that log, of the renderer's too: "\tHR: ", what fmt says */
void log_line(std::FILE *log, const char *fmt, va_list args);

/* What the graphics card of the host does for the renderer (renderer.hpp),
 * which draws the pictures of the board: the other side of the door of
 * host_graphics.hpp, said in the words of the board, never in those of the
 * API. HostGraphics makes one for each game, with the renderer, and it goes
 * with it. The *_gl.cpp files do it with OpenGL, drawing_vk.cpp with Vulkan.
 *
 * The pictures it holds are `scale` times the screen of the hardware each
 * way (SCREEN_WIDTH by SCREEN_HEIGHT, hikaru_draw.hpp):
 *  - the scene, where a display list is drawn, with its depth;
 *  - the KEPT pictures, kept from one frame to the next: the two 3D
 *    buffers of the hardware, then the pictures of the last three lists
 *    (the pipe), on their way to the screen;
 *  - the picture of the frame: the 3D and the 2D layers composed.
 *
 * On the drawing thread (display.hpp), as the renderer. */
class Drawing {
public:
	/* The kept pictures: the 3D buffers, then the places of the pipe */
	static constexpr unsigned BUFFERS = 2, PIPE = 3, KEPT = BUFFERS + PIPE;
	/* A kept picture as read_kept() gives it: RGBA at the size of the screen */
	static constexpr size_t PICTURE_SIZE = size_t{SCREEN_WIDTH} * SCREEN_HEIGHT * 4;
	/* Bytes of a node_t of the shaders: a translucent fragment in its list */
	static constexpr unsigned NODE_SIZE = 32;
	/* The family of the 101 (depth_bias.hpp) whose depth a mesh of that
	 * variant is drawn into in a view with both, 1 flagged or 0;
	 * NO_FAMILY: a variant without families, into the scene */
	static constexpr unsigned NO_FAMILY = 2;
	static unsigned family_of(const odin_mesh_t &mesh, const MeshVariant &variant)
	{
		return !variant.families ? NO_FAMILY : depth_bias::flagged(static_cast<int>(mesh.depth_bias)) ? 1 : 0;
	}
	/* The modelview of instance i of a mesh: its place in the list, or the
	 * place after the last (num_mvs) for IDENTITY where the mesh has none
	 * or one out of the list, said (something is drawn anyway) */
	static constexpr odin_modelview_t IDENTITY = { {
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
	} };
	static uint32_t modelview_of(const odin_display_list_t &list, const odin_mesh_t &mesh, unsigned i)
	{
		if (mesh.mv_index == ~0u) {
			repeats::error("attempting to draw with no modelview!");
			return list.num_mvs;
		}
		if (mesh.mv_index + i >= list.num_mvs) {
			repeats::error("a modelview outside its list: %u", mesh.mv_index + i);
			return list.num_mvs;
		}
		return mesh.mv_index + i;
	}
	/* The clipping planes of a view said on stderr where they are not
	 * right: the far one not finite, the near one below 0, or the planes
	 * the wrong way round */
	static void check_view(const odin_viewport_t &vp)
	{
		if (!std::isfinite(vp.clip.f) || !(std::isfinite(vp.clip.n) && vp.clip.n >= 0.0f))
			repeats::error("non-positive viewport clipping planes: %s", odin_viewport_str(&vp));
		if (vp.clip.l >= vp.clip.r || vp.clip.b >= vp.clip.t || vp.clip.n >= std::fabs(vp.clip.f))
			repeats::error("inverted viewport clipping planes: %s", odin_viewport_str(&vp));
	}
	/* The clip window of a view in the pixels of a picture `scale` times
	 * the screen, its lines counted from the bottom as the pictures keep
	 * them; the scissor cuts what leaves the screen */
	struct ClipWindow {
		int32_t x, y, w, h;
	};
	static ClipWindow clip_window(const odin_viewport_t &vp, unsigned scale)
	{
		const int32_t n = static_cast<int32_t>(scale);

		return { static_cast<int32_t>(vp.clip.l) * n,
			 (static_cast<int32_t>(SCREEN_HEIGHT) - static_cast<int32_t>(vp.clip.t)) * n,
			 static_cast<int32_t>(vp.clip.r - vp.clip.l) * n, static_cast<int32_t>(vp.clip.t - vp.clip.b) * n };
	}
	/* Its second modelview: the first one where it has none */
	static uint32_t modelview2_of(const odin_display_list_t &list, const odin_mesh_t &mesh, uint32_t first)
	{
		return mesh.mv2_index < list.num_mvs ? mesh.mv2_index : first;
	}
	/* Where the count of the fragments of a view is kept, for the list of
	 * a picture of the pipe */
	static constexpr unsigned count_index(unsigned slot, unsigned view) { return slot * ODIN_VIEWPORTS + view; }

	/* With OpenGL, on the thread its context is current on; with Vulkan,
	 * on that card (vk_common.hpp) */
	static std::unique_ptr<Drawing> make_gl();
	static std::unique_ptr<Drawing> make_vk(vk::Context &card);
	virtual ~Drawing() = default;

	/* The pictures `scale` times finer, 1 at first; the kept ones carried
	 * over, each pixel the nearest */
	virtual void set_scale(unsigned scale) = 0;
	/* Target FBO for hardware render (e.g. Libretro) */
	virtual void set_target_fbo(uint32_t fbo) { (void)fbo; }
	/* Whether it draws Native 2D (MeshVariant::native: geometry shaders) */
	virtual bool draws_native() const = 0;
	/* Whether it sorts the translucent fragments of a scene of the Neon
	 * 250 pixel by pixel (PmxDraw::per_pixel) */
	virtual bool sorts_per_pixel() const = 0;

	/* A display list drawn into the scene: its vertices sent and the
	 * scene emptied; each of its views from an empty depth; a pass for
	 * each polygon type, with its meshes; the end of it. The list is read
	 * until end_scene. */
	virtual void begin_scene(const odin_display_list_t &list, const SceneDebug &debug) = 0;
	virtual void clear_depth() = 0;
	virtual void begin_pass(const PassRules &rules) = 0;
	virtual void draw_mesh(const odin_mesh_t &mesh, const MeshDraw &draw) = 0;
	virtual void end_pass() = 0;
	virtual void end_scene() = 0;

	/* A view with meshes of both families of the 101 (depth_bias.hpp),
	 * until clear_depth: each mesh that writes the depth (variant
	 * families) is drawn into the depth of its family, where the 101
	 * moves it, and meets the other family at the eye depths both leave;
	 * the translucent fragments meet what kept each pixel the same way. */
	virtual void begin_families() = 0;

	/* The translucent polygons of a view (renderer_translucent.cpp),
	 * within its pass: begin_translucent; then their meshes drawn between
	 * begin_collect and end_collect put their fragments in lists, and the
	 * count of them is kept for counted_fragments under a picture of the
	 * pipe, or waited for now; then end_translucent blends them over the
	 * colour below, or not. */
	virtual void begin_translucent() = 0;
	virtual void begin_collect(unsigned view, unsigned slot) = 0;
	virtual void end_collect() = 0;
	virtual void keep_count(unsigned view, unsigned slot) = 0;
	virtual uint32_t count_now(unsigned view, unsigned slot) = 0;
	virtual void end_translucent(bool blend) = 0;
	/* The most fragments a view of the list of that picture left, its
	 * counts waited for and forgotten; or forgotten unread */
	virtual uint32_t counted_fragments(unsigned slot, uint32_t views) = 0;
	virtual void forget_counts(unsigned slot) = 0;
	/* Room for `max` fragments, what it held lost; the most the card
	 * takes */
	virtual void size_fragments(uint32_t max) = 0;
	virtual uint32_t fragment_limit() const = 0;

	/* The programs of the meshes (MeshVariant): each one the driver links
	 * kept in that folder, and taken from there the next times; listed in
	 * a file from now on; one made ready (compiled and drawn once), between
	 * begin_preparing and end_preparing; how many were read back from the
	 * folder and compiled so far */
	virtual void keep_programs_in(const std::string &folder) = 0;
	virtual void list_programs_in(const std::string &path) = 0;
	virtual bool holds_program(uint64_t key) const = 0;
	virtual void begin_preparing() = 0;
	virtual void prepare_program(uint64_t key) = 0;
	virtual void end_preparing() = 0;
	virtual unsigned programs_read_back() const = 0;
	virtual unsigned programs_compiled() const = 0;

	/* Debug (ODIN_DRAW_TIMES, renderer_times.hpp): stamps the card puts in
	 * its stream of commands, with the time it reaches each. Whether it
	 * has such a clock; one put now; whether it has reached it; its time,
	 * in ns, once reached; forgotten, to be put again or not; what was
	 * asked of the card so far sent to it now. */
	virtual bool has_stamps() const = 0;
	virtual uint32_t put_stamp() = 0;
	virtual bool reached(uint32_t stamp) = 0;
	virtual uint64_t stamp_time(uint32_t stamp) = 0;
	virtual void forget_stamp(uint32_t stamp, bool again) = 0;
	virtual void send_commands() = 0;

	/* The scene into a picture of the pipe: as it is, or blended with a
	 * 3D buffer, which fades by `rate` (181) at each frame: the motion
	 * blur of the cabinets */
	virtual void scene_to_pipe(unsigned slot) = 0;
	virtual void blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate) = 0;
	/* A 3D buffer takes a picture of the pipe */
	virtual void keep_picture(unsigned slot, unsigned buffer) = 0;

	/* A 2D layer: SCREEN_WIDTH by SCREEN_HEIGHT pixels, the top line
	 * first, kept until it is given again */
	virtual void set_layer(unsigned i, LayerTexels texels, const void *pixels) = 0;
	/* The picture of the frame: the picture `shown` of the pipe and the
	 * layers shown, as `compose` says (hikaru_draw.hpp) */
	virtual void compose(const Compose &compose, unsigned shown, const bool layers[2]) = 0;
	/* The picture of the frame in the window, at `rect`, black around:
	 * pixel for pixel at its size, else each pixel the mean of what it
	 * covers; through the filter if there is one */
	virtual void present(PictureRect rect, unsigned window_w, unsigned window_h) = 0;
	virtual HostPicture frame_picture() const = 0;
	/* Draws a Neon 250 scene (pmx_gl.cpp) as the frame's picture, at the
	 * picture scale, with that texture anisotropy */
	virtual void draw_pmx(const PmxScene &scene, unsigned anisotropy) = 0;
	/* Makes the programs draw_pmx draws with before its first scene, and
	 * those of the keys of a list of the Neon 250 (read_pmx_list) the card
	 * runs; `progress` is told how many of how many are made: none, then
	 * after each one */
	virtual void prepare_pmx(const std::vector<uint64_t> &keys,
	                         const std::function<void(unsigned, unsigned)> &progress) = 0;
	/* Lists in that file the programs draw_pmx makes from now on, after
	 * those it lists */
	virtual void list_pmx_in(const std::string &path) = 0;
	/* The filter of a preset (picture_filter_gl.hpp), built when the path
	 * changes; none with an empty path, or when it does not build (said
	 * once on stderr) */
	virtual void set_filter(const std::string &preset) = 0;
	/* The loading screen: the window black, then each rectangle of it at
	 * its shade */
	virtual void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) = 0;

	/* The kept pictures for a save state: KEPT of them at the size of the
	 * screen of the hardware, bytes of RGBA, the bottom line first, the
	 * first pixel of each scale x scale; and back, each pixel then all of
	 * its scale x scale */
	virtual void read_kept(std::span<uint8_t> pictures) = 0;
	virtual void write_kept(std::span<const uint8_t> pictures) = 0;

	/* The textures: one made of its levels, the largest first, as large
	 * as the first, their texels read before it returns; some forgotten */
	virtual HostTexture make_texture(Texels texels, std::span<const TexelRect> levels) = 0;
	virtual void forget_textures(std::span<const HostTexture> textures) = 0;
	/* A way to read them with that sampling (hikaru_draw.hpp) and that
	 * anisotropic filtering, 1 for none; forgotten */
	virtual HostSampler make_sampler(const TextureSampling &sampling, unsigned anisotropy) = 0;
	virtual void forget_sampler(HostSampler sampler) = 0;

protected:
	/* A kept picture as read_kept() gives it, from the whole of one `scale`
	 * times finer (RGBA, its lines in the same order) */
	static void screen_of(const uint8_t *whole, unsigned scale, uint8_t *out)
	{
		for (unsigned y = 0; y < SCREEN_HEIGHT; y++) {
			const uint8_t *row = whole + size_t{y} * scale * SCREEN_WIDTH * scale * 4;

			for (unsigned x = 0; x < SCREEN_WIDTH; x++, out += 4)
				std::memcpy(out, row + size_t{x} * scale * 4, 4);
		}
	}
};

} /* namespace odin */

#endif /* ODIN_BOARD_DRAWING_HPP */
