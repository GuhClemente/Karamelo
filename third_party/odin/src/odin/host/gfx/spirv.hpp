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

#ifndef ODIN_SPIRV_HPP
#define ODIN_SPIRV_HPP

/* The shaders of Vulkan as SPIR-V, in the binary (spirv.S) */

#include "odin/host/gfx/vk_common.hpp"

#define ODIN_SPIRV(name) \
	extern "C" const uint32_t odin_spirv_##name[], odin_spirv_##name##_end[];
ODIN_SPIRV(whole_picture_vert)
ODIN_SPIRV(blend_frag)
ODIN_SPIRV(compose_vert)
ODIN_SPIRV(compose_frag)
ODIN_SPIRV(present_frag)
ODIN_SPIRV(mesh_vert)
ODIN_SPIRV(mesh_frag)
ODIN_SPIRV(mesh_collect_frag)
ODIN_SPIRV(mesh_families_frag)
ODIN_SPIRV(mesh_geom)
ODIN_SPIRV(sort_frag)
ODIN_SPIRV(pmx_vert)
ODIN_SPIRV(pmx_frag)
ODIN_SPIRV(pmx_geom)
ODIN_SPIRV(pmx_native_frag)
ODIN_SPIRV(pmx_list_frag)
ODIN_SPIRV(pmx_native_list_frag)
ODIN_SPIRV(pmx_sort_frag)
ODIN_SPIRV(image_vert)
ODIN_SPIRV(image_frag)
ODIN_SPIRV(whole_window_vert)
ODIN_SPIRV(blur_frag)
ODIN_SPIRV(show_frag)
ODIN_SPIRV(average_frag)
ODIN_SPIRV(sides_frag)
#undef ODIN_SPIRV

/* A shader of spirv.S by its name */
#define ODIN_SPIRV_OF(name) \
	(odin::vk::Spirv{ odin_spirv_##name, \
	                  static_cast<size_t>(odin_spirv_##name##_end - odin_spirv_##name) * sizeof(uint32_t) })

#endif /* ODIN_SPIRV_HPP */
