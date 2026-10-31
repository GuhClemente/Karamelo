#include "netplay_protocol.h"

#include <string.h>
#include <cstddef>
#include <algorithm>

size_t NetplayParseStream(const uint8_t* data, size_t len,
                          struct NetPacket* out, size_t max_out, size_t* out_count)
{
	size_t count = 0;
	size_t pos = 0;

	if (out_count) *out_count = 0;
	if (!data || !out || max_out == 0) return 0;

	while (count < max_out)
	{
		// Resynchronise: find the next plausible packet start.
		while (pos < len && data[pos] != NET_MAGIC) pos++;

		// Not enough bytes left for a whole packet - keep them for next time.
		if (len - pos < sizeof(struct NetPacket)) break;

		memcpy(&out[count], data + pos, sizeof(struct NetPacket));

		// A magic byte can legitimately appear inside payload data, so a
		// candidate whose type is nonsense is treated as a false start and we
		// resume scanning one byte later.
		if (out[count].type < NET_TYPE_HELLO || out[count].type > NET_TYPE_SESSION_END)
		{
			pos++;
			continue;
		}

		pos += sizeof(struct NetPacket);
		count++;
	}

	if (out_count) *out_count = count;
	return pos;
}

void NetStream::Feed(const uint8_t* data, size_t len)
{
	// Everything already read goes first, or a long session grows without end.
	if (pos_ > 0 && pos_ >= buf_.size() / 2)
	{
		buf_.erase(buf_.begin(), buf_.begin() + (std::ptrdiff_t)pos_);
		pos_ = 0;
	}
	buf_.insert(buf_.end(), data, data + len);
}

bool NetStream::Next(NetPacket& out, std::vector<uint8_t>& blob)
{
	for (;;)
	{
		while (pos_ < buf_.size() && buf_[pos_] != NET_MAGIC) pos_++;
		if (buf_.size() - pos_ < sizeof(NetPacket)) return false;

		memcpy(&out, buf_.data() + pos_, sizeof(NetPacket));
		if (out.type < NET_TYPE_HELLO || out.type > NET_TYPE_SESSION_END ||
		    (out.type == NET_TYPE_BLOB && out.send_tick > NET_MAX_BLOB))
		{
			pos_++;
			continue;
		}
		if (out.type != NET_TYPE_BLOB)
		{
			pos_ += sizeof(NetPacket);
			return true;
		}
		if (buf_.size() - pos_ - sizeof(NetPacket) < out.send_tick) return false;
		const uint8_t* payload = buf_.data() + pos_ + sizeof(NetPacket);
		blob.assign(payload, payload + out.send_tick);
		pos_ += sizeof(NetPacket) + out.send_tick;
		return true;
	}
}

void NetLockstep::Start(uint32_t delay)
{
	memset(local_, 0, sizeof(local_));
	memset(remote_, 0, sizeof(remote_));
	for (uint32_t i = 0; i < kWindow; i++)
	{
		local_tag_[i] = 0xFFFFFFFFu;
		remote_tag_[i] = 0xFFFFFFFFu;
	}
	delay_ = delay;
	frame_ = 0;
	next_send_ = delay;
	for (uint32_t f = 0; f < delay && f < kWindow; f++)
	{
		local_tag_[f] = f;
		remote_tag_[f] = f;
	}
}

uint32_t NetLockstep::QueueLocal(const NetInput& input)
{
	uint32_t queued = 0;
	while (next_send_ <= frame_ + delay_)
	{
		local_[next_send_ % kWindow] = input;
		local_tag_[next_send_ % kWindow] = next_send_;
		next_send_++;
		queued++;
	}
	return queued;
}

void NetLockstep::SetRemote(uint32_t frame, const NetInput& input)
{
	remote_[frame % kWindow] = input;
	remote_tag_[frame % kWindow] = frame;
}

bool NetLockstep::HasRemote(uint32_t frame) const
{
	return remote_tag_[frame % kWindow] == frame;
}

bool NetLockstep::Ready(uint32_t frame) const
{
	return local_tag_[frame % kWindow] == frame && remote_tag_[frame % kWindow] == frame;
}

bool NetLockstep::Get(uint32_t frame, NetInput& local, NetInput& remote) const
{
	if (!Ready(frame)) return false;
	local = local_[frame % kWindow];
	remote = remote_[frame % kWindow];
	return true;
}

uint32_t NetplayHashState(const void* data, size_t size)
{
	uint32_t h = 2166136261u;
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 16777619u;
	return h;
}

std::vector<uint32_t> NetplayStateMask(const uint8_t* a, const uint8_t* b, size_t size)
{
	std::vector<uint32_t> blocks;
	const size_t count = size / 8 + 1;
	uint32_t last = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; i += 8)
	{
		const size_t n = size - i < 8 ? size - i : 8;
		if (memcmp(a + i, b + i, n) == 0) continue;
		const uint32_t block = (uint32_t)(i / 8);
		for (uint32_t k = block == 0 ? 0 : block - 1; k <= block + 1 && k < count; k++)
		{
			if (last != 0xFFFFFFFFu && k <= last) continue;
			blocks.push_back(k);
			last = k;
		}
	}
	return blocks;
}

void NetplayApplyStateMask(uint8_t* state, size_t size, const std::vector<uint32_t>& blocks)
{
	for (uint32_t block : blocks)
	{
		const size_t at = (size_t)block * 8;
		if (at >= size) continue;
		memset(state + at, 0, size - at < 8 ? size - at : 8);
	}
}

bool NetplayLearnStateMask(const uint8_t* a, const uint8_t* b, size_t size, std::vector<uint32_t>& mask,
                           std::vector<uint32_t>& previous_diff)
{
	const std::vector<uint32_t> diff = NetplayStateMask(a, b, size);
	const bool stands_still = !diff.empty() && diff == previous_diff;
	previous_diff = diff;
	if (diff.size() > kNetMaskMaxGrowth && !stands_still) return false;
	std::vector<uint32_t> merged = mask;
	for (uint32_t block : diff)
		if (std::find(merged.begin(), merged.end(), block) == merged.end())
			merged.push_back(block);
	if (merged.size() > kNetMaskMaxBlocks) return false;
	mask = merged;
	return true;
}
