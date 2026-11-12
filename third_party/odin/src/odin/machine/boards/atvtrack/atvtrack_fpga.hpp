// license:BSD-3-Clause
// copyright-holders:Samuele Zannoli
/*
 * The boot program of the ATV Track board, from MAME
 * src/mame/gaelco/atvtrack.cpp, rewritten for Odin. Odin is GPL-3.0;
 * this file keeps its BSD-3-Clause licence and copyright holder.
 */

#ifndef ODIN_ATVTRACK_FPGA_HPP
#define ODIN_ATVTRACK_FPGA_HPP

#include <cstdint>
#include <span>
#include <vector>

namespace odin {

/* The FPGA's 1 KB boot RAM (Altera FLEX 10KE): its first two Embedded Array
 * Blocks, from epc1pc8.ic23. Empty if the image is too short. */
std::vector<uint8_t> atvtrack_boot(std::span<const uint8_t> config);

} /* namespace odin */

#endif
