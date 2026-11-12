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

/* The shaders of Vulkan, compiled from glsl/ to SPIR-V when Odin is built
 * (CMakeLists.txt: glslangValidator), in the binary as they come out:
 * odin_spirv_<name> starts one, odin_spirv_<name>_end follows it
 * (spirv.hpp). */

#include "../embedded.inc"

	READ_ONLY

#define SPIRV(name, file) \
	.global C_NAME(odin_spirv_##name) NEXT .global C_NAME(odin_spirv_##name##_end) NEXT .balign 16 NEXT \
	C_NAME(odin_spirv_##name): .incbin file NEXT C_NAME(odin_spirv_##name##_end):

/* The board drawing */
SPIRV(whole_picture_vert, "glsl/whole_picture.vert.spv")
SPIRV(blend_frag, "glsl/blend.frag.spv")
SPIRV(compose_vert, "glsl/compose.vert.spv")
SPIRV(compose_frag, "glsl/compose.frag.spv")
SPIRV(present_frag, "glsl/present.frag.spv")
SPIRV(mesh_vert, "glsl/mesh.vert.spv")
SPIRV(mesh_frag, "glsl/mesh.frag.spv")
SPIRV(mesh_collect_frag, "glsl/mesh_collect.frag.spv")
SPIRV(mesh_families_frag, "glsl/mesh_families.frag.spv")
SPIRV(mesh_geom, "glsl/mesh.geom.spv")
SPIRV(sort_frag, "glsl/sort.frag.spv")

/* The window */
SPIRV(pmx_vert, "glsl/pmx.vert.spv")
SPIRV(pmx_frag, "glsl/pmx.frag.spv")
SPIRV(pmx_geom, "glsl/pmx.geom.spv")
SPIRV(pmx_native_frag, "glsl/pmx_native.frag.spv")
SPIRV(pmx_list_frag, "glsl/pmx_list.frag.spv")
SPIRV(pmx_native_list_frag, "glsl/pmx_native_list.frag.spv")
SPIRV(pmx_sort_frag, "glsl/pmx_sort.frag.spv")
SPIRV(image_vert, "glsl/image.vert.spv")
SPIRV(image_frag, "glsl/image.frag.spv")
SPIRV(whole_window_vert, "glsl/whole_window.vert.spv")
SPIRV(blur_frag, "glsl/blur.frag.spv")
SPIRV(show_frag, "glsl/show.frag.spv")
SPIRV(average_frag, "glsl/average.frag.spv")
SPIRV(sides_frag, "glsl/sides.frag.spv")

	STACK_NOTE
