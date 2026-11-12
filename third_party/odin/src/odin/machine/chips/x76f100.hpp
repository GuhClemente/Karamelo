// license:BSD-3-Clause
// copyright-holders:smf
/*
 * Xicor X76F100 secure serial EEPROM, from MAME
 * src/devices/machine/x76f100, rewritten for Odin. Odin is GPL-3.0;
 * this file keeps its BSD-3-Clause licence and copyright holder.
 */

#ifndef ODIN_X76F100_HPP
#define ODIN_X76F100_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace odin {

/* X76F100: 896 bits (112 bytes) of flash behind two 64-bit passwords,
 * one for reads and one for writes, on a two-wire bus (SCL, SDA) with
 * CS and RST. On the Hikaru it is the ROM board EEPROM (x76f100.ic85),
 * driven from the SH-4 at 0x14000000 (MAME hikaru.cpp:907): write bit 0
 * SDA, bit 1 SCL, bit 2 /CS, bit 3 /RST; read bit 0 SDA.
 *
 * Bus protocol as MAME has it: RST rising with CS low starts the
 * 4-byte response to reset, shifted out LSB first on SCL falling edges
 * and repeating. A START (SDA falling with SCL high) is followed by the
 * command byte, MSB first, then 8 password bytes, each answered with an
 * ACK (SDA low on the ninth clock). The host then polls with the byte
 * 0x55, possibly after another START: ACK when the password matched,
 * NAK (SDA high) otherwise. A read then shifts data bytes out, MSB
 * first, the host ACKing each one to get the next; a write takes 8
 * bytes and flushes them as one page. STOP is SDA rising with SCL high.
 *
 * Command byte: bit 7 set, bit 0 = 1 read / 0 write, bits 4-1 the
 * 8-byte page; 0xFC changes the write password and 0xFE the read
 * password, both after the write password. Eight wrong passwords in a
 * row erase the passwords and the data.
 *
 * Image, 0x84 bytes, in the MAME nvram order:
 *   0x00  4 bytes   response to reset (19 00 AA 55 when blank)
 *   0x04  8 bytes   write password
 *   0x0C  8 bytes   read password
 *   0x14  112 bytes data, 14 pages of 8 */
struct X76f100State;

class X76f100 {
public:
	static constexpr uint32_t RESPONSE_SIZE = 4;
	static constexpr uint32_t PASSWORD_SIZE = 8;
	static constexpr uint32_t PAGE_SIZE = 8;
	static constexpr uint32_t DATA_SIZE = 112;
	static constexpr uint32_t IMAGE_RESPONSE = 0x00;
	static constexpr uint32_t IMAGE_WRITE_PASSWORD = 0x04;
	static constexpr uint32_t IMAGE_READ_PASSWORD = 0x0C;
	static constexpr uint32_t IMAGE_DATA = 0x14;
	static constexpr uint32_t IMAGE_SIZE = IMAGE_DATA + DATA_SIZE;

	X76f100();

	void reset();

	/* Pin levels, CS and RST active low. Edges are detected here, the
	 * pins taken in the order CS, RST, SCL, SDA. */
	void set_pins(bool cs, bool rst, bool scl, bool sda);
	/* SDA as the chip drives it: released (high) while not selected. */
	bool sda_read() const;

	/* The whole image, in the layout above. `load` takes exactly
	 * IMAGE_SIZE bytes and returns false, keeping the current image,
	 * for any other size. */
	bool load(std::span<const uint8_t> image);

	void save(X76f100State &out) const;
	void load(const X76f100State &in);
	std::span<uint8_t> data() { return m_image; }


private:
	enum class State : uint8_t {
		STOP,
		RESPONSE_TO_RESET,
		LOAD_COMMAND,
		LOAD_PASSWORD,
		VERIFY_PASSWORD,
		READ_DATA,
		WRITE_DATA
	};

	void on_cs(bool level);
	void on_rst(bool level);
	void on_scl(bool level);
	void on_sda(bool level);
	void on_byte_in();
	void check_password();
	void store_page();
	void on_read_clock();

	std::span<uint8_t> response_to_reset();
	std::span<uint8_t> write_password();
	std::span<uint8_t> read_password();
	std::span<uint8_t> memory();
	std::span<uint8_t> password();
	void password_ok();
	std::optional<uint32_t> data_offset() const;

	std::array<uint8_t, IMAGE_SIZE> m_image;
	std::array<uint8_t, PASSWORD_SIZE> m_write_buffer;
	State m_state;
	bool m_cs, m_rst, m_scl, m_sdaw, m_sdar;
	uint8_t m_shift;
	uint8_t m_bit;
	uint32_t m_byte;
	uint8_t m_command;
	uint8_t m_password_retry_counter;
	bool m_is_password_accepted;
};

/* Everything the chip keeps between two pin changes. Plain data. */
struct X76f100State {
	std::array<uint8_t, X76f100::IMAGE_SIZE> image;
	std::array<uint8_t, X76f100::PASSWORD_SIZE> write_buffer;
	uint32_t byte;
	uint8_t state;
	uint8_t cs, rst, scl, sdaw, sdar;
	uint8_t shift;
	uint8_t bit;
	uint8_t command;
	uint8_t password_retry_counter;
	uint8_t is_password_accepted;
};

} /* namespace odin */

#endif /* ODIN_X76F100_HPP */
