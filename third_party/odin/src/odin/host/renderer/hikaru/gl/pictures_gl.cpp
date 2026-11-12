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

/* The pictures of the board drawing with OpenGL (drawing.hpp) and
 * what goes from one to the next: the buffers the 3D is drawn in, the
 * pictures kept from one frame to the next, the picture of the frame and
 * the window. */

#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

#include "odin/host/gfx/gl_common.hpp"
#include "odin/host/gfx/glsl_texts.hpp"

namespace odin {

GlDrawing::GlDrawing()
{
	m_peel.on = !gl::fragment_lists();
	build_compose();
	build_presenter();
	build_pictures();
	build_blender();
	build_quad();

	glGenFramebuffers(2, m_copy_fb);
	m_state_picture = gl::new_texture(GL_RGBA8, SCREEN_WIDTH, SCREEN_HEIGHT);

	build_list_pictures();
	build_lists();

	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("building the board drawing");
}

GlDrawing::~GlDrawing()
{
	glBindVertexArray(0);
	if (m_mesh_vao) {
		glDeleteVertexArrays(1, &m_mesh_vao);
		glDeleteBuffers(1, &m_mesh_vbo);
		glDeleteBuffers(1, &m_mesh_ibo);
	}
	destroy_list_pictures();
	destroy_lists();

	glDeleteVertexArrays(1, &m_quad_vao);
	glDeleteBuffers(1, &m_quad_vbo);

	destroy_pictures(true);
	glDeleteFramebuffers(2, m_copy_fb);
	glDeleteTextures(1, &m_state_picture);
	glDeleteTextures(2, m_layer_tex);

	glUseProgram(0);
	glDeleteProgram(m_blender);
	glDeleteProgram(m_compose);
	glDeleteProgram(m_presenter);
	if (!m_stamps.empty())
		glDeleteQueries(static_cast<GLsizei>(m_stamps.size()), m_stamps.data());
}

void
GlDrawing::build_pictures()
{
	const unsigned w = width(), h = height();

	/* Where the lists are drawn */
	m_scene_color = gl::new_texture(GL_RGBA8, w, h);
	m_scene_fb = gl::new_framebuffer(m_scene_color);
	glGenRenderbuffers(1, &m_scene_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_scene_depth);
#ifdef ODIN_GLES
	/* Sized format, matching m_solid_depth for the blit */
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
#else
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
	                      static_cast<GLsizei>(w), static_cast<GLsizei>(h));
#endif
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	                          GL_RENDERBUFFER, m_scene_depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		gl::fatal("the framebuffer of the lists is not complete");

	/* Where the two families of the 101 are drawn (begin_families) */
	{
		static const GLenum targets[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

		glGenRenderbuffers(1, &m_flag_depth);
		glBindRenderbuffer(GL_RENDERBUFFER, m_flag_depth);
#ifdef ODIN_GLES
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
#else
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
#endif
		glGenFramebuffers(2, m_family_fb);
		for (unsigned f = 0; f < 2; f++) {
			m_eye[f] = gl::new_texture(GL_RG32F, w, h);
			glBindFramebuffer(GL_FRAMEBUFFER, m_family_fb[f]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_scene_color, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_eye[f], 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
			                          f == 0 ? m_scene_depth : m_flag_depth);
			glDrawBuffers(2, targets);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				gl::fatal("a framebuffer of the families is not complete");
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	/* The two 3D buffers and the pictures of the last three lists,
	 * empty: an empty 3D buffer lets everything through, its alpha is
	 * 1. */
	{
		std::vector<uint8_t> empty(size_t{w} * h * 4, 0);

		for (size_t i = 3; i < empty.size(); i += 4)
			empty[i] = 0xFF;
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		for (unsigned i = 0; i < KEPT; i++) {
			kept(i) = gl::new_texture(GL_RGBA8, w, h);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			                static_cast<GLsizei>(w), static_cast<GLsizei>(h),
			                GL_RGBA, GL_UNSIGNED_BYTE, empty.data());
		}
	}
	/* The blender of the 3D buffers writes to the pictures. */
	for (unsigned i = 0; i < PIPE; i++)
		m_pipe_fb[i] = gl::new_framebuffer(m_pipe[i]);

	/* The picture of the frame */
	m_frame_color = gl::new_texture(GL_RGBA8, w, h);
	m_frame_fb = gl::new_framebuffer(m_frame_color);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("the buffers");
}

void
GlDrawing::destroy_pictures(bool kept_too)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &m_scene_fb);
	glDeleteTextures(1, &m_scene_color);
	glDeleteRenderbuffers(1, &m_scene_depth);
	glDeleteFramebuffers(2, m_family_fb);
	glDeleteTextures(2, m_eye);
	glDeleteRenderbuffers(1, &m_flag_depth);
	glDeleteFramebuffers(3, m_pipe_fb);
	glDeleteFramebuffers(1, &m_frame_fb);
	glDeleteTextures(1, &m_frame_color);
	if (kept_too) {
		glDeleteTextures(2, m_buffers);
		glDeleteTextures(3, m_pipe);
	}
}

