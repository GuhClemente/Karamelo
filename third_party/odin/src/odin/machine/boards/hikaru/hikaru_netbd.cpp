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


/* The network board of the cabinet (netbd_837_13404.hpp) and its link to
 * other Odins (netbd_ring.hpp): the part of the board (hikaru.cpp) that
 * builds, runs and links it. */

#include "odin/machine/boards/hikaru/hikaru.hpp"

#include <cstdio>
#include <cstdlib>

#include "odin/common/cabinet_settings.hpp"
#include "odin/common/clock_ratio.hpp"
#include "odin/common/env.hpp"

namespace odin {

namespace {

/* A network board whose ring is closed on itself: its frames come back to
 * it. HEURISTIC: 10 us on the fibre and in the board around it. */
class NetLoop : public NetLink {
public:
	explicit NetLoop(NetBoard837_13404 &board) : m_board(board) {}
	void send(const NetFrame &frame) override { m_board.receive(frame, NetBoard837_13404::CLOCK_HZ / 100000); }

private:
	NetBoard837_13404 &m_board;
};

} /* namespace */

/* The network board, as the cabinet may have one: a game that can be
 * linked ("link" in the list) has the one [network] of odin.ini asks for,
 * and its settings are those of the cabinet chosen there (take_cabinet)
 * (NetLinkSettings): its optical ring closed on itself (loop: the games
 * see it, as on a cabinet with its cable looped back) or linked to other
 * Odins (ring). Without it, page 0E answers nothing and the games find no
 * board. For the tests: ODIN_NETBD (none, loop, ring), ODIN_NETBD_PORT,
 * ODIN_NETBD_NEXT and ODIN_NETBD_CABINET put one in any game instead. */
void
Hikaru::build_netbd(NetLinkSettings link)
{
	if (const char *const board = std::getenv("ODIN_NETBD")) {
		const char *const port = std::getenv("ODIN_NETBD_PORT");
		const char *const next = std::getenv("ODIN_NETBD_NEXT");

		link = {};
		if (!NetLinkSettings::board_named(board, link.board) ||
		    (port && !NetLinkSettings::port_named(port, link.port))) {
			std::fprintf(stderr, "ERROR: ODIN_NETBD=%s ODIN_NETBD_PORT=%s: not right (none, loop or ring; a port):"
			             " no network board\n", board, port ? port : "");
			return;
		}
		link.next = next ? next : "";
		link.cabinet = 0;
		if (const char *const cabinet = std::getenv("ODIN_NETBD_CABINET");
		    cabinet && !NetLinkSettings::cabinet_named(cabinet, link.cabinet))
			std::fprintf(stderr, "ERROR: ODIN_NETBD_CABINET=%s: not 1 to %u: settings left as they are\n", cabinet,
			             NetLinkSettings::MOST_CABINETS);
	} else if (!m_game.link.cabinets) {
		return;
	}
	m_link = link;
	if (link.board == NetLinkSettings::Board::NONE)
		return;

	std::string host;
	uint16_t next_port;

	if (!link.next_cabinet(host, next_port)) {
		std::fprintf(stderr, "ERROR: network board: the next cabinet '%s' is not an address or address:port:"
		             " no network board\n", link.next.c_str());
		return;
	}
	m_netbd = std::make_unique<NetBoard837_13404>();
	m_netbd->reset();
	/* For the tests: ODIN_TURNS=0, its 68000 runs every turn of its
	 * waits. The memory and the pictures must be the same. */
	m_netbd->set_waits_counted(env_flag("ODIN_TURNS", true));
	if (link.board == NetLinkSettings::Board::RING) {
		m_netbd_runner = std::make_unique<NetBoardRunner>(*m_netbd);
		m_netbd_cable = std::make_unique<NetCable>(*m_netbd_runner, m_game.name, link.port, host, next_port);
		m_netbd->set_link(m_netbd_cable.get());
		if (!m_netbd_cable->start()) {
			m_netbd_cable.reset();
			m_netbd_runner.reset();
			m_netbd.reset();
			m_netbd_failed = true;
			std::fprintf(stderr, "ERROR: no network board\n");
			return;
		}
		m_netbd_runner->start();
		std::printf("network board: 837-13404, linked to other cabinets\n");
		return;
	}
	m_netbd_link = std::make_unique<NetLoop>(*m_netbd);
	m_netbd->set_link(m_netbd_link.get());
	std::printf("network board: 837-13404, its ring closed on itself\n");
}

/* What the operator sets in the menus for a link, set at power-on for
 * the cabinet [network] chooses (Game::Link): COMMUNICATION MODE of the
 * BOOTROM (SYSTEM ASSIGNMENTS: NO LINK 0, MASTER 1, SLAVE 2), cabinet 1
 * the master, and the seat of the game. A game that makes its block of
 * settings at its first power-on gets its seat once it has made it, then
 * the cabinet is switched off and on (end of run_frame), as the operator
 * would after the test menu. Cabinet 0 (ODIN_NETBD without
 * ODIN_NETBD_CABINET, the tests) leaves the settings as they are. */
void
Hikaru::take_cabinet()
{
	const unsigned cabinet = link_cabinet();
	const std::span<uint8_t> chip = m_mie->eeprom().data();

	if (cabinet == 0 || !m_game.link.cabinets)
		return;
	if (m_link.board == NetLinkSettings::Board::NONE) {
		if (chip[cabinet::COMMUNICATION_MODE] != 0) {
			cabinet::set_mie(chip, cabinet::COMMUNICATION_MODE, 0, 8, 0);
			std::printf("network board: none, the cabinet set back to NO LINK\n");
		}
		return;
	}
	cabinet::set_mie(chip, cabinet::COMMUNICATION_MODE, 0, 8, cabinet == 1 ? cabinet::MASTER : cabinet::SLAVE);
	std::printf("network board: cabinet %u of the link, the %s\n", cabinet, cabinet == 1 ? "master" : "slave");
	if (m_game.link.seat_in == Game::Link::In::NONE)
		return;
	if (cabinet > m_game.link.cabinets) {
		std::fprintf(stderr, "ERROR: network board: %s links %u cabinets at most: cabinet %u keeps the seat it had\n",
		             m_game.title.c_str(), m_game.link.cabinets, cabinet);
		return;
	}
	if (take_seat())
		return;
	/* Switched off with no folder to keep it, the seat would be lost */
	if (kept_path("nvram").empty()) {
		std::fprintf(stderr, "ERROR: network board: no save folder to keep the seat of cabinet %u in\n", cabinet);
		return;
	}
	m_seat_owed = true;
	std::printf("network board: the seat waits for the settings the game makes at its first power-on\n");
}

/* The cabinet of the link this one is: the first with the cable looped
 * back on it */
unsigned
Hikaru::link_cabinet() const
{
	return m_link.board == NetLinkSettings::Board::LOOP ? 1 : m_link.cabinet;
}

/* The seat of the cabinet in its settings; false when the game has not
 * made its block of settings yet */
bool
Hikaru::take_seat()
{
	const Game::Link &link = m_game.link;
	const unsigned cabinet = link_cabinet();
	const unsigned seat = link.seat_first + cabinet - 1;
	const bool taken = link.seat_in == Game::Link::In::MIE ?
	                   cabinet::set_mie(m_mie->eeprom().data(), link.seat_byte, link.seat_shift, link.seat_bits, seat) :
	                   cabinet::set_backup(m_bram, link.seat_byte, link.seat_shift, link.seat_bits, seat);

	if (taken && link.seat_letter)
		std::printf("network board: seat %c\n", static_cast<char>('A' + cabinet - 1));
	else if (taken)
		std::printf("network board: seat %u\n", cabinet);
	return taken;
}

Hikaru::LinkStatus
Hikaru::link_status() const
{
	LinkStatus status;

	status.board = m_netbd != nullptr;
	status.failed = m_netbd_failed;
	status.linked = m_netbd_cable != nullptr;
	status.previous = m_netbd_cable && m_netbd_cable->previous_linked();
	status.next = m_netbd_cable && m_netbd_cable->next_linked();
	return status;
}

/* The clock of the network board as far as the SH-4 ran, owed: the board
 * only meets the host in page 0E, so it runs to it when the host reaches
 * it (netbd_sync) and at the end of the frame. A clock, not a number of
 * clocks: a run ends on an instruction a little after its clock, which
 * the next run does not add again. It is then where it would be run at
 * every slice, in longer runs where its waits are counted. */
void
Hikaru::run_netbd(uint32_t cycles)
{
	m_netbd_owed += take_ticks(m_netbd_acc, cycles, NetBoard837_13404::CLOCK_HZ, m_clock);
}

void
Hikaru::netbd_sync()
{
	if (!m_netbd_runner)
		m_netbd->run_until(m_netbd_owed);
}

/* The host in page 0E: the board run to it, or, on its own thread,
 * held while the host touches it */
std::unique_lock<std::mutex>
Hikaru::netbd_meet()
{
	if (m_netbd_runner)
		return std::unique_lock<std::mutex>(m_netbd_runner->board_lock());
	netbd_sync();
	return {};
}

} /* namespace odin */
