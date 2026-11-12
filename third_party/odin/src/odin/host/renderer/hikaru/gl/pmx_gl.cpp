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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>

#include "odin/host/gfx/glsl_texts.hpp"
#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"

/* Draws Neon 250 scenes to a target, then to the frame's picture */

namespace odin {

struct GlDrawing::Pmx {
	/* Plain and Native 2D (pmx.geom), then the same putting fragments in
	 * the lists of their pixels */
	struct Program {
		GLuint id = 0;
		GLint most_w = 0, lod_bias = 0, flags = 0, scale = -1, order = -1, mode = -1;
		GLint fog_density = 0, fog_color = 0, fog_vertex_color = 0;

		void take(GLuint program, bool native, bool list)
		{
			id = program;
			most_w = gl::uniform(id, "u_most_w");
			lod_bias = gl::uniform(id, "u_lod_bias");
			flags = gl::uniform(id, "u_flags");
			fog_density = gl::uniform(id, "u_fog_density");
			fog_color = gl::uniform(id, "u_fog_color");
			fog_vertex_color = gl::uniform(id, "u_fog_vertex_color");
			/* The texture on unit 0, the fog table on unit 1 */
			glUseProgram(id);
			glUniform1i(gl::uniform(id, "u_fog"), 1);
			glUseProgram(0);
			if (native)
				scale = gl::uniform(id, "u_scale");
			if (list) {
				order = gl::uniform(id, "u_order");
				mode = gl::uniform(id, "u_mode");
			}
		}
	} programs[4];
	/* The per-pixel sort (pmx_sort.frag): the last fragment of each pixel,
	 * the fragments, their count, the colour below them. Room for 8 a pixel
	 * of the board at first (the smoke of Smashing Drive needs 5.2): the fragments of a
	 * full buffer are lost in the order the card takes them, a picture that
	 * differs from run to run. The count a frame leaves comes back two
	 * frames later, and the buffer grows past it. A fragment takes 16
	 * bytes, its rank in TA order 26 bits of them (pmx.frag). */
	struct Lists {
		static constexpr GLsizeiptr NODE_SIZE = 16;
		static constexpr size_t RANKS = size_t{ 1 } << 26;
		GLuint sort = 0, heads = 0, heads_fb = 0, nodes = 0, count = 0, below = 0;
		GLuint readback[2] = { 0, 0 };
		GLsync counted[2] = { nullptr, nullptr };
		unsigned w = 0, h = 0, turn = 0;
		GLsizeiptr room = 0, most = 0;
	} lists;
	GLuint vao = 0, vbo = 0;
	/* The fog table, and what it holds */
	GLuint fog = 0;
	std::array<uint8_t, 256> fog_table{};
	bool fog_loaded = false;
	/* Where scenes are drawn, if the depth can be a float from 0 to 1 as
	 * the chip's is: games lay polygons a hair apart at very small 1/W */
	GLuint fine_fb = 0, fine_depth = 0;
	unsigned fine_w = 0, fine_h = 0;
	std::unordered_map<uint32_t, GLuint> images;
	std::unordered_map<uint32_t, GLuint> samplers;

	~Pmx()
	{
		for (const auto &[id, texture] : images)
			glDeleteTextures(1, &texture);
		for (const auto &[key, sampler] : samplers)
			glDeleteSamplers(1, &sampler);
		glDeleteFramebuffers(1, &fine_fb);
		glDeleteTextures(1, &fine_depth);
		for (GLsync &fence : lists.counted)
			if (fence)
				glDeleteSync(fence);
		glDeleteBuffers(2, lists.readback);
		glDeleteBuffers(1, &lists.nodes);
		glDeleteBuffers(1, &lists.count);
		glDeleteFramebuffers(1, &lists.heads_fb);
		glDeleteTextures(1, &lists.heads);
		glDeleteTextures(1, &lists.below);
		glDeleteProgram(lists.sort);
		glDeleteBuffers(1, &vbo);
		glDeleteVertexArrays(1, &vao);
		glDeleteTextures(1, &fog);
		for (const Program &program : programs)
			glDeleteProgram(program.id);
	}

