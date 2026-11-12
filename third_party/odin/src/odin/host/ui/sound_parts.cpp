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


/* Which of the user's speakers each part of a cabinet's sound reaches, as
 * the page Audio draws and says it */

#include "odin/host/ui/sound_parts.hpp"

#include <array>

namespace odin {

namespace {

using Kind = SoundPart::Kind;
using Place = SoundPart::Place;

/* "a", "a and b", "a, b and c" */
std::string
listed(const std::vector<std::string> &names)
{
	std::string out;

	for (size_t i = 0; i < names.size(); i++)
		out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
	return out;
}

} /* namespace */

unsigned
sound_feeds(SoundFamily family, int played, const SoundPart &part)
{
	const bool left = part.at == Place::FRONT_LEFT || part.at == Place::SEAT_LEFT;
	const bool right = part.at == Place::FRONT_RIGHT || part.at == Place::SEAT_RIGHT;
	const auto pair = [left, right](unsigned l, unsigned r) { return left ? l : right ? r : l | r; };
	const bool quad = played == 2 && family != SoundFamily::ONE;
	/* Left and right: the main board, or the two last channels of a DAC */
	const bool front = family == SoundFamily::WOOFER ? part.channel >= 2 : !part.option;

	if (family == SoundFamily::ONE)
		return pair(YOUR_LEFT, YOUR_RIGHT);
	if (front)
		return played < 0 ? 0 : quad ? pair(YOUR_FRONT_LEFT, YOUR_FRONT_RIGHT) : pair(YOUR_LEFT, YOUR_RIGHT);
	if (played == 0)
		return 0;
	if (family == SoundFamily::WOOFER && part.channel == 1)
		return quad ? unsigned{ YOUR_REAR_LEFT } : unsigned{ YOUR_LEFT | YOUR_RIGHT };
	if (family == SoundFamily::WOOFER)
		return quad ? unsigned{ YOUR_REAR_RIGHT } : 0u;
	return quad ? pair(YOUR_REAR_LEFT, YOUR_REAR_RIGHT) : pair(YOUR_LEFT, YOUR_RIGHT);
}

unsigned
your_speakers(SoundFamily family, int played)
{
	return played == 2 && family != SoundFamily::ONE ?
	       YOUR_FRONT_LEFT | YOUR_FRONT_RIGHT | YOUR_REAR_LEFT | YOUR_REAR_RIGHT : YOUR_LEFT | YOUR_RIGHT;
}

std::string
sound_part_name(const SoundPart &part)
{
	std::string name;

	switch (part.kind) {
	case Kind::SPEAKER:
		name = part.at == Place::FRONT_LEFT ? "Left screen speaker" :
		       part.at == Place::FRONT_RIGHT ? "Right screen speaker" :
		       part.at == Place::SEAT_LEFT ? "Left seat speaker" : part.at == Place::SEAT_RIGHT ? "Right seat speaker" :
		       "Speaker";
		break;
	case Kind::WOOFER:
		name = "Woofer";
		break;
	case Kind::SHAKER:
		name = part.at == Place::SEAT ? "Seat shaker" : "Panel shaker";
		break;
	case Kind::UNWIRED:
		return "Fourth channel (no speaker on the cabinet)";
	}
	return name;
}

std::string
sound_heard(const SoundScene &scene)
{
	/* The parts in groups, in the order of the page: what each is called
	 * and where it is heard */
	struct Group {
		std::string name;
		unsigned feeds = 0;
		bool present = false;
	};
	std::array<Group, 6> groups = { { { "the screen speakers" }, { scene.lows ? "the woofer's lows" : "the woofer" },
	                                  { "the panel shaker's signal" }, { "the seat speakers" },
	                                  { "the seat shaker's signal" }, { "the fourth channel" } } };

	for (const SoundPart &part : scene.parts) {
		const size_t g = part.kind == Kind::UNWIRED ? 5 : part.kind == Kind::WOOFER ? 1 :
		                 part.kind == Kind::SHAKER ? (part.at == Place::SEAT ? 4 : 2) :
		                 part.at == Place::SEAT_LEFT || part.at == Place::SEAT_RIGHT ? 3 : 0;

		groups[g].present = true;
		groups[g].feeds |= sound_feeds(scene.family, scene.played, part);
	}

	std::vector<std::string> front, rear, silent;
	const bool four = your_speakers(scene.family, scene.played) & YOUR_REAR_LEFT;

	for (const Group &group : groups) {
		if (!group.present)
			continue;
		if (!group.feeds)
			silent.push_back(group.name);
		else if (four && (group.feeds & (YOUR_REAR_LEFT | YOUR_REAR_RIGHT)))
			rear.push_back(group.name);
		else
			front.push_back(group.name);
	}

	std::string out;

	if (four)
		out = "Front pair: " + (front.empty() ? std::string("nothing") : listed(front)) + ". Rear pair: " +
		      (rear.empty() ? std::string("nothing") : listed(rear)) + ".";
	else
		out = front.empty() ? "You hear nothing." : "You hear " + listed(front) + ".";
	if (!silent.empty())
		out += " Not heard: " + listed(silent) + ".";
	return out;
}

} /* namespace odin */
