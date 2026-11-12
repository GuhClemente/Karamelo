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

/* The graphics card of the host with OpenGL (host_graphics.hpp). Each step
 * sets the state it draws with, its viewport too: none counts on what
 * another one left. */

#include "odin/host/gfx/host_graphics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "odin/host/renderer/hikaru/drawing.hpp"
#include "odin/host/gfx/gl_common.hpp"
#include "odin/host/gfx/glsl_texts.hpp"

namespace odin {

namespace {

class GlGraphics : public HostGraphics {
public:
	GlGraphics(SDL_Window *window, SDL_GLContext context, std::string name)
		: m_window(window), m_context(context), m_name(std::move(name))
	{
	}
	~GlGraphics() override;

	const std::string &name() const override { return m_name; }
	bool vulkan() const override { return false; }
#ifdef ODIN_GLES
	const char *api() const override { return "OpenGL ES"; }
#else
	const char *api() const override { return "OpenGL"; }
#endif
	void take_thread() override { SDL_GL_MakeCurrent(m_window, m_context); }
	void leave_thread() override { SDL_GL_MakeCurrent(m_window, nullptr); }

	void clear_window(unsigned window_w, unsigned window_h) override;
	void show_window() override { SDL_GL_SwapWindow(m_window); }
	bool set_waits(bool wait) override { return SDL_GL_SetSwapInterval(wait ? 1 : 0); }
	void read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels) override;

	HostImage make_image(const SDL_Surface &rgba, bool smooth) override;
	void forget_image(HostImage image) override;

	void draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h) override;
	void draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h) override;
	void draw_white_border(PictureRect rect, unsigned size, unsigned window_w, unsigned window_h) override;

	void blur_background(HostPicture picture) override;
	void blur_window(unsigned window_w, unsigned window_h) override;
	void draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light) override;
	void start_ui() override;
	void draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what) override;
	void stop_ui() override;
	uint64_t ui_image(HostImage image) const override { return image.id; }
	void ui_blocks(ImDrawList *list, bool on) const override;
	void ui_levels(ImDrawList *list, bool on) const override { ui_blocks(list, on); }

	std::unique_ptr<Drawing> make_drawing() override { return Drawing::make_gl(); }

private:
	/* The program of an image over a viewport, made when first drawn */
	void image_program();
	/* Those of the sides lit by the picture, and the filtering sampler
	 * they read with, made when first drawn */
	void sides_programs();
	/* The two textures of the background, at the size of the half
	 * picture; the background from that framebuffer and buffer, w by h */
	void size_background(unsigned w, unsigned h);
	void blur(GLuint fb, GLenum buffer, unsigned w, unsigned h);

	SDL_Window *m_window;
	SDL_GLContext m_context;
	std::string m_name;
	GLuint m_image_program = 0, m_image_vao = 0;
	GLint m_image_light = 0;
	/* The sides lit by the picture: its mean at two sizes, the sides in a
	 * small image of the window, then that image over the window */
	GLuint m_sides_fb[3] = {}, m_sides_tex[3] = {};
	GLuint m_average = 0, m_sides = 0, m_stretch = 0, m_linear = 0;
	GLint m_average_block = 0, m_sides_rect = 0, m_sides_band = 0, m_sides_light = 0;
	/* The background of the menu: the picture at half its size, blurred
	 * back and forth between two textures, the result in the first */
	bool m_ui = false;
	GLuint m_vao = 0, m_blur = 0, m_show = 0;
	GLint m_blur_step = 0, m_show_light = 0;
	std::array<GLuint, 2> m_blur_tex{}, m_blur_fb{};
	unsigned m_blur_w = 0, m_blur_h = 0;
};

GlGraphics::~GlGraphics()
{
	glDeleteFramebuffers(3, m_sides_fb);
	glDeleteTextures(3, m_sides_tex);
	glDeleteProgram(m_average);
	glDeleteProgram(m_sides);
	glDeleteProgram(m_stretch);
	glDeleteSamplers(1, &m_linear);
	glDeleteProgram(m_image_program);
	glDeleteVertexArrays(1, &m_image_vao);
	glDeleteFramebuffers(2, m_blur_fb.data());
	glDeleteTextures(2, m_blur_tex.data());
	glDeleteProgram(m_blur);
	glDeleteProgram(m_show);
	glDeleteVertexArrays(1, &m_vao);
	SDL_GL_DestroyContext(m_context);
}

