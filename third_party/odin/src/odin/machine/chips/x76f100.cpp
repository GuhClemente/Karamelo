// license:BSD-3-Clause
// copyright-holders:smf
/*
 * Xicor X76F100 secure serial EEPROM, from MAME
 * src/devices/machine/x76f100, rewritten for Odin. Odin is GPL-3.0;
 * this file keeps its BSD-3-Clause licence and copyright holder.
 */

#include "odin/machine/chips/x76f100.hpp"

#include <algorithm>

namespace odin {

namespace {

enum : uint8_t {
	COMMAND_WRITE = 0x80,
	COMMAND_READ = 0x81,
	COMMAND_CHANGE_WRITE_PASSWORD = 0xFC,
	COMMAND_CHANGE_READ_PASSWORD = 0xFE,
	COMMAND_ACK_PASSWORD = 0x55
};

/* Wrong passwords in a row before the chip erases itself. */
constexpr uint8_t PASSWORD_RETRIES = 8;

/* One buffer takes both a password and a page. */
static_assert(X76f100::PAGE_SIZE == X76f100::PASSWORD_SIZE);
static_assert(X76f100::IMAGE_SIZE == 0x84);

} /* namespace */

X76f100::X76f100()
{
	/* Blank chip: default response to reset, no passwords. */
	m_image.fill(0);
	m_image[0] = 0x19;
	m_image[1] = 0x00;
	m_image[2] = 0xAA;
	m_image[3] = 0x55;
	reset();
}

/* The contents survive a reset. */
void X76f100::reset()
{
	m_write_buffer.fill(0);
	m_state = State::STOP;
	m_cs = m_rst = m_scl = m_sdaw = m_sdar = false;
	m_shift = 0;
	m_bit = 0;
	m_byte = 0;
	m_command = 0;
	m_password_retry_counter = 0;
	m_is_password_accepted = false;
}

bool X76f100::load(std::span<const uint8_t> image)
{
	if (image.size() != IMAGE_SIZE) {
		return false;
	}
	std::copy_n(image.begin(), IMAGE_SIZE, m_image.begin());
	return true;
}

void X76f100::set_pins(bool cs, bool rst, bool scl, bool sda)
{
	on_cs(cs);
	on_rst(rst);
	on_scl(scl);
	on_sda(sda);
}

bool X76f100::sda_read() const
{
	if (m_cs) {
		return true;
	}
	return m_sdar;
}

std::span<uint8_t> X76f100::response_to_reset()
{
	return std::span<uint8_t>(m_image).subspan(IMAGE_RESPONSE, RESPONSE_SIZE);
}

std::span<uint8_t> X76f100::write_password()
{
	return std::span<uint8_t>(m_image).subspan(IMAGE_WRITE_PASSWORD, PASSWORD_SIZE);
}

std::span<uint8_t> X76f100::read_password()
{
	return std::span<uint8_t>(m_image).subspan(IMAGE_READ_PASSWORD, PASSWORD_SIZE);
}

std::span<uint8_t> X76f100::memory()
{
	return std::span<uint8_t>(m_image).subspan(IMAGE_DATA, DATA_SIZE);
}

/* Either edge of CS drops the command in progress. Whether SDA floats
 * once deselected is not known; sda_read() gives it high while deselected,
 * as MAME does. */
void X76f100::on_cs(bool level)
{
	if (m_cs != level) {
		m_state = State::STOP;
		if (level) {
			m_sdar = false;
		}
	}
	m_cs = level;
}

void X76f100::on_rst(bool level)
{
	if (!m_rst && level && !m_cs) {
		m_state = State::RESPONSE_TO_RESET;
		m_bit = 0;
		m_byte = 0;
	}
	m_rst = level;
}

void X76f100::on_scl(bool level)
{
	if (!m_cs) {
		switch (m_state) {
		case State::STOP:
			break;

		case State::RESPONSE_TO_RESET:
			if (m_scl && !level) {
				if (m_bit == 0) {
					m_shift = response_to_reset()[m_byte];
				}
				m_sdar = (m_shift & 1) != 0;
				m_shift = static_cast<uint8_t>(m_shift >> 1);
				m_bit++;
				if (m_bit == 8) {
					m_bit = 0;
					m_byte++;
					if (m_byte == RESPONSE_SIZE) {
						m_byte = 0;
					}
				}
			}
			break;

		case State::LOAD_COMMAND:
		case State::LOAD_PASSWORD:
		case State::VERIFY_PASSWORD:
		case State::WRITE_DATA:
			/* As in MAME: taking the bit on the rising edge makes SDA
			 * change while SCL is high, which the bus does not allow,
			 * and neither device ids nor commands are validated. */
			if (!m_scl && level) {
				if (m_bit < 8) {
					m_shift = static_cast<uint8_t>((m_shift << 1) | (m_sdaw ? 1 : 0));
					m_bit++;
				} else {
					m_sdar = false;
					on_byte_in();
					m_bit = 0;
					m_shift = 0;
				}
			}
			break;

		case State::READ_DATA:
			if (!m_scl && level) {
				on_read_clock();
			}
			break;
		}
	}
	m_scl = level;
}

/* START and STOP conditions: SDA moving while SCL is high. */
void X76f100::on_sda(bool level)
{
	if (!m_cs && m_scl) {
		if (!m_sdaw && level) {
			m_state = State::STOP;
			m_sdar = false;
		}
		if (m_sdaw && !level) {
			switch (m_state) {
			case State::STOP:
				m_state = State::LOAD_COMMAND;
				break;
			case State::LOAD_PASSWORD:
				/* Would be the 0xC0 command; not handled as a
				 * command. */
				break;
			case State::READ_DATA:
				/* Continue reading? Not known. */
				break;
			default:
				break;
			}
			m_bit = 0;
			m_byte = 0;
			m_shift = 0;
			m_sdar = false;
		}
	}
	m_sdaw = level;
}

/* Ninth clock of a byte from the host: the byte is in and SDA is
 * already low (ACK). Only the password poll can turn that into a NAK. */
void X76f100::on_byte_in()
{
	switch (m_state) {
	case State::LOAD_COMMAND:
		m_command = m_shift;
		/* The command is not checked for validity. */
		m_state = State::LOAD_PASSWORD;
		break;

	case State::LOAD_PASSWORD:
		m_write_buffer[m_byte++] = m_shift;
		if (m_byte == PASSWORD_SIZE) {
			m_state = State::VERIFY_PASSWORD;
			check_password();
		}
		break;

	case State::VERIFY_PASSWORD:
		/* The poll byte is not handled as a command, and the real
		 * chip takes up to 10 ms before it answers. */
		if (m_shift == COMMAND_ACK_PASSWORD) {
			if (m_is_password_accepted) {
				password_ok();
			} else {
				m_sdar = true;
			}
		}
		break;

	case State::WRITE_DATA:
		/* No START before the poll leaves m_byte at 8 here; MAME then
		 * writes past its buffer. The bytes are dropped instead. */
		if (m_byte >= PAGE_SIZE) {
			break;
		}
		m_write_buffer[m_byte++] = m_shift;
		if (m_byte == PAGE_SIZE)
			store_page();
		break;

	default:
		break;
	}
}

/* The password just received against the one of the command. Compared
 * here and not on each 0x55 poll: the poll is repeated and would run the
 * retry counter over. */
void X76f100::check_password()
{
	const std::span<uint8_t> expected = password();

	m_is_password_accepted = std::equal(m_write_buffer.begin(), m_write_buffer.end(), expected.begin());
	if (!m_is_password_accepted) {
		m_password_retry_counter++;
		if (m_password_retry_counter >= PASSWORD_RETRIES) {
			std::ranges::fill(read_password(), 0);
			std::ranges::fill(write_password(), 0);
			std::ranges::fill(memory(), 0);
			m_password_retry_counter = 0;
		}
	}
}

/* A page received whole: a password, or the bytes of the memory */
void X76f100::store_page()
{
	if (m_command == COMMAND_CHANGE_WRITE_PASSWORD) {
		std::ranges::copy(m_write_buffer, write_password().begin());
	} else if (m_command == COMMAND_CHANGE_READ_PASSWORD) {
		std::ranges::copy(m_write_buffer, read_password().begin());
	} else {
		for (m_byte = 0; m_byte < PAGE_SIZE; m_byte++) {
			const std::optional<uint32_t> offset = data_offset();
			if (!offset)
				break;
			memory()[*offset] = m_write_buffer[m_byte];
		}
	}
	m_byte = 0;
}

/* SCL rising in a read: 8 data bits out, MSB first, then the host's
 * ACK (next byte) or NAK on the ninth clock. */
void X76f100::on_read_clock()
{
	if (m_bit < 8) {
		if (m_bit == 0) {
			const std::optional<uint32_t> offset = data_offset();
			if (offset) {
				m_shift = memory()[*offset];
			} else {
				m_shift = 0;
			}
		}
		m_sdar = (m_shift & 0x80) != 0;
		m_shift = static_cast<uint8_t>(m_shift << 1);
		m_bit++;
	} else {
		m_bit = 0;
		m_sdar = false;
		if (!m_sdaw) {
			m_byte++;
		}
	}
}

/* Which password the command is checked against. */
std::span<uint8_t> X76f100::password()
{
	if ((m_command & 0xE1) == COMMAND_READ) {
		return read_password();
	}
	return write_password();
}

void X76f100::password_ok()
{
	m_password_retry_counter = 0;
	if ((m_command & 0x81) == COMMAND_READ) {
		m_state = State::READ_DATA;
	} else if ((m_command & 0x81) == COMMAND_WRITE) {
		m_state = State::WRITE_DATA;
	} else {
		/* Bit 7 clear: left open in MAME. */
	}
}

/* Four page bits in the command but only 14 pages of data; what the
 * chip does for pages 14 and 15 is not verified. */
std::optional<uint32_t> X76f100::data_offset() const
{
	const uint32_t page = static_cast<uint32_t>((m_command >> 1) & 0x0F);
	const uint32_t offset = page * PAGE_SIZE + m_byte;
	if (offset >= DATA_SIZE) {
		return std::nullopt;
	}
	return offset;
}

void X76f100::save(X76f100State &out) const
{
	out.image = m_image;
	out.write_buffer = m_write_buffer;
	out.byte = m_byte;
	out.state = static_cast<uint8_t>(m_state);
	out.cs = m_cs;
	out.rst = m_rst;
	out.scl = m_scl;
	out.sdaw = m_sdaw;
	out.sdar = m_sdar;
	out.shift = m_shift;
	out.bit = m_bit;
	out.command = m_command;
	out.password_retry_counter = m_password_retry_counter;
	out.is_password_accepted = m_is_password_accepted;
}

void X76f100::load(const X76f100State &in)
{
	m_image = in.image;
	m_write_buffer = in.write_buffer;
	m_byte = in.byte;
	m_state = static_cast<State>(in.state);
	m_cs = in.cs != 0;
	m_rst = in.rst != 0;
	m_scl = in.scl != 0;
	m_sdaw = in.sdaw != 0;
	m_sdar = in.sdar != 0;
	m_shift = in.shift;
	m_bit = in.bit;
	m_command = in.command;
	m_password_retry_counter = in.password_retry_counter;
	m_is_password_accepted = in.is_password_accepted != 0;
}

} /* namespace odin */
