// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * Sega Naomi M1 board stream decoder, from MAME src/mame/sega/naomim1,
 * rewritten for Odin. Odin is GPL-3.0; this file keeps its
 * BSD-3-Clause licence and copyright holders.
 */

#include "odin/machine/chips/actel_m1.hpp"

#include "odin/common/bytes.hpp"

namespace odin {

ActelM1::ActelM1(uint32_t key, uint16_t id)
	: m_key(swap32(key)),
	  m_id(id)
{
	reset();
}

void ActelM1::reset()
{
	m_dict.fill(0);
	m_last.fill(0);
	m_running = false;
	m_ended = false;
	m_bits = 0;
	m_num_bits = 0;
	m_rom_address = 0;
	m_num_out = 0;
	m_control = 0;
	m_scratch = 0;
}

uint32_t ActelM1::read(uint32_t offset) const
{
	switch (offset) {
	case REG_CONTROL:
		return m_control;
	case REG_STATUS:
		return m_running ? STATUS_READY : 0;
	case REG_ID:
		return m_id;
	case REG_SCRATCH:
		return m_scratch;
	default:
		return 0;
	}
}

void ActelM1::write(uint32_t offset, uint32_t value)
{
	switch (offset) {
	case REG_CONTROL:
		if ((value & CONTROL_START) && !(m_control & CONTROL_START)) {
			start();
		} else if (!(value & CONTROL_START)) {
			m_running = false;
		}
		m_control = value;
		break;
	case REG_SCRATCH:
		m_scratch = value;
		break;
	default:
		break;
	}
}

void ActelM1::start()
{
	m_running = true;
	m_ended = false;
	m_bits = 0;
	m_num_bits = 0;
	m_rom_address = 0;
	m_num_out = 0;
	m_last.fill(0);
	for (uint8_t &entry : m_dict) {
		entry = static_cast<uint8_t>(take(8));
	}
}

/* The bytes of a ROM word, mixed and xored with the key. */
uint32_t ActelM1::next_word()
{
	uint8_t b[4] = {0, 0, 0, 0};
	if (m_read) {
		for (uint32_t i = 0; i < 4; i++) {
			b[i] = m_read(m_rom_address + i);
		}
	}
	m_rom_address += 4;
	return m_key ^ ((static_cast<uint32_t>(b[1] ^ b[3]) << 24) |
	                (static_cast<uint32_t>(b[0] ^ b[2]) << 16) |
	                (static_cast<uint32_t>(b[1]) << 8) | b[0]);
}

uint32_t ActelM1::look(uint32_t count)
{
	if (count > m_num_bits) {
		m_bits = (m_bits << 32) | next_word();
		m_num_bits += 32;
	}
	return static_cast<uint32_t>(m_bits >> (m_num_bits - count)) & ((1u << count) - 1);
}

uint32_t ActelM1::take(uint32_t count)
{
	const uint32_t value = look(count);
	m_num_bits -= count;
	return value;
}

/* One prefix code: 00 + 2 bits (0 escapes to a raw byte), 010 + 2,
 * 011 + 3, 10 + 5, 11 + 6; the last entry ends the stream. */
bool ActelM1::next_code(uint8_t &byte)
{
	uint32_t index;

	switch (look(3)) {
	case 0:
	case 1:
		m_num_bits -= 2;
		index = take(2);
		byte = index ? m_dict[index] : static_cast<uint8_t>(take(8));
		return true;
	case 2:
		m_num_bits -= 3;
		byte = m_dict[take(2) + 4];
		return true;
	case 3:
		m_num_bits -= 3;
		byte = m_dict[take(3) + 8];
		return true;
	case 4:
	case 5:
		m_num_bits -= 2;
		byte = m_dict[take(5) + 16];
		return true;
	default:
		m_num_bits -= 2;
		index = take(6) + 48;
		if (index == DICT_SIZE) {
			return false;
		}
		byte = m_dict[index];
		return true;
	}
}

/* Past the end of the file the chip of MAME hands zeroes. */
uint8_t ActelM1::next_byte()
{
	uint8_t byte = 0;

	if (!m_running || m_ended) {
		return 0;
	}
	if (!next_code(byte)) {
		m_ended = true;
		return 0;
	}
	if ((m_dict[0] & 0x40) && m_num_out >= 2) {
		byte = static_cast<uint8_t>(m_last[m_num_out & 1] - byte);
	}
	m_last[m_num_out & 1] = byte;
	m_num_out++;
	return byte;
}

uint32_t ActelM1::bus_read(uint32_t offset, unsigned size)
{
	const uint32_t reg = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	uint32_t value = 0;

	if (reg == REG_DATA) {
		for (unsigned i = 0; i < size && i < 4; i++) {
			value |= static_cast<uint32_t>(next_byte()) << (i * 8);
		}
		return value;
	}
	value = read(reg) >> shift;
	return (size >= 4) ? value : (value & ((1u << (size * 8)) - 1));
}

void ActelM1::bus_write(uint32_t offset, unsigned size, uint32_t value)
{
	const uint32_t reg = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	const uint32_t mask = (size >= 4) ? 0xFFFFFFFFu : (((1u << (size * 8)) - 1) << shift);

	write(reg, (read(reg) & ~mask) | ((value << shift) & mask));
}

void ActelM1::save(ActelM1State &out) const
{
	out.dict = m_dict;
	out.last = m_last;
	out.running = m_running;
	out.ended = m_ended;
	out.bits = m_bits;
	out.num_bits = m_num_bits;
	out.rom_address = m_rom_address;
	out.num_out = m_num_out;
	out.control = m_control;
	out.scratch = m_scratch;
}

void ActelM1::load(const ActelM1State &in)
{
	m_dict = in.dict;
	m_last = in.last;
	m_running = in.running != 0;
	m_ended = in.ended != 0;
	m_bits = in.bits;
	m_num_bits = in.num_bits;
	m_rom_address = in.rom_address;
	m_num_out = in.num_out;
	m_control = in.control;
	m_scratch = in.scratch;
}

} /* namespace odin */
