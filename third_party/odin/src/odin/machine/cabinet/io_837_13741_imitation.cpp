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

#include "odin/machine/cabinet/io_837_13741_imitation.hpp"

#include <algorithm>
#include <array>

namespace odin {

/* Block 44BC */
const char *Io837_13741Imitation::device_id() const
{
	return "SEGA ENTERPRISES,LTD.;837-13741  I/O CONTROL BD2;Ver0.15;99/06";
}

/* Block 45AC, the list of the DIP switches all off */
int32_t Io837_13741Imitation::function_list(std::span<uint8_t> out)
{
	static constexpr std::array<uint8_t, 16> list = {
		jvs::FUNC_SWITCH_INPUT, PLAYERS, 12, 0,
		jvs::FUNC_COIN_INPUT, COIN_SLOTS, 0, 0,
		jvs::FUNC_ANALOG_INPUT, ANALOG_CHANNELS, 0, 0,	/* bits not told */
		jvs::FUNC_GENERAL_OUTPUT, 22, 0, 0
	};

	std::copy(list.begin(), list.end(), out.begin());
	return static_cast<int32_t>(list.size());
}

/* 17CB: no player and no byte gives the system byte alone; 1 or 2 players
 * take 1 or 2 bytes each (1834, 18FD). The system byte has TEST and TILT
 * (bits 7 and 6, 0C2C), a player 8 switches then 4 in the high half of
 * the second byte (0C2C, 0CB3). */
int32_t Io837_13741Imitation::switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player)
{
	if (players > PLAYERS || (players == 0) != (bytes_per_player == 0) || bytes_per_player > 2)
		return -1;

	uint32_t pos = 0;

	out[pos++] = m_inputs.system() & 0xC0;
	for (uint8_t i = 0; i < players; i++) {
		const uint16_t value = m_inputs.player(i);

		out[pos++] = static_cast<uint8_t>(value >> 8);
		if (bytes_per_player == 2)
			out[pos++] = static_cast<uint8_t>(value & 0xF0);
	}
	return static_cast<int32_t>(pos);
}

/* The time to a reply, in clocks of its Z80, measured with epr-21868 run on
 * the Z80 of Odin, the request ending anywhere in a tick of its timer (the
 * median of 32 phases; their spread is some 20 clocks). A frame: the checksum
 * of the request (0523, 26 a byte, the address and the length with it), that
 * of the reply (073B, 34 a byte, the address and the length with it, the sum
 * not), the rest; a command its own part, by its parameters. A request ends
 * when the stop bit of its last byte does, a reply starts with the start bit
 * of its first. */
uint32_t Io837_13741Imitation::frame_cycles(size_t request_bytes, size_t reply_bytes) const
{
	return static_cast<uint32_t>(941 + 26 * (request_bytes + 2) + 34 * (reply_bytes + 2));
}

/* 0708: status 3, the commands not read */
uint32_t Io837_13741Imitation::sum_error_cycles(size_t request_bytes) const
{
	return static_cast<uint32_t>(969 + 26 * (request_bytes + 2) + 34 * 3);
}

uint32_t Io837_13741Imitation::command_cycles(std::span<const uint8_t> command, Outcome outcome) const
{
	static constexpr std::array<uint16_t, 3> SWITCHES[3] = {
		{ 716, 677, 677 }, { 892, 965, 1041 }, { 909, 1040, 1175 }
	};
	static constexpr std::array<uint16_t, 10> ANALOGS = { 661, 775, 901, 1026, 1153, 1279, 1406, 1532, 1643, 677 };
	static constexpr std::array<uint16_t, 4> OUTPUTS = { 747, 809, 873, 937 };
	const uint8_t a = command.size() > 1 ? command[1] : 0;
	const uint8_t b = command.size() > 2 ? command[2] : 0;

	if (outcome == Outcome::UNKNOWN)
		return 483;
	switch (command[0]) {
	case jvs::SET_ADDRESS:
		return 559;
	case jvs::IO_IDENTIFY:
		return 1730;
	case jvs::COMMAND_REVISION:
		return 442;
	case jvs::JVS_REVISION:
		return 458;
	case jvs::COMM_VERSION:
		return 478;
	case jvs::FEATURE_CHECK:
		return 938;
	case jvs::SWITCH_INPUTS:
		/* 3 players or more, or 3 bytes or more */
		if (a > 2)
			return 697;
		return b > 2 ? (a ? SWITCHES[a][0] : SWITCHES[0][1]) : SWITCHES[a][b];
	case jvs::COIN_INPUTS:
		return a == 1 ? 923 : a == 2 ? 1159 : a == 0 ? 644 : 662;
	case jvs::ANALOG_INPUTS:
		return ANALOGS[a < 9 ? a : 9];
	case jvs::GENERIC_OUTPUT_1:
		/* Refused for 4 bytes or more, then its data read as commands */
		return a < 4 ? OUTPUTS[a] : 764;
	case jvs::COIN_COUNTER_DECREASE:
		if (a == 0)
			return 780;
		return outcome == Outcome::DONE ? (a == 1 ? 853 : 858) : (a == 1 ? 862 : 881);
	case jvs::COIN_COUNTER_INCREASE:
		if (a == 0)
			return 815;
		return outcome == Outcome::DONE ? (a == 1 ? 914 : 922) : (a == 1 ? 923 : 939);
	default:
		return 483;
	}
}

/* 1DC9: 1 to 3 bytes, kept in 8910-8912 */
bool Io837_13741Imitation::swoutputs(std::span<const uint8_t> values)
{
	if (values.empty() || values.size() > OUTPUT_BYTES)
		return false;
	for (size_t i = 0; i < values.size(); i++)
		m_outputs[i] = values[i];
	return true;
}

} /* namespace odin */