void
GlGraphics::clear_window(unsigned window_w, unsigned window_h)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void
GlGraphics::read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, picture.id);
	glReadBuffer(picture.id ? GL_COLOR_ATTACHMENT0 : GL_BACK);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
#ifdef ODIN_GLES
	/* OpenGL ES only guarantees RGBA reads */
	std::vector<uint8_t> rgba(size_t{w} * h * 4);

	glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	for (size_t i = 0; i < size_t{w} * h; i++)
		std::copy_n(&rgba[i * 4], 3, &pixels[i * 3]);
#else
	glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGB, GL_UNSIGNED_BYTE, pixels);
#endif
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

HostImage
GlGraphics::make_image(const SDL_Surface &rgba, bool smooth)
{
	GLuint texture = 0;

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba.pitch / 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.w, rgba.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.pixels);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	return { texture, static_cast<float>(rgba.w), static_cast<float>(rgba.h) };
}

/* Dear ImGui binds a sampler of its own, smooth and of one level, over the
 * filters of the texture: the images in blocks and those made smaller
 * (ui_levels) are read with their own */
void
GlGraphics::ui_blocks(ImDrawList *list, bool on) const
{
	const ImGuiPlatformIO &io = ImGui::GetPlatformIO();

	list->AddCallback(on ? io.DrawCallback_SetSamplerFromTex : io.DrawCallback_ResetRenderState, nullptr);
}

void
GlGraphics::forget_image(HostImage image)
{
	if (image.id)
		glDeleteTextures(1, &image.id);
}

void
GlGraphics::image_program()
{
	if (!m_image_program) {
		m_image_program = gl::compile_program(odin_glsl_image_vert, odin_glsl_image_frag);
		m_image_light = glGetUniformLocation(m_image_program, "light");
		glGenVertexArrays(1, &m_image_vao);
	}
}

