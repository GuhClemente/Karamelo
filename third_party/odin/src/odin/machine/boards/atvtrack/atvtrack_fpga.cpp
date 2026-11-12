// license:BSD-3-Clause
// copyright-holders:Samuele Zannoli
/*
 * The boot program of the ATV Track board, from MAME
 * src/mame/gaelco/atvtrack.cpp, rewritten for Odin. Odin is GPL-3.0;
 * this file keeps its BSD-3-Clause licence and copyright holder.
 */

#include "odin/machine/boards/atvtrack/atvtrack_fpga.hpp"

#include <array>

namespace odin {

namespace {

/* Bit `from[i]` of `value` becomes bit 15 - i */
uint16_t swap_bits(uint16_t value, const std::array<unsigned, 16> &from)
{
	uint16_t out = 0;

	for (unsigned i = 0; i < 16; i++)
		out = static_cast<uint16_t>(out | (((value >> from[i]) & 1u) << (15 - i)));
	return out;
}

/* The 4096 bits of Embedded Array Block `eab` in the image. HEURISTIC: the
 * image has no POF header, so the block offsets come from the one known
 * dump. */
void read_eab(std::span<uint8_t, 512> out, std::span<const uint8_t> config, unsigned eab)
{
	const uint32_t start = 0x45B * 8 + 1 + 0x25E6 * 8 * eab;

	for (uint32_t bit = 0; bit < 4096; bit++) {
		/* Address lines 11-10 between 9-3 and 2-0, then 11-6 inverted */
		uint32_t t = (bit & 0xF007) | ((bit >> 7) & 0x18) | ((bit << 2) & 0xFE0);

		t ^= 0x0FC0;
		const uint32_t from = start + (t & 0x1F) + ((t >> 5) & 1) * 63 + ((t >> 6) & 0x3F) * 0x4D * 8;
		const unsigned value = (config[from / 8] >> (from & 7)) & 1u;

		out[bit / 8] = static_cast<uint8_t>((out[bit / 8] & ~(1u << (bit & 7))) | (value << (bit & 7)));
	}
}

} /* namespace */

std::vector<uint8_t> atvtrack_boot(std::span<const uint8_t> config)
{
	static constexpr std::array<unsigned, 16> LOW = { 7, 9, 0, 10, 3, 11, 4, 12, 2, 15, 1, 13, 6, 8, 5, 14 };
	static constexpr std::array<unsigned, 16> HIGH = { 5, 10, 7, 9, 6, 13, 3, 15, 2, 11, 1, 8, 0, 12, 4, 14 };
	std::array<uint8_t, 1024> eabs{};
	std::vector<uint8_t> boot(1024);

	if (config.size() < 0x45B + 2 * 0x25E6 + 0x40 * 0x4D)
		return {};
	read_eab(std::span<uint8_t, 512>(eabs.data(), 512), config, 0);
	read_eab(std::span<uint8_t, 512>(eabs.data() + 512, 512), config, 1);
	/* Each 32-bit word: low half from the second block, high half from
	 * the first, with their data lines swapped */
	for (unsigned i = 0; i < 256; i++) {
		const uint16_t low = swap_bits(static_cast<uint16_t>(eabs[512 + i * 2] | eabs[512 + i * 2 + 1] << 8), LOW);
		const uint16_t high = swap_bits(static_cast<uint16_t>(eabs[i * 2] | eabs[i * 2 + 1] << 8), HIGH);

		boot[i * 4 + 0] = static_cast<uint8_t>(low);
		boot[i * 4 + 1] = static_cast<uint8_t>(low >> 8);
		boot[i * 4 + 2] = static_cast<uint8_t>(high);
		boot[i * 4 + 3] = static_cast<uint8_t>(high >> 8);
	}
	return boot;
}

} /* namespace odin */