/* Copies a picture into another of any size: each pixel takes the nearest
 * one. */
void
GlDrawing::copy_picture(GLuint from, unsigned from_w, unsigned from_h,
                        GLuint to, unsigned to_w, unsigned to_h)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_copy_fb[0]);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, from, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_copy_fb[1]);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to, 0);

	/* The scissor would cut the copy. */
	glDisable(GL_SCISSOR_TEST);
	glBlitFramebuffer(0, 0, static_cast<GLint>(from_w), static_cast<GLint>(from_h),
	                  0, 0, static_cast<GLint>(to_w), static_cast<GLint>(to_h),
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("copying a picture");
}

void
GlDrawing::set_scale(unsigned scale)
{
	const unsigned old_w = width(), old_h = height();
	GLuint old[KEPT];

	for (unsigned i = 0; i < KEPT; i++)
		old[i] = kept(i);
	destroy_pictures(false);

	destroy_list_pictures();

	m_scale = scale;
	build_pictures();
	build_list_pictures();
	for (unsigned i = 0; i < KEPT; i++)
		copy_picture(old[i], old_w, old_h, kept(i), width(), height());
	glDeleteTextures(KEPT, old);
}

void
GlDrawing::build_blender()
{
	m_blender = gl::compile_program(odin_glsl_whole_picture_vert, odin_glsl_blend_frag);
	m_blender_rate = gl::uniform(m_blender, "u_rate");
	glUseProgram(m_blender);
	glUniform1i(gl::uniform(m_blender, "u_drawn"), 0);
	glUniform1i(gl::uniform(m_blender, "u_kept"), 1);
	glUseProgram(0);
}

/* The four corners of the window, for the composition. The shaders that
 * draw one triangle over the whole picture take no vertex: they are drawn
 * with this array bound as well. */
void
GlDrawing::build_quad()
{
	struct Corner {
		float position[3];
		float texcoords[2];
	};
	static const Corner corners[4] = {
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },
		{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
	};

	glGenVertexArrays(1, &m_quad_vao);
	glBindVertexArray(m_quad_vao);
	glGenBuffers(1, &m_quad_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Corner),
	                      reinterpret_cast<const void *>(offsetof(Corner, position)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Corner),
	                      reinterpret_cast<const void *>(offsetof(Corner, texcoords)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);
}

