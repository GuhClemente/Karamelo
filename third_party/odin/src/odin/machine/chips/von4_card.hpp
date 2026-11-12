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

#ifndef ODIN_VON4_CARD_HPP
#define ODIN_VON4_CARD_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "odin/machine/chips/card_reader.hpp"

namespace odin {

/* The card of a new pilot of Virtual-On, as the terminal of the game makes it
 * (VIRTUAL-ON FORCE TERMINAL): the name the player types on its keyboard, the
 * first VR it shows, all else as the terminal sets it, encrypted as the game
 * encrypts it. The tables are those of the program of the game, read where it
 * keeps them. */
class Von4Card {
public:
	static constexpr size_t NAME_SIZE = 8;

	/* `program`: the EPROMs of the game as its bus sees them
	 * (Hikaru::program()), kept by the caller */
	explicit Von4Card(std::span<const uint8_t> program);

	/* The program has the tables where von4 keeps them */
	bool valid() const { return m_valid; }
	/* The characters of the keyboard of the terminal, the space aside */
	const std::string &keys() const { return m_keys; }
	/* The card of a pilot of that name: up to NAME_SIZE characters of
	 * keys() or spaces, one of them not a space. Nothing for another name,
	 * or without the tables. */
	std::optional<std::array<uint8_t, CardReader::CARD_SIZE>> make(std::string_view name) const;

private:
	std::span<const uint8_t> m_program;
	bool m_valid;
	std::string m_keys;
};

} /* namespace odin */

#endif /* ODIN_VON4_CARD_HPP */
