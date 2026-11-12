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

#ifndef ODIN_NETBD_837_13404_HPP
#define ODIN_NETBD_837_13404_HPP

#include "odin/machine/cpu/m68000/m68000.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace odin {

/* What travels on the fibre: the bytes of one transmission, and the clocks
 * of the sending board at which the first and the last of them leave it. */
struct NetFrame {
	std::vector<uint8_t> data;
	uint64_t start = 0;
	uint64_t end = 0;
};

/* Where a board sends its frames: the next board of the ring. */
class NetLink {
public:
	virtual ~NetLink() = default;
	virtual void send(const NetFrame &frame) = 0;
};

/* Sega 837-13404, the communication board of Hikaru: a 68000 with 64 KB
 * of RAM, two banks of 64 KB of communication RAM shared with the host,
 * and two 315-5917 that send and receive frames on the optical ring.
 *
 * The 68000 runs the program the game uploads ("m3_comm.asm ver. 1.3",
 * the same in von4, sgnascar and podrace). Its map and registers are those
 * that program uses (notes 98 sections 4, 6, 10); what the 315-5917 do is
 * deduced from what the program needs, each choice marked HEURISTIC. */
class NetBoard837_13404 : public M68000Bus {
public:
	/* HEURISTIC: 40 MHz / 4; the 68000 is a 12 MHz part. */
	static constexpr uint32_t CLOCK_HZ = 10'000'000;
	static constexpr uint32_t RAM_SIZE = 0x10000;
	static constexpr uint32_t COMM_SIZE = 0x10000;

	NetBoard837_13404();

	void reset();

	/* The host window, bus page 0E: communication RAM 00000-0FFFF (the
	 * bank the 68000 is not using), registers 10000-101FF (one 16-bit
	 * register per 32-bit word), 68000 RAM 20000-3FFFF (one 16-bit word
	 * per 32-bit word). */
	uint32_t host_read(uint32_t offset, unsigned size);
	void host_write(uint32_t offset, unsigned size, uint32_t value);

	/* Runs the board to `clock` (its own clocks). Frames from the ring
	 * must have been given with receive() before. */
	void run_until(uint64_t clock);
	uint64_t clocks() const { return m_clock; }

	void set_link(NetLink *next) { m_next = next; }
	/* A frame from the previous board, `latency` clocks after it left; not
	 * before one received earlier. */
	void receive(const NetFrame &frame, uint64_t latency);

	/* What a save state keeps of it; the frames on their way to it apart
	 * (frames_save), as they have no fixed size */
	struct State {
		M68000State cpu;
		std::array<uint8_t, RAM_SIZE> ram;
		std::array<std::array<uint8_t, COMM_SIZE>, 2> comm;
		std::array<uint8_t, 0x100> io;
		std::array<uint16_t, 0x80> host_regs;
		uint64_t clock;
		uint64_t tx_end, timer_zero, timer_next;
		uint32_t bank, irq;
		uint16_t rx_addr, rx_size, tx_addr, tx_size;
		uint16_t status0, status1, timer, leds;
		uint8_t rx_armed, tx_busy, running, ticked;

		bool operator==(const State &) const = default;
	};
	void save(State &out) const;
	void load(const State &in);
	/* The frames waiting in it (their count, then each its size and its
	 * bytes), then those on their way to it (each when it comes, its end,
	 * its size and its bytes); false for bytes that are not that */
	std::vector<uint8_t> frames_save() const;
	bool frames_load(std::span<const uint8_t> bytes);

	/* The clock of its next event: its counter, the end of a frame it
	 * sends, a frame that comes (UINT64_MAX: none) */
	uint64_t next_event() const;
	/* Its 68000 did more than wait in its last run: it may answer before
	 * its next event */
	bool busy() const { return m_running && !m_quiet; }
	/* For the tests: ODIN_TURNS=0, the program runs every turn of its
	 * waits. The memories and the states must be the same. */
	void set_waits_counted(bool on)
	{
		m_waits_counted = on;
		m_proof.on = false;
	}
	/* For the tests: a byte of the communication RAM, and the bank of the
	 * 68000 */
	uint8_t comm_byte(unsigned bank, uint32_t offset) const { return m_comm[bank & 1][offset & (COMM_SIZE - 1)]; }
	unsigned bank() const { return m_bank; }
	/* For the tests: everything the board keeps, hashed (FNV-1a); its
	 * LEDs */
	uint64_t fingerprint() const;
	uint16_t leds() const { return m_leds; }
	/* What went out and came in since it was made (ODIN_STATS=1); held:
	 * came while its 68000 did not receive and waited for it; lost: came
	 * while its 68000 was held in reset; stale: waited longer than
	 * HELD_MOST and was lost when its 68000 received */
	struct Counts {
		uint64_t sent, received, held, lost, stale;
	};
	const Counts &counts() const { return m_counts; }
	/* For the tests */
	const M68000 &cpu() const { return m_cpu; }

