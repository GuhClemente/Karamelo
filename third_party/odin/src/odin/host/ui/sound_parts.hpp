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


#ifndef ODIN_SOUND_PARTS_HPP
#define ODIN_SOUND_PARTS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "odin/host/app/games.hpp"

namespace odin {

/* How the boards of a game reach the speakers of the user: one board in
 * stereo; a main board and an optional one (the Hikaru); the four channels
 * of a DAC with a woofer (the Gaelco boards) */
enum class SoundFamily : uint8_t { ONE, TWO_BOARDS, WOOFER };

/* The speakers of the user: the two of stereo, or the four of Quad */
enum YourSpeaker : unsigned {
	YOUR_LEFT = 1, YOUR_RIGHT = 2, YOUR_FRONT_LEFT = 4, YOUR_FRONT_RIGHT = 8, YOUR_REAR_LEFT = 16, YOUR_REAR_RIGHT = 32,
};

/* The speakers of the user a part is heard in, the game played that way
 * (Menu::PLAYED: 0 Main, 1 Both, 2 Quad; -1 the optional board alone) */
unsigned sound_feeds(SoundFamily family, int played, const SoundPart &part);
/* The speakers of the user, played that way: two, or four in Quad */
unsigned your_speakers(SoundFamily family, int played);

/* A game in the drawing: the parts of the cabinet shown, how it is
 * played, what is pointed at (a part, or the user's speakers) */
struct SoundScene {
	std::vector<SoundPart> parts;
	bool seated = false;
	SoundFamily family = SoundFamily::ONE;
	int played = 0;
	/* The woofer plays only its lows in the mix */
	bool lows = false;
	int pointed = -1;
	unsigned pointed_speakers = 0;
};

/* A part as the page names it */
std::string sound_part_name(const SoundPart &part);
/* What the user hears, played that way, in a sentence */
std::string sound_heard(const SoundScene &scene);

} /* namespace odin */

#endif /* ODIN_SOUND_PARTS_HPP */
