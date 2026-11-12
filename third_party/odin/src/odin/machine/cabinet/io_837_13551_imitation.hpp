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

#ifndef ODIN_IO_837_13551_IMITATION_HPP
#define ODIN_IO_837_13551_IMITATION_HPP

#include "odin/machine/cabinet/jvs.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* The 837-13551, the I/O board of most Hikaru games. Its TMP90PH44 runs
 * the program 315-6215, which is not run here: the node answers as that
 * program does, 2 players x 13 switches, 2 coin slots, 8 analog channels
 * of 8 bits, 6 driver outputs. */
class Io837_13551Imitation : public JvsIoBoard {
public:
	static constexpr uint8_t PLAYERS = 2;

	explicit Io837_13551Imitation(JvsInputs &inputs) : JvsIoBoard(inputs) {}

protected:
	const char *device_id() const override;
	/* What the 837-13551 answers (MAME jvs13551.cpp:68-78); the MIE
	 * program copies them into the node record (C101-C103). */
	uint8_t command_format_version() const override { return 0x11; }
	uint8_t jvs_standard_version() const override { return 0x20; }
	uint8_t comm_method_version() const override { return 0x10; }
	int32_t function_list(std::span<uint8_t> out) override;
	int32_t switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player) override;
	bool swoutputs(std::span<const uint8_t> values) override;
	/* 315-6215 at 122F: past the count, the data of a refused 32 */
	bool refused_outputs_skip_data() const override { return true; }
	/* 02F5, 0400: F0 D9 at the head of a frame to every node */
	bool reset_in_any_frame() const override { return false; }
};

} /* namespace odin */

#endif /* ODIN_IO_837_13551_IMITATION_HPP */
