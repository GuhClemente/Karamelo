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

#ifndef ODIN_GL_COMMON_HPP
#define ODIN_GL_COMMON_HPP

/* What the renderer and the board drawing need around OpenGL: a program
 * from its two texts, a check after a stage, textures and framebuffers to
 * draw in. */

/* OpenGL 4.3 through GLEW, or OpenGL ES 3.1+ (ODIN_GLES) */
#ifdef ODIN_GLES
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#endif

namespace odin::gl {

/* Compiles and links a program of two texts of glsl/ (glsl_texts.hpp),
 * each after its #version line, and a geometry shader between them if
 * given. Aborts with what the driver says if it cannot. `retrievable`: the
 * driver is told glGetProgramBinary will be asked for it. */
GLuint compile_program(const char *vertex, const char *fragment, bool retrievable = false,
                       const char *geometry = nullptr);
/* The location of a uniform the program must have. */
GLint uniform(GLuint program, const char *name);
/* Aborts if OpenGL has an error to tell; `stage` names where. */
void check(const char *stage);
/* Says what went wrong on stderr and aborts. */
[[noreturn]] void fatal(const char *what);

/* Whether the context has an extension */
bool has_extension(const char *name);
bool fragment_lists();
bool primitive_ids();
bool geometry_shaders();
bool copy_image();
bool buffer_storage();
bool anisotropic_filter();
bool border_clamp();
bool blend_per_buffer();
/* Whether a draw may read, as a texture, each pixel it draws once, after
 * read_drawn() (glTextureBarrier) */
bool texture_barrier();
void read_drawn();
/* Whether the depth can go from 0 to 1 (clip control), as a float; that
 * range set, or the one by default */
bool depth_from_0();
void set_depth_from_0(bool on);
#ifdef ODIN_GLES
/* #version and precision lines for GLSL ES */
const char *es_version_head();
#endif
void storage(GLsizei levels, GLenum format, GLsizei w, GLsizei h);
/* Reads back the buffer bound to `target` */
void read_buffer(GLenum target, GLintptr offset, GLsizeiptr size, void *out);
/* The attachment of the framebuffer bound to `target` is not read again
 * before it is drawn over: a tiler neither stores nor loads it. */
void invalidate(GLenum target, GLenum attachment);
/* A texture of one level read pixel for pixel; a framebuffer that draws
 * to a texture. Each is left bound. */
GLuint new_texture(GLenum format, unsigned w, unsigned h);
GLuint new_framebuffer(GLuint color);

} /* namespace odin::gl */

#endif /* ODIN_GL_COMMON_HPP */