	GLuint sampler(const PmxDraw &draw, unsigned anisotropy)
	{
		const uint32_t key = static_cast<uint32_t>(draw.wrap_u) | static_cast<uint32_t>(draw.wrap_v) << 2 |
		                     uint32_t{ draw.smooth } << 4 | uint32_t{ draw.mipmap } << 5 | anisotropy << 6;
		const auto found = samplers.find(key);

		if (found != samplers.end())
			return found->second;

		const auto wrap = [](PmxWrap how) {
			return how == PmxWrap::CLAMP ? GL_CLAMP_TO_EDGE : how == PmxWrap::MIRROR ? GL_MIRRORED_REPEAT : GL_REPEAT;
		};
		GLuint id;

		glGenSamplers(1, &id);
		glSamplerParameteri(id, GL_TEXTURE_WRAP_S, wrap(draw.wrap_u));
		glSamplerParameteri(id, GL_TEXTURE_WRAP_T, wrap(draw.wrap_v));
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, draw.smooth ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
		                    draw.mipmap ? (draw.smooth ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST)
		                                : (draw.smooth ? GL_LINEAR : GL_NEAREST));
		if (anisotropy > 1 && draw.mipmap && gl::anisotropic_filter())
			glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, static_cast<float>(anisotropy));
		samplers.emplace(key, id);
		return id;
	}
};

namespace {

/* Whether the depth can be a float from 0 to 1, as the chip's is */
bool
float_depth()
{
	return gl::depth_from_0();
}

} /* namespace */

/* The programs (the plain one, its native 2D form, the same two putting
 * their fragments in lists, the sort of the lists), kept with the mesh
 * programs (MeshPrograms::link), each said made to `progress`; then the
 * vertex array */
void
GlDrawing::make_pmx(const std::function<void(unsigned, unsigned)> &progress)
{
	const unsigned total = 1 + (gl::geometry_shaders() ? 1 : 0) +
	                       (gl::fragment_lists() ? (gl::geometry_shaders() ? 3 : 2) : 0);
	unsigned done = 0;
	const auto made = [&] {
		if (progress)
			progress(++done, total);
	};

	const std::string vertex = std::string(float_depth() ? "#define DEPTH_FROM_0 1\n" : "") + odin_glsl_pmx_vert;

	if (progress)
		progress(0, total);
	m_pmx = std::make_shared<Pmx>();
	m_pmx->programs[0].take(m_programs.link("pmx", vertex, odin_glsl_pmx_frag, ""), false, false);
	made();
	if (gl::geometry_shaders()) {
		const std::string fragment = std::string("#define NATIVE 1\n") + odin_glsl_pmx_frag;

		const GLuint native = m_programs.link("pmx-native", vertex, fragment, odin_glsl_pmx_geom);

		m_pmx->programs[1].take(native, true, false);
		made();
	}
	if (gl::fragment_lists()) {
		const std::string list = std::string("#define LIST 1\n") + odin_glsl_pmx_frag;
		Pmx::Lists &lists = m_pmx->lists;

		m_pmx->programs[2].take(m_programs.link("pmx-list", vertex, list, ""), false, true);
		made();
		if (gl::geometry_shaders()) {
			const std::string fragment = "#define NATIVE 1\n" + list;

			const GLuint native = m_programs.link("pmx-native-list", vertex, fragment, odin_glsl_pmx_geom);

			m_pmx->programs[3].take(native, true, true);
			made();
		}
		lists.sort = m_programs.link("pmx-sort", odin_glsl_whole_picture_vert, odin_glsl_pmx_sort_frag, "");
		glUseProgram(lists.sort);
		glUniform1i(gl::uniform(lists.sort, "u_below"), 0);
		glUseProgram(0);
		made();
		glGenBuffers(1, &lists.nodes);
		glGenBuffers(1, &lists.count);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, lists.count);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
		glGenBuffers(2, lists.readback);
		for (const GLuint buffer : lists.readback) {
			glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
			glBufferData(GL_COPY_WRITE_BUFFER, sizeof(GLuint), nullptr, GL_STREAM_READ);
		}
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		GLint64 most = 0;

		glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &most);
		lists.most = static_cast<GLsizeiptr>(most / Pmx::Lists::NODE_SIZE);
	}
	glGenVertexArrays(1, &m_pmx->vao);
	glGenBuffers(1, &m_pmx->vbo);
	glBindVertexArray(m_pmx->vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_pmx->vbo);
	for (GLuint i = 0; i < 4; i++)
		glEnableVertexAttribArray(i);
	const auto at = [](size_t offset) { return reinterpret_cast<const void *>(offset); };
	const GLsizei size = sizeof(PmxVertex);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, size, at(offsetof(PmxVertex, x)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, size, at(offsetof(PmxVertex, u)));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, size, at(offsetof(PmxVertex, color)));
	glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, size, at(offsetof(PmxVertex, offset)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glGenTextures(1, &m_pmx->fog);
	glBindTexture(GL_TEXTURE_2D, m_pmx->fog);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG8, 128, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
}

