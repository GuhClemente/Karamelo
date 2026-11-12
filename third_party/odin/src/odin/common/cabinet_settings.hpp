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

#ifndef ODIN_CABINET_SETTINGS_HPP
#define ODIN_CABINET_SETTINGS_HPP

#include <cstddef>
#include <cstdint>
#include <span>

/* The settings a cabinet keeps without power that a link of cabinets needs,
 * written as the menus of the BOOTROM and of the games write them: which
 * cabinet is the master, and the seat of each. */
namespace odin::cabinet {

/* The CRC of the BOOTROM on the settings it keeps in the 93C46 of the MIE
 * (0C007504), which the games also put on their block of the backup RAM
 * (swracer 0C010F48): CRC-16 0x1021 in the high half of a 32-bit register
 * that starts at 0x01A18104 and takes each byte in its low bits, then
 * turns 8 more times. */
uint16_t crc(std::span<const uint8_t> bytes);

/* The 93C46 of the MIE: two copies of 64 bytes, each 62 bytes of settings
 * then their CRC, low byte first. */
constexpr size_t MIE_COPY = 64;
constexpr size_t MIE_SETTINGS = 62;
/* SYSTEM ASSIGNMENTS, COMMUNICATION MODE: 1 master, 2 slave */
constexpr size_t COMMUNICATION_MODE = 9;
constexpr unsigned MASTER = 1;
constexpr unsigned SLAVE = 2;

/* Bits `shift` to `shift + bits - 1` of byte `at` of the settings take
 * `value`, in both copies, with their CRC again; false (nothing changed)
 * when the chip is not 128 bytes or `at` is not in the settings */
bool set_mie(std::span<uint8_t> chip, size_t at, unsigned shift, unsigned bits, unsigned value);

/* The block of settings of a game in the backup RAM (swracer 0C010D80,
 * 0C010EFC): at 3F8 the game's id, then a word, the size of the block
 * above its CRC (low half first); the block at 400, a copy of it right
 * after. */
constexpr size_t BACKUP_HEAD = 0x3F8;
constexpr size_t BACKUP_BLOCK = 0x400;

/* The size of the block, 0 when there is none whose CRC is right */
size_t backup_block(std::span<const uint8_t> bram);
/* A block whose CRC is right and whose copy is the same, as the game
 * leaves them between its writes (the files its menus write) */
bool backup_at_rest(std::span<const uint8_t> bram);
/* As set_mie, byte `at` of the block, in both copies, with its CRC again;
 * false (nothing changed) when there is no block or `at` is not in it */
bool set_backup(std::span<uint8_t> bram, size_t at, unsigned shift, unsigned bits, unsigned value);

} /* namespace odin::cabinet */

#endif /* ODIN_CABINET_SETTINGS_HPP */
