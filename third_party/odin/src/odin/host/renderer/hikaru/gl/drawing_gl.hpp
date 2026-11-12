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

#ifndef ODIN_BOARD_DRAWING_GL_HPP
#define ODIN_BOARD_DRAWING_GL_HPP

/* The board drawing with OpenGL (drawing.hpp), for its own files
 * only: drawing_gl.cpp (the textures), pictures_gl.cpp (the
 * pictures and what goes from one to the next), scene_gl.cpp (the
 * meshes of a list), translucent_gl.cpp (its translucent fragments),
 * programs_gl.cpp (the programs of the meshes). */

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "odin/host/gfx/gl_common.hpp"

#include "odin/host/renderer/hikaru/drawing.hpp"
#include "odin/host/renderer/hikaru/gl/programs_gl.hpp"
#include "odin/machine/boards/hikaru/hikaru_draw.hpp"
#include "odin/host/video/picture_filter_gl.hpp"

namespace odin {

class GlDrawing : public Drawing {
public:
	GlDrawing();
	~GlDrawing() override;
	GlDrawing(const GlDrawing &) = delete;
	GlDrawing &operator=(const GlDrawing &) = delete;

	void set_scale(unsigned scale) override;
	void set_target_fbo(uint32_t fbo) override { m_target_fbo = fbo; }
	bool draws_native() const override { return gl::geometry_shaders(); }
	bool sorts_per_pixel() const override { return gl::fragment_lists() && gl::primitive_ids(); }

	void begin_scene(const odin_display_list_t &list, const SceneDebug &debug) override;
	void clear_depth() override;
	void begin_pass(const PassRules &rules) override;
	void draw_mesh(const odin_mesh_t &mesh, const MeshDraw &draw) override;
	void end_pass() override;
	void end_scene() override;
	void begin_families() override;

	void begin_translucent() override;
	void begin_collect(unsigned view, unsigned slot) override;
	void end_collect() override;
	void keep_count(unsigned view, unsigned slot) override;
	uint32_t count_now(unsigned view, unsigned slot) override;
	void end_translucent(bool blend) override;
	uint32_t counted_fragments(unsigned slot, uint32_t views) override;
	void forget_counts(unsigned slot) override;
	void size_fragments(uint32_t max) override;
	uint32_t fragment_limit() const override { return m_lists.limit; }

	void keep_programs_in(const std::string &folder) override { m_programs.keep_in(folder); }
	void list_programs_in(const std::string &path) override { m_programs.list_in(path); }
	bool holds_program(uint64_t key) const override { return m_programs.holds(key); }
	void begin_preparing() override;
	void prepare_program(uint64_t key) override;
	void end_preparing() override;
	unsigned programs_read_back() const override { return m_programs.read_back(); }
	unsigned programs_compiled() const override { return m_programs.compiled(); }

	bool has_stamps() const override;
	uint32_t put_stamp() override;
	bool reached(uint32_t stamp) override;
	uint64_t stamp_time(uint32_t stamp) override;
	void forget_stamp(uint32_t stamp, bool again) override;
	void send_commands() override { glFlush(); }

	void scene_to_pipe(unsigned slot) override;
	void blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate) override;
	void keep_picture(unsigned slot, unsigned buffer) override;

	void set_layer(unsigned i, LayerTexels texels, const void *pixels) override;
	void compose(const Compose &compose, unsigned shown, const bool layers[2]) override;
	void present(PictureRect rect, unsigned window_w, unsigned window_h) override;
	void draw_pmx(const PmxScene &scene, unsigned anisotropy) override;
	void prepare_pmx(const std::vector<uint64_t> &keys,
	                 const std::function<void(unsigned, unsigned)> &progress) override;
	/* All its programs are made before the first scene: none to list */
	void list_pmx_in(const std::string &) override {}
	HostPicture frame_picture() const override { return { m_frame_fb, width(), height() }; }
	void set_filter(const std::string &preset) override;
	void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) override;

	void read_kept(std::span<uint8_t> pictures) override;
	void write_kept(std::span<const uint8_t> pictures) override;

	HostTexture make_texture(Texels texels, std::span<const TexelRect> levels) override;
	void forget_textures(std::span<const HostTexture> textures) override;
	HostSampler make_sampler(const TextureSampling &sampling, unsigned anisotropy) override;
	void forget_sampler(HostSampler sampler) override
	{
#ifdef ODIN_GLES
		m_sampler_bias.erase(sampler.id);
#endif
		glDeleteSamplers(1, &sampler.id);
	}

