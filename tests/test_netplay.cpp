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

static NetPacket MakeBlobHeader(uint32_t kind, uint32_t size)
{
	NetPacket p;
	memset(&p, 0, sizeof(p));
	p.magic = NET_MAGIC;
	p.type = NET_TYPE_BLOB;
	p.frame_seq = kind;
	p.send_tick = size;
	return p;
}

TEST_CASE(NetStreamWaitsForAWholeBlobAndKeepsThePacketsAfterIt)
{
	NetPacket blob_hdr = MakeBlobHeader(NET_BLOB_STATE, 1000);
	std::vector<uint8_t> payload(1000, NET_MAGIC); // full of bytes that look like packets
	NetPacket after;
	memset(&after, 0, sizeof(after));
	after.magic = NET_MAGIC;
	after.type = NET_TYPE_READY;

	NetStream st;
	NetPacket got;
	std::vector<uint8_t> blob;
	st.Feed((const uint8_t*)&blob_hdr, sizeof(blob_hdr));
	ASSERT_FALSE(st.Next(got, blob));
	st.Feed(payload.data(), 400);
	ASSERT_FALSE(st.Next(got, blob));
	st.Feed(payload.data() + 400, 600);
	st.Feed((const uint8_t*)&after, sizeof(after));
	ASSERT_TRUE(st.Next(got, blob));
	ASSERT_EQ((int)got.type, NET_TYPE_BLOB);
	ASSERT_EQ(blob.size(), (size_t)1000);
	ASSERT_TRUE(st.Next(got, blob));
	ASSERT_EQ((int)got.type, NET_TYPE_READY);
	ASSERT_FALSE(st.Next(got, blob));
}

TEST_CASE(NetStreamRefusesAnAbsurdBlobSize)
{
	NetPacket bad = MakeBlobHeader(NET_BLOB_STATE, NET_MAX_BLOB + 1);
	NetStream st;
	NetPacket got;
	std::vector<uint8_t> blob;
	st.Feed((const uint8_t*)&bad, sizeof(bad));
	ASSERT_FALSE(st.Next(got, blob));
}

static NetInput InputOf(int16_t v)
{
	NetInput in;
	memset(&in, 0, sizeof(in));
	in.buttons[0] = v;
	return in;
}

// Two machines through the same logic, messages delivered late on purpose: the
// same input must run on the same frame on both, whichever arrives first.
TEST_CASE(NetLockstepBothSidesRunTheSameInputOnTheSameFrame)
{
	NetLockstep a, b;
	a.Start(3);
	b.Start(3);
	std::vector<std::pair<uint32_t, NetInput>> to_a, to_b;
	std::vector<int> a_saw_b, b_saw_b, a_saw_a, b_saw_a;

	for (int step = 0; step < 300; step++)
	{
		for (int side = 0; side < 2; side++)
		{
			NetLockstep& me = side == 0 ? a : b;
			auto& outbox = side == 0 ? to_b : to_a;
			const NetInput in = InputOf((int16_t)(side * 1000 + step));
			const uint32_t n = me.QueueLocal(in);
			for (uint32_t i = 0; i < n; i++)
				outbox.push_back({ me.NextToSend() - n + i, in });
			NetInput mine, theirs;
			while (me.Get(me.Frame(), mine, theirs))
			{
				(side == 0 ? a_saw_a : b_saw_a).push_back(side == 0 ? mine.buttons[0] : theirs.buttons[0]);
				(side == 0 ? a_saw_b : b_saw_b).push_back(side == 0 ? theirs.buttons[0] : mine.buttons[0]);
				me.Advance();
				const uint32_t again = me.QueueLocal(in);
				for (uint32_t i = 0; i < again; i++)
					outbox.push_back({ me.NextToSend() - again + i, in });
			}
		}
		// Delivery lags behind the sender and differs per direction.
		if (step % 2 == 0)
		{
			for (auto& m : to_a) a.SetRemote(m.first, m.second);
			to_a.clear();
		}
		if (step % 3 == 0)
		{
			for (auto& m : to_b) b.SetRemote(m.first, m.second);
			to_b.clear();
		}
	}
	const size_t common = a_saw_a.size() < b_saw_a.size() ? a_saw_a.size() : b_saw_a.size();
	ASSERT_TRUE(common > 200);
	for (size_t i = 0; i < common; i++)
	{
		ASSERT_EQ(a_saw_a[i], b_saw_a[i]);
		ASSERT_EQ(a_saw_b[i], b_saw_b[i]);
	}
}

