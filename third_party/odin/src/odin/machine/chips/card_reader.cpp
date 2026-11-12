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

#include "odin/machine/chips/card_reader.hpp"

#include <algorithm>
#include <bit>
#include <format>

#include "odin/host/app/user_files.hpp"

namespace odin {

namespace {

/* Frame: STX LEN CMD 00 00 00 params ETX BCC; reply: STX LEN CMD st1 st2
 * st3 data ETX BCC */
constexpr uint32_t FRAME_PARAMS = 6;
constexpr uint32_t FRAME_OVERHEAD = 8;
constexpr uint32_t REPLY_DATA = 6;
constexpr uint32_t LEN_MIN = 6;

/* The sensors each position covers (287E) */
constexpr std::array<uint8_t, 10> SENSORS = { 0x00, 0x01, 0x03, 0x07, 0x06, 0x0E, 0x0C, 0x1C, 0x18, 0x10 };

/* What the decoders say of a track (40D4, 4336,
 * 457E): read, no start (blank), no end, LRC, parity */
enum Track : uint8_t { GOOD = 'O', BLANK = 'S', NO_END = 'E', LRC = 'L', PARITY = 'P' };

/* Track selector of 33 and 53: '0'-'2' one track, '3' T1 T2, '4' T1 T3,
 * '5' T2 T3, '6' all three; bit n = track n + 1, 0 not one */
uint8_t tracks_of(uint8_t selector)
{
	static constexpr std::array<uint8_t, 7> MASK = { 1, 2, 4, 3, 5, 6, 7 };

	return selector >= '0' && selector <= '6' ? MASK[selector - '0'] : 0;
}

bool even_parity(uint8_t byte)
{
	return (std::popcount(byte) & 1) == 0;
}

/* What a track of the card reads as; a read of seven bits and parity
 * also wants each byte even */
Track track_status(const uint8_t *raw, bool seven)
{
	if (raw[0] != 0xFF)
		return BLANK;
	if (raw[CardReader::TRACK_DATA + 1] != 0xFF)
		return NO_END;
	if (CardReader::frame_xor(std::span<const uint8_t>(raw + 1, CardReader::TRACK_DATA + 1)) !=
	    raw[CardReader::TRACK_DATA + 2])
		return LRC;
	for (uint32_t i = 1; seven && i <= CardReader::TRACK_DATA; i++)
		if (!even_parity(raw[i]))
			return PARITY;
	return GOOD;
}

/* The letter of the tracks asked that failed (8230), some of them good */
uint8_t failure_letter(uint8_t mask, const std::array<uint8_t, CardReader::TRACKS> &status)
{
	const bool bad1 = (mask & 1) && status[0] != GOOD;
	const bool bad2 = (mask & 2) && status[1] != GOOD;
	const bool bad3 = (mask & 4) && status[2] != GOOD;

	if (bad1 && bad2)
		return 'T';
	if (bad1 && bad3)
		return 'U';
	if (bad2 && bad3)
		return 'V';
	if (bad1)
		return 'Q';
	return bad2 ? 'R' : 'S';
}

} /* namespace */

CardReader::CardReader(std::string card_file)
	: m_in_hand(false), m_card{}, m_position(NONE), m_dispensed(false), m_ejected(false), m_shutter_open(false),
	  m_stocker_full(true), m_retries(4), m_phase(IDLE), m_rx{}, m_rx_count(0), m_command{}, m_command_size(0),
	  m_second{}, m_second_size(0), m_reply{}, m_reply_size(0), m_reply_sent(false), m_tx{}, m_tx_head(0),
	  m_tx_count(0), m_log(nullptr), m_trace(nullptr), m_frame_logged(false)
{
	m_cmd_logged.fill(false);
	reset();
	choose(card_file);
}

bool CardReader::choose(const std::string &card_file)
{
	std::array<uint8_t, CARD_SIZE> card;

	if (m_position != NONE && !m_dispensed)
		return false;
	m_card_file = card_file;
	m_in_hand = !card_file.empty() && read_card(card_file, card);
	return true;
}

/* The card at the entrance; a read that waits takes it in at its next
 * ENQ (AA32 state 3 ends when S1 is covered) */
bool CardReader::insert()
{
	if (!m_in_hand || m_position != NONE || !m_shutter_open || !read_card(m_card_file, m_card))
		return false;
	m_in_hand = false;
	m_dispensed = false;
	m_position = S1;
	return true;
}

bool CardReader::take(const std::string &new_file)
{
	if (m_position != S1)
		return false;

	const std::string path = m_dispensed || m_card_file.empty() ? new_file : m_card_file;

	if (!path.empty() && save_card(path)) {
		m_card_file = path;
		m_in_hand = true;
	}
	m_position = NONE;
	m_dispensed = false;
	return true;
}

CardReader::Where CardReader::where() const
{
	if (m_position == S1)
		return Where::AT_SLOT;
	if (m_position != NONE && !m_dispensed)
		return Where::INSIDE;
	return m_in_hand ? Where::IN_HAND : Where::NO_CARD;
}

/* The routine at 20EE: four tries, the shutter as its sensors are (closed at
 * power-up here), the boot reply in the buffer for a bare ENQ */
void CardReader::reset()
{
	m_ejected = false;
	m_shutter_open = false;
	m_retries = 4;
	m_phase = IDLE;
	m_rx_count = 0;
	m_command_size = 0;
	m_second_size = 0;
	m_tx_head = 0;
	m_tx_count = 0;
	reply(CMD_INIT, '0', '0');
}

uint8_t CardReader::frame_xor(std::span<const uint8_t> bytes)
{
	uint8_t x = 0;

	for (uint8_t b : bytes)
		x = static_cast<uint8_t>(x ^ b);
	return x;
}

void CardReader::seal_track(uint8_t *track)
{
	track[TRACK_DATA + 1] = 0xFF;
	track[TRACK_DATA + 2] = frame_xor(std::span<const uint8_t>(track + 1, TRACK_DATA + 1));
}

uint8_t CardReader::status1() const
{
	return static_cast<uint8_t>(SENSORS[m_position] | (m_stocker_full ? 0x20 : 0) | (m_shutter_open ? 0x80 : 0x40));
}

/* The line, byte by byte (77FA). Outside a frame: ENQ runs the
 * command ACKed, or sends the reply again (7FCE), as NAK does;
 * STX starts a frame, which replaces one ACKed; anything else is let
 * go. */
void CardReader::receive(uint8_t byte)
{
	if (m_rx_count == 0) {
		if (byte == ENQ || byte == NAK) {
			bool answer = true;

			if (m_trace)
				std::fputs(byte == ENQ ? "card: rx ENQ\n" : "card: rx NAK\n", m_trace);
			if (m_phase == ACKED && byte == NAK)
				return;
			if (m_phase == ACKED)
				run(std::span<const uint8_t>(m_command.data(), m_command_size));
			else if (byte == ENQ && m_phase == WAITING && m_second_size)
				answer = second_frame(std::span<const uint8_t>(m_second.data(), m_second_size));
			else if (m_phase == WAITING && m_position != NONE)
				read(std::span<const uint8_t>(m_command.data() + FRAME_PARAMS, 3));
			else if (m_phase == WAITING)
				reply(CMD_READ, '0', '4');
			if (answer)
				send_reply();
			return;
		}
		if (byte != STX)
			return;
		m_second_size = 0;
	}
	m_rx[m_rx_count++] = byte;
	if (m_rx_count == 2 && byte < LEN_MIN) {
		m_rx_count = 0;
	} else if (m_rx_count >= 2 && m_rx_count == static_cast<uint32_t>(m_rx[1]) + 2) {
		frame_complete();
		m_rx_count = 0;
	} else if (m_rx_count == FRAME_MAX) {
		if (m_log && !m_frame_logged) {
			m_frame_logged = true;
			std::fputs("card: frame longer than 256 bytes dropped\n", m_log);
		}
		m_rx_count = 0;
	}
}

bool CardReader::has_byte()
{
	return m_tx_count > m_tx_head;
}

uint8_t CardReader::take_byte()
{
	if (m_tx_count <= m_tx_head)
		return 0;

	const uint8_t v = m_tx[m_tx_head++];

	if (m_tx_head == m_tx_count) {
		m_tx_head = 0;
		m_tx_count = 0;
	}
	return v;
}

void CardReader::send(uint8_t byte)
{
	if (m_tx_count < m_tx.size())
		m_tx[m_tx_count++] = byte;
}

/* ETX in place and the BCC right: ACK, and the frame waits for its ENQ;
 * while a read waits for a card it is a second frame, kept when it is
 * one the program looks at then (7ED2, 77FA states 7-8) */
void CardReader::frame_complete()
{
	const std::span<const uint8_t> frame(m_rx.data(), m_rx_count);

	trace_frame("rx", frame);
	if (frame[m_rx_count - 2] != ETX || frame_xor(frame.subspan(1, m_rx_count - 2)) != frame[m_rx_count - 1]) {
		if (m_log && !m_frame_logged) {
			m_frame_logged = true;
			std::fputs(std::format("card: bad frame, BCC {:02x}\n", frame[m_rx_count - 1]).c_str(), m_log);
		}
		send(NAK);
		if (m_trace)
			std::fputs("card: tx NAK\n", m_trace);
		return;
	}
	send(ACK);
	if (m_trace)
		std::fputs("card: tx ACK\n", m_trace);
	if (m_phase == WAITING) {
		const uint8_t cmd = frame[2];

		if (cmd == CMD_CANCEL || cmd == CMD_STATUS || cmd == CMD_INIT || cmd == CMD_INIT_SAME || cmd == CMD_INIT_ZERO ||
		    cmd == CMD_STOP) {
			m_second_size = std::min<uint32_t>(m_rx_count, static_cast<uint32_t>(m_second.size()));
			std::copy_n(m_rx.begin(), m_second_size, m_second.begin());
		}
		return;
	}
	std::copy_n(m_rx.begin(), m_rx_count, m_command.begin());
	m_command_size = m_rx_count;
	m_phase = ACKED;
}

/* The ENQ after a second frame, while a read waits for a card
 * (77FA state 9, 60D0): 40 cancels it (no card is in),
 * 20 gets the reply of the read again, 10 / 1C / E0 replace it, FF stops
 * the program and its watchdog resets the reader, with no reply. False:
 * nothing to send. */
bool CardReader::second_frame(std::span<const uint8_t> frame)
{
	const uint8_t cmd = frame[2];
	std::array<uint8_t, 16> copy;

	std::copy(frame.begin(), frame.end(), copy.begin());
	m_second_size = 0;
	if (cmd == CMD_CANCEL) {
		m_phase = IDLE;
		reply(CMD_CANCEL, '0', '0');
	} else if (cmd == CMD_STATUS) {
		reply(CMD_READ, '0', '4');
	} else if (cmd == CMD_STOP) {
		reset();
		return false;
	} else {
		run(std::span<const uint8_t>(copy.data(), frame.size()));
	}
	return true;
}

/* The command ACKed, on its ENQ, whose reply it builds (the command
 * dispatch at 2492) */
void CardReader::run(std::span<const uint8_t> frame)
{
	uint8_t cmd = frame[2];
	const uint32_t count = frame.size() > FRAME_OVERHEAD ? static_cast<uint32_t>(frame.size()) - FRAME_OVERHEAD : 0;
	std::array<uint8_t, FRAME_MAX> copy;
	std::span<const uint8_t> params(copy.data(), count);

	std::copy_n(frame.begin() + FRAME_PARAMS, count, copy.begin());
	m_phase = IDLE;
	if (cmd == CMD_INIT_SAME || cmd == CMD_INIT_ZERO) {
		if (cmd == CMD_INIT_ZERO && count)
			copy[0] = '0';
		cmd = CMD_INIT;
	}
	switch (cmd) {
	case CMD_INIT:
		/* '1' ejects a card, refused with the shutter closed */
		if (!count || (params[0] != '0' && params[0] != '1') ||
		    (params[0] == '1' && m_position != NONE && !m_shutter_open)) {
			refuse(cmd);
			break;
		}
		if (params[0] == '1' && m_position != NONE)
			eject();
		reply(cmd, '0', '0');
		break;
	case CMD_STATUS:
		reply(cmd, '0', '0');
		break;
	case CMD_READ:
		read(params);
		break;
	case CMD_WRITE:
		write(params);
		break;
	case CMD_FONT:
		/* A glyph for slot 1-0x14 (kept by the reader for its prints;
		 * nothing is printed here) */
		if (!count || params[0] < 1 || params[0] > FONT_SLOTS)
			refuse(cmd);
		else
			reply(cmd, '0', '0');
		break;
	case CMD_PRINT:
		if (m_position == NONE || m_position == S1)
			refuse(cmd);
		else
			reply(cmd, '0', '0');
		break;
	case CMD_EJECT:
		if (m_position == NONE || !m_shutter_open) {
			refuse(cmd);
			break;
		}
		eject();
		reply(cmd, '0', '0');
		break;
	case CMD_CLEAN:
		/* HEURISTIC: the cleaning card is given at once (on a cabinet the
		 * operator inserts it, the reader waiting with st3 '4') */
		if (!m_shutter_open || m_ejected) {
			refuse(cmd);
			break;
		}
		log_once(cmd, "cleaning card given at once");
		reply(cmd, '0', '0');
		break;
	case CMD_DISPENSE:
		/* A card from the stocker stops at S3S4S5; nothing is written on
		 * it. HEURISTIC: its track 1 recorded by the factory, 69 zeros
		 * (the game reads 'V': only track 1 good); tracks 2 and 3 blank */
		if (!m_stocker_full) {
			reply(cmd, '0', '5');
			break;
		}
		if (m_position != NONE && m_position != S5) {
			refuse(cmd);
			break;
		}
		m_card = blank_card();
		m_position = S3S4S5;
		m_dispensed = true;
		reply(cmd, '0', '0');
		break;
	case CMD_RETRIES:
		if (!count || params[0] < '1' || params[0] > '9') {
			refuse(cmd);
			break;
		}
		m_retries = static_cast<uint8_t>(params[0] - '0');
		reply(cmd, '0', '0');
		break;
	case CMD_SHUTTER:
		/* Refused when it is already as asked; any other parameter
		 * than '1' closes it */
		if ((count && params[0] == '1') == m_shutter_open) {
			refuse(cmd);
			break;
		}
		m_shutter_open = !m_shutter_open;
		reply(cmd, '0', '0');
		break;
	case CMD_CANCEL:
		/* Not a command at rest: it only cancels a read that waits */
		refuse(cmd);
		break;
	default:
		log_once(cmd, "not answered as the program does: refused");
		refuse(cmd);
		break;
	}
	if (m_phase != WAITING)
		m_command_size = 0;
}

/* 33 mode parity track (5D0A, the read op 9602): the card
 * taken in and read, tracks T1 T2 T3 in order, 69 bytes each */
void CardReader::read(std::span<const uint8_t> params)
{
	const uint8_t mask = params.size() >= 3 ? tracks_of(params[2]) : 0;

	if (params.size() < 3 || params[0] < '0' || params[0] > '2' || (params[1] != '0' && params[1] != '1') ||
	    !mask || m_ejected) {
		m_phase = IDLE;
		refuse(CMD_READ);
		return;
	}
	if (params[0] != '0')
		log_once(CMD_READ, "read mode other than '0' answered as '0'");
	if (m_position == NONE) {
		if (!m_shutter_open) {
			m_phase = IDLE;
			refuse(CMD_READ);
			return;
		}
		/* Until the player inserts a card: st3 '4' on each ENQ */
		m_phase = WAITING;
		reply(CMD_READ, '0', '4');
		return;
	}
	m_phase = IDLE;

	const bool seven = params[1] == '0';
	std::array<uint8_t, DATA_SIZE> data{};
	std::array<uint8_t, TRACKS> status{};
	uint32_t size = 0;
	unsigned good = 0, selected = 0;

	for (uint32_t t = 0; t < TRACKS; t++) {
		const uint8_t *const raw = m_card.data() + t * TRACK_SIZE;

		if (!(mask & (1u << t)))
			continue;
		selected++;
		status[t] = track_status(raw, seven);
		good += status[t] == GOOD;
		for (uint32_t i = 0; i < TRACK_DATA; i++)
			data[size++] = status[t] == BLANK ? 0 : static_cast<uint8_t>(seven ? raw[1 + i] & 0x7F : raw[1 + i]);
	}
	if (good == selected) {
		/* Read on its way in: the card left at S4S5 */
		m_position = S4S5;
		reply(CMD_READ, '0', '0', std::span<const uint8_t>(data.data(), size));
		return;
	}
	/* Read again on its way out, then the tries C1 gives, all the same
	 * here: the card left at S1S2. One track, or none good: '1' without
	 * the data; else the letter of the tracks that failed (8230) */
	m_position = S1S2;
	if (selected == 1 || good == 0) {
		reply(CMD_READ, '1', '0');
		return;
	}
	reply(CMD_READ, failure_letter(mask, status), '0', std::span<const uint8_t>(data.data(), size));
}

/* 53 mode parity track data (652C, the write op 974A): the
 * tracks written as the program records them, the card left at S4S5. The
 * length of the frame is not checked against the tracks. */
void CardReader::write(std::span<const uint8_t> params)
{
	const uint8_t mask = params.size() >= 3 ? tracks_of(params[2]) : 0;

	if (params.size() < 3 || params[0] != '0' || (params[1] != '0' && params[1] != '1') || !mask) {
		if (params.size() >= 1 && params[0] == '1')
			log_once(CMD_WRITE, "variable length write refused");
		refuse(CMD_WRITE);
		return;
	}

	const bool seven = params[1] == '0';
	const std::span<const uint8_t> data = params.subspan(3);

	if (seven && std::any_of(data.begin(), data.end(), [](uint8_t b) { return b >= 0x80; })) {
		refuse(CMD_WRITE);
		return;
	}
	if (m_position == NONE || m_position == S1 || m_position == S5) {
		refuse(CMD_WRITE);
		return;
	}

	uint32_t from = 0;

	for (uint32_t t = 0; t < TRACKS; t++) {
		uint8_t *const raw = m_card.data() + t * TRACK_SIZE;

		if (!(mask & (1u << t)))
			continue;
		raw[0] = 0xFF;
		for (uint32_t i = 0; i < TRACK_DATA; i++, from++) {
			const uint8_t b = from < data.size() ? data[from] : 0;

			raw[1 + i] = seven && !even_parity(b) ? static_cast<uint8_t>(b | 0x80) : b;
		}
		seal_track(raw);
	}
	m_position = S4S5;
	if (!m_dispensed && !m_card_file.empty())
		save_card(m_card_file);
	reply(CMD_WRITE, '0', '0');
}

/* The card pushed out to the slot (ABE4), where it stays until
 * the player takes it (take). The player's card kept in its file. */
void CardReader::eject()
{
	m_position = S1;
	m_ejected = true;
	if (!m_dispensed && !m_card_file.empty())
		save_card(m_card_file);
}

/* The reply, kept until an ENQ asks for it; a reply that sees no card
 * clears the mark an eject leaves (9418) */
void CardReader::reply(uint8_t cmd, uint8_t st2, uint8_t st3, std::span<const uint8_t> data)
{
	data = data.first(std::min<size_t>(data.size(), FRAME_MAX - FRAME_OVERHEAD));

	uint32_t n = 0;

	m_reply[n++] = STX;
	m_reply[n++] = static_cast<uint8_t>(REPLY_DATA + data.size());
	m_reply[n++] = cmd;
	m_reply[n++] = status1();
	m_reply[n++] = st2;
	m_reply[n++] = st3;
	for (uint8_t b : data)
		m_reply[n++] = b;
	m_reply[n++] = ETX;
	m_reply[n] = frame_xor(std::span<const uint8_t>(m_reply.data() + 1, n - 1));
	m_reply_size = n + 1;
	m_reply_sent = false;
	if (m_position == NONE)
		m_ejected = false;
}

/* The reply on the line; sent once already, as the sensors are now, st2
 * '0' and st3 '0' unless '3' (7FCE) */
void CardReader::send_reply()
{
	if (m_reply_sent) {
		const uint32_t n = m_reply_size - 1;

		m_reply[3] = status1();
		m_reply[4] = '0';
		if (m_reply[5] != '3')
			m_reply[5] = '0';
		m_reply[n] = frame_xor(std::span<const uint8_t>(m_reply.data() + 1, n - 1));
		if (m_position == NONE)
			m_ejected = false;
	}
	for (uint32_t i = 0; i < m_reply_size; i++)
		send(m_reply[i]);
	m_reply_sent = true;
	trace_frame("tx", std::span<const uint8_t>(m_reply.data(), m_reply_size));
}

void CardReader::trace_frame(const char *dir, std::span<const uint8_t> bytes) const
{
	static constexpr size_t SHOWN = 32;

	if (!m_trace)
		return;

	std::string line = std::format("card: {} ({} bytes)", dir, bytes.size());
	const size_t shown = std::min(bytes.size(), SHOWN);

	for (size_t i = 0; i < shown; i++)
		line += std::format(" {:02x}", bytes[i]);
	if (shown < bytes.size())
		line += " ...";
	line += '\n';
	std::fputs(line.c_str(), m_trace);
}

void CardReader::log_once(uint8_t cmd, const char *what)
{
	if (!m_log || m_cmd_logged[cmd])
		return;
	m_cmd_logged[cmd] = true;
	std::fputs(std::format("card: command {:02x}: {}\n", cmd, what).c_str(), m_log);
}

std::array<uint8_t, CardReader::CARD_SIZE> CardReader::blank_card()
{
	std::array<uint8_t, CARD_SIZE> card{};

	card[0] = 0xFF;
	seal_track(card.data());
	return card;
}

std::array<uint8_t, CardReader::CARD_SIZE> CardReader::card_of(std::span<const uint8_t, DATA_SIZE> data)
{
	std::array<uint8_t, CARD_SIZE> card;

	for (uint32_t t = 0; t < TRACKS; t++) {
		uint8_t *const raw = card.data() + t * TRACK_SIZE;

		raw[0] = 0xFF;
		std::copy_n(data.begin() + t * TRACK_DATA, TRACK_DATA, raw + 1);
		seal_track(raw);
	}
	return card;
}

bool CardReader::whole_card(std::span<const uint8_t, CARD_SIZE> card)
{
	for (uint32_t t = 0; t < TRACKS; t++) {
		const std::span<const uint8_t> raw = card.subspan(t * TRACK_SIZE, TRACK_SIZE);

		if (std::all_of(raw.begin(), raw.end(), [](uint8_t b) { return b == 0; }))
			continue;
		if (raw[0] != 0xFF || raw[TRACK_DATA + 1] != 0xFF || raw[TRACK_DATA + 2] != frame_xor(raw.subspan(1, TRACK_DATA + 1)))
			return false;
	}
	return true;
}

bool CardReader::write_card(const std::string &path, std::span<const uint8_t, CARD_SIZE> card, FILE *log)
{
	const auto said = [&](const std::string &what) {
		if (log)
			std::fputs(std::format("card: '{}' {}\n", path, what).c_str(), log);
	};

	if (!whole_card(card)) {
		said("not written: a track is neither blank nor whole");
		return false;
	}

	/* What the file holds now: the same card is not written again (an
	 * eject after a write), another whole one is kept */
	std::array<uint8_t, CARD_SIZE + 1> held;
	size_t n = 0;

	if (FILE *const f = std::fopen(path.c_str(), "rb")) {
		n = std::fread(held.data(), 1, held.size(), f);
		std::fclose(f);
	}
	if (n == CARD_SIZE && std::equal(card.begin(), card.end(), held.begin()))
		return true;
	if (n == CARD_SIZE && whole_card(std::span<const uint8_t, CARD_SIZE>(held.data(), CARD_SIZE)) &&
	    !write_kept(path + ".bak", std::span<const uint8_t>(held.data(), CARD_SIZE)))
		said("kept no copy of the card before");
	if (!write_kept(path, card)) {
		said("not written");
		return false;
	}
	return true;
}

/* A card file: the three tracks as on the card, or their 69 bytes each,
 * recorded. One not whole (cut short, changed by hand) gives way to its
 * copy <file>.bak (write_card) when that one is whole. */
bool CardReader::read_card(const std::string &path, std::array<uint8_t, CARD_SIZE> &card) const
{
	const Read read = read_card_file(path, card);

	if (read == Read::WHOLE)
		return true;

	std::array<uint8_t, CARD_SIZE> kept;

	if (read_card_file(path + ".bak", kept) == Read::WHOLE) {
		if (m_log)
			std::fputs(std::format("card: '{}' is not whole: its copy '{}.bak' taken\n", path, path).c_str(), m_log);
		card = kept;
		return true;
	}
	return read != Read::NONE;
}

CardReader::Read CardReader::read_card_file(const std::string &path, std::array<uint8_t, CARD_SIZE> &card) const
{
	FILE *const f = std::fopen(path.c_str(), "rb");

	if (!f)
		return Read::NONE;

	std::array<uint8_t, CARD_SIZE + 1> bytes;
	const size_t n = std::fread(bytes.data(), 1, bytes.size(), f);

	std::fclose(f);
	if (n == CARD_SIZE) {
		std::copy_n(bytes.begin(), CARD_SIZE, card.begin());
	} else if (n == DATA_SIZE) {
		card = card_of(std::span<const uint8_t, DATA_SIZE>(bytes.data(), DATA_SIZE));
	} else {
		if (m_log)
			std::fputs(std::format("card: file '{}' holds {} bytes, {} or {} expected\n", path, n, CARD_SIZE,
			                       DATA_SIZE).c_str(), m_log);
		return Read::NONE;
	}
	return whole_card(card) ? Read::WHOLE : Read::NOT_WHOLE;
}

/* The card in the reader into a file (write_card) */
bool CardReader::save_card(const std::string &path)
{
	return write_card(path, m_card, m_log);
}

void CardReader::save(CardReaderState &out) const
{
	out.card = m_card;
	out.rx = m_rx;
	out.command = m_command;
	out.second = m_second;
	out.reply = m_reply;
	out.tx = m_tx;
	out.rx_count = m_rx_count;
	out.command_size = m_command_size;
	out.second_size = m_second_size;
	out.reply_size = m_reply_size;
	out.tx_head = m_tx_head;
	out.tx_count = m_tx_count;
	out.position = m_position;
	out.in_hand = m_in_hand;
	out.dispensed = m_dispensed;
	out.ejected = m_ejected;
	out.shutter_open = m_shutter_open;
	out.stocker_full = m_stocker_full;
	out.retries = m_retries;
	out.phase = m_phase;
	out.reply_sent = m_reply_sent;
}

void CardReader::load(const CardReaderState &in)
{
	m_card = in.card;
	m_rx = in.rx;
	m_command = in.command;
	m_second = in.second;
	m_reply = in.reply;
	m_tx = in.tx;
	m_rx_count = in.rx_count;
	m_command_size = in.command_size;
	m_second_size = in.second_size;
	m_reply_size = in.reply_size;
	m_tx_head = in.tx_head;
	m_tx_count = in.tx_count;
	m_position = in.position <= S5 ? static_cast<Position>(in.position) : NONE;
	m_in_hand = in.in_hand != 0;
	m_dispensed = in.dispensed != 0;
	m_ejected = in.ejected != 0;
	m_shutter_open = in.shutter_open != 0;
	m_stocker_full = in.stocker_full != 0;
	m_retries = in.retries;
	m_phase = in.phase <= WAITING ? static_cast<Phase>(in.phase) : IDLE;
	m_reply_sent = in.reply_sent != 0;
}

} /* namespace odin */
