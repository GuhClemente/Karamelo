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

#include "odin/host/gfx/gl_common.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef ODIN_GLES
#include <SDL3/SDL_video.h>
#endif

namespace odin::gl {

namespace {

#ifdef ODIN_GLES
/* 31 or 32 */
int
es_version()
{
	static const int version = [] {
		GLint major = 0, minor = 0;

		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);
		return major * 10 + minor;
	}();
	return version;
}

/* GLSL ES 3.10 needs extensions for what Odin uses of 3.20 */
std::string
es_head()
{
	std::string head = es_version() >= 32 ? "#version 320 es\n" : "#version 310 es\n";

	if (es_version() < 32) {
		for (const char *ext : { "GL_EXT_geometry_shader", "GL_OES_shader_image_atomic", "GL_EXT_gpu_shader5" })
			if (has_extension(ext))
				head += std::string("#extension ") + ext + " : enable\n";
		if (!has_extension("GL_EXT_gpu_shader5"))
			head += "#define precise\n";
	}
	head += "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
	        "precision highp usampler2D;\nprecision highp image2D;\nprecision highp uimage2D;\n";
	if (!fragment_lists())
		head += "#define PEELING 1\n";
	return head;
}
#endif

/* Prepended to the GLSL 4.30 shaders */
GLuint
compile_shader(GLenum type, const char *text)
{
	const GLuint id = glCreateShader(type);
#ifdef ODIN_GLES
	static const std::string head = es_head();
	const char *const texts[2] = { head.c_str(), text };
#else
	const char *const texts[2] = { fragment_lists() ? "#version 430\n" : "#version 410\n#define PEELING 1\n", text };
#endif
	GLint status;

	glShaderSource(id, 2, texts, nullptr);
	glCompileShader(id);
	glGetShaderiv(id, GL_COMPILE_STATUS, &status);
	if (!status) {
		char info[1024];

		glGetShaderInfoLog(id, sizeof(info), nullptr, info);
		std::fprintf(stderr, "renderer: could not compile a shader: '%s'\n%s\n", info, text);
		std::abort();
	}
	return id;
}

} /* namespace */

GLuint
compile_program(const char *vertex, const char *fragment, bool retrievable, const char *geometry)
{
	const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
	const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
	const GLuint gs = geometry ? compile_shader(GL_GEOMETRY_SHADER, geometry) : 0;
	const GLuint id = glCreateProgram();
	GLint status;

	glAttachShader(id, vs);
	glAttachShader(id, fs);
	if (gs)
		glAttachShader(id, gs);
	if (retrievable)
		glProgramParameteri(id, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	glLinkProgram(id);
	glGetProgramiv(id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		char info[256];

		glGetProgramInfoLog(id, sizeof(info), nullptr, info);
		std::fprintf(stderr, "renderer: could not link a program: '%s'\n%s\n%s\n",
		             info, vertex, fragment);
		std::abort();
	}

	/* The program keeps its shaders until it goes. */
	glDeleteShader(vs);
	glDeleteShader(fs);
	if (gs)
		glDeleteShader(gs);
	check("a program");
	return id;
}

GLint
uniform(GLuint program, const char *name)
{
	const GLint location = glGetUniformLocation(program, name);

	if (location < 0) {
		std::fprintf(stderr, "renderer: the uniform '%s' has no location\n", name);
		std::abort();
	}
	return location;
}

void
check(const char *stage)
{
	const GLenum error = glGetError();
	const char *what;

	if (error == GL_NO_ERROR)
		return;
	switch (error) {
	case GL_INVALID_ENUM:			what = "invalid enum"; break;
	case GL_INVALID_VALUE:			what = "invalid value"; break;
	case GL_INVALID_OPERATION:		what = "invalid operation"; break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:	what = "invalid framebuffer operation"; break;
	case GL_OUT_OF_MEMORY:			what = "out of memory"; break;
	default:				what = "unknown error"; break;
	}
	std::fprintf(stderr, "renderer: OpenGL error (%s) after %s\n", what, stage);
	std::abort();
}

void
fatal(const char *what)
{
	std::fprintf(stderr, "renderer: %s\n", what);
	std::abort();
}

bool
has_extension(const char *name)
{
	GLint count = 0;

	glGetIntegerv(GL_NUM_EXTENSIONS, &count);
	for (GLint i = 0; i < count; i++) {
		const char *const ext = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));

		if (ext && std::strcmp(ext, name) == 0)
			return true;
	}
	return false;
}

#ifdef ODIN_GLES
bool
fragment_lists()
{
	static const bool lists = [] {
		GLint buffers = 0;

		glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &buffers);
		return buffers > 0 && primitive_ids() && (es_version() >= 32 || has_extension("GL_OES_shader_image_atomic"));
	}();
	return lists;
}

bool
geometry_shaders()
{
	static const bool has = es_version() >= 32 || has_extension("GL_EXT_geometry_shader");
	return has;
}

bool
primitive_ids()
{
	return geometry_shaders();
}

bool
copy_image()
{
	return es_version() >= 32;
}

