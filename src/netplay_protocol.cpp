#include "netplay_protocol.h"

#include <string.h>

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
		if (out[count].type < NET_TYPE_HELLO || out[count].type > NET_TYPE_BYE)
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
