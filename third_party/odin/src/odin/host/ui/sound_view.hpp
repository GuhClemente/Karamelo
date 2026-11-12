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


#ifndef ODIN_SOUND_VIEW_HPP
#define ODIN_SOUND_VIEW_HPP

#include "imgui/imgui.h"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/ui/sound_parts.hpp"

namespace odin {

/* The generic cabinet seen from behind the player, on the right and from
 * above, in silhouette, in a..b: each part at its place, a part heard lit,
 * a part silent dotted, a part pointed at in the accent. The part under
 * the mouse, or -1. */
int draw_sound_cabinet(const MenuKit &kit, ImVec2 a, ImVec2 b, const SoundScene &scene);
/* The speakers of the user, two in a row or four in a square, from `at`:
 * lit where a part is heard; the one under the mouse (YourSpeaker, 0:
 * none). Its size first. */
ImVec2 your_speakers_size(const MenuKit &kit, const SoundScene &scene);
unsigned draw_your_speakers(const MenuKit &kit, ImVec2 at, const SoundScene &scene);
} /* namespace odin */

#endif /* ODIN_SOUND_VIEW_HPP */
