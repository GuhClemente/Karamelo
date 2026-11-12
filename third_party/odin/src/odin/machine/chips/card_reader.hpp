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

#ifndef ODIN_CARD_READER_HPP
#define ODIN_CARD_READER_HPP

#include "odin/machine/chips/mie.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace odin {

/* The Sanwa CRP-1231BR-10, the magnetic card reader and writer of Virtual-On
 * Force with its card dispenser (Sega 601-11082), on the line of the serial
 * block of the MIE through the 838-13661.
 *
 * Its program (crp1231br10_ver0107.ic2, an H8/3003, MAME's initd set) is
 * not run: the reader answers each command as that program does,
 * at once (the time its mechanism takes is not counted).
 *
 * Host frame: 02 LEN CMD 00 00 00 [params] 03 BCC, LEN counting the bytes
 * after it, BCC the XOR of LEN..ETX. The reader answers ACK (06) or NAK
 * (15); the command runs on the ENQ (05) that follows, whose reply is
 * 02 LEN CMD st1 st2 st3 [data] 03 BCC:
 *   st1  sensors, binary: bits 4-0 the five card sensors S5..S1 (bit 0 the
 *        entrance), bit 5 cards in the stocker, bit 6 shutter closed, bit
 *        7 shutter open;
 *   st2  '0', or what went wrong: '1' read error, 'Q'-'V' read error of
 *        some of the tracks, '2' write error, '3' transport;
 *   st3  '0' done, '2' refused, '3' busy, '4' waiting for a card, '5' no
 *        card in the stocker.
 * A reply already sent goes again on the next ENQ or NAK, st2 and st3 back
 * to '0' (st3 '3' kept), st1 as the sensors are then.
 *
 * Its player inserts the card and takes it back (choose, insert, take): a
 * read waits for it as long as it takes, and the game goes on after an
 * eject only once the reader sees no card (0C180E20). */
struct CardReaderState;

class CardReader : public MieSerialDevice {
public:
	/* A track as the program writes it on the card: FF, 69 bytes, FF,
	 * LRC (XOR of the 70 bytes after the first FF) */
	static constexpr uint32_t TRACK_DATA = 0x45;
	static constexpr uint32_t TRACK_SIZE = TRACK_DATA + 3;
	static constexpr uint32_t TRACKS = 3;
	/* The card file: the three tracks as on the card; a file of their 69
	 * bytes only (207, as YACardEmu keeps a card) is read as recorded */
	static constexpr uint32_t CARD_SIZE = TRACK_SIZE * TRACKS;
	static constexpr uint32_t DATA_SIZE = TRACK_DATA * TRACKS;
	static constexpr uint32_t FRAME_MAX = 256;
	/* Slots for the glyphs of command 7A */
	static constexpr uint8_t FONT_SLOTS = 0x14;

	enum : uint8_t {
		STX = 0x02,
		ETX = 0x03,
		ENQ = 0x05,
		ACK = 0x06,
		NAK = 0x15
	};

	/* The commands of the game; the program refuses any
	 * other it does not know */
	enum : uint8_t {
		CMD_INIT = 0x10,
		CMD_INIT_SAME = 0x1C,	/* taken as 10 */
		CMD_STATUS = 0x20,
		CMD_READ = 0x33,
		CMD_CANCEL = 0x40,
		CMD_WRITE = 0x53,
		CMD_FONT = 0x7A,
		CMD_PRINT = 0x7C,
		CMD_EJECT = 0x80,
		CMD_CLEAN = 0xA0,
		CMD_DISPENSE = 0xB0,
		CMD_RETRIES = 0xC1,
		CMD_SHUTTER = 0xD0,
		CMD_INIT_ZERO = 0xE0,	/* taken as 10, its parameter as '0' */
		CMD_STOP = 0xFF		/* the program stops: its watchdog resets the reader */
	};

	/* Where the card is: the sensors it covers, as the program counts its
	 * positions (287E) */
	enum Position : uint8_t {
		NONE, S1, S1S2, S1S2S3, S2S3, S2S3S4, S3S4, S3S4S5, S4S5, S5
	};

	/* Where the player's card is (the menu says it) */
	enum class Where : uint8_t { NO_CARD, IN_HAND, AT_SLOT, INSIDE };

	/* `card_file` holds the player's card, in their hand; empty: none */
	explicit CardReader(std::string card_file = std::string());

	/* The player, as at the cabinet: nothing goes in or out by itself.
	 * choose: the card of that file in hand (empty: none), while their
	 * card is not in the reader. insert: the card in hand pushed into the
	 * slot, the shutter open: it waits at the entrance (S1) until a read
	 * takes it in. take: the card at the slot (after an eject) pulled out,
	 * kept in its file; a new card from the stocker in `new_file`, which
	 * is then the player's. False, nothing done, when it cannot be. */
	bool choose(const std::string &card_file);
	bool insert();
	bool take(const std::string &new_file);
	Where where() const;
	const std::string &card_file() const { return m_card_file; }

	/* Power-up: the shutter closed, the stocker full, the boot reply ready.
	 * A card inside stays there. */
	void reset();

	/* MieSerialDevice: the line */
	void receive(uint8_t byte) override;
	bool has_byte() override;
	uint8_t take_byte() override;

	void save(CardReaderState &out) const;
	void load(const CardReaderState &in);

