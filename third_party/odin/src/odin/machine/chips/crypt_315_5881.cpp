// license:BSD-3-Clause
// copyright-holders:Andreas Naive, Olivier Galibert, David Haywood
/*
 * Sega 315-5881 decryption chip, from MAME
 * src/mame/sega/315-5881_crypt, rewritten for Odin. Odin is GPL-3.0;
 * this file keeps its BSD-3-Clause licence and copyright holders.
 */

#include "odin/machine/chips/crypt_315_5881.hpp"

#include <cassert>
#include <format>
#include <utility>

#include "odin/common/bytes.hpp"

namespace odin {

namespace {

constexpr uint32_t bit(uint32_t x, uint32_t n)
{
	return (x >> n) & 1u;
}

/* MAME's bitswap<16>: bits[0] is the source of result bit 15. */
constexpr uint16_t bitswap16(uint32_t val, const std::array<uint8_t, 16> &bits)
{
	uint32_t out = 0;
	for (uint32_t i = 0; i < 16; i++) {
		out |= bit(val, bits[i]) << (15 - i);
	}
	return static_cast<uint16_t>(out);
}

/* Six input bits, two output bits. An input of -1 takes no data bit,
 * only the key. 255 in a table is a value MAME never saw. */
struct Sbox {
	std::array<uint8_t, 64> table;
	std::array<int8_t, 6> inputs;
	std::array<uint8_t, 2> outputs;
};

using Round = std::array<Sbox, 4>;

const std::array<Round, 4> FN1_SBOXES = {{
	{{	/* 1st round */
		{
			{{
				0,3,2,2,1,3,1,2,3,2,1,2,1,2,3,1,3,2,2,0,2,1,3,0,0,3,2,3,2,1,2,0,
				2,3,1,1,2,2,1,1,1,0,2,3,3,0,2,1,1,1,1,1,3,0,3,2,1,0,1,2,0,3,1,3,
			}},
			{{3,4,5,7,-1,-1}},
			{{0,4}}
		},
		{
			{{
				2,2,2,0,3,3,0,1,2,2,3,2,3,0,2,2,1,1,0,3,3,2,0,2,0,1,0,1,2,3,1,1,
				0,1,3,3,1,3,3,1,2,3,2,0,0,0,2,2,0,3,1,3,0,3,2,2,0,3,0,3,1,1,0,2,
			}},
			{{0,1,2,5,6,7}},
			{{1,6}}
		},
		{
			{{
				0,1,3,0,3,1,1,1,1,2,3,1,3,0,2,3,3,2,0,2,1,1,2,1,1,3,1,0,0,2,0,1,
				1,3,1,0,0,3,2,3,2,0,3,3,0,0,0,0,1,2,3,3,2,0,3,2,1,0,0,0,2,2,3,3,
			}},
			{{0,2,5,6,7,-1}},
			{{2,3}}
		},
		{
			{{
				3,2,1,2,1,2,3,2,0,3,2,2,3,1,3,3,0,2,3,0,3,3,2,1,1,1,2,0,2,2,0,1,
				1,3,3,0,0,3,0,3,0,2,1,3,2,1,0,0,0,1,1,2,0,1,0,0,0,1,3,3,2,0,3,3,
			}},
			{{1,2,3,4,6,7}},
			{{5,7}}
		},
	}},
	{{	/* 2nd round */
		{
			{{
				3,3,1,2,0,0,2,2,2,1,2,1,3,1,1,3,3,0,0,3,0,3,3,2,1,1,3,2,3,2,1,3,
				2,3,0,1,3,2,0,1,2,1,3,1,2,2,3,3,3,1,2,2,0,3,1,2,2,1,3,0,3,0,1,3,
			}},
			{{0,1,3,4,5,7}},
			{{0,4}}
		},
		{
			{{
				2,0,1,0,0,3,2,0,3,3,1,2,1,3,0,2,0,2,0,0,0,2,3,1,3,1,1,2,3,0,3,0,
				3,0,2,0,0,2,2,1,0,2,3,3,1,3,1,0,1,3,3,0,0,1,3,1,0,2,0,3,2,1,0,1,
			}},
			{{0,1,3,4,6,-1}},
			{{1,5}}
		},
		{
			{{
				2,2,2,3,1,1,0,1,3,3,1,1,2,2,2,0,0,3,2,3,3,0,2,1,2,2,3,0,1,3,0,0,
				3,2,0,3,2,0,1,0,0,1,2,2,3,3,0,2,2,1,3,1,1,1,1,2,0,3,1,0,0,2,3,2,
			}},
			{{1,2,5,6,7,6}},
			{{2,7}}
		},
		{
			{{
				0,1,3,3,3,1,3,3,1,0,2,0,2,0,0,3,1,2,1,3,1,2,3,2,2,0,1,3,0,3,3,3,
				0,0,0,2,1,1,2,3,2,2,3,1,1,2,0,2,0,2,1,3,1,1,3,3,1,1,3,0,2,3,0,0,
			}},
			{{2,3,4,5,6,7}},
			{{3,6}}
		},
	}},
	{{	/* 3rd round */
		{
			{{
				0,0,1,0,1,0,0,3,2,0,0,3,0,1,0,2,0,3,0,0,2,0,3,2,2,1,3,2,2,1,1,2,
				0,0,0,3,0,1,1,0,0,2,1,0,3,1,2,2,2,0,3,1,3,0,1,2,2,1,1,1,0,2,3,1,
			}},
			{{1,2,3,4,5,7}},
			{{0,5}}
		},
		{
			{{
				1,2,1,0,3,1,1,2,0,0,2,3,2,3,1,3,2,0,3,2,2,3,1,1,1,1,0,3,2,0,0,1,
				1,0,0,1,3,1,2,3,0,0,2,3,3,0,1,0,0,2,3,0,1,2,0,1,3,3,3,1,2,0,2,1,
			}},
			{{0,2,4,5,6,7}},
			{{1,6}}
		},
		{
			{{
				0,3,0,2,1,2,0,0,1,1,0,0,3,1,1,0,0,3,0,0,2,3,3,2,3,1,2,0,0,2,3,0,
				255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
			}},
			{{0,2,4,6,7,-1}},
			{{2,3}}
		},
		{
			{{
				0,0,1,0,0,1,0,2,3,3,0,3,3,2,3,0,2,2,2,0,3,2,0,3,1,0,0,3,3,0,0,0,
				2,2,1,0,2,0,3,2,0,0,3,1,3,3,0,0,2,1,1,2,1,0,1,1,0,3,1,2,0,2,0,3,
			}},
			{{0,1,2,3,6,-1}},
			{{4,7}}
		},
	}},
	{{	/* 4th round */
		{
			{{
				0,3,3,3,3,3,2,0,0,1,2,0,2,2,2,2,1,1,0,2,2,1,3,2,3,2,0,1,2,3,2,1,
				3,2,2,3,1,0,1,0,0,2,0,1,2,1,2,3,1,2,1,1,2,2,1,0,1,3,2,3,2,0,3,1,
			}},
			{{0,1,3,4,5,6}},
			{{0,5}}
		},
		{
			{{
				0,3,0,0,2,0,3,1,1,1,2,2,2,1,3,1,2,2,1,3,2,2,3,3,0,3,1,0,3,2,0,1,
				3,0,2,0,1,0,2,1,3,3,1,2,2,0,2,3,3,2,3,0,1,1,3,3,0,2,1,3,0,2,2,3,
			}},
			{{0,1,2,3,5,7}},
			{{1,7}}
		},
		{
			{{
				0,1,2,3,3,3,3,1,2,0,2,3,2,1,0,1,2,2,1,2,0,3,2,0,1,1,0,1,3,1,3,1,
				3,1,0,0,1,0,0,0,0,1,2,2,1,1,3,3,1,2,3,3,3,2,3,0,2,2,1,3,3,0,2,0,
			}},
			{{2,3,4,5,6,7}},
			{{2,3}}
		},
		{
			{{
				0,2,1,1,3,2,0,3,1,0,1,0,3,2,1,1,2,2,0,3,1,0,1,2,2,2,3,3,0,0,0,0,
				1,2,1,0,2,1,2,2,2,3,2,3,0,1,3,0,0,1,3,0,0,1,1,0,1,0,0,0,0,2,0,1,
			}},
			{{0,1,2,4,6,7}},
			{{4,6}}
		},
	}},
}};

const std::array<Round, 4> FN2_SBOXES = {{
	{{	/* 1st round */
		{
			{{
				3,3,0,1,0,1,0,0,0,3,0,0,1,3,1,2,0,3,3,3,2,1,0,1,1,1,2,2,2,3,2,2,
				2,1,3,3,1,3,1,1,0,0,1,2,0,2,2,1,1,2,3,1,2,1,3,1,2,2,0,1,3,0,2,2,
			}},
			{{1,3,4,5,6,7}},
			{{0,7}}
		},
		{
			{{
				0,1,3,0,1,1,2,3,2,0,0,3,2,1,3,1,3,3,0,0,1,0,0,3,0,3,3,2,3,2,0,1,
				3,2,3,2,2,1,3,1,1,1,0,3,3,2,2,1,1,2,0,2,0,1,1,0,1,0,1,1,2,0,3,0,
			}},
			{{0,3,5,6,5,0}},
			{{1,2}}
		},
		{
			{{
				0,2,2,1,0,1,2,1,2,0,1,2,3,3,0,1,3,1,1,2,1,2,1,3,3,2,3,3,2,1,0,1,
				0,1,0,2,0,1,1,3,2,0,3,2,1,1,1,3,2,3,0,2,3,0,2,2,1,3,0,1,1,2,2,2,
			}},
			{{0,2,3,4,7,-1}},
			{{3,4}}
		},
		{
			{{
				2,3,1,3,2,0,1,2,0,0,3,3,3,3,3,1,2,0,2,1,2,3,0,2,0,1,0,3,0,2,1,0,
				2,3,0,1,3,0,3,2,3,1,2,0,3,1,1,2,0,3,0,0,2,0,2,1,2,2,3,2,1,2,3,1,
			}},
			{{1,2,5,6,-1,-1}},
			{{5,6}}
		},
	}},
	{{	/* 2nd round */
		{
			{{
				2,3,1,3,1,0,3,3,3,2,3,3,2,0,0,3,2,3,0,3,1,1,2,3,1,1,2,2,0,1,0,0,
				2,1,0,1,2,0,1,2,0,3,1,1,2,3,1,2,0,2,0,1,3,0,1,0,2,2,3,0,3,2,3,0,
			}},
			{{0,1,4,5,6,7}},
			{{0,7}}
		},
		{
			{{
				0,2,2,0,2,2,0,3,2,3,2,1,3,2,3,3,1,1,0,0,3,0,2,1,1,3,3,2,3,2,0,1,
				1,2,3,0,1,0,3,0,3,1,0,2,1,2,0,3,2,3,1,2,2,0,3,2,3,0,0,1,2,3,3,3,
			}},
			{{0,2,3,6,7,-1}},
			{{1,5}}
		},
		{
			{{
				1,0,3,0,0,1,2,1,0,0,1,0,0,0,2,3,2,2,0,2,0,1,3,0,2,0,1,3,2,3,0,1,
				1,2,2,2,1,3,0,3,0,1,1,0,3,2,3,3,2,0,0,3,1,2,1,3,3,2,1,0,2,1,2,3,
			}},
			{{2,3,4,6,7,2}},
			{{2,3}}
		},
		{
			{{
				2,3,1,3,1,1,2,3,3,1,1,0,1,0,2,3,2,1,0,0,2,2,0,1,0,2,2,2,0,2,1,0,
				3,1,2,3,1,3,0,2,1,0,1,0,0,1,2,2,3,2,3,1,3,2,1,1,2,0,2,1,3,3,1,0,
			}},
			{{1,2,3,4,5,6}},
			{{4,6}}
		},
	}},
	{{	/* 3rd round */
		{
			{{
				0,3,0,1,3,0,0,2,1,0,1,3,2,2,2,0,3,3,3,0,2,2,0,3,0,0,2,3,0,3,2,1,
				3,3,0,3,0,2,3,3,1,1,1,0,2,2,1,1,3,0,3,1,2,0,2,0,0,0,3,2,1,1,0,0,
			}},
			{{1,4,5,6,7,5}},
			{{0,5}}
		},
		{
			{{
				0,3,0,1,3,0,3,1,3,2,2,2,3,0,3,2,2,1,2,2,0,3,2,2,0,0,2,1,1,3,2,3,
				2,3,3,1,2,0,1,2,2,1,0,0,0,0,2,3,1,2,0,3,1,3,1,2,3,2,1,0,3,0,0,2,
			}},
			{{0,2,3,4,6,7}},
			{{1,7}}
		},
		{
			{{
				2,2,0,3,0,3,1,0,1,1,2,3,2,3,1,0,0,0,3,2,2,0,2,3,1,3,2,0,3,3,1,3,
				255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
			}},
			{{1,2,4,7,2,-1}},
			{{2,4}}
		},
		{
			{{
				0,2,3,1,3,1,1,0,0,1,3,0,2,1,3,3,2,0,2,1,1,2,3,3,0,0,0,2,0,2,3,0,
				3,3,3,3,2,3,3,2,3,0,1,0,2,3,3,2,0,1,3,1,0,1,2,3,3,0,2,0,3,0,3,3,
			}},
			{{0,1,2,3,5,7}},
			{{3,6}}
		},
	}},
	{{	/* 4th round */
		{
			{{
				0,1,1,0,0,1,0,2,3,3,0,1,2,3,0,2,1,0,3,3,2,0,3,0,0,2,1,0,1,0,1,3,
				0,3,3,1,2,0,3,0,1,3,2,0,3,3,1,3,0,2,3,3,2,1,1,2,2,1,2,1,2,0,1,1,
			}},
			{{0,1,2,4,7,-1}},
			{{0,5}}
		},
		{
			{{
				2,0,0,2,3,0,2,3,3,1,1,1,2,1,1,0,0,2,1,0,0,3,1,0,0,3,3,0,1,0,1,2,
				0,2,0,2,0,1,2,3,2,1,1,0,3,3,3,3,3,3,1,0,3,0,0,2,0,3,2,0,2,2,0,1,
			}},
			{{0,1,3,5,6,-1}},
			{{1,3}}
		},
		{
			{{
				0,1,1,2,1,3,1,1,0,0,3,1,1,1,2,0,3,2,0,1,1,2,3,3,3,0,3,0,0,2,0,3,
				3,2,0,0,3,2,3,1,2,3,0,3,2,0,1,2,2,2,0,2,0,1,2,2,3,1,2,2,1,1,1,1,
			}},
			{{0,2,3,4,5,7}},
			{{2,7}}
		},
		{
			{{
				0,1,2,0,3,3,0,3,2,1,3,3,0,3,1,1,3,2,3,2,3,0,0,0,3,0,2,2,3,2,2,3,
				2,2,3,1,2,3,1,2,0,3,0,2,3,1,0,0,3,2,1,2,1,2,1,3,1,0,2,3,3,1,3,2,
			}},
			{{2,3,4,5,6,7}},
			{{4,6}}
		},
	}},
}};

/* Key scheduling: {key bit, subkey bit}; subkey bit n goes to bit
 * n % 24 of the subkey of round n / 24. */
using KeyBit = std::array<uint8_t, 2>;

constexpr std::array<KeyBit, 38> FN1_GAME_KEY_SCHEDULING = {{
	{1,29},  {1,71},  {2,4},   {2,54},  {3,8},   {4,56},  {4,73},  {5,11},
	{6,51},  {7,92},  {8,89},  {9,9},   {9,39},  {9,58},  {10,90}, {11,6},
	{12,64}, {13,49}, {14,44}, {15,40}, {16,69}, {17,15}, {18,23}, {18,43},
	{19,82}, {20,81}, {21,32}, {22,5},  {23,66}, {24,13}, {24,45}, {25,12},
	{25,35}, {26,61}, {27,10}, {27,59}, {28,25}, {29,86}
}};

constexpr std::array<KeyBit, 32> FN2_GAME_KEY_SCHEDULING = {{
	{0,0},   {1,3},   {2,11},  {3,20},  {4,22},  {5,23},  {6,29},  {7,38},
	{8,39},  {9,55},  {9,86},  {9,87},  {10,50}, {11,57}, {12,59}, {13,61},
	{14,63}, {15,67}, {16,72}, {17,83}, {18,88}, {19,94}, {20,35}, {21,17},
	{22,6},  {23,85}, {24,16}, {25,25}, {26,92}, {27,47}, {28,28}, {29,90}
}};

constexpr std::array<KeyBit, 20> FN1_SEQUENCE_KEY_SCHEDULING = {{
	{0,52},  {1,34},  {2,17},  {3,36}, {4,84},  {4,88},  {5,57},  {6,48},
	{6,68},  {7,76},  {8,83},  {9,30}, {10,22}, {10,41}, {11,38}, {12,55},
	{13,74}, {14,19}, {14,80}, {15,26}
}};

constexpr std::array<uint8_t, 16> FN2_SEQUENCE_KEY_SCHEDULING = {
	77,34,8,42,36,27,69,66,13,9,79,31,49,7,24,64
};

constexpr std::array<uint8_t, 16> FN2_MIDDLE_RESULT_SCHEDULING = {
	1,10,44,68,74,78,81,95,2,4,30,40,41,51,53,58
};

/* Node format of the decompression trees:
 *   0xxxxxxx  next node index
 *   1a0bbccc  end node: a = 0 repeat a byte from the stream, 1 fetch
 *             from the previous line at offset bb (00 = 0, 01 = 1,
 *             11 = -1); the count is ccc + 1
 *   11111111  empty node */
using Tree = std::array<std::array<uint8_t, 32>, 2>;

constexpr std::array<Tree, 9> TREES = {{
	{{
		{{0x01,0x10,0x0f,0x05,0xc4,0x13,0x87,0x0a,0xcc,0x81,0xce,0x0c,0x86,0x0e,0x84,0xc2,
			0x11,0xc1,0xc3,0xcf,0x15,0xc8,0xcd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc7,0x02,0x03,0x04,0x80,0x06,0x07,0x08,0x09,0xc9,0x0b,0x0d,0x82,0x83,0x85,0xc0,
			0x12,0xc6,0xc5,0x14,0x16,0xca,0xcb,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x02,0x80,0x05,0x04,0x81,0x10,0x15,0x82,0x09,0x83,0x0b,0x0c,0x0d,0xdc,0x0f,0xde,
			0x1c,0xcf,0xc5,0xdd,0x86,0x16,0x87,0x18,0x19,0x1a,0xda,0xca,0xc9,0x1e,0xce,0xff,}},
		{{0x01,0x17,0x03,0x0a,0x08,0x06,0x07,0xc2,0xd9,0xc4,0xd8,0xc8,0x0e,0x84,0xcb,0x85,
			0x11,0x12,0x13,0x14,0xcd,0x1b,0xdb,0xc7,0xc0,0xc1,0x1d,0xdf,0xc3,0xc6,0xcc,0xff,}},
	}},
	{{
		{{0xc6,0x80,0x03,0x0b,0x05,0x07,0x82,0x08,0x15,0xdc,0xdd,0x0c,0xd9,0xc2,0x14,0x10,
			0x85,0x86,0x18,0x16,0xc5,0xc4,0xc8,0xc9,0xc0,0xcc,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0x01,0x02,0x12,0x04,0x81,0x06,0x83,0xc3,0x09,0x0a,0x84,0x11,0x0d,0x0e,0x0f,0x19,
			0xca,0xc1,0x13,0xd8,0xda,0xdb,0x17,0xde,0xcd,0xcb,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x01,0x80,0x0d,0x04,0x05,0x15,0x83,0x08,0xd9,0x10,0x0b,0x0c,0x84,0x0e,0xc0,0x14,
			0x12,0xcb,0x13,0xca,0xc8,0xc2,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc5,0x02,0x03,0x07,0x81,0x06,0x82,0xcc,0x09,0x0a,0xc9,0x11,0xc4,0x0f,0x85,0xd8,
			0xda,0xdb,0xc3,0xdc,0xdd,0xc1,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x01,0x80,0x06,0x0c,0x05,0x81,0xd8,0x84,0x09,0xdc,0x0b,0x0f,0x0d,0x0e,0x10,0xdb,
			0x11,0xca,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc4,0x02,0x03,0x04,0xcb,0x0a,0x07,0x08,0xd9,0x82,0xc8,0x83,0xc0,0xc1,0xda,0xc2,
			0xc9,0xc3,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x01,0x02,0x06,0x0a,0x83,0x0b,0x07,0x08,0x09,0x82,0xd8,0x0c,0xd9,0xda,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc3,0x80,0x03,0x04,0x05,0x81,0xca,0xc8,0xdb,0xc9,0xc0,0xc1,0x0d,0xc2,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x01,0x02,0x03,0x04,0x81,0x07,0x08,0xd8,0xda,0xd9,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc2,0x80,0x05,0xc9,0xc8,0x06,0x82,0xc0,0x09,0xc1,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x01,0x80,0x04,0xc8,0xc0,0xd9,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc1,0x02,0x03,0x81,0x05,0xd8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
	{{
		{{0x01,0xd8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
		{{0xc0,0x80,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,}},
	}},
}};

/* Previous-line offsets of a fetch node, by its bb field. */
constexpr std::array<int32_t, 4> FETCH_OFFSETS = {0, 1, 0, -1};

uint32_t feistel_function(uint32_t input, const Round &sboxes, uint32_t subkeys)
{
	uint32_t result = 0;
	for (uint32_t m = 0; m < 4; ++m) {
		uint32_t aux = 0;
		for (uint32_t k = 0; k < 6; ++k) {
			if (sboxes[m].inputs[k] != -1) {
				aux |= bit(input, static_cast<uint32_t>(sboxes[m].inputs[k])) << k;
			}
		}
		aux = sboxes[m].table[(aux ^ subkeys) & 0x3f];
		for (uint32_t k = 0; k < 2; ++k) {
			result |= bit(aux, k) << sboxes[m].outputs[k];
		}
		subkeys >>= 6;
	}
	return result;
}

void schedule(std::array<uint32_t, 4> &subkeys, uint32_t subkey_bit)
{
	subkeys[subkey_bit / 24] ^= 1u << (subkey_bit % 24);
}

/* The 16-bit block cipher: the first network turns the counter into a
 * key for the second one, which decrypts the word. */
uint16_t block_decrypt(uint32_t game_key, uint16_t sequence_key, uint16_t counter, uint16_t data)
{
	std::array<uint32_t, 4> fn1_subkeys = {};
	std::array<uint32_t, 4> fn2_subkeys = {};

	for (const KeyBit &kb : FN1_GAME_KEY_SCHEDULING) {
		if (bit(game_key, kb[0]) != 0) {
			schedule(fn1_subkeys, kb[1]);
		}
	}
	for (const KeyBit &kb : FN2_GAME_KEY_SCHEDULING) {
		if (bit(game_key, kb[0]) != 0) {
			schedule(fn2_subkeys, kb[1]);
		}
	}
	for (const KeyBit &kb : FN1_SEQUENCE_KEY_SCHEDULING) {
		if (bit(sequence_key, kb[0]) != 0) {
			schedule(fn1_subkeys, kb[1]);
		}
	}
	for (uint32_t j = 0; j < 16; ++j) {
		if (bit(sequence_key, j) != 0) {
			schedule(fn2_subkeys, FN2_SEQUENCE_KEY_SCHEDULING[j]);
		}
	}

	uint32_t aux = bitswap16(counter, {5, 12, 14, 13, 9, 3, 6, 4, 8, 1, 15, 11, 0, 7, 10, 2});
	uint32_t b = aux >> 8;
	uint32_t a = (aux & 0xff) ^ feistel_function(b, FN1_SBOXES[0], fn1_subkeys[0]);
	b ^= feistel_function(a, FN1_SBOXES[1], fn1_subkeys[1]);
	a ^= feistel_function(b, FN1_SBOXES[2], fn1_subkeys[2]);
	b ^= feistel_function(a, FN1_SBOXES[3], fn1_subkeys[3]);
	const uint32_t middle_result = (b << 8) | a;

	for (uint32_t j = 0; j < 16; ++j) {
		if (bit(middle_result, j) != 0) {
			schedule(fn2_subkeys, FN2_MIDDLE_RESULT_SCHEDULING[j]);
		}
	}

	aux = bitswap16(data, {14, 3, 8, 12, 13, 7, 15, 4, 6, 2, 9, 5, 11, 0, 1, 10});
	b = aux >> 8;
	a = (aux & 0xff) ^ feistel_function(b, FN2_SBOXES[0], fn2_subkeys[0]);
	b ^= feistel_function(a, FN2_SBOXES[1], fn2_subkeys[1]);
	a ^= feistel_function(b, FN2_SBOXES[2], fn2_subkeys[2]);
	b ^= feistel_function(a, FN2_SBOXES[3], fn2_subkeys[3]);
	aux = (b << 8) | a;

	return bitswap16(aux, {15, 7, 6, 14, 13, 12, 5, 4, 3, 2, 11, 10, 9, 1, 0, 8});
}

} /* namespace */

Crypt315_5881::Crypt315_5881(uint32_t key)
	: m_key(key), m_log(nullptr), m_unknown_told(false), m_buffer_bit2(0), m_buffer2a(0), m_block_size(0), m_block_pos(0),
	  m_block_numlines(0), m_done_compression(false)
{
	m_buffer2.fill(0);
	reset();
}

/* What MAME's device_reset clears. The block state, the bit position
 * in the decompression word and that word itself are left alone, as
 * there. */
void Crypt315_5881::reset()
{
	m_buffer.fill(0);
	m_line_buffer.fill(0);
	m_line_buffer_prev.fill(0);

	m_prot_cur_address = 0;
	m_subkey = 0;
	m_dec_hist = 0;
	m_dec_header = 0;
	m_enc_ready = false;
	m_first_read = false;

	m_buffer_pos = 0;
	m_line_buffer_pos = 0;
	m_line_buffer_size = 0;
	m_buffer_bit = 0;
}

/* Bit 0 busy; MAME never reports it. */
uint32_t Crypt315_5881::bus_read(uint32_t offset, unsigned size)
{
	if (size == 2) {
		switch (offset) {
		case REG_READY:
			return ready();
		case REG_DECRYPT:
			return read_decrypt();
		default:
			break;
		}
	}
	tell_unknown('R', offset, size, 0);
	return 0;
}

void Crypt315_5881::bus_write(uint32_t offset, unsigned size, uint32_t value)
{
	if (size == 2) {
		const uint16_t data = static_cast<uint16_t>(value);

		switch (offset) {
		case REG_ADDR_LO:
			write_addr_lo(data);
			return;
		case REG_ADDR_HI:
			write_addr_hi(data);
			return;
		case REG_SUBKEY:
			write_subkey(data);
			return;
		default:
			break;
		}
	}
	tell_unknown('W', offset, size, value);
}

void Crypt315_5881::tell_unknown(char rw, uint32_t offset, unsigned size, uint32_t value)
{
	if (m_unknown_told) {
		return;
	}
	m_unknown_told = true;
	std::fprintf(m_log ? m_log : stderr, "315-5881: unknown register %c offset %X size %u value %04X\n",
	             rw, offset, size, value);
}

uint16_t Crypt315_5881::ready() const
{
	return 0;
}

void Crypt315_5881::write_addr_lo(uint16_t data)
{
	m_prot_cur_address = (m_prot_cur_address & 0xffff0000) | data;
	m_enc_ready = false;
	m_first_read = true;
}

/* MAME forces the high word to 0: its standard hookup crashes LA
 * Machine Gun otherwise (315-5881_crypt.cpp:25). */
void Crypt315_5881::write_addr_hi(uint16_t data)
{
	if (m_log && data != 0) {
		std::fputs(std::format("315-5881: address high {:04x} taken as 0\n", data).c_str(), m_log);
	}
	m_prot_cur_address &= 0x0000ffff;
	m_enc_ready = false;
	m_buffer_bit = 7;
	m_buffer_bit2 = 15;
	m_first_read = true;
}

void Crypt315_5881::write_subkey(uint16_t data)
{
	if (m_log) {
		std::fputs(std::format("315-5881: subkey {:04x}\n", data).c_str(), m_log);
	}
	m_subkey = data;
	m_enc_ready = false;
}

/* The little-endian side of the chip: the stream word is byte-swapped
 * on the way out (MAME decrypt_le_r). */
uint16_t Crypt315_5881::read_decrypt()
{
	/* The RAM schemes expect a dummy word before the stream; 0 as in
	 * MAME, which matched its earlier simulation of Dynamite Cop. */
	if (m_first_read) {
		m_first_read = false;
		return 0;
	}
	return swap16(do_decrypt());
}

/* The next stream word, big-endian as the buffers hold it. Two
 * places use >= where MAME tests for equality: a line of odd size
 * never lands on its end, and a compressed block handing over to an
 * uncompressed header can leave the byte position at 1; MAME reads
 * past its buffers there, this takes the next line or word. */
uint16_t Crypt315_5881::do_decrypt()
{
	if (!m_enc_ready) {
		enc_start();
	}
	uint32_t hi, lo;
	if (m_dec_header & FLAG_COMPRESSED) {
		if (m_line_buffer_pos >= m_line_buffer_size) {
			/* As in MAME the line is filled even when the header
			 * just read is not a compressed one. */
			if (m_done_compression) {
				enc_start();
			}
			line_fill();
		}
		hi = m_line_buffer[m_line_buffer_pos];
		lo = m_line_buffer[m_line_buffer_pos + 1];
		m_line_buffer_pos += 2;
	} else {
		if (m_buffer_pos + 1 >= BUFFER_SIZE) {
			enc_fill();
		}
		hi = m_buffer[m_buffer_pos];
		lo = m_buffer[m_buffer_pos + 1];
		m_buffer_pos += 2;
	}
	return static_cast<uint16_t>((hi << 8) | lo);
}

/* One cipher word: its low 2 bits go to this plaintext word, the high
 * 14 bits of the previous one complete it. */
uint16_t Crypt315_5881::get_decrypted_16()
{
	assert(m_read);
	const uint16_t enc = m_read ? m_read(m_prot_cur_address) : 0;
	const uint16_t dec = block_decrypt(m_key, m_subkey, static_cast<uint16_t>(m_prot_cur_address), enc);
	const uint16_t res = static_cast<uint16_t>((dec & 3) | (m_dec_hist & 0xfffc));
	m_dec_hist = dec;
	m_prot_cur_address++;
	return res;
}

/* Reads the 18-bit header of a substream. */
void Crypt315_5881::enc_start()
{
	m_block_pos = 0;
	m_done_compression = false;
	m_buffer_pos = BUFFER_SIZE;

	/* Bits still unread in the decompression word serve as the start
	 * of the next header (twcup98 needs this; twsoc98 sits at 14 on
	 * its title screen and expects the normal path). */
	if (m_buffer_bit2 < 14) {
		m_dec_header = static_cast<uint32_t>(m_buffer2a & 0x0003) << 16;
	} else {
		/* Without this a run would depend on the one before (astrass). */
		m_dec_hist = 0;
		m_dec_header = static_cast<uint32_t>(get_decrypted_16()) << 16;
	}
	m_dec_header |= get_decrypted_16();

	/* 0x20000 compressed, 0x1ff00 size A, 0xff size B; the block is
	 * (A+1)*(B+1) bytes and a compressed line is A+1 bytes. */
	m_block_numlines = (m_dec_header & 0x000000ff) + 1;
	const uint32_t blocky = ((m_dec_header & 0x0001ff00) >> 8) + 1;
	m_block_size = m_block_numlines * blocky;

	if (m_dec_header & FLAG_COMPRESSED) {
		m_line_buffer_size = blocky;
		m_line_buffer_pos = m_line_buffer_size;
		m_buffer_bit = 7;
		m_buffer_bit2 = 15;
	}

	m_enc_ready = true;
}

/* Next word of an uncompressed block, little-endian in the buffer. */
void Crypt315_5881::enc_fill()
{
	for (uint32_t i = 0; i != BUFFER_SIZE; i += 2) {
		const uint16_t val = get_decrypted_16();
		m_buffer[i] = static_cast<uint8_t>(val);
		m_buffer[i + 1] = static_cast<uint8_t>(val >> 8);
		m_block_pos += 2;

		/* A compressed block ends on its output size, in line_fill. */
		if (!(m_dec_header & FLAG_COMPRESSED) && m_block_pos == m_block_size) {
			enc_start();
		}
	}
	m_buffer_pos = 0;
}

/* Bits of the compressed stream, MSB first out of each byte, low byte
 * of each cipher word first. */
uint32_t Crypt315_5881::get_compressed_bit()
{
	if (m_buffer_bit2 == 15) {
		m_buffer_bit2 = 0;
		m_buffer2a = get_decrypted_16();
		m_buffer2[0] = static_cast<uint8_t>(m_buffer2a);
		m_buffer2[1] = static_cast<uint8_t>(m_buffer2a >> 8);
		m_buffer_pos = 0;
	} else {
		m_buffer_bit2++;
	}

	const uint32_t res = (static_cast<uint32_t>(m_buffer2[(m_buffer_pos & 1) ^ 1]) >> m_buffer_bit) & 1;
	if (m_buffer_bit == 0) {
		m_buffer_bit = 7;
		m_buffer_pos++;
	} else {
		m_buffer_bit--;
	}
	return res;
}

/* Decodes one line of a compressed block. Tree 0 codes the start of
 * the line, tree 1 its interior, trees 2 to 8 its last 7 to 1 bytes.
 * The bytes of a line are stored swapped in pairs, as the words come
 * out big-endian. Runs that would pass the end of the line are cut
 * there; MAME writes past its buffer instead. */
void Crypt315_5881::line_fill()
{
	std::swap(m_line_buffer, m_line_buffer_prev);
	const std::array<uint8_t, LINE_SIZE> &lp = m_line_buffer_prev;
	std::array<uint8_t, LINE_SIZE> &lc = m_line_buffer;
	const uint32_t size = m_line_buffer_size;

	m_line_buffer_pos = 0;

	uint32_t i = 0;
	while (i < size) {
		uint32_t slot;
		if (i == 0) {
			slot = 0;
		} else if (i + 7 < size) {
			slot = 1;
		} else {
			slot = (i & 7) + 1;
		}

		uint32_t tmp = 0;
		while (!(tmp & 0x80)) {
			tmp = TREES[slot][get_compressed_bit()][tmp];
		}
		if (tmp == 0xff) {
			continue;
		}
		const uint32_t count = (tmp & 7) + 1;

		if (tmp & 0x40) {
			const int32_t offset = FETCH_OFFSETS[(tmp & 0x18) >> 3];
			for (uint32_t j = 0; j != count && i < size; j++) {
				const uint32_t src = static_cast<uint32_t>(static_cast<int32_t>(i + size) + offset) % size;
				lc[i ^ 1] = lp[src ^ 1];
				i++;
			}
		} else {
			uint8_t byte = 0;
			for (uint32_t k = 0; k < 8; k++) {
				byte = static_cast<uint8_t>((byte << 1) | get_compressed_bit());
			}
			for (uint32_t j = 0; j != count && i < size; j++) {
				lc[i ^ 1] = byte;
				i++;
			}
		}
	}

	m_block_pos++;
	if (m_block_numlines == m_block_pos) {
		m_done_compression = true;
	}
}

void Crypt315_5881::save(Crypt315_5881State &out) const
{
	out.buffer = m_buffer;
	out.line_buffer = m_line_buffer;
	out.line_buffer_prev = m_line_buffer_prev;
	out.buffer2 = m_buffer2;
	out.prot_cur_address = m_prot_cur_address;
	out.dec_header = m_dec_header;
	out.buffer_pos = m_buffer_pos;
	out.line_buffer_pos = m_line_buffer_pos;
	out.line_buffer_size = m_line_buffer_size;
	out.buffer_bit = m_buffer_bit;
	out.buffer_bit2 = m_buffer_bit2;
	out.block_size = m_block_size;
	out.block_pos = m_block_pos;
	out.block_numlines = m_block_numlines;
	out.subkey = m_subkey;
	out.dec_hist = m_dec_hist;
	out.buffer2a = m_buffer2a;
	out.first_read = m_first_read;
	out.enc_ready = m_enc_ready;
	out.done_compression = m_done_compression;
}

void Crypt315_5881::load(const Crypt315_5881State &in)
{
	m_buffer = in.buffer;
	m_line_buffer = in.line_buffer;
	m_line_buffer_prev = in.line_buffer_prev;
	m_buffer2 = in.buffer2;
	m_prot_cur_address = in.prot_cur_address;
	m_dec_header = in.dec_header;
	m_buffer_pos = in.buffer_pos;
	m_line_buffer_pos = in.line_buffer_pos;
	m_line_buffer_size = in.line_buffer_size;
	m_buffer_bit = in.buffer_bit;
	m_buffer_bit2 = in.buffer_bit2;
	m_block_size = in.block_size;
	m_block_pos = in.block_pos;
	m_block_numlines = in.block_numlines;
	m_subkey = in.subkey;
	m_dec_hist = in.dec_hist;
	m_buffer2a = in.buffer2a;
	m_first_read = in.first_read != 0;
	m_enc_ready = in.enc_ready != 0;
	m_done_compression = in.done_compression != 0;
}

} /* namespace odin */
