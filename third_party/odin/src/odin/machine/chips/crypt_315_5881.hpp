// license:BSD-3-Clause
// copyright-holders:Andreas Naive, Olivier Galibert, David Haywood
/*
 * Sega 315-5881 decryption chip, from MAME
 * src/mame/sega/315-5881_crypt, rewritten for Odin. Odin is GPL-3.0;
 * this file keeps its BSD-3-Clause licence and copyright holders.
 */

#ifndef ODIN_CRYPT_315_5881_HPP
#define ODIN_CRYPT_315_5881_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>

namespace odin {

/* 315-5881 ("M2 crypt" of the Naomi and Hikaru ROM boards): a stream
 * cipher in counter mode over a 16-bit block cipher made of two
 * 4-round Feistel networks, keyed by a per-game key, a 16-bit sequence
 * key and the word counter. On the Hikaru the ciphertext is the 64 KB
 * protected RAM at local bus 0x0A000000 (M3: the game writes it there
 * and reads the plaintext back from the chip). Registers at
 * 0x0A010000, 16 bits each (MAME hikaru.cpp:965, 315-5881_crypt.cpp
 * iomap_le):
 *   +0x0  read   ready, bit 0 busy
 *   +0x8  write  address low word, restarts the stream
 *   +0xA  write  address high word, taken as 0 as in MAME
 *   +0xC  write  sequence key
 *   +0xE  read   next plaintext word
 *
 * Stream format as MAME has it: each substream opens with 18 header
 * bits, 1+9+8: a compressed flag, then A and B giving the substream
 * length (A+1)*(B+1) bytes. Those two bits shift the plaintext against
 * the cipher words: a decrypted word gives its 2 low bits to the
 * current plaintext word and its 14 high bits to the next one. A
 * compressed substream is a run-length / previous-line code read bit
 * by bit out of the decrypted words, A+1 bytes per line, B+1 lines.
 * The first read after an address write returns a dummy 0. */
/* Everything the chip keeps between two accesses. Plain data. */
struct Crypt315_5881State {
	std::array<uint8_t, 2> buffer;
	std::array<uint8_t, 512> line_buffer;
	std::array<uint8_t, 512> line_buffer_prev;
	std::array<uint8_t, 2> buffer2;
	uint32_t prot_cur_address;
	uint32_t dec_header;
	uint32_t buffer_pos;
	uint32_t line_buffer_pos;
	uint32_t line_buffer_size;
	uint32_t buffer_bit;
	uint32_t buffer_bit2;
	uint32_t block_size;
	uint32_t block_pos;
	uint32_t block_numlines;
	uint16_t subkey;
	uint16_t dec_hist;
	uint16_t buffer2a;
	uint8_t first_read;
	uint8_t enc_ready;
	uint8_t done_compression;
};

class Crypt315_5881 {
public:
	/* Called with the word counter; returns the ciphertext word. */
	using ReadFn = std::function<uint16_t(uint32_t addr)>;

	explicit Crypt315_5881(uint32_t key);

	void reset();

	void set_read(ReadFn read) { m_read = std::move(read); }

	uint16_t ready() const;
	void write_addr_lo(uint16_t data);
	void write_addr_hi(uint16_t data);
	void write_subkey(uint16_t data);
	uint16_t read_decrypt();

	/* The registers on the bus of the ROM board, 16 bits each. An
	 * access of another width or at another offset does nothing and is
	 * told once. */
	enum : uint32_t {
		REG_READY = 0x0,
		REG_ADDR_LO = 0x8,
		REG_ADDR_HI = 0xA,
		REG_SUBKEY = 0xC,
		REG_DECRYPT = 0xE
	};
	uint32_t bus_read(uint32_t offset, unsigned size);
	void bus_write(uint32_t offset, unsigned size, uint32_t value);

	/* Debug: the subkey and the addresses the program writes told there;
	 * only the tests set it */
	void set_log(FILE *out) { m_log = out; }

	void save(Crypt315_5881State &out) const;
	void load(const Crypt315_5881State &in);

private:
	/* MAME reads the stream two bytes at a time. */
	static constexpr uint32_t BUFFER_SIZE = 2;
	static constexpr uint32_t LINE_SIZE = 512;
	static constexpr uint32_t FLAG_COMPRESSED = 0x20000;

	void tell_unknown(char rw, uint32_t offset, unsigned size, uint32_t value);

	uint16_t do_decrypt();
	uint16_t get_decrypted_16();
	uint32_t get_compressed_bit();
	void enc_start();
	void enc_fill();
	void line_fill();

	uint32_t m_key;
	ReadFn m_read;
	FILE *m_log;
	bool m_unknown_told;

	bool m_first_read;
	bool m_enc_ready;
	uint32_t m_prot_cur_address;
	uint16_t m_subkey;
	uint16_t m_dec_hist;
	uint32_t m_dec_header;

	std::array<uint8_t, BUFFER_SIZE> m_buffer;
	std::array<uint8_t, LINE_SIZE> m_line_buffer;
	std::array<uint8_t, LINE_SIZE> m_line_buffer_prev;
	uint32_t m_buffer_pos;
	uint32_t m_line_buffer_pos;
	uint32_t m_line_buffer_size;
	uint32_t m_buffer_bit;
	uint32_t m_buffer_bit2;
	std::array<uint8_t, 2> m_buffer2;
	uint16_t m_buffer2a;

	uint32_t m_block_size;
	uint32_t m_block_pos;
	uint32_t m_block_numlines;
	bool m_done_compression;
};

} /* namespace odin */

#endif /* ODIN_CRYPT_315_5881_HPP */
