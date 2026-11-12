// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * JVS host side and device base, from MAME src/devices/machine/jvshost
 * and jvsdev, rewritten for Odin. Odin is GPL-3.0; this file keeps its
 * BSD-3-Clause licence and copyright holder.
 *
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Odin changes: Copyright (C) 2026 Réda Chérif-Touil
 */

#include "odin/machine/cabinet/jvs.hpp"

#include <cstring>

namespace odin {

using namespace jvs;

namespace {

/* A command done: its report, then the `n` bytes it wrote after it. */
int32_t done(std::span<uint8_t> out, uint32_t &used, int32_t n, uint32_t request)
{
	out[0] = REPORT_NORMAL;
	used = static_cast<uint32_t>(1 + n);
	return static_cast<int32_t>(request);
}

/* A command refused: report 2 alone. The boards read on past the request
 * bytes it took (315-6215 at 050B, epr-21868 at 06DF). */
int32_t refused(std::span<uint8_t> out, uint32_t &used, size_t left, uint32_t request)
{
	out[0] = REPORT_PARAMETER_COUNT;
	used = 1;
	return static_cast<int32_t>(request < left ? request : left);
}

/* The sum of a frame, of every byte after the sync */
uint8_t sum_of(std::span<const uint8_t> bytes)
{
	uint8_t sum = 0;

	for (const uint8_t b : bytes)
		sum = static_cast<uint8_t>(sum + b);
	return sum;
}

} /* namespace */

/* Host */

JvsHost::JvsHost()
	: m_first(nullptr)
{
	reset();
}

void JvsHost::reset()
{
	m_send_size = 0;
	m_recv_size = 0;
	m_reply_cycles = 0;
	m_send.fill(0);
	m_recv.fill(0);
}

void JvsHost::add_device(JvsDevice *dev)
{
	if (m_first) {
		m_first->chain(dev);
	} else {
		m_first = dev;
	}
}

bool JvsHost::address_set_line() const
{
	return m_first && m_first->address_set_line();
}

void JvsHost::push(uint8_t value)
{
	if (m_send_size < BUFFER_SIZE) {
		m_send[m_send_size++] = value;
	}
}

/* Decoded frame: dest, length (data bytes plus the checksum), data. */
void JvsHost::commit_encoded()
{
	m_recv_size = 0;
	if (m_send_size == 0) {
		return;
	}
	const uint32_t size = decode(m_send, m_send_size);
	m_send_size = 0;
	/* The checksum is the sum of every byte after the sync; a node
	 * answers status 3 when it does not match (JVS sum error). */
	commit_raw(std::span<const uint8_t>(m_send.data(), size), sum_of(std::span<const uint8_t>(m_send.data(), size)) == m_send[size]);
}

void JvsHost::commit_raw(std::span<const uint8_t> frame, bool sum_ok)
{
	const uint32_t size = static_cast<uint32_t>(frame.size());
	m_recv_size = 0;
	m_reply_cycles = 0;
	if (size < 3 || frame[0] == HOST || frame[1] != size - 1 || !sum_ok) {
		/* Malformed or bad checksum: sum error. */
		m_recv[0] = HOST;
		m_recv[1] = 0x02;	/* the status and the sum */
		m_recv[2] = STATUS_CHECKSUM_ERROR;
		m_recv_size = 3;
		if (m_first && size >= 3 && !sum_ok)
			m_reply_cycles = m_first->sum_error_cycles(size - 2);
	} else if (m_first) {
		const uint32_t n = m_first->message(frame[0], frame.subspan(2),
		                                    std::span<uint8_t>(m_recv.data() + 2, BUFFER_SIZE - 3),
		                                    m_reply_cycles);
		if (n) {
			/* Reply header: the host is always destination 0. */
			m_recv[0] = HOST;
			m_recv[1] = static_cast<uint8_t>(n + 1);
			m_recv_size = n + 2;
		}
	}
	if (m_recv_size) {
		m_recv_size = encode(m_recv, m_recv_size);
	}
}

/* Strips the sync byte and unescapes D0 xx into xx+1. Returns the
 * decoded size without the checksum, which stays in the buffer right
 * after the data. */
uint32_t JvsHost::decode(std::span<uint8_t> buffer, uint32_t size)
{
	uint32_t pos = 0;
	for (uint32_t i = 0; i < size; i++) {
		uint8_t t = buffer[i];
		if (i == 0 && t == SYNC) {
			continue;
		}
		if (t == MARK) {
			i++;
			if (i >= size) {
				break;
			}
			t = static_cast<uint8_t>(buffer[i] + 1);
		}
		buffer[pos++] = t;
	}
	return pos ? pos - 1 : 0;
}

/* Appends the checksum, escapes D0 and E0 as D0 xx-1 and prepends the
 * sync byte. The buffer holds at most BUFFER_SIZE bytes; a reply that
 * would not fit after escaping is truncated, which the host sees as a
 * checksum error. */
uint32_t JvsHost::encode(std::span<uint8_t> buffer, uint32_t size)
{
	if (size == 0 || size + 1 > BUFFER_SIZE) {
		return 0;
	}
	buffer[size] = sum_of(buffer.first(size));
	size++;
	uint32_t add = 1;
	for (uint32_t i = 0; i < size; i++) {
		if (buffer[i] == MARK || buffer[i] == SYNC) {
			add++;
		}
	}
	if (size + add > BUFFER_SIZE) {
		return 0;
	}
	for (uint32_t i = size, j = add; i; i--) {
		const uint8_t t = buffer[i - 1];
		if (t == MARK || t == SYNC) {
			buffer[i + j - 1] = static_cast<uint8_t>(t - 1);
			buffer[i + j - 2] = MARK;
			j--;
		} else {
			buffer[i + j - 1] = t;
		}
	}
	buffer[0] = SYNC;
	return size + add;
}

/* Device */

JvsDevice::JvsDevice()
	: m_next(nullptr), m_address(BROADCAST)
{
}

void JvsDevice::reset()
{
	restart();
	if (m_next) {
		m_next->reset();
	}
}

/* The program runs again from 0000 (epr-21868 at 16F5, 315-6215 at 127B):
 * no address, its memory cleared. */
void JvsDevice::restart()
{
	m_address = BROADCAST;
	device_reset();
}

void JvsDevice::chain(JvsDevice *dev)
{
	if (m_next) {
		m_next->chain(dev);
	} else {
		m_next = dev;
	}
}

bool JvsDevice::address_set_line() const
{
	return m_address != BROADCAST && (!m_next || m_next->address_set_line());
}

uint32_t JvsDevice::message(uint8_t dest, std::span<const uint8_t> in, std::span<uint8_t> out, uint32_t &cycles)
{
	/* The time of a reply: nothing for a board whose program was not
	 * timed */
	const auto timed = [this, &in](uint32_t commands, uint32_t reply) {
		return commands ? commands + frame_cycles(in.size(), reply) : 0;
	};
	uint32_t size = 0;

	cycles = 0;
	/* Set address: taken by the farthest node that has none yet. */
	if (in.size() == 2 && in[0] == SET_ADDRESS) {
		if (m_next && !m_next->address_set_line()) {
			return m_next->message(dest, in, out, cycles);
		}
		m_address = in[1];
		out[0] = STATUS_NORMAL;
		out[1] = REPORT_NORMAL;
		cycles = timed(command_cycles(in, Outcome::DONE), 2);
		return 2;
	}

	if (dest == BROADCAST && !reset_in_any_frame()) {
		/* 315-6215 at 02F5 and 0400; any other frame to every node is
		 * left unanswered (0613). */
		if (in.size() >= 2 && in[0] == RESET && in[1] == RESET_CHECK) {
			restart();
		}
	} else if (dest == BROADCAST || dest == m_address) {
		uint32_t pos = 0;
		uint32_t used = 0;
		bool unknown = false;
		bool restarted = false;
		uint32_t commands = 0;
		out[pos++] = STATUS_NORMAL;
		size_t s = 0;
		while (s < in.size()) {
			const int32_t len = handle_message(in.subspan(s), out.subspan(pos), used);
			if (len == RESTART) {
				/* The reports built so far are lost. */
				restart();
				restarted = true;
				break;
			}
			if (len < 0) {
				/* The commands before it keep their reports (§ 4.2;
				 * 315-6215 at 0547, epr-21868 at 072F). */
				out[0] = STATUS_UNKNOWN_COMMAND;
				unknown = true;
				commands += command_cycles(in.subspan(s, 1), Outcome::UNKNOWN);
				break;
			}
			commands += command_cycles(in.subspan(s, static_cast<size_t>(len)),
			                           out[pos] == REPORT_NORMAL ? Outcome::DONE : Outcome::REFUSED);
			pos += used;
			s += static_cast<size_t>(len);
		}
		/* The status byte alone is not a reply when a node behind us
		 * may still answer a broadcast. */
		if (!restarted && (!m_next || pos > 1 || unknown)) {
			size = pos;
			cycles = timed(commands, size);
		}
	}

	if (m_next && (dest == BROADCAST || size == 0)) {
		return m_next->message(dest, in, out, cycles);
	}
	return size;
}

int32_t JvsDevice::handle_message(std::span<const uint8_t> in, std::span<uint8_t> out, uint32_t &used)
{
	used = 0;

	switch (in[0]) {
	case RESET:
		/* One is enough and nothing answers (epr-21868 at 058B jumps to
		 * 0000 whatever follows). The standard sends it twice. */
		return reset_in_any_frame() ? RESTART : -1;

	case IO_IDENTIFY: {
		const char *id = device_id();
		const size_t len = std::strlen(id) + 1;
		std::memcpy(out.data() + 1, id, len);
		return done(out, used, static_cast<int32_t>(len), 1);
	}

	case COMMAND_REVISION:
		out[1] = command_format_version();
		return done(out, used, 1, 1);

	case JVS_REVISION:
		out[1] = jvs_standard_version();
		return done(out, used, 1, 1);

	case COMM_VERSION:
		out[1] = comm_method_version();
		return done(out, used, 1, 1);

	case FEATURE_CHECK: {
		const int32_t n = function_list(out.subspan(1));
		out[1 + static_cast<uint32_t>(n)] = FUNC_END;
		return done(out, used, n + 1, 1);
	}

	case SWITCH_INPUTS: {
		const int32_t n = in.size() < 3 ? -1 : switches(out.subspan(1), in[1], in[2]);
		return n < 0 ? refused(out, used, in.size(), 3) : done(out, used, n, 3);
	}

	case COIN_INPUTS: {
		const int32_t n = in.size() < 2 ? -1 : coin_counters(out.subspan(1), in[1]);
		return n < 0 ? refused(out, used, in.size(), 2) : done(out, used, n, 2);
	}

	case ANALOG_INPUTS: {
		const int32_t n = in.size() < 2 ? -1 : analogs(out.subspan(1), in[1]);
		return n < 0 ? refused(out, used, in.size(), 2) : done(out, used, n, 2);
	}

	case COIN_COUNTER_DECREASE:
	case COIN_COUNTER_INCREASE: {
		const int32_t count = in.size() < 4 ? 0 : (in[2] << 8) | in[3];
		const bool taken = in.size() >= 4 && coin_add(in[1], in[0] == COIN_COUNTER_DECREASE ? -count : count);
		return taken ? done(out, used, 0, 4) : refused(out, used, in.size(), 4);
	}

	case GENERIC_OUTPUT_1: {
		const uint32_t bytes = in.size() < 2 ? 0 : in[1];
		const bool taken = in.size() >= 2u + bytes && swoutputs(in.subspan(2, bytes));
		return taken ? done(out, used, 0, 2 + bytes)
		             : refused(out, used, in.size(), refused_outputs_skip_data() ? 2 + bytes : 2);
	}

	/* The programs of the 837-13551 (315-6215, jump table at 3000) and of
	 * the 837-13741 (epr-21868, 058B) take no other command: 31, 33, 34,
	 * 36, 37 and 38 are unknown to both. */
	default:
		return -1;
	}
}

/* Sega I/O boards */

JvsIoBoard::JvsIoBoard(JvsInputs &inputs)
	: m_inputs(inputs)
{
	JvsIoBoard::device_reset();
}

void JvsIoBoard::device_reset()
{
	m_coin_counter.fill(0);
	m_outputs.fill(0);
}

void JvsIoBoard::inc_coin(uint8_t slot)
{
	/* Held at 3FFF (315-6215 at 0804, epr-21868 at 1286) */
	if (slot < COIN_SLOTS && m_coin_counter[slot] < 0x3FFF) {
		m_coin_counter[slot]++;
	}
}

/* Two bytes per slot: bits 15-14 condition (0 normal), 14-bit count. */
int32_t JvsIoBoard::coin_counters(std::span<uint8_t> out, uint8_t slots)
{
	/* 0 slots is refused too (315-6215 at 0E4D, epr-21868 at 1A2F) */
	if (slots == 0 || slots > COIN_SLOTS) {
		return -1;
	}
	uint32_t pos = 0;
	for (uint8_t i = 0; i < slots; i++) {
		out[pos++] = static_cast<uint8_t>(m_coin_counter[i] >> 8);
		out[pos++] = static_cast<uint8_t>(m_coin_counter[i]);
	}
	return static_cast<int32_t>(pos);
}

/* A counter that would go below 0 or above 3FFF is refused and kept as it
 * is (315-6215 at 1141; epr-21868 at 1D6F and 1EB8). */
bool JvsIoBoard::coin_add(uint8_t slot, int32_t count)
{
	const int index = coin_slot(slot);

	if (index < 0) {
		return false;
	}
	const int32_t value = static_cast<int32_t>(m_coin_counter[static_cast<size_t>(index)]) + count;
	if (value < 0 || value > 0x3FFF) {
		return false;
	}
	m_coin_counter[static_cast<size_t>(index)] = static_cast<uint16_t>(value);
	return true;
}

int32_t JvsIoBoard::analogs(std::span<uint8_t> out, uint8_t channels)
{
	/* 0 channels is refused too (315-6215 at 0A0F, epr-21868 at 1B15) */
	if (channels == 0 || channels > ANALOG_CHANNELS) {
		return -1;
	}
	uint32_t pos = 0;
	for (uint8_t i = 0; i < channels; i++) {
		const uint16_t value = m_inputs.analog(i);
		/* The converters give 8 bits; the low byte goes out as 0
		 * (315-6215 at 0A46, epr-21868 at 1B1F). */
		out[pos++] = static_cast<uint8_t>(value >> 8);
		out[pos++] = 0;
	}
	return static_cast<int32_t>(pos);
}

void JvsIoBoard::save(JvsIoBoardState &out) const
{
	JvsDevice::save(out.node);
	out.coin_counter = m_coin_counter;
	out.outputs = m_outputs;
}

void JvsIoBoard::load(const JvsIoBoardState &in)
{
	JvsDevice::load(in.node);
	m_coin_counter = in.coin_counter;
	m_outputs = in.outputs;
}

void JvsHost::save(JvsHostState &out) const
{
	out.send = m_send;
	out.recv = m_recv;
	out.send_size = m_send_size;
	out.recv_size = m_recv_size;
}

void JvsHost::load(const JvsHostState &in)
{
	m_send = in.send;
	m_recv = in.recv;
	m_send_size = in.send_size;
	m_recv_size = in.recv_size;
}

void JvsDevice::save(JvsDeviceState &out) const
{
	out.unused = 0;
	out.address = m_address;
}

void JvsDevice::load(const JvsDeviceState &in)
{
	m_address = in.address;
}

} /* namespace odin */
