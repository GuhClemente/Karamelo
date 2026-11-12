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

#ifndef ODIN_NETBD_RING_HPP
#define ODIN_NETBD_RING_HPP

#include "odin/machine/cabinet/netbd_837_13404.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace odin {

/* Cabinets linked by the network of the computers.
 *
 * The program of the boards needs a frame round the ring within a turn of its
 * IRQ5 counter, about 15 ms (measured with that program: 4 boards link with
 * 3.3 ms a hop, not with 4.2 ms). Odins that waited for each other at each
 * frame would be a frame apart at each hop; so each board runs as on a
 * cabinet: on a thread of its own, on the clock of the computer, its frames
 * sent at once. The host meets it in page 0E under its lock, and the games of
 * the cabinets keep their own pace, as each cabinet keeps its own crystals. */

/* What takes the frames a cable brings in (any thread): a frame's bytes,
 * the clock of the other end when it began to leave it, and the clocks it
 * took */
class NetArrivals {
public:
	virtual ~NetArrivals() = default;
	virtual void arrive(std::vector<uint8_t> data, uint64_t start, uint64_t duration) = 0;
};

/* A network board on a thread of its own, run on the clock of the computer:
 * it wakes at its next event and when a frame comes, so that it answers
 * within the time the computer takes to wake a thread. The program allows
 * little: in a turn of 16.4 ms the small frame goes round the ring, one
 * hop after the other, then each board sends its part (0x4800 bytes for
 * four sites, 14.7 ms at 10 Mbit/s); a small frame that comes to a board
 * still busy with the parts is lost, and the ring stops. */
class NetBoardRunner : public NetArrivals {
public:
	/* The longest it sleeps: what the host waits at most for an answer */
	static constexpr std::chrono::microseconds TICK{ 250 };
	/* While its 68000 works (an interrupt, a frame to send) */
	static constexpr std::chrono::microseconds BUSY_TICK{ 20 };

	explicit NetBoardRunner(NetBoard837_13404 &board);
	~NetBoardRunner();
	NetBoardRunner(const NetBoardRunner &) = delete;
	NetBoardRunner &operator=(const NetBoardRunner &) = delete;

	void start();
	void stop();
	/* Held by another thread (the host) to touch the board */
	std::mutex &board_lock() { return m_board_lock; }
	/* A frame from the previous cabinet as it comes in: its bytes, the
	 * clock of the other board when it began to leave it and the clocks it
	 * took to (any thread) */
	void arrive(std::vector<uint8_t> data, uint64_t start, uint64_t duration) override;

	/* HEURISTIC: a frame comes in as late after the fastest of the last
	 * GAPS as 9 in 10 of them came, STEADY clocks (0.2 ms) at most; one
	 * later than that comes as it comes. A fibre takes the same time for
	 * each frame, and the programs time the frames of the master to a few
	 * microseconds; a network does not, and each hop it takes longer is
	 * taken from the turn of the ring. */
	static constexpr uint64_t STEADY = 2000;
	static constexpr size_t GAPS = 128;

private:
	using Clock = std::chrono::steady_clock;
	using BoardClocks = std::chrono::duration<uint64_t, std::ratio<1, NetBoard837_13404::CLOCK_HZ>>;

	struct Incoming {
		std::vector<uint8_t> data;
		uint64_t start, duration;
		Clock::time_point when;
	};

	void run();
	/* Until `wake`, a frame or stop; the frames come into `taken` */
	void wait(std::vector<Incoming> &taken, Clock::time_point wake);
	uint64_t board_clock(Clock::time_point t) const;
	Clock::time_point wall_clock(uint64_t clock) const;

	NetBoard837_13404 &m_board;
	std::mutex m_board_lock;
	std::mutex m_inbox_lock;
	std::condition_variable m_wake;
#ifdef _WIN32
	/* MinGW's timed waits last 15.6 ms at least, even for 20 us: a timer of
	 * high resolution (0.8 ms on an i3-6100U) and an event set when a frame
	 * comes, as HANDLEs */
	void *m_timer = nullptr;
	void *m_arrived = nullptr;
#endif
	std::vector<Incoming> m_inbox;
	std::atomic<bool> m_stop{ false };
	std::thread m_thread;
	Clock::time_point m_start;
	uint64_t m_clock_at_start = 0;
	std::deque<int64_t> m_gaps;	/* clock here less clock there, as frames came */
};

/* The optical cable as TCP: frames come in from the previous cabinet on
 * `port` and go out to the next one at `next_host`, `next_port`. Each end
 * first says the game and the version of this cable; a cabinet of another
 * game is not linked. A frame sent with no next cabinet is lost, as with
 * a cable pulled out. */
class NetCable : public NetLink {
public:
	static constexpr uint16_t PORT = 13404;
	static constexpr uint32_t VERSION = 2;

	NetCable(NetArrivals &runner, const std::string &game, uint16_t port, const std::string &next_host,
	         uint16_t next_port);
	~NetCable() override;
	NetCable(const NetCable &) = delete;
	NetCable &operator=(const NetCable &) = delete;

	/* False when the port cannot be opened */
	bool start();
	void stop();
	/* From the thread of the board: queued for the next cabinet */
	void send(const NetFrame &frame) override;
	/* The cable plugged in at each end (any thread) */
	bool previous_linked() const { return m_previous; }
	bool next_linked() const { return m_linked; }

private:
	struct Outgoing {
		std::vector<uint8_t> data;
		uint64_t start, duration;
	};

	void receive_loop();
	void send_loop();
	bool receive_frames(intptr_t from);
	bool hello_ok(intptr_t from);

	NetArrivals &m_runner;
	std::string m_game;
	uint16_t m_port;
	std::string m_next_host;
	uint16_t m_next_port;
	intptr_t m_listener;
	std::atomic<bool> m_stop{ false };
	std::atomic<bool> m_linked{ false };
	std::atomic<bool> m_previous{ false };
	std::mutex m_outbox_lock;
	std::condition_variable m_outbox_ready;
	std::deque<Outgoing> m_outbox;
	std::thread m_receiver, m_sender;
};

/* How this cabinet is linked: [network] of odin.ini. none: no board;
 * loop: its optical ring closed on itself; ring: linked to other Odins,
 * the previous one coming in on `port`, the next one at `next`
 * ("address" or "address:port", port 13404 by default; empty: this
 * computer on the same port, a ring of one). The text forms are those of
 * odin.ini. */
struct NetLinkSettings {
	enum class Board { NONE, LOOP, RING };

	/* The most cabinets a game links (Game::Link) */
	static constexpr unsigned MOST_CABINETS = 8;

	Board board = Board::NONE;
	uint16_t port = NetCable::PORT;
	std::string next;
	/* Which cabinet of the link this one is, 1 the master: its game is
	 * set so as it starts (Hikaru::take_cabinet); 0 leaves its settings
	 * as they are (the tests) */
	unsigned cabinet = 1;

	static const char *board_name(Board board);
	static bool board_named(const std::string &text, Board &out);
	static bool port_named(const std::string &text, uint16_t &out);
	static bool cabinet_named(const std::string &text, unsigned &out);
	/* The address and the port of the next cabinet; false when `next` is
	 * not one */
	bool next_cabinet(std::string &host, uint16_t &next_port) const;
	/* Whether a text is right as `next` (whatever the port) */
	static bool next_right(const std::string &next);
};

} /* namespace odin */

#endif /* ODIN_NETBD_RING_HPP */