void
GlDrawing::build_compose()
{
	m_compose = gl::compile_program(odin_glsl_compose_vert, odin_glsl_compose_frag);

	m_compose_locs.front = gl::uniform(m_compose, "u_front");
	m_compose_locs.back = gl::uniform(m_compose, "u_back");
	m_compose_locs.layer1 = gl::uniform(m_compose, "u_layer1");
	m_compose_locs.layer2 = gl::uniform(m_compose, "u_layer2");
	m_compose_locs.factor = gl::uniform(m_compose, "u_factor");
	m_compose_locs.mult_f = gl::uniform(m_compose, "u_mult_f");
	m_compose_locs.mult_b = gl::uniform(m_compose, "u_mult_b");
	m_compose_locs.mult_1 = gl::uniform(m_compose, "u_mult_1");
	m_compose_locs.mult_2 = gl::uniform(m_compose, "u_mult_2");
	m_compose_locs.keyed_1 = gl::uniform(m_compose, "u_keyed_1");
	m_compose_locs.keyed_2 = gl::uniform(m_compose, "u_keyed_2");
	m_compose_locs.keyed_3d = gl::uniform(m_compose, "u_keyed_3d");
	m_compose_locs.above_1 = gl::uniform(m_compose, "u_above_1");
	m_compose_locs.above_2 = gl::uniform(m_compose, "u_above_2");
	m_compose_locs.invert_f = gl::uniform(m_compose, "u_invert_f");
	m_compose_locs.invert_b = gl::uniform(m_compose, "u_invert_b");
	m_compose_locs.clear = gl::uniform(m_compose, "u_clear");
}

void
GlDrawing::build_presenter()
{
	m_presenter = gl::compile_program(odin_glsl_whole_picture_vert, odin_glsl_present_frag);
	m_presenter_rect = gl::uniform(m_presenter, "u_rect");
	glUseProgram(m_presenter);
	glUniform1i(gl::uniform(m_presenter, "u_picture"), 0);
	glUseProgram(0);
}

/* What goes from one picture to another: no depth, no blending, no
 * scissor, nothing culled, all of the picture. */
void
GlDrawing::set_2d_state()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
}

void
GlDrawing::scene_to_pipe(unsigned slot)
{
	set_2d_state();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
	gl::invalidate(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT);
	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, m_pipe[slot]);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
	                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("a list");
}

/* The blender output is what the 3D buffer of the list keeps: the other one
 * then holds every earlier frame, fading by the rate at each frame. This is
 * the motion blur of the cabinets (the stars of the PHARRIER logo scene
 * trail behind them at 181 = 01C6). */
void
GlDrawing::blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate)
{
	set_2d_state();
	/* begin_scene clears the depth; the blender draws every pixel. */
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	gl::invalidate(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT);
	glBindFramebuffer(GL_FRAMEBUFFER, m_pipe_fb[slot]);
	gl::invalidate(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	glUseProgram(m_blender);
	glUniform1i(m_blender_rate, static_cast<GLint>(rate));

	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, m_buffers[buffer]);
	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, m_scene_color);

	glBindVertexArray(m_quad_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);

	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("a list");
}

void
GlDrawing::keep_picture(unsigned slot, unsigned buffer)
{
	if (gl::copy_image()) {
		glCopyImageSubData(m_pipe[slot], GL_TEXTURE_2D, 0, 0, 0, 0,
		                   m_buffers[buffer], GL_TEXTURE_2D, 0, 0, 0, 0,
		                   static_cast<GLsizei>(width()), static_cast<GLsizei>(height()), 1);
	} else {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pipe_fb[slot]);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_buffers[buffer]);
		m_unit0_texture = ~0u;
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
		                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}
	gl::check("keeping a picture");
}

/* The texture of a layer is made the first time, and made again when what
 * it holds changes. */