	/* M68000Bus */
	uint16_t read16(uint32_t addr, unsigned fc) override;
	uint8_t read8(uint32_t addr, unsigned fc) override;
	void write16(uint32_t addr, uint16_t value, unsigned fc) override;
	void write8(uint32_t addr, uint8_t value, unsigned fc) override;

private:
	/* HEURISTIC: one tick of the 15-bit counter of IRQ5, 0.5 us, so that
	 * a turn of it (8000 ticks, 16.4 ms) is about a frame. */
	static constexpr uint64_t TICK = 5;
	/* HEURISTIC: 10 Mbit/s on the fibre, 2 bytes around each frame. */
	static constexpr uint64_t BYTE_CLOCKS = 8;
	static constexpr uint64_t FRAME_OVERHEAD = 2;
	/* HEURISTIC: the longest a frame waits in the chip for the 68000 to
	 * receive, 1 ms; an older one is lost, as a frame that comes while
	 * the board does not receive. The waits that save a ring from the
	 * jitter of Odins linked by a network are shorter than 0.05 ms; a
	 * board started late had frames wait for seconds, came in out of
	 * time and never joined. */
	static constexpr uint64_t HELD_MOST = 10000;

	enum Irq : unsigned { IRQ2 = 1u << 2, IRQ4 = 1u << 4, IRQ5 = 1u << 5, IRQ6 = 1u << 6 };

	/* A wait: a turn of a few instructions that only reads the RAM, with
	 * the CPU the same at its end, goes round the same way until an
	 * interrupt or a change of what it reads. */
	static constexpr unsigned MAX_TURN = 16;
	static constexpr unsigned MAX_READS = 16;
	struct Read {
		uint32_t addr;
		uint8_t size;
		uint16_t value;
	};
	struct Proof {
		bool on;
		M68000State cpu;	/* at the start of the turn */
		uint64_t length;	/* its clocks */
		unsigned reads;
		std::array<Read, MAX_READS> read;
	};

	struct Arrival {
		NetFrame frame;
		uint64_t at;
	};
	struct Held {
		NetFrame frame;
		uint64_t came;		/* the clock it came at */
	};

	uint8_t io_read(uint32_t reg);
	void io_write(uint32_t reg, uint8_t value);
	void ctrl_write(uint32_t reg, uint16_t value);
	void raise(unsigned irq) { m_irq |= irq; update_ipl(); }
	void lower(unsigned irq) { m_irq &= ~irq; update_ipl(); }
	void update_ipl();
	void start_transmit();
	void deliver(const NetFrame &frame);
	void schedule_timer();
	uint8_t &comm(unsigned bank, uint32_t offset) { return m_comm[bank][offset & (COMM_SIZE - 1)]; }
	/* The clock of the board now, also in the middle of a run of the
	 * 68000 (a register it writes, a frame it sends) */
	uint64_t now() const { return m_running ? m_cpu.cycles() - m_cpu_base : m_clock; }
	/* An event set at board clock `at` while the 68000 may be running:
	 * its run stops there, so the event is not taken late. */
	void event_at(uint64_t at)
	{
		if (m_running)
			m_cpu.end_run_at(at + m_cpu_base);
	}
	void wait_count(uint64_t stop);
	bool wait_prove(uint64_t stop);
	bool reads_same() const;
	uint16_t peek(uint32_t addr, unsigned size) const;
	void note_read(uint32_t addr, unsigned size, uint16_t value);

	M68000 m_cpu;
	std::vector<uint8_t> m_ram;
	std::array<std::vector<uint8_t>, 2> m_comm;
	unsigned m_bank;		/* the bank the 68000 sees at 80000 */

	std::array<uint8_t, 0x100> m_io;
	uint16_t m_rx_addr, m_rx_size, m_tx_addr, m_tx_size;
	uint16_t m_status0, m_status1;	/* 88 and 8A: what the host reads */
	bool m_rx_armed;
	bool m_tx_busy;
	uint64_t m_tx_end;

	uint16_t m_timer;		/* 40000+C0: the count that gives IRQ5 */
	bool m_ticked;			/* that count gave IRQ5 since 40000+E0 */
	uint64_t m_timer_zero;		/* the clock at which the count was 0 */
	uint64_t m_timer_next;
	uint16_t m_leds;
	unsigned m_irq;

	std::array<uint16_t, 0x80> m_host_regs;
	bool m_running;			/* host register C0: the 68000 out of reset */

	uint64_t m_clock;
	uint64_t m_cpu_base;		/* cycles of the 68000 at clock 0 */
	std::deque<Arrival> m_arrivals;
	std::deque<Held> m_held;	/* came while not receiving */
	NetLink *m_next;

	Counts m_counts{};
	Proof m_proof{};
	bool m_waits_counted = true;
	bool m_recording = false;	/* a turn being proven */
	bool m_touched = false;		/* a write or a register since the mark */
	bool m_quiet = false;		/* the last run touched nothing */
	unsigned m_recorded = 0;
	std::array<Read, MAX_READS> m_record{};
};

} /* namespace odin */

#endif
