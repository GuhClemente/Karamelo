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
 * Valkyrie
 * Copyright (C) 2011, 2012, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/boards/hikaru/rom_board.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "odin/common/bytes.hpp"

/*
 * Rom Board (ROMBD)
 * =================
 *
 * There are two known types of ROM board (which are neatly documented in
 * the MAME hikaru driver.) The EPROM/MASKROM external BUS address can be
 * recovered from information at offset +13C of the EPROM data.
 *
 * This is what this data looks like (the tables of the other games are
 * read the same way):
 *
 *	AIRTRIX (Type 1)		ICs	Size
 *	================		===	====
 *	
 *	0003 fee8 c889 97c2 620c	29,30	2 x 4MB
 *	ffff 0000 0000 0000 0000	 		
 *	ffff 0000 0000 0000 0000	 		
 *	ffff 0000 0000 0000 0000	 		
 *	0005 b9a5 9e67 a52a bce0	37,38	2 x 16MB
 *	ffff 0000 0000 0000 0000	 
 *	0005 dabb b621 4bd4 5e6b	41,42	2 x 16MB
 *	ffff 0000 0000 0000 0000	 
 *	0005 0d06 ad63 790f a27e	45,46	2 x 16MB
 *	ffff 0000 0000 0000 0000	 
 *	0005 bdbb 4f01 14a7 6a4e	49,50	2 x 16MB
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	
 * The mapping from IC numbers to entries has been determined by matching
 * the CRCs in the entries and the CRC values reported in the MAME driver.
 *
 * Each entry (10 bytes) can be defined like this:
 *
 *	struct {
 *		uint16_t log_size;
 *		uint16_t bytesum_lo;
 *		uint16_t wordsum_lo;
 *		uint16_t bytesum_hi;
 *		uint16_t wordsum_hi;
 *	} rom_layout[4+16];
 *
 * and describes the size and CRC for an IC pair: the first four entries
 * describe the EPROMs, the remaining 16 the MASKROMs. If the first word
 * is FFFF, those ICs are not populated.
 *
 * Given the i'th entry, the size and location of an IC (pair) can be
 * computed as follows (see @0C004ED2):
 *
 *	base = samurai_tag_at (0x18000000) ? 8 :
 *	       samurai_tag_at (0x10000000) ? 0 : 0;
 *	
 *	size = 1*MB << rom_layout[i].log_size
 *	
 *	if (i < 4)
 *		///////////////////////////////////////////
 *		// EPROM
 *		// SAMURAI at 10000000 -> base = 0 -> EPROM at 10,11,12,13
 *		// SAMURAI at 18000000 -> base = 8 -> EPROM at 18,19,1A,1B
 *		// each section is 4 or 8 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base + 0x10 + i
 *
 *	else if ((_0C00F01C & C) != 8)
 *		///////////////////////////////////////////
 *		// MASKROM, type 1
 *		// SAMURAI at 10000000 -> base = 0 -> MASKROM at 20,21,22,23,...
 *		// SAMURAI at 18000000 -> base = 8 -> MASKROM at 30,31,32,33,...
 *		// each section is 16 or 32 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base * 2 + 0x1C + i 
 *
 *	else
 *		///////////////////////////////////////////
 *		// MASKROM, type 2
 *		// SAMURAI at 10000000 -> base = 0 -> MASKROM at 20,22,24,26,...
 *		// SAMURAI at 18000000 -> base = 8 -> MASKROM at 30,32,34,36,...
 *		// each section is 16 or 32 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base * 2 + 0x20 + (i - 4) * 2
 *	
 *	bus_addr = bank >> 24
 *	if (size < 8*MB)
 *		bus_addr += 4*MB
 *
 * Here base is determined by the bootrom (see @0C00B834) by looking for
 * the 'SAMURAI' string at both BUS addresses 18000000 and 10000000. If
 * the string is found at bank 18, then the base is 8; it's 0 otherwise.
 *
 * What IC is mapped to what entry is determined by bits 2 and 3 of 0C00F01C.
 * See @0C004B32.
 */

