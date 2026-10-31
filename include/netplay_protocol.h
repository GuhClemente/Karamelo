#ifndef NETPLAY_PROTOCOL_H_INCLUDED
#define NETPLAY_PROTOCOL_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

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
#define NET_TYPE_BLOB        5  // frame_seq=kind, send_tick=payload bytes, echo_tick=argument; raw payload follows
#define NET_TYPE_READY       6  // the player has the session's game running
#define NET_TYPE_STATE_ACK   7  // the state arrived and was loaded
#define NET_TYPE_HASH        8  // frame_seq=frame, send_tick=hash of the game state at that frame
#define NET_TYPE_SESSION_END 9  // the sender's game closed: stop waiting for its input

#define NET_BLOB_SESSION 1      // which game: "core\nrom file name\nrom size\n"
#define NET_BLOB_STATE   2      // savestate; echo_tick = input delay in frames
#define NET_BLOB_MASK    3      // parts of the state that are not the game; see NetplayStateMask
#define NET_BLOB_CHECK   4      // the guest's state at frame echo_tick, after the hashes disagreed

#define NET_PROTOCOL_VERSION 2
#define NET_MAX_BLOB (256u * 1024u * 1024u)

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

// The same framing with blobs: a packet of type NET_TYPE_BLOB is followed by
// send_tick raw bytes, which would look like garbage to the scan above.
class NetStream
{
public:
	void Clear() { buf_.clear(); pos_ = 0; }
	void Feed(const uint8_t* data, size_t len);
	// True when a whole packet (and its payload, for a blob) came out.
	bool Next(NetPacket& out, std::vector<uint8_t>& blob);
	size_t Pending() const { return buf_.size() - pos_; }
private:
	std::vector<uint8_t> buf_;
	size_t pos_ = 0;
};

struct NetInput
{
	int16_t buttons[16];
	int16_t analog[2][2];
};

// Who pressed what on which frame, for two machines that must run the same
// frames with the same input. Each side's input for frame F+delay goes out when
// it starts frame F, so neither has to wait as long as the link is faster than
// the delay. No frame runs until both inputs for it are here.
//
// Frames below start+delay have no input from anyone yet: both sides agree they
// are empty.
class NetLockstep
{
public:
	static const uint32_t kWindow = 1024;
	NetLockstep() { Start(0); }
	void Start(uint32_t delay);
	uint32_t Delay() const { return delay_; }
	uint32_t Frame() const { return frame_; }
	void SetFrame(uint32_t frame) { frame_ = frame; }
	void Advance() { frame_++; }
	// Local input of this frame goes to the frames not sent yet. Returns how
	// many to send; they are NextToSend()-n .. NextToSend()-1.
	uint32_t QueueLocal(const NetInput& input);
	uint32_t NextToSend() const { return next_send_; }
	void SetRemote(uint32_t frame, const NetInput& input);
	bool Ready(uint32_t frame) const;
	bool HasRemote(uint32_t frame) const;
	bool Get(uint32_t frame, NetInput& local, NetInput& remote) const;
private:
	NetInput local_[kWindow];
	NetInput remote_[kWindow];
	uint32_t local_tag_[kWindow];
	uint32_t remote_tag_[kWindow];
	uint32_t delay_ = 0;
	uint32_t frame_ = 0;
	uint32_t next_send_ = 0;
};

// 32-bit FNV-1a: comparing two states, not protecting anything.
uint32_t NetplayHashState(const void* data, size_t size);

// Some cores store more than the game in a state: mGBA records when it was made.
// Two saves in a row with no frame between them can only differ in such bytes, so
// what differs (in blocks of 8, with a neighbour on each side, because a clock
// that ticks changes more bytes than the ones seen changing) is left out of the
// comparison. Returns the block numbers.
std::vector<uint32_t> NetplayStateMask(const uint8_t* a, const uint8_t* b, size_t size);
void NetplayApplyStateMask(uint8_t* state, size_t size, const std::vector<uint32_t>& blocks);

// Two states that disagree: a clock a core keeps inside them moves a few bytes
// and the game stays the same; a real split spreads across the state within a
// frame or two. True when the difference is small enough to be such a clock and
// the blocks can join the mask; the mask has a ceiling so a slow real split
// cannot hide in it forever. A bigger difference is learned only when exactly the
// same blocks differ twice in a row: pointers a core keeps in its state differ
// between two processes and never move, while a game that split changes every time.
static const size_t kNetMaskMaxBlocks = 512;
static const size_t kNetMaskMaxGrowth = 32;
bool NetplayLearnStateMask(const uint8_t* a, const uint8_t* b, size_t size, std::vector<uint32_t>& mask,
                           std::vector<uint32_t>& previous_diff);

#endif