void
GlDrawing::set_layer(unsigned i, LayerTexels texels, const void *pixels)
{
	const bool same = m_layer_texels[i] == texels;

	glActiveTexture(GL_TEXTURE0 + 0);
	if (m_layer_tex[i] == 0) {
		glGenTextures(1, &m_layer_tex[i]);
		glBindTexture(GL_TEXTURE_2D, m_layer_tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}
	glBindTexture(GL_TEXTURE_2D, m_layer_tex[i]);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	m_layer_texels[i] = texels;

#ifdef ODIN_GLES
	/* Reorder 1555 bits as in drawing_gl.cpp */
	const GLint swizzle_rb = texels == LayerTexels::ABGR1555 ? GL_BLUE : GL_RED;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, swizzle_rb);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, swizzle_rb == GL_BLUE ? GL_RED : GL_BLUE);
	if (texels == LayerTexels::ABGR1555) {
		const uint8_t *const in = static_cast<const uint8_t *>(pixels);
		std::vector<uint16_t> turned(size_t{SCREEN_WIDTH} * SCREEN_HEIGHT);

		for (size_t p = 0; p < turned.size(); p++) {
			const uint16_t v = static_cast<uint16_t>(in[2 * p] | in[2 * p + 1] << 8);

			turned[p] = static_cast<uint16_t>(v << 1 | v >> 15);
		}
		if (same)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
			                GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, turned.data());
		else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
			             GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, turned.data());
	} else if (same) {
#else
	if (texels == LayerTexels::ABGR1555) {
		if (same)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
			                GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, pixels);
		else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
			             GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, pixels);
	} else if (same) {
#endif
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
		                GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, pixels);
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
		             GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, pixels);
	}
	gl::check("a layer");
}

