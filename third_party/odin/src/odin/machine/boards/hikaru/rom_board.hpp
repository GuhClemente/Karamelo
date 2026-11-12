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

#ifndef ODIN_ROM_BOARD_HPP
#define ODIN_ROM_BOARD_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "odin/machine/chips/actel_m1.hpp"
#include "odin/machine/chips/crypt_315_5881.hpp"
#include "odin/machine/chips/x76f100.hpp"

namespace odin {

/* The ROM board of a game, on the external bus of the Hikaru: its EPROMs
 * and mask ROMs in the pages 10 to 3F, its X76F100 EEPROM in a page of its
 * own, and in the page 0A what guards the game: a 315-5881 with its RAM,
 * the security FPGA of SGNASCAR, or the RAM alone. See rom_board.cpp for
 * the layouts. */
class RomBoard {
public:
	/* How the ROMs of a game sit on the bus. Plain data, saved with the
	 * states as it is. */
	struct Config {
		bool has_rom;
		unsigned eprom_bank[2];		/* first and last page */
		unsigned maskrom_bank[2];
		unsigned eeprom_bank;
		uint32_t eprom_bank_size;	/* of a ROM, in MB */
		/* Kept for the layout of the states */
		uint32_t unused_size;
		bool unused_flag;
		/* 315-5881 decryption chip and its 64 KB RAM */
		bool has_5881;
		uint32_t crypt_key;
		/* A bit for each page 10 to 4F: its empty socket was told */
		uint64_t unpopulated_logged;
	};

	/* The security FPGA of the board of SGNASCAR */
	struct Actel {
		uint32_t key;
		uint16_t id;
	};

	static constexpr size_t LOCAL_RAM_SIZE = 0x10000;

	/* The layout of a board whose EPROMs have `eprom_mb` MB (2 or 4), with
	 * a 315-5881 when its key is told; for 0 MB, no board: the BOOTROM
	 * alone. */
	static Config config_of(unsigned eprom_mb, std::optional<uint32_t> crypt_key);

	/* eprom, maskrom: the contents of the ROMs, owned by the caller.
	 * actel: the board has that FPGA. error: called with what a program
	 * did that has no answer here. */
	RomBoard(const Config &config, std::span<const uint8_t> eprom, std::span<const uint8_t> maskrom,
	         std::optional<Actel> actel, std::function<void(const char *)> error);

	/* The power-on state of what the board resets. */
	void reset();

	/* The page 0A of the bus, its local bus 32 bits wide or not (see
	 * MemCtl::wide_local_bus). A read that nothing answers gives zero. */
	uint64_t read_guard(uint32_t offs, unsigned size, bool wide);
	void write_guard(uint32_t offs, unsigned size, uint64_t value, bool wide);
	/* A chip of the board answers at that offset of the page. */
	bool guarded(uint32_t offs) const;
	/* The pages 10 to 3F: the EEPROM in its page, the ROMs in theirs,
	 * and what a page without anything reads. */
	uint64_t read(uint32_t bus_addr, unsigned size);
	/* The bytes read() gives from bus_addr on as they are, up to the end
	 * of the ROM or of its page; nothing where read() does more. */
	std::span<const uint8_t> plain(uint32_t bus_addr) const;
	/* True if the write was to the EEPROM: nothing else is written. */
	bool write(uint32_t bus_addr, unsigned size, uint64_t value);

	const Config &config() const { return m_config; }
	/* The layout as the states keep it, and back */
	void save(Config &out) const;
	void load(const Config &in) { m_config = in; }
	X76f100 &eeprom() { return m_eeprom; }
	/* Nothing on a board without the chip */
	Crypt315_5881 *crypt() { return m_crypt.get(); }
	ActelM1 *fpga() { return m_fpga.get(); }
	/* The RAM of the local bus, empty on a board without it */
	std::span<uint8_t> local_ram() { return m_local_ram; }

private:
	uint64_t read_rom(std::span<const uint8_t> rom, uint32_t offs, unsigned size, uint32_t bus_addr);

	Config m_config;
	std::span<const uint8_t> m_eprom, m_maskrom;
	std::function<void(const char *)> m_error;
	X76f100 m_eeprom;
	std::vector<uint8_t> m_local_ram;
	std::unique_ptr<Crypt315_5881> m_crypt;
	std::unique_ptr<ActelM1> m_fpga;
};

} /* namespace odin */

#endif /* ODIN_ROM_BOARD_HPP */
