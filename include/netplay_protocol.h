#ifndef NETPLAY_PROTOCOL_H_INCLUDED
#define NETPLAY_PROTOCOL_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

// Wire format and stream framing, kept out of netplay.cpp so it can be tested
// without a socket. The framing is where the real bug was: the old code did a
// single recv() straight into a struct and dropped anything that was not
// exactly one packet. TCP is a byte stream, so a split or coalesced delivery
// left the connection permanently misaligned.

#define NET_MAGIC        0x53
#define NET_TYPE_HELLO   1
#define NET_TYPE_INPUT   2
#define NET_TYPE_PING    3
#define NET_TYPE_BYE     4

#pragma pack(push, 1)
struct NetPacket
{
	uint8_t  magic;
	uint8_t  type;
	uint32_t frame_seq;
	uint32_t send_tick;   // sender's clock when it sent this
	uint32_t echo_tick;   // newest send_tick the sender had seen from us
	int16_t  buttons[16];
	int16_t  analog[2][2];
};
#pragma pack(pop)

// Pulls every complete packet out of a receive buffer.
//
// Resynchronises by scanning for the magic byte, so a corrupted or misaligned
// stream recovers instead of staying broken forever. Returns the number of
// bytes consumed; the caller keeps whatever is left for the next call.
//
//   data      raw bytes accumulated so far
//   len       how many bytes are valid
//   out       receives up to max_out parsed packets
//   out_count receives how many were parsed
size_t NetplayParseStream(const uint8_t* data, size_t len,
                          struct NetPacket* out, size_t max_out, size_t* out_count);

#endif
