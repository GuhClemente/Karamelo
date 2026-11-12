// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * Sega Naomi M1 board stream decoder, from MAME src/mame/sega/naomim1,
 * rewritten for Odin. Odin is GPL-3.0; this file keeps its
 * BSD-3-Clause licence and copyright holders.
 */

#ifndef ODIN_ACTEL_M1_HPP
#define ODIN_ACTEL_M1_HPP

#include <array>
#include <cstdint>
#include <functional>

namespace odin {

/* Security FPGA of the SGNASCAR ROM board (834-14125, Actel A54SX32
 * stamped 317-0283-COM): it reads an enciphered, packed file out of
 * the mask ROMs and hands the plain bytes to the game. The stream is
 * the one of the Naomi M1 carts (MAME naomim1.cpp): 32-bit words
 * mixed and xored with the key of the board, then 111 dictionary bytes
 * followed by prefix codes, the output optionally a difference with
 * the byte two places back.
 *
 * The game asks for the 208 bytes at the start of IC19/IC20 at each
 * change of scene and compares them with its own copy (0C012E98); a
 * mismatch adds 20 to the weight of the previous picture in the 181
 * blend, up to 252. With the key 56DEDF33 the chip gives that text.
 *
 * Registers at local bus 0x0A000000, as the game uses them:
 *   +0x00  r/w   control: bit 31 starts the stream, 29 set with it
 *   +0x04  read  status: bit 31 once the data is there
 *   +0x08  read  ID of the board in the low word (0x4252)
 *   +0x0C  r/w   keeps what is written
 *   +0x10  read  next four plain bytes, first byte lowest
 *
 * The stream starts at the first byte of the mask ROMs: no address is
 * ever written (0C012EF4 does not use the 0x114 its caller gives it), and
 * from there the chip gives the text of the game. The status sets bit 15
 * too, which the debug function at 0C0342A4 (table at 0C1AE18C) waits
 * for in 16 bits. Not known, and read by no code of the game: the other
 * bits of the status (0 here), what bits 30, 1 and 0 of the control do
 * (the game keeps them, 0C012F5A). */

/* Everything the chip keeps between two accesses. Plain data. */
struct ActelM1State {
	std::array<uint8_t, 111> dict;
	std::array<uint8_t, 2> last;
	uint8_t running;
	uint8_t ended;
	uint8_t padding[5];
	uint64_t bits;
	uint32_t num_bits;
	uint32_t rom_address;
	uint32_t num_out;
	uint32_t control;
	uint32_t scratch;
};

class ActelM1 {
public:
	/* Called with a byte offset in the mask ROMs. */
	using ReadFn = std::function<uint8_t(uint32_t addr)>;

	enum : uint32_t {
		REG_CONTROL = 0x00,
		REG_STATUS = 0x04,
		REG_ID = 0x08,
		REG_SCRATCH = 0x0C,
		REG_DATA = 0x10
	};

	ActelM1(uint32_t key, uint16_t id);

	void reset();

	void set_read(ReadFn read) { m_read = std::move(read); }

	/* 32-bit registers; `offset` is a multiple of four. Reading the
	 * data register does not move the stream: next_byte() does. */
	uint32_t read(uint32_t offset) const;
	void write(uint32_t offset, uint32_t value);
	uint8_t next_byte();

	/* An access of the bus, of 1, 2 or 4 bytes at any byte of a
	 * register. A read of the data register takes that many bytes of
	 * the stream, the first one lowest. */
	uint32_t bus_read(uint32_t offset, unsigned size);
	void bus_write(uint32_t offset, unsigned size, uint32_t value);

	void save(ActelM1State &out) const;
	void load(const ActelM1State &in);

private:
	static constexpr uint32_t CONTROL_START = 0x80000000u;
	static constexpr uint32_t STATUS_READY = 0x80008000u;
	static constexpr uint32_t DICT_SIZE = 111;

	void start();
	uint32_t next_word();
	uint32_t look(uint32_t count);
	uint32_t take(uint32_t count);
	bool next_code(uint8_t &byte);

	uint32_t m_key;
	uint16_t m_id;
	ReadFn m_read;

	std::array<uint8_t, DICT_SIZE> m_dict;
	std::array<uint8_t, 2> m_last;
	bool m_running;
	bool m_ended;
	uint64_t m_bits;
	uint32_t m_num_bits;
	uint32_t m_rom_address;
	uint32_t m_num_out;
	uint32_t m_control;
	uint32_t m_scratch;
};

} /* namespace odin */

#endif /* ODIN_ACTEL_M1_HPP */
