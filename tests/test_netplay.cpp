#include "test_framework.h"

// The shipped protocol, not a copy. The old test declared its own duplicate of
// the packet struct and then asserted on memcpy between two local variables,
// so it could not have caught anything - least of all the framing bug that
// made the connection unusable.
#include "netplay_protocol.h"

#include <string.h>
#include <vector>

static NetPacket MakeInputPacket(uint32_t seq, int16_t first_button)
{
	NetPacket p;
	memset(&p, 0, sizeof(p));
	p.magic = NET_MAGIC;
	p.type = NET_TYPE_INPUT;
	p.frame_seq = seq;
	p.buttons[0] = first_button;
	p.analog[0][0] = 15000;
	return p;
}

TEST_CASE(NetplayPacketLayoutIsStable)
{
	// Both ends must agree byte for byte.
	ASSERT_EQ(sizeof(NetPacket), (size_t)54);

	NetPacket p = MakeInputPacket(1024, 1);
	ASSERT_EQ(p.magic, (uint8_t)NET_MAGIC);
	ASSERT_EQ(p.frame_seq, (uint32_t)1024);
	ASSERT_EQ(p.analog[0][0], (int16_t)15000);
}

TEST_CASE(NetplayParsesWholePackets)
{
	NetPacket a = MakeInputPacket(1, 1);
	NetPacket b = MakeInputPacket(2, 0);

	uint8_t stream[sizeof(NetPacket) * 2];
	memcpy(stream, &a, sizeof(a));
	memcpy(stream + sizeof(a), &b, sizeof(b));

	NetPacket out[8];
	size_t count = 0;
	size_t used = NetplayParseStream(stream, sizeof(stream), out, 8, &count);

	ASSERT_EQ(count, (size_t)2);
	ASSERT_EQ(used, sizeof(stream));
	ASSERT_EQ(out[0].frame_seq, (uint32_t)1);
	ASSERT_EQ(out[1].frame_seq, (uint32_t)2);
}

TEST_CASE(NetplayKeepsPartialPacketForNextRead)
{
	// This is the case the old code got wrong: it consumed the bytes of a split
	// delivery and threw them away, so the stream stayed misaligned forever.
	NetPacket a = MakeInputPacket(7, 1);

	uint8_t stream[sizeof(NetPacket)];
	memcpy(stream, &a, sizeof(a));

	const size_t partial = sizeof(NetPacket) - 10;

	NetPacket out[4];
	size_t count = 0;
	size_t used = NetplayParseStream(stream, partial, out, 4, &count);

	// Nothing complete yet, and nothing consumed - the caller keeps the bytes.
	ASSERT_EQ(count, (size_t)0);
	ASSERT_EQ(used, (size_t)0);

	// The rest arrives and the packet comes out whole.
	used = NetplayParseStream(stream, sizeof(stream), out, 4, &count);
	ASSERT_EQ(count, (size_t)1);
	ASSERT_EQ(used, sizeof(NetPacket));
	ASSERT_EQ(out[0].frame_seq, (uint32_t)7);
}

TEST_CASE(NetplayResynchronisesAfterGarbage)
{
	// Junk ahead of a good packet must not poison the rest of the session.
	NetPacket a = MakeInputPacket(99, 1);

	std::vector<uint8_t> stream;
	const uint8_t junk[] = { 0x00, 0xFF, 0x12, 0x53, 0x99, 0x01 };
	stream.insert(stream.end(), junk, junk + sizeof(junk));

	const uint8_t* raw = (const uint8_t*)&a;
	stream.insert(stream.end(), raw, raw + sizeof(a));

	NetPacket out[4];
	size_t count = 0;
	NetplayParseStream(stream.data(), stream.size(), out, 4, &count);

	ASSERT_EQ(count, (size_t)1);
	ASSERT_EQ(out[0].frame_seq, (uint32_t)99);
}

TEST_CASE(NetplayDrainsBurstWithoutFallingBehind)
{
	// A burst has to come out in one pass. Reading one packet per frame made
	// the backlog - and the latency - grow without bound.
	const int kBurst = 12;
	std::vector<uint8_t> stream;

	for (int i = 0; i < kBurst; i++)
	{
		NetPacket p = MakeInputPacket((uint32_t)i, (int16_t)(i & 1));
		const uint8_t* raw = (const uint8_t*)&p;
		stream.insert(stream.end(), raw, raw + sizeof(p));
	}

	NetPacket out[32];
	size_t count = 0;
	size_t used = NetplayParseStream(stream.data(), stream.size(), out, 32, &count);

	ASSERT_EQ(count, (size_t)kBurst);
	ASSERT_EQ(used, stream.size());
	ASSERT_EQ(out[kBurst - 1].frame_seq, (uint32_t)(kBurst - 1));
}

TEST_CASE(NetplayRespectsOutputCapacity)
{
	std::vector<uint8_t> stream;
	for (int i = 0; i < 10; i++)
	{
		NetPacket p = MakeInputPacket((uint32_t)i, 0);
		const uint8_t* raw = (const uint8_t*)&p;
		stream.insert(stream.end(), raw, raw + sizeof(p));
	}

	NetPacket out[4];
	size_t count = 0;
	size_t used = NetplayParseStream(stream.data(), stream.size(), out, 4, &count);

	// Only what fits, and only those bytes consumed - the rest stays queued.
	ASSERT_EQ(count, (size_t)4);
	ASSERT_EQ(used, sizeof(NetPacket) * 4);
}