void
GlGraphics::sides_programs()
{
	if (!m_sides) {
		m_average = gl::compile_program(odin_glsl_whole_window_vert, odin_glsl_average_frag);
		m_average_block = gl::uniform(m_average, "u_block");
		m_sides = gl::compile_program(odin_glsl_whole_window_vert, odin_glsl_sides_frag);
		m_sides_rect = gl::uniform(m_sides, "u_rect");
		m_sides_band = gl::uniform(m_sides, "u_band");
		m_sides_light = gl::uniform(m_sides, "u_light");
		glUseProgram(m_sides);
		glUniform1i(gl::uniform(m_sides, "u_near"), 0);
		glUniform1i(gl::uniform(m_sides, "u_far"), 1);
		m_stretch = gl::compile_program(odin_glsl_whole_window_vert, odin_glsl_show_frag);
		glUseProgram(m_stretch);
		glUniform1f(gl::uniform(m_stretch, "u_light"), 1.0f);
		glUseProgram(0);
		glGenSamplers(1, &m_linear);
		glSamplerParameteri(m_linear, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(m_linear, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(m_linear, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(m_linear, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
}

void
GlGraphics::draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h)
{
	if (!bezel.id)
		return;
	image_program();

	const BezelArea area = bezel_area(bezel, window_w, window_h);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(area.x, area.below, area.w, area.h);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(m_image_program);
	glUniform1f(m_image_light, 1.0f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, bezel.id);
	glBindVertexArray(m_image_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

/* Each band cleared white, counted from the bottom as OpenGL counts */
void
GlGraphics::draw_white_border(PictureRect rect, unsigned size, unsigned, unsigned window_h)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glEnable(GL_SCISSOR_TEST);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	for (const PictureRect &band : border_bands(rect, size)) {
		if (!band.w || !band.h)
			continue;
		glScissor(static_cast<GLint>(band.x), static_cast<GLint>(window_h - band.y - band.h),
		          static_cast<GLsizei>(band.w), static_cast<GLsizei>(band.h));
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glDisable(GL_SCISSOR_TEST);
}

void
GlGraphics::draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h)
{
	const GLint ww = static_cast<GLint>(window_w), wh = static_cast<GLint>(window_h);
	GLint texture = 0;

	image_program();
	sides_programs();
	if (!m_sides_fb[0]) {
		glGenFramebuffers(3, m_sides_fb);
		glGenTextures(3, m_sides_tex);
		for (unsigned i = 0; i < 3; i++) {
			glBindTexture(GL_TEXTURE_2D, m_sides_tex[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIDES_W[i], SIDES_H[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindFramebuffer(GL_FRAMEBUFFER, m_sides_fb[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sides_tex[i], 0);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/* The mean of the picture (the texture of its framebuffer), then of
	 * that */
	glBindFramebuffer(GL_FRAMEBUFFER, picture.id);
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
	                                      &texture);
	const GLuint from[2] = { static_cast<GLuint>(texture), m_sides_tex[0] };
	const GLint blocks[2][2] = { { static_cast<GLint>(picture.w) / SIDES_W[0], static_cast<GLint>(picture.h) / SIDES_H[0] },
		                     { SIDES_W[0] / SIDES_W[1], SIDES_H[0] / SIDES_H[1] } };

	glDisable(GL_SCISSOR_TEST);
	glUseProgram(m_average);
	glBindVertexArray(m_image_vao);
	glActiveTexture(GL_TEXTURE0);
	glBindSampler(0, m_linear);
	for (unsigned i = 0; i < 2; i++) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_sides_fb[i]);
		glViewport(0, 0, SIDES_W[i], SIDES_H[i]);
		glBindTexture(GL_TEXTURE_2D, from[i]);
		glUniform2i(m_average_block, blocks[i][0], blocks[i][1]);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	/* The sides in the small image of the window */
	const GLint px = static_cast<GLint>(rect.x), py = wh - static_cast<GLint>(rect.y + rect.h);
	const float sx = static_cast<float>(SIDES_W[2]) / static_cast<float>(ww);
	const float sy = static_cast<float>(SIDES_H[2]) / static_cast<float>(wh);

	glBindFramebuffer(GL_FRAMEBUFFER, m_sides_fb[2]);
	glViewport(0, 0, SIDES_W[2], SIDES_H[2]);
	glUseProgram(m_sides);
	glUniform4f(m_sides_rect, static_cast<float>(px) * sx, static_cast<float>(py) * sy, static_cast<float>(rect.w) * sx,
	            static_cast<float>(rect.h) * sy);
	glUniform2f(m_sides_band, static_cast<float>(px) * sx, static_cast<float>(py) * sy);
	glUniform2f(m_sides_light, SIDES_NEAR, SIDES_FAR);
	glBindTexture(GL_TEXTURE_2D, m_sides_tex[0]);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_sides_tex[1]);
	glBindSampler(1, m_linear);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindSampler(1, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	/* That image over the window, only around the picture */
	const GLint pr = px + static_cast<GLint>(rect.w), pt = py + static_cast<GLint>(rect.h);
	const GLint sides[4][4] = { { 0, 0, px, wh }, { pr, 0, ww - pr, wh }, { px, 0, pr - px, py }, { px, pt, pr - px, wh - pt } };

	glViewport(0, 0, ww, wh);
	glUseProgram(m_stretch);
	glBindTexture(GL_TEXTURE_2D, m_sides_tex[2]);
	glEnable(GL_SCISSOR_TEST);
	for (const auto &side : sides) {
		if (side[2] <= 0 || side[3] <= 0)
			continue;
		glScissor(side[0], side[1], side[2], side[3]);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	glDisable(GL_SCISSOR_TEST);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindSampler(0, 0);
	glBindVertexArray(0);
	glUseProgram(0);
}

void
GlGraphics::size_background(unsigned w, unsigned h)
{
	if (w == m_blur_w && h == m_blur_h)
		return;
	glDeleteFramebuffers(2, m_blur_fb.data());
	glDeleteTextures(2, m_blur_tex.data());
	glGenTextures(2, m_blur_tex.data());
	glGenFramebuffers(2, m_blur_fb.data());
	for (unsigned i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, m_blur_tex[i]);
		gl::storage(1, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindFramebuffer(GL_FRAMEBUFFER, m_blur_fb[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blur_tex[i], 0);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	m_blur_w = w;
	m_blur_h = h;
}

void
GlGraphics::blur(GLuint fb, GLenum buffer, unsigned w, unsigned h)
{
	const unsigned hw = w / 2, hh = h / 2;
	const GLint iw = static_cast<GLint>(hw), ih = static_cast<GLint>(hh);

	size_background(hw, hh);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	/* Half the size: each texel the mean of four pixels */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
	glReadBuffer(buffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blur_fb[0]);
	glBlitFramebuffer(0, 0, static_cast<GLint>(w), static_cast<GLint>(h), 0, 0, iw, ih, GL_COLOR_BUFFER_BIT,
	                  GL_LINEAR);

	/* Across into the second texture, down back into the first */
	const auto blur = [this](unsigned from, float x, float y) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_blur_fb[1 - from]);
		glBindTexture(GL_TEXTURE_2D, m_blur_tex[from]);
		glUniform2f(m_blur_step, x, y);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	};
	glViewport(0, 0, iw, ih);
	glUseProgram(m_blur);
	glBindVertexArray(m_vao);
	glActiveTexture(GL_TEXTURE0);
	for (unsigned round = 0; round < BLUR_ROUNDS; round++) {
		blur(0, BLUR_STEP / static_cast<float>(hw), 0.0f);
		blur(1, 0.0f, BLUR_STEP / static_cast<float>(hh));
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void
GlGraphics::blur_background(HostPicture picture)
{
	blur(picture.id, GL_COLOR_ATTACHMENT0, picture.w, picture.h);
}

void
GlGraphics::blur_window(unsigned window_w, unsigned window_h)
{
	blur(0, GL_BACK, window_w, window_h);
}

void
GlGraphics::draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light)
{
	const GLsizei gw = static_cast<GLsizei>(w), gh = static_cast<GLsizei>(h);

	glViewport((static_cast<GLint>(window_w) - gw) / 2, (static_cast<GLint>(window_h) - gh) / 2, gw, gh);
	glUseProgram(m_show);
	glUniform1f(m_show_light, light);
	glBindVertexArray(m_vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_blur_tex[0]);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
}

void
GlGraphics::start_ui()
{
#ifdef ODIN_GLES
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init(gl::fragment_lists() ? "#version 430 core" : "#version 410 core");
#endif

	glGenVertexArrays(1, &m_vao);
	m_blur = gl::compile_program(odin_glsl_whole_window_vert, odin_glsl_blur_frag);
	m_blur_step = gl::uniform(m_blur, "u_step");
	m_show = gl::compile_program(odin_glsl_whole_window_vert, odin_glsl_show_frag);
	m_show_light = gl::uniform(m_show, "u_light");
	m_ui = true;
}

void
GlGraphics::draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplOpenGL3_RenderDrawData(data);
	gl::check(what);
}

void
GlGraphics::stop_ui()
{
	if (m_ui)
		ImGui_ImplOpenGL3_Shutdown();
	m_ui = false;
}

} /* namespace */

void
HostGraphics::prepare_gl()
{
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
#ifdef ODIN_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
}

uint64_t
HostGraphics::window_flags_gl()
{
	return SDL_WINDOW_OPENGL;
}

/* Try 4.3 then 4.1, or ES 3.2 then 3.1 */
std::unique_ptr<HostGraphics>
HostGraphics::make_gl(SDL_Window *window, unsigned window_w, unsigned window_h)
{
	int num_ms_buffers, num_samples;
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		context = SDL_GL_CreateContext(window);
	}
	if (!context) {
		std::fprintf(stderr, "ERROR: could not create GL context: '%s'\n", SDL_GetError());
		return nullptr;
	}

	/* The frames are paced by the board alone until set_vsync. */
	SDL_GL_SetSwapInterval(0);

#ifndef ODIN_GLES
	/* Under Wayland the context is EGL's: GLEW has the functions of OpenGL
	 * then, only GLX is missing. */
	glewExperimental = GL_TRUE;
	if (const GLenum glew = glewInit(); glew != GLEW_OK && glew != GLEW_ERROR_NO_GLX_DISPLAY) {
		std::fprintf(stderr, "ERROR: could not initialize glew (%u)\n", glew);
		return nullptr;
	}

	glDisable(GL_MULTISAMPLE);
#endif

	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &num_ms_buffers);
	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &num_samples);

	std::printf("renderer: GL vendor    = %s\n", glGetString(GL_VENDOR));
	std::printf("renderer: GL renderer  = %s\n", glGetString(GL_RENDERER));
	std::printf("renderer: GL version   = %s\n", glGetString(GL_VERSION));
	std::printf("renderer: GLSL version = %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	std::printf("renderer: %d samples on %d ms buffers\n", num_samples, num_ms_buffers);

	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));

	/* What SDL and GLEW left: harmless, and in the way of the errors
	 * looked for later. */
	while (glGetError() != GL_NO_ERROR)
		;
	return std::make_unique<GlGraphics>(window, context, reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
}

} /* namespace odin */