TEST_CASE(NetLockstepFirstFramesAreEmptyAndNothingRunsWithoutTheOtherInput)
{
	NetLockstep ls;
	ls.Start(2);
	NetInput mine, theirs;
	ASSERT_TRUE(ls.Get(0, mine, theirs));
	ASSERT_EQ((int)mine.buttons[0], 0);
	ASSERT_TRUE(ls.Get(1, mine, theirs));
	ls.QueueLocal(InputOf(7));
	ASSERT_EQ(ls.NextToSend(), (uint32_t)3);
	ASSERT_FALSE(ls.Ready(2));
	ls.SetRemote(2, InputOf(9));
	ASSERT_TRUE(ls.Get(2, mine, theirs));
	ASSERT_EQ((int)mine.buttons[0], 7);
	ASSERT_EQ((int)theirs.buttons[0], 9);
}

TEST_CASE(NetplayStateMaskIgnoresOnlyWhatChangedBetweenTwoSaves)
{
	std::vector<uint8_t> a(4096, 7), b(4096, 7);
	b[1001] = 9; // a clock inside the state
	const std::vector<uint32_t> mask = NetplayStateMask(a.data(), b.data(), a.size());
	ASSERT_EQ(mask.size(), (size_t)3);

	// The same game with another clock compares equal...
	std::vector<uint8_t> x = a, y = a;
	y[1001] = 200;
	NetplayApplyStateMask(x.data(), x.size(), mask);
	NetplayApplyStateMask(y.data(), y.size(), mask);
	ASSERT_EQ(NetplayHashState(x.data(), x.size()), NetplayHashState(y.data(), y.size()));

	// ...and a real difference anywhere else still counts.
	y[3000] = 1;
	NetplayApplyStateMask(y.data(), y.size(), mask);
	ASSERT_TRUE(NetplayHashState(x.data(), x.size()) != NetplayHashState(y.data(), y.size()));

	// Nothing changed, nothing left out.
	ASSERT_EQ(NetplayStateMask(a.data(), a.data(), a.size()).size(), (size_t)0);
}

TEST_CASE(NetplayLearnsAClockButNotARealSplit)
{
	std::vector<uint8_t> host(64 * 1024, 5), guest(64 * 1024, 5);
	guest[1145] = 0x0e; // the three bytes gambatte's clock moved in a real run
	guest[1690] = 0x01;
	guest[1815] = 0x02;
	std::vector<uint32_t> mask, previous;
	ASSERT_TRUE(NetplayLearnStateMask(host.data(), guest.data(), host.size(), mask, previous));
	NetplayApplyStateMask(host.data(), host.size(), mask);
	NetplayApplyStateMask(guest.data(), guest.size(), mask);
	ASSERT_EQ(NetplayHashState(host.data(), host.size()), NetplayHashState(guest.data(), guest.size()));

	// A game that went another way differs all over: never learned, the host resends.
	std::vector<uint32_t> before = mask;
	for (size_t i = 0; i < guest.size(); i += 97) guest[i] = (uint8_t)(i * 31);
	ASSERT_FALSE(NetplayLearnStateMask(host.data(), guest.data(), host.size(), mask, previous));
	ASSERT_EQ(mask.size(), before.size());

	// Unless the very same blocks differ again: pointers, which stay put. Here they
	// fit under the ceiling; a game that split would have moved by now.
	std::vector<uint8_t> p(64 * 1024, 0), q(64 * 1024, 0);
	for (size_t i = 0; i < 40; i++) q[i * 800] = 1;
	std::vector<uint32_t> ptr_mask, ptr_previous;
	ASSERT_FALSE(NetplayLearnStateMask(p.data(), q.data(), p.size(), ptr_mask, ptr_previous));
	ASSERT_TRUE(NetplayLearnStateMask(p.data(), q.data(), p.size(), ptr_mask, ptr_previous));
	ASSERT_TRUE(ptr_mask.size() > 32);

	// Nor can small differences grow it without end.
	std::vector<uint32_t> full;
	for (uint32_t i = 0; i < kNetMaskMaxBlocks; i++) full.push_back(i + 4096);
	std::vector<uint8_t> x(64 * 1024, 0), y(64 * 1024, 0);
	y[8] = 1;
	std::vector<uint32_t> none;
	ASSERT_FALSE(NetplayLearnStateMask(x.data(), y.data(), x.size(), full, none));
}

TEST_CASE(NetplayStateHashSeesAnyDifference)
{
	std::vector<uint8_t> x(4096, 0);
	const uint32_t h = NetplayHashState(x.data(), x.size());
	x[2000] = 1;
	ASSERT_TRUE(NetplayHashState(x.data(), x.size()) != h);
}
