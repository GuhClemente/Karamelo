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

/* The pictures the page Display of the menu shows, real captures of Odin
 * (pictures/), in the binary as their PNG files are: odin_picture_<name>
 * starts one, odin_picture_<name>_end follows it (menu.cpp). */

#include "../embedded.inc"

	READ_ONLY

#define PICTURE(name, file) \
	.global C_NAME(odin_picture_##name) NEXT .global C_NAME(odin_picture_##name##_end) NEXT .balign 16 NEXT \
	C_NAME(odin_picture_##name): .incbin file NEXT C_NAME(odin_picture_##name##_end):

PICTURE(upscale_1, "pictures/upscale-1.png")
PICTURE(upscale_2, "pictures/upscale-2.png")
PICTURE(upscale_3, "pictures/upscale-3.png")
PICTURE(upscale_4, "pictures/upscale-4.png")
PICTURE(upscale_5, "pictures/upscale-5.png")
PICTURE(upscale_6, "pictures/upscale-6.png")
PICTURE(filter_1, "pictures/filter-1.png")
PICTURE(filter_2, "pictures/filter-2.png")
PICTURE(filter_4, "pictures/filter-4.png")
PICTURE(filter_8, "pictures/filter-8.png")
PICTURE(filter_16, "pictures/filter-16.png")
PICTURE(screen, "pictures/screen.png")
PICTURE(whole, "pictures/whole.png")
PICTURE(torn, "pictures/torn.png")
PICTURE(mode_cabinet, "pictures/mode-cabinet.png")
PICTURE(mode_raw, "pictures/mode-raw.png")
PICTURE(mode_snap, "pictures/mode-snap.png")
PICTURE(mode_native, "pictures/mode-native.png")
PICTURE(crt_none, "pictures/crt-none.png")
PICTURE(crt_sega_monitor_29, "pictures/crt-sega-monitor-29.png")
PICTURE(crt_sega_projector_50, "pictures/crt-sega-projector-50.png")
PICTURE(crt_gaelco_monitor_28, "pictures/crt-gaelco-monitor-28.png")
PICTURE(crt_gaelco_monitor_34, "pictures/crt-gaelco-monitor-34.png")

	STACK_NOTE