/* No persistent maps: they need GL_EXT_buffer_storage */
bool
buffer_storage()
{
	return false;
}

bool
anisotropic_filter()
{
	static const bool has = has_extension("GL_EXT_texture_filter_anisotropic");
	return has;
}

bool
border_clamp()
{
	static const bool has = es_version() >= 32 || has_extension("GL_EXT_texture_border_clamp") ||
	                        has_extension("GL_OES_texture_border_clamp");
	return has;
}

/* glDisablei */
bool
blend_per_buffer()
{
	return es_version() >= 32;
}

bool
texture_barrier()
{
	return false;
}

void
read_drawn()
{
}

/* GL_EXT_clip_control */
static PFNGLCLIPCONTROLEXTPROC
clip_control()
{
	static const PFNGLCLIPCONTROLEXTPROC clip =
		has_extension("GL_EXT_clip_control")
			? reinterpret_cast<PFNGLCLIPCONTROLEXTPROC>(SDL_GL_GetProcAddress("glClipControlEXT"))
			: nullptr;
	return clip;
}

bool
depth_from_0()
{
	return clip_control() != nullptr;
}

void
set_depth_from_0(bool on)
{
	if (const PFNGLCLIPCONTROLEXTPROC clip = clip_control())
		clip(GL_LOWER_LEFT_EXT, on ? GL_ZERO_TO_ONE_EXT : GL_NEGATIVE_ONE_TO_ONE_EXT);
}

const char *
es_version_head()
{
	static const std::string head = std::string(es_version() >= 32 ? "#version 320 es\n" : "#version 310 es\n") +
	                                "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
	return head.c_str();
}
#else
bool
fragment_lists()
{
	return GLEW_VERSION_4_3;
}

bool
geometry_shaders()
{
	return true;
}

bool
primitive_ids()
{
	return true;
}

bool
copy_image()
{
	return GLEW_VERSION_4_3 || GLEW_ARB_copy_image;
}

bool
buffer_storage()
{
	return GLEW_ARB_buffer_storage;
}

bool
blend_per_buffer()
{
	return true;
}

bool
texture_barrier()
{
	return GLEW_VERSION_4_5 || GLEW_ARB_texture_barrier || GLEW_NV_texture_barrier;
}

void
read_drawn()
{
	if (GLEW_VERSION_4_5 || GLEW_ARB_texture_barrier)
		glTextureBarrier();
	else
		glTextureBarrierNV();
}

bool
depth_from_0()
{
	return GLEW_ARB_clip_control;
}

void
set_depth_from_0(bool on)
{
	glClipControl(GL_LOWER_LEFT, on ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
}

bool
anisotropic_filter()
{
	return GLEW_EXT_texture_filter_anisotropic;
}

bool
border_clamp()
{
	return true;
}
#endif

void
storage(GLsizei levels, GLenum format, GLsizei w, GLsizei h)
{
#ifdef ODIN_GLES
	const bool immutable = true;
#else
	const bool immutable = GLEW_VERSION_4_2 || GLEW_ARB_texture_storage;
#endif

	if (immutable) {
		glTexStorage2D(GL_TEXTURE_2D, levels, format, w, h);
		return;
	}
	const bool depth = format == GL_DEPTH_COMPONENT24 || format == GL_DEPTH_COMPONENT32F;
	const GLenum layout = depth ? GL_DEPTH_COMPONENT : (format == GL_R32UI) ? GL_RED_INTEGER : GL_RGBA;
	const GLenum type = depth ? GL_FLOAT : (format == GL_R32UI) ? GL_UNSIGNED_INT : GL_UNSIGNED_BYTE;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
	for (GLint level = 0; level < levels; level++) {
		glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(format), w, h, 0, layout, type, nullptr);
		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);
	}
}

void
read_buffer(GLenum target, GLintptr offset, GLsizeiptr size, void *out)
{
#ifdef ODIN_GLES
	const void *const at = glMapBufferRange(target, offset, size, GL_MAP_READ_BIT);

	if (!at)
		fatal("a buffer could not be mapped to be read");
	std::memcpy(out, at, static_cast<size_t>(size));
	glUnmapBuffer(target);
#else
	glGetBufferSubData(target, offset, size, out);
#endif
}

void
invalidate(GLenum target, GLenum attachment)
{
#ifndef ODIN_GLES
	if (!GLEW_VERSION_4_3 && !GLEW_ARB_invalidate_subdata)
		return;
#endif
	glInvalidateFramebuffer(target, 1, &attachment);
}

GLuint
new_texture(GLenum format, unsigned w, unsigned h)
{
	GLuint id;

	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	storage(1, format, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	return id;
}

GLuint
new_framebuffer(GLuint color)
{
	static const GLenum targets[1] = { GL_COLOR_ATTACHMENT0 };
	GLuint id;

	glGenFramebuffers(1, &id);
	glBindFramebuffer(GL_FRAMEBUFFER, id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	glDrawBuffers(1, targets);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		fatal("a framebuffer is not complete");
	return id;
}

} /* namespace odin::gl */
