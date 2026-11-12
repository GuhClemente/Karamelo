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

#ifndef ODIN_GLSL_TEXTS_HPP
#define ODIN_GLSL_TEXTS_HPP

/* The texts of the shaders, each a string, in the binary as their files of
 * glsl/ are (glsl_texts.S). OpenGL is given each after its #version line
 * (gl::compile_program), a mesh after the defines of its variant too. */

extern "C" {

/* The board drawing: a mesh of the display list (and the geometry shader
 * of Native 2D); one triangle over the whole picture and, for it, the
 * translucent fragments of every pixel put in order and blended, the two 3D
 * buffers blended into one, the picture of the frame at the size of the
 * window; the picture shown and the 2D layers, composed */
extern const char odin_glsl_mesh_vert[], odin_glsl_mesh_frag[], odin_glsl_mesh_geom[];
extern const char odin_glsl_whole_picture_vert[];
extern const char odin_glsl_sort_frag[], odin_glsl_blend_frag[], odin_glsl_present_frag[];
extern const char odin_glsl_peel_blend_frag[], odin_glsl_peel_show_frag[];
extern const char odin_glsl_compose_vert[], odin_glsl_compose_frag[];

/* The window: an image over a viewport (the bezel); one triangle over the
 * whole viewport and, for it, a blur and a picture at a light (the
 * background of the menu), the mean of a picture and the sides it lights */
extern const char odin_glsl_image_vert[], odin_glsl_image_frag[];
extern const char odin_glsl_pmx_vert[], odin_glsl_pmx_frag[], odin_glsl_pmx_geom[], odin_glsl_pmx_sort_frag[];
extern const char odin_glsl_whole_window_vert[], odin_glsl_blur_frag[], odin_glsl_show_frag[];
extern const char odin_glsl_average_frag[], odin_glsl_sides_frag[];

}

#endif /* ODIN_GLSL_TEXTS_HPP */
