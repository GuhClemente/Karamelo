// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * Sega 837-13551 JVS I/O board, from MAME src/mame/sega/jvs13551,
 * rewritten for Odin. Odin is GPL-3.0; this file keeps its BSD-3-Clause
 * licence and copyright holder.
 *
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Odin changes: Copyright (C) 2026 Réda Chérif-Touil
 */

#include "odin/machine/cabinet/io_837_13551_imitation.hpp"

#include <algorithm>

namespace odin {

/* Identification string reported by the 315-6215 firmware of the Type 1
 * board (MAME jvs13551.cpp ROM list). */
const char *Io837_13551Imitation::device_id() const
{
	return "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10";
}

int32_t Io837_13551Imitation::function_list(std::span<uint8_t> out)
{
	static constexpr std::array<uint8_t, 16> list = {
		jvs::FUNC_SWITCH_INPUT, PLAYERS, 13, 0,	/* 13 switches per player */
		jvs::FUNC_COIN_INPUT, COIN_SLOTS, 0, 0,
		jvs::FUNC_ANALOG_INPUT, ANALOG_CHANNELS, 0, 0,	/* bits not told (block at 3079) */
		jvs::FUNC_GENERAL_OUTPUT, 6, 0, 0		/* 6 driver outputs */
	};
	std::copy(list.begin(), list.end(), out.begin());
	return static_cast<int32_t>(list.size());
}

int32_t Io837_13551Imitation::switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player)
{
	if (players > PLAYERS || bytes_per_player > 2) {
		return -1;
	}
	uint32_t pos = 0;
	out[pos++] = m_inputs.system();
	for (uint8_t i = 0; i < players; i++) {
		const uint16_t value = m_inputs.player(i);
		for (uint8_t j = 0; j < bytes_per_player; j++) {
			out[pos++] = static_cast<uint8_t>(value >> ((1 - j) * 8));
		}
	}
	return static_cast<int32_t>(pos);
}

/* JVS counts output bits from the most significant end: six outputs
 * live in bits 7..2 of the first byte. */
bool Io837_13551Imitation::swoutputs(std::span<const uint8_t> values)
{
	if (values.size() != 1) {
		return false;
	}
	m_outputs[0] = values[0] & 0xFC;
	return true;
}

} /* namespace odin */