/* One program does every blend: the keys need none of their own */
void
GlDrawing::prepare_pmx(const std::vector<uint64_t> &, const std::function<void(unsigned, unsigned)> &progress)
{
	if (!m_pmx)
		make_pmx(progress);
}

void
GlDrawing::draw_pmx(const PmxScene &scene, unsigned anisotropy)
{
	static constexpr std::array<GLenum, 8> SRC = { GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
		                                       GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
	static constexpr std::array<GLenum, 8> DST = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
		                                       GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };

	const bool fine_depth = float_depth();
	/* The chip's depth compare codes (PG, the chip's programming guide, p. 69), 4 "greater". Without the
	 * float depth, polygons the chip's 1/W tells apart tie, and flat ones
	 * fail against the clear: "greater or equal" there (HEURISTIC) */
	const GLenum greater = fine_depth ? GLenum{ GL_GREATER } : GLenum{ GL_GEQUAL };
	const std::array<GLenum, 8> compare = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
		                                greater, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };

	if (!m_pmx)
		make_pmx(nullptr);

	Pmx &pmx = *m_pmx;

	for (const uint32_t id : scene.gone_images)
		if (const auto found = pmx.images.find(id); found != pmx.images.end()) {
			glDeleteTextures(1, &found->second);
			pmx.images.erase(found);
		}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	for (const std::shared_ptr<const PmxImage> &image : scene.new_images) {
		GLuint texture;

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(image->levels.size()), GL_RGBA8,
		               static_cast<GLsizei>(image->width), static_cast<GLsizei>(image->height));
		for (size_t level = 0; level < image->levels.size(); level++)
			glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
			                std::max(static_cast<GLsizei>(image->width >> level), 1),
			                std::max(static_cast<GLsizei>(image->height >> level), 1), GL_RGBA, GL_UNSIGNED_BYTE,
			                image->levels[level].data());
		pmx.images[image->id] = texture;
	}
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, pmx.fog);
	glBindSampler(1, 0);
	if (!pmx.fog_loaded || pmx.fog_table != scene.fog_table) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 1, GL_RG, GL_UNSIGNED_BYTE, scene.fog_table.data());
		pmx.fog_table = scene.fog_table;
		pmx.fog_loaded = true;
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	const GLsizei w = static_cast<GLsizei>(width()), h = static_cast<GLsizei>(height());
	Pmx::Lists &lists = pmx.lists;
	const bool sorting = lists.sort && scene.vertices.size() / 3 < Pmx::Lists::RANKS &&
	                     std::any_of(scene.draws.begin(), scene.draws.end(),
	                                 [](const PmxDraw &draw) { return draw.per_pixel; });

	if (sorting) {
		if (lists.w != width() || lists.h != height()) {
			glDeleteFramebuffers(1, &lists.heads_fb);
			glDeleteTextures(1, &lists.heads);
			glDeleteTextures(1, &lists.below);
			lists.heads = gl::new_texture(GL_R32UI, width(), height());
			lists.heads_fb = gl::new_framebuffer(lists.heads);
			if (!gl::texture_barrier())
				lists.below = gl::new_texture(GL_RGBA8, width(), height());
			lists.w = width();
			lists.h = height();
		}
		/* The count of two frames before, when the card is done with it */
		GLsync &fence = lists.counted[lists.turn];
		/* At first 8 fragments a pixel of the board's picture (the smoke of
		 * Smashing Drive needs 5.2), or one a pixel drawn; more when a frame
		 * needs them */
		GLsizeiptr wanted = lists.room ? lists.room
		                               : std::max<GLsizeiptr>(GLsizeiptr{ 8 } * SCREEN_WIDTH * SCREEN_HEIGHT,
		                                                      static_cast<GLsizeiptr>(w) * h);

		if (fence && glClientWaitSync(fence, 0, 0) != GL_TIMEOUT_EXPIRED) {
			glBindBuffer(GL_COPY_READ_BUFFER, lists.readback[lists.turn]);
			if (const auto *count = static_cast<const GLuint *>(
				    glMapBufferRange(GL_COPY_READ_BUFFER, 0, sizeof(GLuint), GL_MAP_READ_BIT))) {
				if (static_cast<GLsizeiptr>(*count) + *count / 4 > lists.room)
					wanted = static_cast<GLsizeiptr>(*count) + *count / 2;
				glUnmapBuffer(GL_COPY_READ_BUFFER);
			}
			glBindBuffer(GL_COPY_READ_BUFFER, 0);
			glDeleteSync(fence);
			fence = nullptr;
		}
		wanted = std::min(wanted, lists.most);
		if (wanted > lists.room) {
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, lists.nodes);
			glBufferData(GL_SHADER_STORAGE_BUFFER, wanted * Pmx::Lists::NODE_SIZE, nullptr,
			             GL_DYNAMIC_COPY);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			lists.room = wanted;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	if (fine_depth) {
		if (pmx.fine_w != width() || pmx.fine_h != height()) {
			glDeleteFramebuffers(1, &pmx.fine_fb);
			glDeleteTextures(1, &pmx.fine_depth);
			pmx.fine_depth = gl::new_texture(GL_DEPTH_COMPONENT32F, width(), height());
			pmx.fine_fb = gl::new_framebuffer(m_scene_color);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, pmx.fine_depth, 0);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				gl::fatal("the framebuffer of the Neon 250 is not complete");
			pmx.fine_w = width();
			pmx.fine_h = height();
		}
		glBindFramebuffer(GL_FRAMEBUFFER, pmx.fine_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_scene_color, 0);
		gl::set_depth_from_0(true);
	}
	glViewport(0, 0, w, h);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepthf(0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearDepthf(1.0f);
	/* Alpha reads back as all ones after the clear */
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	glEnable(GL_DEPTH_TEST);

	const Pmx::Program *program = nullptr;
	GLint target = 0;

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
	/* The translucent draws sorted pixel by pixel: their fragments to the
	 * lists, then each pixel blends its list; the count runs on through
	 * the frame */
	bool collecting = false, counted = false;
	const auto begin_lists = [&] {
		static const GLuint zero[4] = { 0, 0, 0, 0 };

		glBindFramebuffer(GL_FRAMEBUFFER, lists.heads_fb);
		glClearBufferuiv(GL_COLOR, 0, zero);
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(target));
		if (!counted) {
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, lists.count);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), zero);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lists.nodes);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lists.count);
		glBindImageTexture(0, lists.heads, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		collecting = counted = true;
	};
	const auto end_lists = [&] {
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, 0);
		/* The colour below read where it is drawn, else from a copy */
		if (gl::texture_barrier()) {
			glBindTexture(GL_TEXTURE_2D, m_scene_color);
			gl::read_drawn();
		} else {
			glBindTexture(GL_TEXTURE_2D, lists.below);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
		}
		glUseProgram(lists.sort);
		program = nullptr;
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindTexture(GL_TEXTURE_2D, 0);
		glEnable(GL_DEPTH_TEST);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		collecting = false;
	};

	glBindVertexArray(pmx.vao);
	glBindBuffer(GL_ARRAY_BUFFER, pmx.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(scene.vertices.size() * sizeof(PmxVertex)),
	             scene.vertices.data(), GL_STREAM_DRAW);
	glActiveTexture(GL_TEXTURE0);

	for (size_t k = 0; k < scene.draws.size(); k++) {
		const PmxDraw &draw = scene.draws[k];
		const bool listed = sorting && draw.per_pixel;
		const int flags = (draw.image ? 1 : 0) | (draw.use_alpha ? 2 : 0) | (draw.ignore_texture_alpha ? 4 : 0) |
		                  (draw.offset ? 8 : 0) | draw.shading << 4 | draw.fog << 6;

		if (listed && !collecting)
			begin_lists();
		glDepthFunc(compare[draw.compare & 7]);
		glDepthMask(draw.write_z ? GL_TRUE : GL_FALSE);
		if (draw.blend && !listed) {
			glEnable(GL_BLEND);
			glBlendFunc(SRC[draw.src & 7], DST[draw.dst & 7]);
		} else {
			glDisable(GL_BLEND);
		}
		if (const Pmx::Program *const wanted = &pmx.programs[(draw.native ? 1 : 0) + (listed ? 2 : 0)];
		    wanted != program) {
			const auto rgb = [](uint32_t color) {
				return std::array<float, 3>{ static_cast<float>((color >> 16) & 0xFF) / 255.0f,
					                     static_cast<float>((color >> 8) & 0xFF) / 255.0f,
					                     static_cast<float>(color & 0xFF) / 255.0f };
			};

			program = wanted;
			glUseProgram(program->id);
			glUniform1f(program->most_w, scene.most_w);
			glUniform1f(program->scale, static_cast<float>(m_scale));
			glUniform1f(program->fog_density, scene.fog_density);
			glUniform3fv(program->fog_color, 1, rgb(scene.fog_color).data());
			glUniform3fv(program->fog_vertex_color, 1, rgb(scene.fog_vertex_color).data());
		}
		glUniform1i(program->flags, flags);
		glUniform1f(program->lod_bias, draw.lod_bias);
		if (listed) {
			glUniform1ui(program->order, draw.first / 3);
			glUniform1ui(program->mode, (draw.src & 7u) | (draw.dst & 7u) << 3);
		}
		if (draw.image) {
			const auto found = pmx.images.find(draw.image);

			glBindTexture(GL_TEXTURE_2D, found != pmx.images.end() ? found->second : 0);
			glBindSampler(0, pmx.sampler(draw, anisotropy));
		}
		glDrawArrays(GL_TRIANGLES, static_cast<GLint>(draw.first), static_cast<GLsizei>(draw.count));
		if (collecting && (k + 1 == scene.draws.size() || !scene.draws[k + 1].per_pixel))
			end_lists();
	}
	if (counted) {
		GLsync &fence = lists.counted[lists.turn];

		glBindBuffer(GL_COPY_READ_BUFFER, lists.count);
		glBindBuffer(GL_COPY_WRITE_BUFFER, lists.readback[lists.turn]);
		glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(GLuint));
		glBindBuffer(GL_COPY_READ_BUFFER, 0);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
		if (fence)
			glDeleteSync(fence);
		fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		lists.turn ^= 1;
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
	}
	if (fine_depth)
		gl::set_depth_from_0(false);

	glBindSampler(0, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_frame_fb);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("a scene of the Neon 250");
}

} /* namespace odin */