namespace odin {

namespace {

constexpr uint32_t MB = 1024 * 1024;

/* What a page without anything reads: the BOOTROM reads the pages 10 to 1B
 * one after the other, the page of the EEPROM with them, to find where the
 * EPROMs are, and two pages that both read zero would look alike. */
constexpr uint64_t NOTHING = 0xDEADBEEFDEADBEEFull;

uint64_t
low_bytes(uint64_t value, unsigned size)
{
	return (size >= 8) ? value : (value & ((1ull << (size * 8)) - 1));
}

} /* namespace */

RomBoard::Config
RomBoard::config_of(unsigned eprom_mb, std::optional<uint32_t> crypt_key)
{
	Config config;

	std::memset(&config, 0, sizeof(config));
	config.has_rom = eprom_mb != 0;
	if (config.has_rom) {
		/* Four pages of EPROMs, sixteen of mask ROMs */
		config.eprom_bank[0] = 0x10;
		config.eprom_bank[1] = 0x13;
		config.maskrom_bank[0] = 0x20;
		config.maskrom_bank[1] = 0x2F;
		config.eprom_bank_size = eprom_mb;
		config.has_5881 = crypt_key.has_value();
		config.crypt_key = crypt_key.value_or(0);
	}
	config.eeprom_bank = 0x14;
	return config;
}

RomBoard::RomBoard(const Config &config, std::span<const uint8_t> eprom, std::span<const uint8_t> maskrom,
                   std::optional<Actel> actel, std::function<void(const char *)> error)
	: m_config(config), m_eprom(eprom), m_maskrom(maskrom), m_error(std::move(error))
{
	/* Two 8-bit SRAMs, 32 KB each, on both boards MAME draws (CY7C1399 on
	 * the 837-14140, CY7C199 on the 837-13403), at 0A000000 (MAME
	 * localbus_map_5881). HEURISTIC: also on the boards whose 315-5881 is
	 * not fitted (BRAVEFF, VON4): the probe of the BOOTROM then finds the
	 * EPROMs from IC29, as their names are; not on the board of SGNASCAR,
	 * whose FPGA answers there. */
	if (m_config.has_rom && !actel)
		m_local_ram.assign(LOCAL_RAM_SIZE, 0);
	if (m_config.has_5881) {
		m_crypt = std::make_unique<Crypt315_5881>(m_config.crypt_key);
		/* The chip reads its input from the RAM, one byte-swapped word
		 * at a time (MAME hikaru.cpp prot_ram_r). */
		m_crypt->set_read([this](uint32_t addr) {
			return be16(&m_local_ram[(addr & 0x7FFF) * 2]);
		});
	}
	/* 834-14125 ROM board: Actel A54SX32 stamped 317-0283-COM. The key is
	 * proven by the game: the chip then gives the text SGNASCAR compares
	 * it with. */
	if (actel) {
		m_fpga = std::make_unique<ActelM1>(actel->key, actel->id);
		m_fpga->set_read([this](uint32_t addr) -> uint8_t {
			return (addr < m_maskrom.size()) ? m_maskrom[addr] : 0;
		});
	}
}

/* Field by field, into zeros: the bytes between the fields are saved too. */
void
RomBoard::save(Config &out) const
{
	std::memset(&out, 0, sizeof(out));
	out.has_rom = m_config.has_rom;
	out.eprom_bank[0] = m_config.eprom_bank[0];
	out.eprom_bank[1] = m_config.eprom_bank[1];
	out.maskrom_bank[0] = m_config.maskrom_bank[0];
	out.maskrom_bank[1] = m_config.maskrom_bank[1];
	out.eeprom_bank = m_config.eeprom_bank;
	out.eprom_bank_size = m_config.eprom_bank_size;
	out.unused_size = m_config.unused_size;
	out.unused_flag = m_config.unused_flag;
	out.has_5881 = m_config.has_5881;
	out.crypt_key = m_config.crypt_key;
	out.unpopulated_logged = m_config.unpopulated_logged;
}

void
RomBoard::reset()
{
	std::fill(m_local_ram.begin(), m_local_ram.end(), 0);
	if (m_fpga)
		m_fpga->reset();
}

uint64_t
RomBoard::read_guard(uint32_t offs, unsigned size, bool wide)
{
	if (!m_local_ram.empty() && offs <= 0xFFFF) {
		/* The RAM is 16 bits wide: a 32-bit access on a wide local bus
		 * reaches its low half only, and the high half reads zero
		 * (HEURISTIC). */
		const unsigned reached = (wide && size == 4) ? 2 : size;

		return offs + reached <= LOCAL_RAM_SIZE ? load_bytes(&m_local_ram[offs], reached) : 0;
	}
	if (m_crypt && offs >= 0x10000 && offs <= 0x1000F)
		return low_bytes(m_crypt->bus_read(offs & 0xF, size), size);
	if (m_fpga && offs <= 0x13)
		return low_bytes(m_fpga->bus_read(offs, size), size);
	return 0;
}

bool
RomBoard::guarded(uint32_t offs) const
{
	return (!m_local_ram.empty() && offs <= 0xFFFF) || (m_crypt && offs >= 0x10000 && offs <= 0x1000F) ||
	       (m_fpga && offs <= 0x13);
}

void
RomBoard::write_guard(uint32_t offs, unsigned size, uint64_t value, bool wide)
{
	if (!m_local_ram.empty() && offs <= 0xFFFF) {
		const unsigned reached = (wide && size == 4) ? 2 : size;

		if (offs + reached <= LOCAL_RAM_SIZE)
			store_bytes(&m_local_ram[offs], reached, value);
	} else if (m_crypt && offs >= 0x10000 && offs <= 0x1000F) {
		m_crypt->bus_write(offs & 0xF, size, static_cast<uint32_t>(value));
	} else if (m_fpga && offs <= 0x13) {
		m_fpga->bus_write(offs, size, static_cast<uint32_t>(value));
	}
}

/* A read of a set of ROMs at an offset in it. Past its end is an empty
 * socket, which reads zero (HEURISTIC). The games probe every socket at
 * boot (the routine of the BOOTROM at 0x7984, which they copy: von4
 * 0C713C84, sgnascar 0C070A24: pages 10-1B, 254 bytes each against the
 * +4MB mirror), so an empty one is read hundreds of times: it is told
 * once a page. */
uint64_t
RomBoard::read_rom(std::span<const uint8_t> rom, uint32_t offs, unsigned size, uint32_t bus_addr)
{
	uint64_t value = 0;

	if (offs < rom.size()) {
		if (offs + size <= rom.size())
			value = load_bytes(&rom[offs], size);
		return value;
	}

	const uint32_t bank = bus_addr >> 24;
	const uint64_t bit = 1ull << ((bank - 0x10) & 63);

	if (!(m_config.unpopulated_logged & bit)) {
		char text[96];

		m_config.unpopulated_logged |= bit;
		std::snprintf(text, sizeof(text), "ROMBD unpopulated socket: bank %02X reads as zero (first R%u %08X)",
		              bank, size * 8, bus_addr);
		if (m_error)
			m_error(text);
	}
	return 0;
}

uint64_t
RomBoard::read(uint32_t bus_addr, unsigned size)
{
	const uint32_t bank = bus_addr >> 24;
	const uint32_t offs = bus_addr & 0xFFFFFF;

	/* The EEPROM: bit 0 is SDA (MAME hikaru.cpp:907) */
	if (bank == m_config.eeprom_bank)
		return m_eeprom.sda_read() ? 1 : 0;

	if (!m_config.has_rom)
		return low_bytes(NOTHING, size);

	if (bank >= m_config.eprom_bank[0] && bank <= m_config.eprom_bank[1]) {
		/* A page for each pair of EPROMs, of 2 or 4 MB each */
		const uint32_t num = bank - m_config.eprom_bank[0];
		const uint32_t bank_size = (m_config.eprom_bank_size == 2) ? 4 * MB : 8 * MB;

		return read_rom(m_eprom, (offs & (bank_size - 1)) + num * bank_size, size, bus_addr);
	}
	if (bank >= m_config.maskrom_bank[0] && bank <= m_config.maskrom_bank[1]) {
		/* Sixteen pages of 16 MB, whatever the size of the ROMs */
		const uint32_t num = bank - m_config.maskrom_bank[0];

		return read_rom(m_maskrom, offs + num * 16 * MB, size, bus_addr);
	}
	return low_bytes(NOTHING, size);
}

std::span<const uint8_t>
RomBoard::plain(uint32_t bus_addr) const
{
	const uint32_t bank = bus_addr >> 24;
	const uint32_t offs = bus_addr & 0xFFFFFF;
	std::span<const uint8_t> rom;
	uint32_t page_size = 0, at = 0;

	if (bank == m_config.eeprom_bank || !m_config.has_rom)
		return {};
	if (bank >= m_config.eprom_bank[0] && bank <= m_config.eprom_bank[1]) {
		page_size = (m_config.eprom_bank_size == 2) ? 4 * MB : 8 * MB;
		at = (offs & (page_size - 1)) + (bank - m_config.eprom_bank[0]) * page_size;
		rom = m_eprom;
	} else if (bank >= m_config.maskrom_bank[0] && bank <= m_config.maskrom_bank[1]) {
		page_size = 16 * MB;
		at = offs + (bank - m_config.maskrom_bank[0]) * page_size;
		rom = m_maskrom;
	} else {
		return {};
	}
	const size_t end = std::min<size_t>(at - at % page_size + page_size, rom.size());

	return at < end ? rom.subspan(at, end - at) : std::span<const uint8_t>();
}

bool
RomBoard::write(uint32_t bus_addr, unsigned, uint64_t value)
{
	if ((bus_addr >> 24) != m_config.eeprom_bank)
		return false;
	/* Bit 0 SDA, 1 SCL, 2 /CS, 3 /RST (MAME hikaru.cpp:909) */
	m_eeprom.set_pins(!(value & 4), !(value & 8), (value >> 1) & 1, value & 1);
	return true;
}

} /* namespace odin */