private:
	uint32_t m_target_fbo = 0;
	struct Pmx;
	std::shared_ptr<Pmx> m_pmx;
	void make_pmx(const std::function<void(unsigned, unsigned)> &progress);
	/* Layers peeled at a time, each with a query of whether it drew, read
	 * once the batch is done */
	static constexpr unsigned PEEL_BATCH = 8;

	unsigned width() const { return SCREEN_WIDTH * m_scale; }
	unsigned height() const { return SCREEN_HEIGHT * m_scale; }
	void build_pictures();
	void destroy_pictures(bool kept_too);
	GLuint &kept(unsigned i) { return (i < BUFFERS) ? m_buffers[i] : m_pipe[i - BUFFERS]; }
	void copy_picture(GLuint from, unsigned from_w, unsigned from_h, GLuint to, unsigned to_w, unsigned to_h);
	void build_blender();
	void build_quad();
	void build_compose();
	void build_presenter();
	void set_2d_state();
	void show_picture(PictureRect rect, unsigned window_w, unsigned window_h);

	/* scene_gl.cpp */
	void log(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
	void upload_vertices(const odin_vertex_body_t *vertices, uint32_t num,
	                     const uint32_t *indices, uint32_t num_indices);
	void use_program(const MeshVariant &variant);
	void upload_viewport(const odin_mesh_t &mesh);
	void upload_modelview(const odin_mesh_t &mesh, unsigned i);
	void bind_texture(const MeshDraw &draw);
	void upload_lightset(const odin_mesh_t &mesh);
	void draw_instance(const odin_mesh_t &mesh, unsigned i);
	void set_blend(MeshBlend blend);

	/* translucent_gl.cpp */
	void copy_solid_depth();
	void build_list_pictures();
	void destroy_list_pictures();
	void build_lists();
	void destroy_lists();
	void peel();
	void draw_peel_layer();

	unsigned m_scale = 1;
	/* Where a list is drawn: a colour buffer and a depth buffer */
	GLuint m_scene_fb, m_scene_color, m_scene_depth;
	/* A view with both families of the 101 (begin_families): for each,
	 * the eye depth of the mesh that keeps each pixel (0: none) and the
	 * scale its 101 gives, and where it is drawn: the colour of the scene,
	 * that eye depth, a depth (the unflagged family that of the scene, the
	 * flagged one its own); the family drawn into now, NO_FAMILY: the
	 * scene */
	GLuint m_eye[2], m_flag_depth, m_family_fb[2];
	bool m_families_view = false;
	unsigned m_family = NO_FAMILY;
	/* The two 3D buffers of the hardware: the picture last blended into
	 * each */
	GLuint m_buffers[BUFFERS];
	/* The pictures of the last three lists */
	GLuint m_pipe[PIPE], m_pipe_fb[PIPE];
	/* For copy_picture, and a picture of the size of the screen of the
	 * hardware for the save states */
	GLuint m_copy_fb[2];
	GLuint m_state_picture;

	/* Blends the scene and a 3D buffer, see blend_scene_to_pipe */
	GLuint m_blender;
	GLint m_blender_rate;
	/* The picture of the frame: the 3D and the layers, composed */
	GLuint m_frame_fb, m_frame_color;
	/* Brings it to the size of the window */
	GLuint m_presenter;
	GLint m_presenter_rect;
	/* Composes the picture and the layers */
	GLuint m_quad_vao, m_quad_vbo;
	GLuint m_compose;
	struct {
		GLint front, back, layer1, layer2;
		GLint factor;
		GLint mult_f, mult_b, mult_1, mult_2;
		GLint keyed_1, keyed_2, keyed_3d;
		GLint above_1, above_2;
		GLint invert_f, invert_b;
		GLint clear;
	} m_compose_locs;
	/* The texture of each layer, kept from frame to frame, and what it
	 * holds (none before the first) */
	GLuint m_layer_tex[2] = { 0, 0 };
	std::optional<LayerTexels> m_layer_texels[2];

	/* The filter of the picture, its preset, the frames it has drawn */
	std::unique_ptr<PictureFilter> m_filter;
	std::string m_filter_preset;
	unsigned m_filter_frames = 0;

	/* The meshes: the list being drawn, its vertices and the indices of
	 * its triangles, the programs, the one in use and what is set for the
	 * list (~0u when not known) */
	const odin_display_list_t *m_list = nullptr;
	SceneDebug m_debug{};
	GLuint m_mesh_vao = 0, m_mesh_vbo = 0, m_mesh_ibo = 0;
	MeshPrograms m_programs;
	MeshProgram *m_program = nullptr;	/* nullptr: none of the mesh programs */
	MeshVariant m_variant;			/* of m_program */
	uint64_t m_variant_key = 0;
	uint32_t m_drawing = 0;			/* counts the lists drawn, see begin_scene */
	uint32_t m_scissor_vp_index = ~0u;
	GLuint m_unit0_texture = ~0u, m_unit0_sampler = ~0u;
#ifdef ODIN_GLES
	/* The LOD bias of each sampler, which the mesh shader applies */
	std::unordered_map<GLuint, float> m_sampler_bias;
#endif
	GLenum m_blend[4] = { ~0u, ~0u, ~0u, ~0u };
	/* How the meshes of the polygon type being drawn are drawn */
	struct {
		float alpha_ref;	/* fragments at or below it are dropped */
		bool depth_bias;	/* the 101 bias moves the mesh */
		uint32_t order;		/* triangles sent so far, while collecting */
	} m_pass{};

	/* The translucent fragments, see translucent_gl.cpp */
	GLuint m_solid_depth;		/* the depth the other types left */
#ifdef ODIN_GLES
	GLuint m_solid_depth_fb;	/* to blit it there */
#endif
	bool m_said_no_translucent = false;
	struct {
		GLuint program;		/* blends them */
		GLuint heads, heads_fb;	/* the last one of each pixel */
		GLuint nodes;
		GLuint count[PIPE * ODIN_VIEWPORTS];	/* one per viewport of each list of the pipe */
		/* Their copy where the processor reads them, with
		 * ARB_buffer_storage, else 0 and nullptr */
		GLuint readback;
		const GLuint *counts;
		uint32_t limit;		/* nodes the graphics card takes in a buffer */
		GLuint below;		/* the colour under them */
	} m_lists;
	/* Each list of the pipe: its counts kept, or nullptr */
	GLsync m_counted[PIPE] = {};
	struct PeelDraw {
		const odin_mesh_t *mesh;
		MeshDraw draw;
	};
	struct {
		bool on;
		std::vector<PeelDraw> draws;
		bool drawing;
		GLuint depth, layer, info, layer_fb;
		GLuint color[2], last[2], color_fb[2];
		GLuint blend, show;
		GLuint any[PEEL_BATCH];
	} m_peel{};

	/* The stamps not in use, to be put again */
	std::vector<GLuint> m_stamps;
};

} /* namespace odin */

#endif /* ODIN_BOARD_DRAWING_GL_HPP */
