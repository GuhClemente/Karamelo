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

#ifndef ODIN_IO_837_13741_IMITATION_HPP
#define ODIN_IO_837_13741_IMITATION_HPP

#include "odin/machine/cabinet/jvs.hpp"

#include <cstdint>
#include <span>

namespace odin {

/* The 837-13741 (837-13844 on later stickers), the I/O board of NASCAR. A
 * 315-6146 runs the program epr-21868, which is not run here: the node
 * answers as that program does with its DIP switches all off, as the
 * manual of NASCAR sets them: 2 players x 12 switches, 2 coin slots, 8
 * analog channels of 8 bits, 22 driver outputs. The addresses quoted are
 * those of epr-21868. */
class Io837_13741Imitation : public JvsIoBoard {
public:
	static constexpr uint8_t PLAYERS = 2;
	static constexpr uint8_t OUTPUT_BYTES = 3;

	explicit Io837_13741Imitation(JvsInputs &inputs) : JvsIoBoard(inputs) {}

	uint32_t sum_error_cycles(size_t request_bytes) const override;

protected:
	const char *device_id() const override;
	/* Blocks 45A3, 45A6, 45A9 */
	uint8_t command_format_version() const override { return 0x11; }
	uint8_t jvs_standard_version() const override { return 0x20; }
	uint8_t comm_method_version() const override { return 0x10; }
	int32_t function_list(std::span<uint8_t> out) override;
	int32_t switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player) override;
	bool swoutputs(std::span<const uint8_t> values) override;
	/* 1E60: a refused 32 leaves its data to be read as commands */
	bool refused_outputs_skip_data() const override { return false; }
	/* 058B: F0 in any frame it reads (to every node or to its own) */
	bool reset_in_any_frame() const override { return true; }
	/* 1D52, 1E9A: 0 refused, 1 the first, any other the second */
	int coin_slot(uint8_t slot) const override { return slot == 0 ? -1 : slot == 1 ? 0 : 1; }
	uint32_t frame_cycles(size_t request_bytes, size_t reply_bytes) const override;
	uint32_t command_cycles(std::span<const uint8_t> command, Outcome outcome) const override;
};

} /* namespace odin */

#endif /* ODIN_IO_837_13741_IMITATION_HPP */