/* The two blender inputs are on the units 0 and 1, the 2D layers on 2 and 3. */
void
GlDrawing::compose(const Compose &c, unsigned shown, const bool layers[2])
{
	const GLuint layer[2] = { layers[0] ? m_layer_tex[0] : 0, layers[1] ? m_layer_tex[1] : 0 };
	const auto texture = [&](Compose::Source source) {
		return source == Compose::Source::PICTURE ? m_pipe[shown] :
		       source == Compose::Source::LAYER_1 ? layer[0] : layer[1];
	};

	set_2d_state();
	glBindFramebuffer(GL_FRAMEBUFFER, m_frame_fb);
	gl::invalidate(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
	glUseProgram(m_compose);

	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, texture(c.front));
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, texture(c.back));
	glActiveTexture(GL_TEXTURE0 + 2);
	glBindTexture(GL_TEXTURE_2D, c.alone[0] ? layer[0] : 0);
	glActiveTexture(GL_TEXTURE0 + 3);
	glBindTexture(GL_TEXTURE_2D, c.alone[1] ? layer[1] : 0);

	glUniform1i(m_compose_locs.front, 0);
	glUniform1i(m_compose_locs.back, 1);
	glUniform1i(m_compose_locs.layer1, 2);
	glUniform1i(m_compose_locs.layer2, 3);
	glUniform1f(m_compose_locs.factor, c.factor);
	/* Uniforms persist in the program: set them all, every frame. */
	glUniform1f(m_compose_locs.mult_f, c.mult_front);
	glUniform1f(m_compose_locs.mult_b, c.mult_back);
	glUniform1f(m_compose_locs.mult_1, c.mult_alone[0]);
	glUniform1f(m_compose_locs.mult_2, c.mult_alone[1]);
	glUniform1f(m_compose_locs.keyed_1, c.keyed[0] ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.keyed_2, c.keyed[1] ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.keyed_3d, c.keyed_3d ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.above_1, c.above[0] ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.above_2, c.above[1] ? 1.0f : 0.0f);
	glUniform1i(m_compose_locs.invert_f, c.invert_front ? 1 : 0);
	glUniform1i(m_compose_locs.invert_b, c.invert_back ? 1 : 0);
	glUniform4f(m_compose_locs.clear, c.clear[0], c.clear[1], c.clear[2], c.clear[3]);

	glBindVertexArray(m_quad_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void
GlDrawing::present(PictureRect rect, unsigned window_w, unsigned window_h)
{
	show_picture(rect, window_w, window_h);
	gl::check("a frame");
}

void
GlDrawing::show_picture(PictureRect rect, unsigned window_w, unsigned window_h)
{
	const unsigned w = rect.w, h = rect.h;

	if (w == 0 || h == 0 || rect.x + w > window_w || rect.y + h > window_h)
		return;
	/* OpenGL counts from the bottom */
	const GLint x = static_cast<GLint>(rect.x);
	const GLint y = static_cast<GLint>(window_h - rect.y - h);
	const GLint pw = static_cast<GLint>(width()), ph = static_cast<GLint>(height());

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_target_fbo);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	if (w != window_w || h != window_h) {
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	/* A filter takes the picture at the size of the screen of the
	 * hardware, a finer one brought down to it as below */
	if (m_filter) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_filter->frame(SCREEN_WIDTH, SCREEN_HEIGHT));
		glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		glUseProgram(m_presenter);
		glUniform4f(m_presenter_rect, 0.0f, 0.0f, static_cast<float>(m_scale), static_cast<float>(m_scale));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_frame_color);
		glBindVertexArray(m_quad_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, m_target_fbo);
		m_filter->draw(x, y, w, h, m_filter_frames++);
		return;
	}

	if (w == width() && h == height()) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_frame_fb);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_target_fbo);
		glBlitFramebuffer(0, 0, pw, ph, x, y, x + pw, y + ph,
		                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_target_fbo);
		return;
	}

	/* Where the rectangle starts, and the pixels of the picture one of
	 * its pixels covers each way */
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_target_fbo);
	glViewport(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	glUseProgram(m_presenter);
	glUniform4f(m_presenter_rect, static_cast<float>(x), static_cast<float>(y),
	            static_cast<float>(pw) / static_cast<float>(w),
	            static_cast<float>(ph) / static_cast<float>(h));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_frame_color);
	glBindVertexArray(m_quad_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

void
GlDrawing::set_filter(const std::string &preset)
{
	if (preset == m_filter_preset)
		return;
	m_filter_preset = preset;
	m_filter = preset.empty() ? nullptr : PictureFilter::load(preset);
	m_filter_frames = 0;
}

void
GlDrawing::draw_loading(std::span<const LoadingRect> rects, unsigned, unsigned window_h)
{
	/* What the drawing of the frames finds is left as it was. */
	const GLboolean scissoring = glIsEnabled(GL_SCISSOR_TEST);
	GLint box[4];
	GLfloat color[4];

	glGetIntegerv(GL_SCISSOR_BOX, box);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);
	for (const LoadingRect &r : rects) {
		glScissor(static_cast<GLint>(r.x), static_cast<GLint>(window_h - r.y - r.h), static_cast<GLsizei>(r.w),
		          static_cast<GLsizei>(r.h));
		glClearColor(r.shade, r.shade, r.shade, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (!scissoring)
		glDisable(GL_SCISSOR_TEST);
	glScissor(box[0], box[1], box[2], box[3]);
	glClearColor(color[0], color[1], color[2], color[3]);
	gl::check("the loading screen");
}

void
GlDrawing::read_kept(std::span<uint8_t> pictures)
{
	const size_t size = PICTURE_SIZE;
	std::vector<uint8_t> whole(size_t{width()} * height() * 4);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	for (unsigned i = 0; i < KEPT; i++) {
#ifdef ODIN_GLES
		/* OpenGL ES reads a texture through a framebuffer only */
		GLuint fb;

		glGenFramebuffers(1, &fb);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, kept(i), 0);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()), GL_RGBA, GL_UNSIGNED_BYTE,
		             whole.data());
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fb);
#else
		glBindTexture(GL_TEXTURE_2D, kept(i));
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, whole.data());
#endif
		screen_of(whole.data(), m_scale, &pictures[i * size]);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("saving the pictures");
}

void
GlDrawing::write_kept(std::span<const uint8_t> pictures)
{
	const size_t size = PICTURE_SIZE;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (unsigned i = 0; i < KEPT; i++) {
		glBindTexture(GL_TEXTURE_2D, m_state_picture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
		                GL_RGBA, GL_UNSIGNED_BYTE, &pictures[i * size]);
		copy_picture(m_state_picture, SCREEN_WIDTH, SCREEN_HEIGHT, kept(i), width(), height());
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("loading the pictures");
}

} /* namespace odin */
