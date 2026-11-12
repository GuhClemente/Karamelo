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

#include "odin/host/video/picture_filter_gl.hpp"

#include <cstdio>
#include <cstring>

namespace odin {

namespace {

GLuint
build_shader(GLenum type, const std::string &desktop, const std::string &name)
{
#ifdef ODIN_GLES
	/* Replace the desktop #version line */
	const size_t line = desktop.rfind("#version", 0) == 0 ? desktop.find('\n') : std::string::npos;
	const std::string text = gl::es_version_head() + (line == std::string::npos ? desktop : desktop.substr(line + 1));
#else
	const std::string &text = desktop;
#endif
	const GLuint shader = glCreateShader(type);
	const char *const source = text.c_str();
	GLint status;

	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1024];

		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		std::fprintf(stderr, "ERROR: filter: %s (%s) does not build: %s\n", name.c_str(),
		             type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

/* The program of a pass; 0, said on stderr, when it does not build */
GLuint
build_program(const std::string &source, const std::string &name)
{
	const GLuint vertex = build_shader(GL_VERTEX_SHADER, odin::preset::part(source, "VERTEX"), name);
	const GLuint fragment = vertex ? build_shader(GL_FRAGMENT_SHADER, odin::preset::part(source, "FRAGMENT"), name) : 0;
	GLuint program = 0;
	GLint status = GL_FALSE;

	if (vertex && fragment) {
		program = glCreateProgram();
		glAttachShader(program, vertex);
		glAttachShader(program, fragment);
		glLinkProgram(program);
		glGetProgramiv(program, GL_LINK_STATUS, &status);
	}
	if (program && !status) {
		char log[1024];

		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		std::fprintf(stderr, "ERROR: filter: %s does not link: %s\n", name.c_str(), log);
		glDeleteProgram(program);
		program = 0;
	}
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	return program;
}

/* Black, its alpha at 1 */
void
clear_texture(GLuint fb)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Each parameter a constant of the source: its value does not change while
 * the filter is used, and the driver then leaves out the work it turns off
 * (the crt pass of Lola Retro on an HD 520: 115 ms a picture with uniforms,
 * 6 ms so). One declared otherwise stays a uniform. */
void
with_constants(std::string &source, const std::vector<std::pair<std::string, float>> &parameters)
{
	for (const auto &[name, value] : parameters) {
		const std::string uniform = "uniform float " + name + ";";
		char literal[32];

		std::snprintf(literal, sizeof(literal), "%.9g", static_cast<double>(value));

		const std::string constant = "const float " + name + " = " + literal +
		                             (std::strpbrk(literal, ".e") ? "" : ".0") + ";";

		for (size_t at = source.find(uniform); at != std::string::npos; at = source.find(uniform, at))
			source.replace(at, uniform.size(), constant);
	}
}

/* A texture of 86Box is RGB: here RGBA, which every driver draws into, its
 * alpha kept at 1 (cleared so, never written: draw) */
void
make_texture(GLuint &texture, GLuint &fb, unsigned w, unsigned h, bool is_float)
{
	if (!texture)
		glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, is_float ? GL_RGBA32F : GL_RGBA8, static_cast<GLsizei>(w),
	             static_cast<GLsizei>(h), 0, GL_RGBA, is_float ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (!fb)
		glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	clear_texture(fb);
}

} /* namespace */

std::unique_ptr<PictureFilter>
PictureFilter::load(const std::string &preset)
{
	const std::optional<preset::Plan> plan = preset::read_plan(preset);

	if (!plan)
		return nullptr;

	std::unique_ptr<PictureFilter> filter(new PictureFilter());

	if (!filter->build(*plan))
		return nullptr;
	std::printf("filter: '%s', %zu passes\n", preset.c_str(), filter->m_passes.size());
	return filter;
}

bool
PictureFilter::build(const preset::Plan &plan)
{
	m_passes.resize(plan.passes.size());
	for (size_t i = 0; i < plan.passes.size(); i++) {
		const preset::Plan::Pass &from = plan.passes[i];
		Pass &pass = m_passes[i];

		pass.out.linear = from.linear;
		pass.out.is_float = from.is_float;
		pass.out.wrap = from.wrap == preset::Plan::Wrap::REPEAT ? GL_REPEAT :
		                from.wrap == preset::Plan::Wrap::MIRRORED ? GL_MIRRORED_REPEAT :
		                from.wrap == preset::Plan::Wrap::EDGE || !gl::border_clamp() ? GL_CLAMP_TO_EDGE :
		                GL_CLAMP_TO_BORDER;
		pass.scale_type = from.scale_type;
		pass.scale = from.scale;
	}
	m_passes.back().window = true;

	for (size_t i = 0; i < m_passes.size(); i++) {
		Pass &pass = m_passes[i];
		std::string source = plan.passes[i].source;

		with_constants(source, plan.parameters);
		pass.program = build_program(source, plan.passes[i].file);
		if (!pass.program)
			return false;
		find_uniforms(pass, i, plan.parameters);
		make_corners(pass);
	}
	return true;
}

/* The uniforms of a pass the frames set: its texture and sizes, those of
 * the source and of each pass before it, the parameters of the preset */
void
PictureFilter::find_uniforms(Pass &pass, size_t i, const std::vector<std::pair<std::string, float>> &parameters)
{
	const auto uniform = [&pass](const std::string &uniform_name) {
		return glGetUniformLocation(pass.program, uniform_name.c_str());
	};

	pass.texture = uniform("Texture");
	pass.input_size = uniform("InputSize");
	pass.texture_size = uniform("TextureSize");
	pass.output_size = uniform("OutputSize");
	pass.frame_count = uniform("FrameCount");
	pass.frame_direction = uniform("FrameDirection");
	pass.mvp = uniform("MVPMatrix");
	pass.orig_texture = uniform("OrigTexture");
	pass.orig_input_size = uniform("OrigInputSize");
	pass.orig_texture_size = uniform("OrigTextureSize");
	for (size_t k = 1; k <= i; k++) {
		const std::string before = "Pass" + std::to_string(k);

		pass.passes.push_back({ uniform(before + "Texture"), uniform(before + "TextureSize"),
		                        uniform(before + "InputSize") });
	}
	for (const auto &[parameter, value] : parameters)
		if (const GLint at = uniform(parameter); at >= 0)
			pass.parameters.emplace_back(at, value);
}

/* Four corners: VertexCoord, then TexCoord, set by size() */
void
PictureFilter::make_corners(Pass &pass)
{
	static constexpr GLfloat QUAD[16] = {};
	const GLint vertex = glGetAttribLocation(pass.program, "VertexCoord");
	const GLint tex = glGetAttribLocation(pass.program, "TexCoord");

	glGenVertexArrays(1, &pass.vao);
	glGenBuffers(1, &pass.vbo);
	glBindVertexArray(pass.vao);
	glBindBuffer(GL_ARRAY_BUFFER, pass.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_DYNAMIC_DRAW);
	if (vertex >= 0) {
		glEnableVertexAttribArray(static_cast<GLuint>(vertex));
		glVertexAttribPointer(static_cast<GLuint>(vertex), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
	}
	if (tex >= 0) {
		glEnableVertexAttribArray(static_cast<GLuint>(tex));
		glVertexAttribPointer(static_cast<GLuint>(tex), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
		                      reinterpret_cast<const void *>(2 * sizeof(GLfloat)));
	}
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

PictureFilter::~PictureFilter()
{
	for (Pass &pass : m_passes) {
		glDeleteProgram(pass.program);
		glDeleteVertexArrays(1, &pass.vao);
		glDeleteBuffers(1, &pass.vbo);
		glDeleteFramebuffers(1, &pass.out.fb);
		glDeleteTextures(1, &pass.out.texture);
	}
	glDeleteFramebuffers(1, &m_frame.fb);
	glDeleteTextures(1, &m_frame.texture);
	for (const auto &[kind, sampler] : m_samplers)
		glDeleteSamplers(1, &sampler);
}

GLuint
PictureFilter::frame(unsigned w, unsigned h)
{
	if (m_frame_size[0] != w || m_frame_size[1] != h) {
		m_frame_size = { w, h };
		m_frame.size = { static_cast<float>(w), static_cast<float>(h) };
		m_frame.texture_size = preset::texture_of(m_frame.size);
		make_texture(m_frame.texture, m_frame.fb, static_cast<unsigned>(m_frame.texture_size[0]),
		             static_cast<unsigned>(m_frame.texture_size[1]), false);
		m_window = {};
	}
	return m_frame.fb;
}

/* The size of each pass for a window w by h, its texture and its corners;
 * the textures black again (a smaller picture leaves nothing of the larger
 * around it) */
void
PictureFilter::size(unsigned w, unsigned h)
{
	const std::array<float, 2> window = { static_cast<float>(w), static_cast<float>(h) };
	const Picture *in = &m_frame;

	m_window = { w, h };
	for (Pass &pass : m_passes) {
		pass.out.size = preset::pass_size(pass.scale_type, pass.scale, window, in->size);
		if (!pass.window) {
			const std::array<float, 2> texture_size = preset::texture_of(pass.out.size);

			if (texture_size != pass.out.texture_size || !pass.out.texture) {
				pass.out.texture_size = texture_size;
				make_texture(pass.out.texture, pass.out.fb, static_cast<unsigned>(texture_size[0]),
				             static_cast<unsigned>(texture_size[1]), pass.out.is_float);
			} else {
				clear_texture(pass.out.fb);
			}
		}

		const std::array<GLfloat, 16> corners = preset::corners(in->size, in->texture_size);

		glBindBuffer(GL_ARRAY_BUFFER, pass.vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(corners), corners.data());
		in = &pass.out;
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

GLuint
PictureFilter::sampler(bool linear, GLint wrap)
{
	GLuint &held = m_samplers[{ linear, wrap }];

	if (!held) {
		static constexpr GLfloat BLACK[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		glGenSamplers(1, &held);
		glSamplerParameteri(held, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(held, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(held, GL_TEXTURE_WRAP_S, wrap);
		glSamplerParameteri(held, GL_TEXTURE_WRAP_T, wrap);
		if (gl::border_clamp())
			glSamplerParameterfv(held, GL_TEXTURE_BORDER_COLOR, BLACK);
	}
	return held;
}

void
PictureFilter::draw(int x, int y, unsigned w, unsigned h, unsigned count)
{
	static constexpr GLfloat IDENTITY[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const Picture *in = &m_frame;
	GLuint units = 0;
	/* What the drawing of the frames left on: back faces culled */
	const GLboolean culling = glIsEnabled(GL_CULL_FACE);

	glDisable(GL_CULL_FACE);
	if (m_window[0] != w || m_window[1] != h)
		size(w, h);
	for (size_t i = 0; i < m_passes.size(); i++) {
		const Pass &pass = m_passes[i];
		/* The last reads its input linear, as 86Box does with its own
		 * filter setting */
		const bool linear = pass.window || in->linear;
		GLuint unit = 0;
		const auto bind = [&](GLint uniform, const Picture &picture, bool picture_linear) {
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, picture.texture);
			glBindSampler(unit, sampler(picture_linear, picture.wrap));
			if (uniform >= 0)
				glUniform1i(uniform, static_cast<GLint>(unit));
			unit++;
		};

		glUseProgram(pass.program);
		bind(pass.texture, *in, linear);
		glUniform2f(pass.input_size, in->size[0], in->size[1]);
		glUniform2f(pass.texture_size, in->texture_size[0], in->texture_size[1]);
		glUniform2f(pass.output_size, static_cast<float>(w), static_cast<float>(h));
		glUniform1i(pass.frame_count, static_cast<GLint>(count));
		glUniform1i(pass.frame_direction, 1);
		glUniformMatrix4fv(pass.mvp, 1, GL_FALSE, IDENTITY);
		for (const auto &[at, value] : pass.parameters)
			glUniform1f(at, value);
		if (i > 0) {
			bind(pass.orig_texture, m_frame, m_frame.linear);
			glUniform2f(pass.orig_input_size, m_frame.size[0], m_frame.size[1]);
			glUniform2f(pass.orig_texture_size, m_frame.texture_size[0], m_frame.texture_size[1]);
			for (size_t k = 0; k < i; k++) {
				const Picture &drawn = m_passes[k].out, &read = k ? m_passes[k - 1].out : m_frame;

				bind(pass.passes[k][0], drawn, &drawn == in ? linear : drawn.linear);
				glUniform2f(pass.passes[k][1], read.texture_size[0], read.texture_size[1]);
				glUniform2f(pass.passes[k][2], read.size[0], read.size[1]);
			}
		}

		if (pass.window) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glViewport(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
		} else {
			/* No pass leaves a pixel of its picture undrawn: no clear
			 * (8192 by 512 floats for the beam of Lola Retro) */
			glBindFramebuffer(GL_FRAMEBUFFER, pass.out.fb);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
			glViewport(0, 0, static_cast<GLsizei>(pass.out.size[0]), static_cast<GLsizei>(pass.out.size[1]));
		}
		glBindVertexArray(pass.vao);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		units = std::max(units, unit);
		in = &pass.out;
	}
	for (GLuint unit = 0; unit < units; unit++) {
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(unit, 0);
	}
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(0);
	glUseProgram(0);
	if (culling)
		glEnable(GL_CULL_FACE);
}

} /* namespace odin */