	/* For the tests: where the card is, the shutter, a full stocker; the
	 * three tracks as on the card */
	bool card_in_hand() const { return m_in_hand; }
	Position position() const { return m_position; }
	bool shutter_open() const { return m_shutter_open; }
	void set_stocker(bool full) { m_stocker_full = full; }
	std::span<uint8_t> card() { return m_card; }

	/* XOR of a frame slice, as the program checks it */
	static uint8_t frame_xor(std::span<const uint8_t> bytes);
	/* A new card as the stocker has them (CMD_DISPENSE) */
	static std::array<uint8_t, CARD_SIZE> blank_card();
	/* The card of the 69 bytes of each track, as the program writes them */
	static std::array<uint8_t, CARD_SIZE> card_of(std::span<const uint8_t, DATA_SIZE> data);
	/* Each track blank or whole: its marks and its LRC as the program
	 * writes them */
	static bool whole_card(std::span<const uint8_t, CARD_SIZE> card);
	/* A card into its file whole or not at all: through a temporary file
	 * (write_kept), the whole card the file held before kept beside it as
	 * <file>.bak; nothing for a card not whole, nor for the same card
	 * again. False, said to `log`, when it was not written. */
	static bool write_card(const std::string &path, std::span<const uint8_t, CARD_SIZE> card, FILE *log);

	/* Commands it does not answer as the program does, and bad frames,
	 * are logged once each to `out` */
	void set_log(FILE *out) { m_log = out; }
	/* One line per frame on the line, both ways */
	void set_trace(FILE *out) { m_trace = out; }

private:
	/* The end of a track after its data: the end mark, then the LRC of
	 * the data and that mark */
	static void seal_track(uint8_t *track);
	/* The exchange: a frame being received, a frame ACKed waiting for its
	 * ENQ, a read waiting for a card */
	enum Phase : uint8_t { IDLE, FRAME, ACKED, WAITING };

	void frame_complete();
	void run(std::span<const uint8_t> frame);
	bool second_frame(std::span<const uint8_t> frame);
	void read(std::span<const uint8_t> params);
	void write(std::span<const uint8_t> params);
	void eject();
	void reply(uint8_t cmd, uint8_t st2, uint8_t st3, std::span<const uint8_t> data = {});
	void refuse(uint8_t cmd) { reply(cmd, '0', '2'); }
	void send_reply();
	uint8_t status1() const;
	void send(uint8_t byte);
	void trace_frame(const char *dir, std::span<const uint8_t> bytes) const;
	void log_once(uint8_t cmd, const char *what);
	/* A card file read: missing or of another size, of the right size, or
	 * whole as well (whole_card) */
	enum class Read : uint8_t { NONE, NOT_WHOLE, WHOLE };
	Read read_card_file(const std::string &path, std::array<uint8_t, CARD_SIZE> &card) const;
	bool read_card(const std::string &path, std::array<uint8_t, CARD_SIZE> &card) const;
	bool save_card(const std::string &path);

	/* The player's card: its file, in their hand or not */
	std::string m_card_file;
	bool m_in_hand;
	/* The card in the reader */
	std::array<uint8_t, CARD_SIZE> m_card;
	Position m_position;
	bool m_dispensed;	/* it came from the stocker */
	bool m_ejected;		/* the program's 'F': refused until a reply sees no card */
	bool m_shutter_open;
	bool m_stocker_full;
	/* The tries of command C1: kept and saved, but each try reads the same
	 * here and the time the mechanism takes is not counted, so nothing
	 * reads it */
	uint8_t m_retries;

	Phase m_phase;
	std::array<uint8_t, FRAME_MAX> m_rx;
	uint32_t m_rx_count;
	std::array<uint8_t, FRAME_MAX> m_command;	/* the frame ACKed, or the read waiting */
	uint32_t m_command_size;
	std::array<uint8_t, 16> m_second;	/* a frame for the read waiting, until its ENQ */
	uint32_t m_second_size;
	std::array<uint8_t, FRAME_MAX> m_reply;
	uint32_t m_reply_size;
	bool m_reply_sent;
	std::array<uint8_t, FRAME_MAX + 2> m_tx;
	uint32_t m_tx_head;
	uint32_t m_tx_count;

	FILE *m_log;
	FILE *m_trace;
	std::array<bool, 256> m_cmd_logged;
	bool m_frame_logged;
};

/* The card in the reader, the exchange in flight. Plain data. */
struct CardReaderState {
	std::array<uint8_t, CardReader::CARD_SIZE> card;
	std::array<uint8_t, CardReader::FRAME_MAX> rx;
	std::array<uint8_t, CardReader::FRAME_MAX> command;
	std::array<uint8_t, 16> second;
	std::array<uint8_t, CardReader::FRAME_MAX> reply;
	std::array<uint8_t, CardReader::FRAME_MAX + 2> tx;
	uint32_t rx_count;
	uint32_t command_size;
	uint32_t second_size;
	uint32_t reply_size;
	uint32_t tx_head;
	uint32_t tx_count;
	uint8_t position;
	uint8_t in_hand;
	uint8_t dispensed;
	uint8_t ejected;
	uint8_t shutter_open;
	uint8_t stocker_full;
	uint8_t retries;
	uint8_t phase;
	uint8_t reply_sent;
};

} /* namespace odin */

#endif /* ODIN_CARD_READER_HPP */
