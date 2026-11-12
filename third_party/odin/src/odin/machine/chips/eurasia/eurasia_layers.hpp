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

/* The 2D layers as the display chip of the Hikaru (315-6087, "Eurasia")
 * is set to show them. Plain data, read by the renderer. */

#ifndef ODIN_EURASIA_LAYERS_HPP
#define ODIN_EURASIA_LAYERS_HPP

#include <cstdint>

/* A rectangle of the framebuffer RAM shown as a layer */
typedef struct {
	uint32_t x0, y0, x1, y1;
	uint32_t format;	/* ODIN_FORMAT_ABGR1555 or ODIN_FORMAT_A2BGR10 */
	uint32_t enabled;
	uint32_t keyed;		/* unit register 3C: pixels without the alpha bit are transparent */
	uint32_t above;		/* unit register 38: drawn above the 3D, below it otherwise */
} odin_layer_t;

typedef struct {
	odin_layer_t layer[2][2];	/* by unit, then layer 1 and layer 2 */
	uint32_t enabled;
	uint32_t fb_keyed;		/* unit 0 register 3C bit 0 */
} odin_layers_t;

#endif /* ODIN_EURASIA_LAYERS_HPP */
