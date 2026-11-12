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

/* The terminal sets the pilot's data to its defaults (0C17B69C), takes the
 * name from its keyboard (0C192878) and the VR from its list, marks the data
 * as its own and has the encoder (0C18516C) make the three tracks of the
 * card: the data in a block of 192 bytes with its checksum (0C18939C) and,
 * as a check, 64 of its bytes encrypted three times; the block encrypted
 * three times, shuffled between them (0C189284). The cipher (0C188644) is
 * a stream of blocks of 64 bits, each made by a cipher of 16 rounds from
 * the bytes before it; each of its six keys has its own tables. */

#include "odin/machine/chips/von4_card.hpp"

#include <algorithm>

#include "odin/common/bytes.hpp"

namespace odin {

namespace {

/* The program runs where the BOOTROM copies the EPROMs: an address of it,
 * less this, is an offset in them */
constexpr uint32_t BASE = 0x0C000000;

/* The keys of the name (0C42C6C8): four rows of 15, the last two of the
 * first the arrows; space, delete and OK under them */
constexpr uint32_t KEYBOARD = 0x0C2ADE04;
constexpr uint32_t KEY_ROWS = 4, ROW_KEYS = 15, ROW_SIZE = 16, ARROWS = 2;

/* The tables of the cipher, those of key k at addr + k * stride: its first
 * state and its key, 16 nibbles each; the order of the 64 bits of a block at
 * its start and at its end; its 8 boxes of 4 rows of 16; in each round the
 * bits that pick the rows of the boxes (rounds A and B, in turn) and those
 * their outputs change; the key schedule: its 56 bits in two halves of 28,
 * their turn in each round, the 32 and 16 bits a round takes of them. */
constexpr unsigned CIPHER_KEYS = 6, ROUNDS = 16, BOXES = 8;

struct Table {
	uint32_t addr, stride, size, limit;
};

constexpr uint32_t STARTS = 0x0C29F674, KEYS = 0x0C29F7F4, NIBBLES_STRIDE = 0x40;
constexpr Table IP = { 0x0C2A65B4, 0x100, 64, 64 }, FP = { 0x0C2A6BB4, 0x100, 64, 64 };
constexpr Table SBOXES = { 0x0C2A71B4, 0x800, 512, 16 };
constexpr Table ROWS_A = { 0x0C2AA1B4, 0x40, 16, 64 }, ROWS_B = { 0x0C2AA334, 0x40, 16, 64 };
constexpr Table OUTS = { 0x0C2AA4B4, 0x80, 32, 32 };
constexpr Table PC1 = { 0x0C2AA7B4, 0xE0, 56, 64 };
constexpr Table PC2_32 = { 0x0C2AACF4, 0x80, 32, 64 }, PC2_16 = { 0x0C2AAFF4, 0x40, 16, 64 };
/* A turn of 28: the half as it is, whole */
constexpr Table TURNS = { 0x0C2AB174, 0x40, 16, 29 };

/* The shuffles: where each byte of the block goes, the second after the
 * second cipher; of the 64 bytes of the check from 0x600 on */
constexpr uint32_t SHUFFLES = 0x0C2AB2F4, SECOND_SHUFFLE = 0x300, CHECK_SHUFFLES = 0x600;
/* The checksum: a weight for each byte, then its two bytes from the sum */
constexpr uint32_t SUM_WEIGHTS = 0x0C2ABEF4, SUM_BYTES = 0x0C2AC2F4;

constexpr size_t BLOCK = 192, CHECK = 64;
/* The bytes of the block copied into the check, its last 64 bytes */
constexpr struct {
	uint8_t first, last;
} CHECKED[] = { { 0x0C, 0x1A }, { 0x1E, 0x26 }, { 0x2D, 0x3A }, { 0x5C, 0x61 }, { 0x65, 0x66 }, { 0x6A, 0x78 },
                { 0x7B, 0x7D } };

using Bits = std::array<uint8_t, 64>;

/* A key of the cipher as 0C1887CC and 0C188AB0 lay it out */
struct Key {
	Bits start;
	std::array<std::array<uint8_t, 32>, ROUNDS> k32;
	std::array<std::array<uint8_t, 16>, ROUNDS> k16;
	std::array<uint8_t, 64> ip, fp;
	std::array<uint8_t, 32> outs;
	std::array<uint8_t, 16> rows_a, rows_b;
	std::array<uint8_t, 512> sbox;
};

uint32_t word(std::span<const uint8_t> program, uint32_t addr)
{
	return le32(program.data() + (addr - BASE));
}

/* The 16 nibbles of a key, high bit first */
Bits nibbles(std::span<const uint8_t> program, uint32_t addr)
{
	Bits bits;

	for (unsigned i = 0; i < 64; i++)
		bits[i] = (program[addr - BASE + i / 4] >> (3 - i % 4)) & 1;
	return bits;
}

template <size_t N>
void read(std::span<const uint8_t> program, const Table &table, unsigned k, std::array<uint8_t, N> &out)
{
	for (size_t i = 0; i < N; i++)
		out[i] = static_cast<uint8_t>(word(program, table.addr + k * table.stride + 4 * static_cast<uint32_t>(i)));
}

Key key_of(std::span<const uint8_t> program, unsigned k)
{
	Key key;
	std::array<uint8_t, 56> pc1;
	std::array<uint8_t, 32> pc2_32;
	std::array<uint8_t, 16> pc2_16, turns;
	const Bits bits = nibbles(program, KEYS + k * NIBBLES_STRIDE);

	key.start = nibbles(program, STARTS + k * NIBBLES_STRIDE);
	read(program, IP, k, key.ip);
	read(program, FP, k, key.fp);
	read(program, SBOXES, k, key.sbox);
	read(program, ROWS_A, k, key.rows_a);
	read(program, ROWS_B, k, key.rows_b);
	read(program, OUTS, k, key.outs);
	read(program, PC1, k, pc1);
	read(program, PC2_32, k, pc2_32);
	read(program, PC2_16, k, pc2_16);
	read(program, TURNS, k, turns);
	for (unsigned r = 0; r < ROUNDS; r++) {
		/* The two halves turned, side by side, and 8 bits at 0 the
		 * tables may take */
		std::array<uint8_t, 64> cd{};

		for (unsigned i = 0; i < 28; i++) {
			cd[i] = bits[pc1[(i + turns[r]) % 28]];
			cd[28 + i] = bits[pc1[28 + (i + turns[r]) % 28]];
		}
		for (unsigned i = 0; i < 32; i++)
			key.k32[r][i] = cd[pc2_32[i]];
		for (unsigned i = 0; i < 16; i++)
			key.k16[r][i] = cd[pc2_16[i]];
	}
	return key;
}

/* 0C188B9C: the rounds A change the left half from the right one, the
 * rounds B the right from the left, the bits as they change */
Bits encrypt(const Key &key, const Bits &in)
{
	Bits lr, out;

	for (unsigned i = 0; i < 64; i++)
		lr[i] = in[key.ip[i]];
	for (unsigned r = 0; r < ROUNDS; r++) {
		const bool a = r % 2 == 0;
		const unsigned from = a ? 32 : 0, to = a ? 0 : 32;
		const std::array<uint8_t, 16> &rows = a ? key.rows_a : key.rows_b;

		for (unsigned b = 0; b < BOXES; b++) {
			const unsigned row = (lr[rows[2 * b]] ^ key.k16[r][2 * b]) << 1 |
			                     (lr[rows[2 * b + 1]] ^ key.k16[r][2 * b + 1]);
			unsigned column = 0;

			for (unsigned j = 0; j < 4; j++)
				column = column << 1 | (lr[from + 4 * b + j] ^ key.k32[r][4 * b + j]);

			const unsigned out_bits = key.sbox[b * 64 + row * 16 + column];

			for (unsigned j = 0; j < 4; j++)
				lr[to + key.outs[4 * b + j]] ^= static_cast<uint8_t>((out_bits >> (3 - j)) & 1);
		}
	}
	for (unsigned i = 0; i < 64; i++)
		out[i] = lr[key.fp[i]];
	return out;
}

/* 0C188F7E: each 8 bytes XORed with the block the cipher makes of the 8
 * before them (of its first state for the first ones) */
void cipher(std::span<const uint8_t> program, unsigned k, std::span<uint8_t> data)
{
	const Key key = key_of(program, k);
	Bits state = key.start;

	for (size_t at = 0; at < data.size(); at += 8) {
		const Bits stream = encrypt(key, state);

		for (unsigned i = 0; i < 8; i++) {
			unsigned byte = 0;

			for (unsigned j = 0; j < 8; j++)
				byte = byte << 1 | stream[8 * i + j];
			data[at + i] = static_cast<uint8_t>(data[at + i] ^ byte);
			for (unsigned j = 0; j < 8; j++)
				state[8 * i + j] = (data[at + i] >> (7 - j)) & 1;
		}
	}
}

void shuffle(std::span<const uint8_t> program, bool second, std::span<uint8_t> data)
{
	const uint32_t table = SHUFFLES + (data.size() == CHECK ? CHECK_SHUFFLES : 0) + (second ? SECOND_SHUFFLE : 0);
	std::array<uint8_t, BLOCK> moved{};

	for (uint32_t i = 0; i < data.size(); i++)
		moved[word(program, table + 4 * i)] = data[i];
	std::copy_n(moved.begin(), data.size(), data.begin());
}

uint16_t checksum(std::span<const uint8_t> program, std::span<const uint8_t> data)
{
	uint32_t sum = 0;

	for (const uint8_t b : data)
		sum += b + word(program, SUM_WEIGHTS + 4u * b);
	return static_cast<uint16_t>((word(program, SUM_BYTES + 4 * ((sum >> 8) & 0xFF)) & 0xFF) << 8 |
	                             (word(program, SUM_BYTES + 4 * (256 + (sum & 0xFF))) & 0xFF));
}

/* Every word of the table, for each key, below its limit */
bool fits(std::span<const uint8_t> program, const Table &table)
{
	for (unsigned k = 0; k < CIPHER_KEYS; k++)
		for (uint32_t i = 0; i < table.size; i++)
			if (word(program, table.addr + k * table.stride + 4 * i) >= table.limit)
				return false;
	return true;
}

} /* namespace */

Von4Card::Von4Card(std::span<const uint8_t> program) : m_program(program), m_valid(false)
{
	if (program.size() < KEYBOARD + KEY_ROWS * ROW_SIZE - BASE)
		return;
	for (uint32_t row = 0; row < KEY_ROWS; row++) {
		const uint8_t *const keys = program.data() + (KEYBOARD + row * ROW_SIZE - BASE);

		if (keys[ROW_KEYS] != 0 || !std::all_of(keys, keys + ROW_KEYS, [](uint8_t c) { return c > ' ' && c < 0x7F; }))
			return;
		m_keys.append(reinterpret_cast<const char *>(keys), row ? ROW_KEYS : ROW_KEYS - ARROWS);
	}
	for (const Table *table : { &IP, &FP, &SBOXES, &ROWS_A, &ROWS_B, &OUTS, &PC1, &PC2_32, &PC2_16, &TURNS })
		if (!fits(program, *table))
			return;
	for (const uint32_t second : { 0u, SECOND_SHUFFLE }) {
		for (uint32_t i = 0; i < BLOCK; i++)
			if (word(program, SHUFFLES + second + 4 * i) >= BLOCK)
				return;
		for (uint32_t i = 0; i < CHECK; i++)
			if (word(program, SHUFFLES + CHECK_SHUFFLES + second + 4 * i) >= CHECK)
				return;
	}
	m_valid = true;
}

std::optional<std::array<uint8_t, CardReader::CARD_SIZE>> Von4Card::make(std::string_view name) const
{
	if (!m_valid || name.size() > NAME_SIZE ||
	    std::any_of(name.begin(), name.end(), [this](char c) { return c != ' ' && m_keys.find(c) == std::string::npos; }) ||
	    std::all_of(name.begin(), name.end(), [](char c) { return c == ' '; }))
		return std::nullopt;

	/* The data of a new pilot (0C17B69C) where the encoder puts it in the
	 * block, 0 where nothing is said, the names those of the game's debug
	 * screen: name, spaces after; type 0 and ready 0 (the first VR of the
	 * list, TEMJIN); color 1; the word at 0x52, 1; the times of 11 stages,
	 * FFFFFF; energy 30; flag 0x800 (no code of four letters, as in a game
	 * room: 0C0AFABA); the word at 0x3C, 12; the one at 0xFC, 250; terome
	 * 5, the mark of the terminal (0C1974E6). */
	std::array<uint8_t, BLOCK> block{};

	std::fill_n(block.begin(), NAME_SIZE, ' ');
	std::copy(name.begin(), name.end(), block.begin());
	block[0x13] = 1;
	block[0x35] = 1;
	std::fill(block.begin() + 0x3B, block.begin() + 0x5C, 0xFF);
	block[0x61] = 30;
	block[0x65] = 0x08;
	block[0x67] = 12;
	block[0x7A] = 250;
	block[0x7B] = 5;

	/* Its checksum over the block filled with FF, then the check */
	std::fill(block.begin() + 0x7C, block.end(), 0xFF);

	const uint16_t sum = checksum(m_program, block);

	block[0x7C] = static_cast<uint8_t>(sum >> 8);
	block[0x7D] = static_cast<uint8_t>(sum);

	std::array<uint8_t, CHECK> check;
	size_t n = 0;

	for (const auto &range : CHECKED)
		for (unsigned i = range.first; i <= range.last; i++)
			check[n++] = block[i];
	cipher(m_program, 3, check);
	shuffle(m_program, false, check);
	cipher(m_program, 4, check);
	shuffle(m_program, true, check);
	cipher(m_program, 5, check);
	std::copy(check.begin(), check.end(), block.end() - CHECK);

	cipher(m_program, 0, block);
	shuffle(m_program, false, block);
	cipher(m_program, 1, block);
	shuffle(m_program, true, block);
	cipher(m_program, 2, block);

	/* The 207 bytes of its write (0C180262): tracks 1 and 2, then the mark
	 * of the game, the rest, and what the terminal leaves after it (the
	 * game reads none of these 7) */
	static constexpr char MARK[] = "SEGABDX0";
	static constexpr uint8_t LEFT[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	std::array<uint8_t, CardReader::DATA_SIZE> data;
	auto at = std::copy_n(block.begin(), 2 * CardReader::TRACK_DATA, data.begin());

	at = std::copy_n(MARK, sizeof(MARK) - 1, at);
	at = std::copy(block.begin() + 2 * CardReader::TRACK_DATA, block.end(), at);
	std::copy(std::begin(LEFT), std::end(LEFT), at);
	return CardReader::card_of(data);
}

} /* namespace odin */
