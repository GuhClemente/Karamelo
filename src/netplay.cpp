#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <algorithm>
#include <filesystem>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
// accept()/getsockopt() take an int* for the length out-param on Winsock,
// where POSIX's socklen_t is unsigned - declaring the locals as socklen_t on
// both platforms means the call sites need no #ifdef of their own.
typedef int socklen_t;
#else
// getaddrinfo/freeaddrinfo/inet_ntop/gethostname/ntohl/setsockopt/getsockopt/
// bind/listen/accept/connect/send/recv/select and the sockaddr* types are the
// same shape on POSIX as on Winsock - only the handful of Windows-only names
// below need a stand-in. FIONBIO is defined by <sys/ioctl.h> on Linux, same
// as ioctlsocket's own contract.
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
typedef int SOCKET;
typedef int BOOL;
typedef unsigned long u_long;
#define TRUE 1
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(s) close(s)
#define ioctlsocket(s, cmd, argp) ioctl(s, cmd, argp)
#define WSAGetLastError() errno
#define WSAEWOULDBLOCK EWOULDBLOCK
static inline void WSACleanup() {}
// Both call sites in this file pass _TRUNCATE, i.e. "always null-terminate,
// silently drop what doesn't fit" - the one behavior this stand-in needs to
// match, not a general strncpy_s replacement.
#define _TRUNCATE ((size_t)-1)
static inline void strncpy_s(char* dst, size_t dst_size, const char* src, size_t) {
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <mutex>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstddef>

#include "netplay.h"
#include "menu.h" // Tr(): every message on screen follows the language setting
#include "core_runner.h"
#include "netplay_protocol.h"
#include "osd.h"
#include "desktop_chat.h"
#include "voice_chat.h"

namespace fs = std::filesystem;

static NetplayRole  net_role = NETPLAY_NONE;
static NetplayState net_state = NETPLAY_DISCONNECTED;
static SOCKET       listen_sock = INVALID_SOCKET;
static SOCKET       peer_sock = INVALID_SOCKET;
static std::string  peer_ip_str;
static std::string  local_ip_str = "127.0.0.1";
static std::string  status_str = "Disconnected";
static int          ping_ms = 0;

// Connection management runs on the UI thread while input exchange runs on the
// core thread, so every socket touch is serialised. Recursive: NetplayUpdate()
// and NetplaySyncInputs() call NetplayDisconnect() while already holding it.
static std::recursive_mutex net_lock;

// Partial deliveries are normal on TCP; leftovers live here until the rest
// arrives.
static NetStream net_stream;

// What makes both machines run the same game. Touched under net_lock.
struct SyncState
{
	NetLockstep lockstep;
	bool  in_lockstep = false;

	// Host
	std::string session_rom;     // the game the guest was last told about
	bool  session_sent = false;
	bool  guest_ready = false;
	bool  want_resync = false;

	// Guest
	std::string want_core, want_rom;
	unsigned long long want_size = 0;
	bool  have_session = false;
	bool  ready_sent = false;
	bool  load_tried = false;
	std::vector<uint8_t> pending_state;
	bool  have_pending_state = false;
	uint32_t pending_delay = 0;
	uint32_t pending_frame = 0;

	bool  state_acked = false;
	std::vector<uint8_t> scratch;
	std::vector<uint32_t> state_mask;
	std::vector<uint32_t> previous_diff;
	std::vector<uint8_t> hashed;   // the masked state behind the newest hash
	uint32_t hashed_frame = 0xFFFFFFFFu;
	struct Hash { uint32_t frame; uint32_t value; };
	std::vector<Hash> local_hashes, remote_hashes;
	NetplayStats stats;
	unsigned resync_budget = 5;
	bool  told_unstable = false;
};
static SyncState sync_state;

static const uint32_t kHashEveryFrames = 60;
static const size_t   kCheapResyncBytes = 8u * 1024u * 1024u;
static const size_t   kMaxHashedState = 64u * 1024u * 1024u;

static uint32_t last_peer_tick = 0;   // newest send_tick seen, echoed back
static uint32_t pending_tick = 0;     // our send_tick awaiting an echo

// Milliseconds on a monotonic clock, truncated to 32 bits like the old
// GetTickCount() this replaces - only ever used for round-trip deltas
// (send_tick echoed back by the peer), so the wraparound and the fact that
// both sides run their own independent clock are both fine.
static uint32_t TickCountMs()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void SetSocketNonBlocking(SOCKET s)
{
	u_long mode = 1;
	ioctlsocket(s, FIONBIO, &mode);
}

static void ConfigurePeerSocket(SOCKET s)
{
	SetSocketNonBlocking(s);

	// Without this, Nagle coalesces our 54-byte per-frame packets and adds up
	// to 40ms of latency - fatal for a netplay link.
	BOOL nodelay = TRUE;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
}

static bool IsPrivateIpv4(uint32_t ip)
{
	uint8_t a = (uint8_t)(ip >> 24), b = (uint8_t)(ip >> 16);
	return (a == 192 && b == 168) || a == 10 || (a == 172 && b >= 16 && b <= 31);
}

std::vector<NetplayLocalInterface> NetplayGetLocalInterfaces()
{
	std::vector<NetplayLocalInterface> out;
	auto add = [&out](uint32_t ip, uint32_t broadcast, bool lan_candidate) {
		if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE) return; // loopback, link-local
		NetplayLocalInterface nic;
		char buf[INET_ADDRSTRLEN] = { 0 };
		in_addr a;
		a.s_addr = htonl(ip);
		if (!inet_ntop(AF_INET, &a, buf, sizeof(buf))) return;
		nic.ip = buf;
		if (broadcast != 0 && broadcast != ip) {
			a.s_addr = htonl(broadcast);
			if (inet_ntop(AF_INET, &a, buf, sizeof(buf))) nic.broadcast = buf;
		}
		nic.lan_candidate = lan_candidate;
		out.push_back(nic);
	};
#ifdef _WIN32
	ULONG size = 32 * 1024;
	std::vector<uint8_t> mem(size);
	IP_ADAPTER_ADDRESSES* list = (IP_ADAPTER_ADDRESSES*)mem.data();
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
	ULONG r = GetAdaptersAddresses(AF_INET, flags, NULL, list, &size);
	if (r == ERROR_BUFFER_OVERFLOW) {
		mem.resize(size);
		list = (IP_ADAPTER_ADDRESSES*)mem.data();
		r = GetAdaptersAddresses(AF_INET, flags, NULL, list, &size);
	}
	if (r != NO_ERROR) return out;
	for (IP_ADAPTER_ADDRESSES* ad = list; ad; ad = ad->Next) {
		if (ad->OperStatus != IfOperStatusUp) continue;
		if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK || ad->IfType == IF_TYPE_TUNNEL ||
		    ad->IfType == IF_TYPE_PPP || ad->IfType == IF_TYPE_PROP_VIRTUAL) continue;
		const bool real_card = ad->FirstGatewayAddress != NULL &&
		                       (ad->IfType == IF_TYPE_ETHERNET_CSMACD || ad->IfType == IF_TYPE_IEEE80211);
		for (IP_ADAPTER_UNICAST_ADDRESS* u = ad->FirstUnicastAddress; u; u = u->Next) {
			if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
			uint32_t ip = ntohl(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
			int bits = u->OnLinkPrefixLength;
			uint32_t bcast = (bits > 0 && bits < 31) ? (ip | (0xFFFFFFFFu >> bits)) : 0;
			add(ip, bcast, real_card);
		}
	}
#else
	ifaddrs* list = NULL;
	if (getifaddrs(&list) != 0) return out;
	for (ifaddrs* it = list; it; it = it->ifa_next) {
		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
		if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
		std::string name = it->ifa_name ? it->ifa_name : "";
		bool is_virtual = false;
		for (const char* prefix : { "docker", "br-", "veth", "virbr", "vmnet", "vboxnet", "tun", "tap", "wg", "utun", "zt", "tailscale" })
			if (name.rfind(prefix, 0) == 0) is_virtual = true;
		if (is_virtual && (it->ifa_flags & IFF_POINTOPOINT)) continue;
		uint32_t ip = ntohl(((sockaddr_in*)it->ifa_addr)->sin_addr.s_addr);
		uint32_t bcast = 0;
		if ((it->ifa_flags & IFF_BROADCAST) && it->ifa_broadaddr)
			bcast = ntohl(((sockaddr_in*)it->ifa_broadaddr)->sin_addr.s_addr);
		add(ip, bcast, !is_virtual);
	}
	freeifaddrs(list);
#endif
	return out;
}

void NetplayInit()
{
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
#endif

	// A private range alone does not make it the LAN: WSL's virtual switch is
	// 172.x and may come first, and this is the address a room code carries.
	for (const auto& nic : NetplayGetLocalInterfaces()) {
		in_addr a;
		if (nic.lan_candidate && inet_pton(AF_INET, nic.ip.c_str(), &a) == 1 && IsPrivateIpv4(ntohl(a.s_addr))) {
			local_ip_str = nic.ip;
			return;
		}
	}

	// gethostbyname returned only the first address, which on a machine with a
	// virtual display or VPN adapter is usually the wrong one - and this is the
	// address the user reads out to a friend. Walk every address and prefer a
	// private LAN range.
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != 0) return;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* results = NULL;
	if (getaddrinfo(hostname, NULL, &hints, &results) != 0) return;

	std::string first_seen;
	for (struct addrinfo* it = results; it; it = it->ai_next)
	{
		if (it->ai_family != AF_INET) continue;

		struct sockaddr_in* sa = (struct sockaddr_in*)it->ai_addr;
		char buf[INET_ADDRSTRLEN] = { 0 };
		if (!inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) continue;

		uint32_t ip = ntohl(sa->sin_addr.s_addr);
		uint8_t a = (uint8_t)(ip >> 24), b = (uint8_t)(ip >> 16);

		if (a == 127) continue;                       // loopback
		if (a == 169 && b == 254) continue;           // link-local, no route

		if (first_seen.empty()) first_seen = buf;

		bool is_lan = (a == 192 && b == 168) || (a == 10) ||
		              (a == 172 && b >= 16 && b <= 31);
		if (is_lan) { local_ip_str = buf; freeaddrinfo(results); return; }
	}

	if (!first_seen.empty()) local_ip_str = first_seen;
	freeaddrinfo(results);
}

void NetplayShutdown()
{
	NetplayDisconnect();
	WSACleanup();
}

// local_ip_str/status_str are reassigned under net_lock from the core thread
// (NetplaySyncInputs's send-error path calls NetplayDisconnect()) as well as
// the UI thread, so handing out .c_str() of the live std::string without the
// lock is the same dangling-pointer-on-reallocation hazard already fixed for
// core_runner.cpp's loaded_game_name/loaded_rom_dir - copy into a thread_local
// buffer under the lock instead.
const char* NetplayGetPeerIp()
{
	static thread_local char buf[64];
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	strncpy_s(buf, sizeof(buf), peer_ip_str.c_str(), _TRUNCATE);
	return buf;
}

const char* NetplayGetLocalIp()
{
	static thread_local char buf[64];
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	strncpy_s(buf, sizeof(buf), local_ip_str.c_str(), _TRUNCATE);
	return buf;
}

NetplayRole NetplayGetRole()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return net_role;
}

NetplayState NetplayGetState()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return net_state;
}

const char* NetplayGetStatusString()
{
	static thread_local char buf[160];
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	strncpy_s(buf, sizeof(buf), status_str.c_str(), _TRUNCATE);
	return buf;
}

int NetplayGetPingMs()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return ping_ms;
}

// NetplayStartHost/StartClient/Disconnect are called from the UI thread
// (the Netplay menu) while NetplaySyncInputs()/NetplayUpdate() touch these
// same globals - peer_sock included, mid send()/recv() - from the core
// thread under net_lock. Without taking the same lock here, a disconnect
// during an active session can close peer_sock (and hand its value back to
// the OS to reuse) at the exact moment the core thread is using it.
bool NetplayStartHost(int port)
{
	NetplayDisconnect();

	{
		std::lock_guard<std::recursive_mutex> lock(net_lock);

		listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listen_sock == INVALID_SOCKET) return false;

		// Without this, re-hosting within the TIME_WAIT window fails with
		// WSAEADDRINUSE - about two minutes of "port busy" after every session.
		// Not on Windows: there it would also let two programs share the port, and
		// TIME_WAIT does not block a listening socket anyway.
#ifndef _WIN32
		BOOL reuse = TRUE;
		setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
#endif

		SetSocketNonBlocking(listen_sock);

		sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons((u_short)port);

		if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			closesocket(listen_sock);
			listen_sock = INVALID_SOCKET;
			return false;
		}

		if (listen(listen_sock, 1) == SOCKET_ERROR)
		{
			closesocket(listen_sock);
			listen_sock = INVALID_SOCKET;
			return false;
		}

		net_role = NETPLAY_HOST;
		net_state = NETPLAY_LISTENING;
		status_str = "Hosting (Waiting for Player 2 on port " + std::to_string(port) + ")...";
	}
	CoreSetToast(Tr("NETPLAY: HOSPEDANDO SESSAO", "NETPLAY: HOSTING SESSION"), 120);
	return true;
}

bool NetplayStartClient(const char* host_ip, int port)
{
	NetplayDisconnect();

	{
		std::lock_guard<std::recursive_mutex> lock(net_lock);

		peer_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (peer_sock == INVALID_SOCKET) return false;

		sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_port = htons((u_short)port);
		addr.sin_addr.s_addr = inet_addr(host_ip);

		ConfigurePeerSocket(peer_sock);
		net_stream.Clear();
		sync_state = SyncState();

		connect(peer_sock, (sockaddr*)&addr, sizeof(addr));

		peer_ip_str = host_ip;
		net_role = NETPLAY_CLIENT;
		net_state = NETPLAY_CONNECTING;
		status_str = "Connecting to Host (" + std::string(host_ip) + ")...";
	}
	CoreSetToast(Tr("NETPLAY: CONECTANDO AO HOST...", "NETPLAY: CONNECTING TO HOST..."), 120);
	return true;
}

void NetplayDisconnect()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);

	if (peer_sock != INVALID_SOCKET)
	{
		closesocket(peer_sock);
		peer_sock = INVALID_SOCKET;
	}
	if (listen_sock != INVALID_SOCKET)
	{
		closesocket(listen_sock);
		listen_sock = INVALID_SOCKET;
	}

	peer_ip_str.clear();
	net_role = NETPLAY_NONE;
	net_state = NETPLAY_DISCONNECTED;
	status_str = "Disconnected";
	net_stream.Clear();
	sync_state = SyncState();
	last_peer_tick = 0;
	pending_tick = 0;
	ping_ms = 0;
}

static void CompareHashes();
static void HostAskResync();

static void NetLog(const char* text)
{
	if (FILE* f = fopen("karamelo.log", "a"))
	{
		fprintf(f, "[INFO] [NETPLAY] %s\n", text);
		fclose(f);
	}
}

static void SleepMs(unsigned ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// The socket is non-blocking and a frame's input or a whole savestate must go
// out in one piece: half a packet desynchronises the stream for good.
static bool SendAll(const void* data, size_t size)
{
	const char* p = (const char*)data;
	while (size > 0 && peer_sock != INVALID_SOCKET)
	{
		const int chunk = size > (1u << 20) ? (1 << 20) : (int)size;
		const int sent = send(peer_sock, p, chunk, MSG_NOSIGNAL);
		if (sent > 0)
		{
			p += sent;
			size -= (size_t)sent;
			continue;
		}
		const int err = WSAGetLastError();
		if (sent == SOCKET_ERROR && (err == WSAEWOULDBLOCK || err == EAGAIN))
		{
			fd_set wr;
			FD_ZERO(&wr);
			FD_SET(peer_sock, &wr);
			timeval tv = { 5, 0 };
			if (select((int)peer_sock + 1, NULL, &wr, NULL, &tv) > 0) continue;
		}
		NetplayDisconnect();
		CoreSetToast(Tr("NETPLAY: CONEXAO PERDIDA", "NETPLAY: CONNECTION LOST"), 120);
		return false;
	}
	return size == 0;
}

static bool SendSimple(uint8_t type, uint32_t frame_seq, uint32_t value)
{
	NetPacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.magic = NET_MAGIC;
	pkt.type = type;
	pkt.frame_seq = frame_seq;
	pkt.send_tick = value;
	return SendAll(&pkt, sizeof(pkt));
}

static bool SendBlob(uint32_t kind, uint32_t argument, const void* data, size_t size)
{
	NetPacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.magic = NET_MAGIC;
	pkt.type = NET_TYPE_BLOB;
	pkt.frame_seq = kind;
	pkt.send_tick = (uint32_t)size;
	pkt.echo_tick = argument;
	if (kind == NET_BLOB_STATE)
	{
		// analog carries which frame this state belongs to on a resync.
		const uint32_t frame = sync_state.in_lockstep ? sync_state.lockstep.Frame() : 0;
		memcpy(pkt.analog, &frame, sizeof(frame));
	}
	return SendAll(&pkt, sizeof(pkt)) && SendAll(data, size);
}

static bool SendInput(uint32_t frame, const NetInput& input)
{
	NetPacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.magic = NET_MAGIC;
	pkt.type = NET_TYPE_INPUT;
	pkt.frame_seq = frame;
	pkt.send_tick = TickCountMs();
	pkt.echo_tick = last_peer_tick;   // lets the peer time its round trip
	memcpy(pkt.buttons, input.buttons, sizeof(pkt.buttons));
	memcpy(pkt.analog, input.analog, sizeof(pkt.analog));
	if (pending_tick == 0) pending_tick = pkt.send_tick;
	return SendAll(&pkt, sizeof(pkt));
}

static void HandlePacket(const NetPacket& pk, const std::vector<uint8_t>& blob)
{
	switch (pk.type)
	{
	case NET_TYPE_INPUT:
	{
		NetInput in;
		memcpy(in.buttons, pk.buttons, sizeof(in.buttons));
		memcpy(in.analog, pk.analog, sizeof(in.analog));
		sync_state.lockstep.SetRemote(pk.frame_seq, in);
		last_peer_tick = pk.send_tick;
		// Real round trip: the peer echoes back the timestamp we sent, so this
		// measures the loop instead of the packet interval.
		if (pk.echo_tick != 0 && pk.echo_tick == pending_tick)
		{
			ping_ms = (int)(TickCountMs() - pk.echo_tick);
			pending_tick = 0;
		}
		break;
	}
	case NET_TYPE_READY:
		if (net_role == NETPLAY_HOST) sync_state.guest_ready = true;
		break;
	case NET_TYPE_STATE_ACK:
		sync_state.state_acked = true;
		break;
	case NET_TYPE_HASH:
		sync_state.remote_hashes.push_back({ pk.frame_seq, pk.send_tick });
		CompareHashes();
		break;
	case NET_TYPE_SESSION_END:
		sync_state.in_lockstep = false;
		sync_state.guest_ready = false;
		sync_state.ready_sent = false;
		sync_state.load_tried = false;
		sync_state.have_session = false;
		break;
	case NET_TYPE_BLOB:
		if (pk.frame_seq == NET_BLOB_SESSION && net_role == NETPLAY_CLIENT)
		{
			const std::string text(blob.begin(), blob.end());
			const size_t a = text.find('\n');
			const size_t b = a == std::string::npos ? a : text.find('\n', a + 1);
			if (b == std::string::npos) break;
			sync_state.want_core = text.substr(0, a);
			// A name from the network is only ever compared with files we have.
			sync_state.want_rom = fs::path(text.substr(a + 1, b - a - 1)).filename().string();
			sync_state.want_size = strtoull(text.c_str() + b + 1, NULL, 10);
			sync_state.have_session = !sync_state.want_rom.empty();
			sync_state.ready_sent = false;
			sync_state.load_tried = false;
			sync_state.in_lockstep = false;
		}
		else if (pk.frame_seq == NET_BLOB_CHECK && net_role == NETPLAY_HOST)
		{
			const bool comparable = pk.echo_tick == sync_state.hashed_frame &&
			                        blob.size() == sync_state.hashed.size() && !blob.empty();
			if (comparable)
			{
				const std::vector<uint32_t> diff = NetplayStateMask(sync_state.hashed.data(), blob.data(), blob.size());
				std::string text = "state differs at frame " + std::to_string(pk.echo_tick) + ": " + std::to_string(diff.size()) + " blocks:";
				for (size_t i = 0; i < diff.size() && i < 24; i++) text += " " + std::to_string(diff[i]);
				NetLog(text.c_str());
			}
			const size_t before = sync_state.state_mask.size();
			if (comparable && NetplayLearnStateMask(sync_state.hashed.data(), blob.data(), blob.size(), sync_state.state_mask, sync_state.previous_diff))
			{
				if (sync_state.state_mask.size() != before)
				{
					sync_state.stats.learned += (unsigned)(sync_state.state_mask.size() - before);
					SendBlob(NET_BLOB_MASK, 0, sync_state.state_mask.data(), sync_state.state_mask.size() * sizeof(uint32_t));
				}
			}
			else
				HostAskResync();
		}
		else if (pk.frame_seq == NET_BLOB_MASK && net_role == NETPLAY_CLIENT)
		{
			// The host's list, so both machines leave out exactly the same bytes.
			sync_state.state_mask.resize(blob.size() / sizeof(uint32_t));
			if (!sync_state.state_mask.empty())
				memcpy(sync_state.state_mask.data(), blob.data(), sync_state.state_mask.size() * sizeof(uint32_t));
		}
		else if (pk.frame_seq == NET_BLOB_STATE && net_role == NETPLAY_CLIENT)
		{
			sync_state.pending_state = blob;
			sync_state.have_pending_state = true;
			sync_state.pending_delay = pk.echo_tick;
			memcpy(&sync_state.pending_frame, pk.analog, sizeof(uint32_t));
		}
		break;
	default:
		break;
	}
}

static void PumpNetwork()
{
	if (net_state != NETPLAY_CONNECTED || peer_sock == INVALID_SOCKET) return;
	static thread_local std::vector<uint8_t> chunk(256 * 1024);
	for (;;)
	{
		int bytes = recv(peer_sock, (char*)chunk.data(), (int)chunk.size(), 0);
		if (bytes > 0)
		{
			net_stream.Feed(chunk.data(), (size_t)bytes);
			continue;
		}
		if (bytes == 0)
		{
			NetplayDisconnect();
			CoreSetToast(Tr("NETPLAY: O OUTRO JOGADOR DESCONECTOU", "NETPLAY: PEER DISCONNECTED"), 120);
			return;
		}
		// SOCKET_ERROR: only WOULDBLOCK means "nothing right now". A real error
		// used to be indistinguishable from an idle socket, so a dropped
		// connection was never noticed.
		const int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK && err != EAGAIN)
		{
			NetplayDisconnect();
			CoreSetToast(Tr("NETPLAY: CONEXAO PERDIDA", "NETPLAY: CONNECTION LOST"), 120);
			return;
		}
		break;
	}
	NetPacket pk;
	std::vector<uint8_t> blob;
	while (net_state == NETPLAY_CONNECTED && net_stream.Next(pk, blob))
		HandlePacket(pk, blob);
}

void NetplayUpdate()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);

	if (net_role == NETPLAY_HOST && net_state == NETPLAY_LISTENING)
	{
		sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		SOCKET s = accept(listen_sock, (sockaddr*)&client_addr, &addr_len);
		if (s != INVALID_SOCKET)
		{
			peer_sock = s;
			ConfigurePeerSocket(peer_sock);
			net_stream.Clear();
			closesocket(listen_sock);
			listen_sock = INVALID_SOCKET;

			peer_ip_str = inet_ntoa(client_addr.sin_addr);
			sync_state = SyncState();
			net_state = NETPLAY_CONNECTED;
			status_str = "Connected with Player 2 (" + peer_ip_str + ")";
			CoreSetToast(Tr("JOGADOR 2 CONECTADO VIA NETPLAY!", "PLAYER 2 CONNECTED VIA NETPLAY!"), 180);
		}
	}
	else if (net_role == NETPLAY_CLIENT && net_state == NETPLAY_CONNECTING)
	{
		fd_set write_fds;
		FD_ZERO(&write_fds);
		FD_SET(peer_sock, &write_fds);

		timeval tv = { 0, 0 };
		// nfds (1st arg): Winsock ignores it entirely, kept only for BSD-socket
		// compatibility, so "0" was harmless there and this went unnoticed for
		// as long as this file only ever ran on Windows. POSIX select() takes
		// it literally - "highest fd in any set, plus 1" - so 0 means "examine
		// no descriptors at all": peer_sock's writability was never actually
		// checked, res was always <= 0, and a Linux client sat in
		// NETPLAY_CONNECTING forever no matter how fast the host accepted it.
		int res = select((int)peer_sock + 1, NULL, &write_fds, NULL, &tv);
		if (res > 0)
		{
			int so_error = 0;
			socklen_t len = sizeof(so_error);
			getsockopt(peer_sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
			if (so_error == 0)
			{
				net_state = NETPLAY_CONNECTED;
				status_str = "Connected to Host as Player 2!";
				CoreSetToast(Tr("CONECTADO AO HOST VIA NETPLAY!", "CONNECTED TO HOST VIA NETPLAY!"), 180);
			}
			else
			{
				NetplayDisconnect();
				status_str = "Connection refused";
				CoreSetToast(Tr("NETPLAY: CONEXAO RECUSADA", "NETPLAY: CONNECTION REFUSED"), 180);
			}
		}
	}

	if (net_state != NETPLAY_CONNECTED || peer_sock == INVALID_SOCKET) return;

	PumpNetwork();
	if (net_state != NETPLAY_CONNECTED) return;

	const std::string active = CoreIsRunning() ? CoreGetActiveRomPath() : std::string();

	if (net_role == NETPLAY_HOST)
	{
		if (active != sync_state.session_rom)
		{
			// New game or none: the old session is over on both sides.
			if (!sync_state.session_rom.empty()) SendSimple(NET_TYPE_SESSION_END, 0, 0);
			sync_state.in_lockstep = false;
			sync_state.guest_ready = false;
			sync_state.session_sent = false;
			sync_state.session_rom = active;
		}
		if (!active.empty() && !sync_state.session_sent)
		{
			std::error_code ec;
			const unsigned long long size = fs::file_size(active, ec);
			std::string text = CoreGetCoreName();
			text += "\n" + fs::path(active).filename().string() + "\n" + std::to_string(ec ? 0ull : size) + "\n";
			if (SendBlob(NET_BLOB_SESSION, 0, text.data(), text.size()))
				sync_state.session_sent = true;
		}
		return;
	}

	// Guest: open the host's game, then say so.
	if (!sync_state.have_session) return;
	const bool same_game = !active.empty() && fs::path(active).filename().string() == sync_state.want_rom;
	if (same_game)
	{
		if (!sync_state.ready_sent && CoreGetVideoFrameCount() > 0)
		{
			std::error_code ec;
			const unsigned long long size = fs::file_size(active, ec);
			if (!ec && sync_state.want_size != 0 && size != sync_state.want_size)
			{
				if (!sync_state.load_tried)
					CoreSetToast((std::string(Tr("NETPLAY: SUA COPIA E DIFERENTE: ", "NETPLAY: YOUR COPY IS DIFFERENT: ")) + sync_state.want_rom).c_str(), 400);
				sync_state.load_tried = true;
				return;
			}
			SendSimple(NET_TYPE_READY, 0, 0);
			sync_state.ready_sent = true;
		}
		return;
	}
	if (sync_state.ready_sent)
	{
		// Our game closed while the host's goes on.
		SendSimple(NET_TYPE_SESSION_END, 0, 0);
		sync_state.ready_sent = false;
		sync_state.in_lockstep = false;
	}
	if (!sync_state.load_tried)
	{
		sync_state.load_tried = true;
		std::string path;
		for (const char* root : { "roms", "app/roms" })
		{
			path = NetplayFindRom(root, sync_state.want_rom, sync_state.want_size);
			if (!path.empty()) break;
		}
		if (path.empty())
			CoreSetToast((std::string(Tr("NETPLAY: VOCE NAO TEM O JOGO ", "NETPLAY: YOU DO NOT HAVE ")) + sync_state.want_rom).c_str(), 400);
		else if (MenuLaunchGamePath(path))
			OsdDisable();
	}
}

std::string NetplayFindRom(const std::string& root, const std::string& file_name, unsigned long long size)
{
	std::error_code ec;
	std::string by_name;
	for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
	     !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec)) continue;
		if (it->path().filename().string() != file_name) continue;
		if (size == 0 || it->file_size(ec) == size) return it->path().string();
		if (by_name.empty()) by_name = it->path().string();
	}
	return by_name; // same name, other size: the guest is warned once it is open
}

bool NetplayLockstepActive()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return sync_state.in_lockstep;
}

NetplayStats NetplayGetStats()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return sync_state.stats;
}

static void CompareHashes()
{
	auto& loc = sync_state.local_hashes;
	auto& rem = sync_state.remote_hashes;
	for (size_t i = 0; i < loc.size();)
	{
		bool matched = false;
		for (size_t k = 0; k < rem.size(); k++)
		{
			if (rem[k].frame != loc[i].frame) continue;
			const bool same = rem[k].value == loc[i].value;
			if (same)
			{
				sync_state.stats.hash_ok++;
				sync_state.stats.hash_streak++;
			}
			else
			{
				sync_state.stats.hash_bad++;
				sync_state.stats.hash_streak = 0;
				// The host decides, from the guest's actual state: a hash cannot
				// tell a core's clock from a game that went another way.
				if (net_role == NETPLAY_CLIENT)
				{
					const bool have = sync_state.hashed_frame == loc[i].frame;
					SendBlob(NET_BLOB_CHECK, loc[i].frame, sync_state.hashed.data(), have ? sync_state.hashed.size() : 0);
				}
			}
			rem.erase(rem.begin() + (std::ptrdiff_t)k);
			matched = true;
			break;
		}
		if (matched) loc.erase(loc.begin() + (std::ptrdiff_t)i);
		else i++;
	}
	// A frame the other side never compared (it was rewound) must not pile up.
	if (loc.size() > 16) loc.erase(loc.begin(), loc.begin() + 8);
	if (rem.size() > 16) rem.erase(rem.begin(), rem.begin() + 8);
}

static void HostAskResync()
{
	// A small state is cheap to send again, and for a core that does not repeat
	// itself exactly (mGBA) that is what keeps the two games together.
	if (sync_state.resync_budget > 0 || sync_state.hashed.size() <= kCheapResyncBytes)
	{
		if (sync_state.resync_budget > 0) sync_state.resync_budget--;
		sync_state.want_resync = true;
	}
	else if (!sync_state.told_unstable)
	{
		sync_state.told_unstable = true;
		CoreSetToast(Tr("NETPLAY: ESTE CORE NAO MANTEM OS DOIS JOGOS IGUAIS", "NETPLAY: THIS CORE DOES NOT KEEP BOTH GAMES EQUAL"), 400);
	}
}

static bool SendState()
{
	size_t size = CoreNetStateSize();
	if (size == 0 || size > NET_MAX_BLOB)
	{
		CoreSetToast(Tr("NETPLAY: ESTE CORE NAO PERMITE PARTIDA EM REDE", "NETPLAY: THIS CORE DOES NOT SUPPORT NETPLAY"), 400);
		return false;
	}
	auto& state = sync_state.scratch;
	state.resize(size);
	if (!CoreNetStateSave(state.data(), size)) return false;

	// The host also goes back to the state it sends. A core's save and load are
	// not perfect mirrors (caches, timing leftovers), so a machine that kept
	// running from the original and one that loaded the copy would drift apart
	// with no input to blame.
	if (!CoreNetStateLoad(state.data(), size)) return false;
	std::vector<uint8_t> first_save(size), second_save(size);
	if (!CoreNetStateSave(first_save.data(), size) || !CoreNetStateSave(second_save.data(), size)) return false;
	sync_state.state_mask = NetplayStateMask(first_save.data(), second_save.data(), size);
	if (!sync_state.state_mask.empty())
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "state has %u blocks that are not the game", (unsigned)sync_state.state_mask.size());
		NetLog(msg);
	}
	if (!SendBlob(NET_BLOB_MASK, 0, sync_state.state_mask.data(), sync_state.state_mask.size() * sizeof(uint32_t)))
		return false;

	// Slower link, more frames of margin. Beyond this the game just waits.
	// Set up before sending: the guest's first input can arrive together with
	// its answer, and starting afterwards would wipe it and hang both machines.
	if (!sync_state.in_lockstep)
	{
		uint32_t delay = 2 + (uint32_t)(ping_ms / 2) / 17;
		if (delay > 10) delay = 10;
		sync_state.lockstep.Start(delay);
		sync_state.stats.delay = delay;
	}

	sync_state.state_acked = false;
	if (!SendBlob(NET_BLOB_STATE, sync_state.lockstep.Delay(), state.data(), size))
		return false;

	// The guest cannot take a frame in step until it has this, and the host
	// must not run ahead of the state it just sent.
	while (net_state == NETPLAY_CONNECTED && !sync_state.state_acked && CoreNetKeepWaiting())
	{
		PumpNetwork();
		if (!sync_state.state_acked)
		{
			net_lock.unlock();
			SleepMs(1);
			net_lock.lock();
		}
	}
	if (!sync_state.state_acked) return false;

	// Whatever the guest compared before this state no longer means anything.
	sync_state.local_hashes.clear();
	sync_state.remote_hashes.clear();
	sync_state.stats.state_bytes = (unsigned)size;
	return true;
}

// 1 this frame runs in step, 0 it runs alone, -1 a state arrived while waiting:
// look again, because it changes which frame this is.
static int LockstepFrameOnce(const NetInput& local, NetInput& port1, NetInput& port2)
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	if (net_state != NETPLAY_CONNECTED || peer_sock == INVALID_SOCKET) return 0;

	PumpNetwork();
	if (net_state != NETPLAY_CONNECTED) return false;

	if (net_role == NETPLAY_HOST)
	{
		const bool start = sync_state.guest_ready && !sync_state.in_lockstep;
		if (start || (sync_state.in_lockstep && sync_state.want_resync))
		{
			const bool first = !sync_state.in_lockstep;
			sync_state.want_resync = false;
			if (!first)
				NetLog("sending the state again");
			if (!first) sync_state.stats.resyncs++;
			if (SendState() && first)
			{
				sync_state.in_lockstep = true;
				CoreSetToast(Tr("NETPLAY: JOGOS SINCRONIZADOS", "NETPLAY: GAMES IN SYNC"), 180);
			}
			if (net_state != NETPLAY_CONNECTED) return false;
		}
	}
	else if (sync_state.have_pending_state)
	{
		sync_state.have_pending_state = false;
		const bool first = !sync_state.in_lockstep;
		if (!CoreNetStateLoad(sync_state.pending_state.data(), sync_state.pending_state.size()))
		{
			CoreSetToast(Tr("NETPLAY: O ESTADO DO OUTRO JOGO NAO SERVE NESTE CORE", "NETPLAY: THE OTHER GAME'S STATE DOES NOT FIT THIS CORE"), 400);
			NetplayDisconnect();
			return false;
		}
		if (first)
		{
			sync_state.lockstep.Start(sync_state.pending_delay);
			sync_state.stats.delay = sync_state.pending_delay;
			sync_state.in_lockstep = true;
			CoreSetToast(Tr("NETPLAY: JOGOS SINCRONIZADOS - VOCE E O JOGADOR 2", "NETPLAY: GAMES IN SYNC - YOU ARE PLAYER 2"), 180);
		}
		else
		{
			sync_state.stats.resyncs++;
			// The host paused on this frame; its input history carries on from it.
			sync_state.lockstep.SetFrame(sync_state.pending_frame);
		}
		sync_state.local_hashes.clear();
		sync_state.remote_hashes.clear();
		sync_state.stats.state_bytes = (unsigned)sync_state.pending_state.size();
		sync_state.pending_state.clear();
		sync_state.pending_state.shrink_to_fit();
		SendSimple(NET_TYPE_STATE_ACK, 0, 0);
	}

	if (!sync_state.in_lockstep) return false;

	NetLockstep& ls = sync_state.lockstep;
	const uint32_t frame = ls.Frame();

	const uint32_t queued = ls.QueueLocal(local);
	for (uint32_t i = 0; i < queued; i++)
		SendInput(ls.NextToSend() - queued + i, local);

	if (!ls.HasRemote(frame))
	{
		sync_state.stats.stalls++;
		while (net_state == NETPLAY_CONNECTED && sync_state.in_lockstep && !ls.HasRemote(frame) &&
		       !sync_state.have_pending_state && CoreNetKeepWaiting())
		{
			net_lock.unlock();
			SleepMs(1);
			net_lock.lock();
			PumpNetwork();
		}
		if (sync_state.have_pending_state) return -1;
		if (net_state != NETPLAY_CONNECTED || !sync_state.in_lockstep || !ls.HasRemote(frame)) return 0;
	}

	// The state before this frame's input, the same on both machines or not at all.
	if (frame % kHashEveryFrames == 0)
	{
		const size_t size = CoreNetStateSize();
		if (size > 0 && size <= kMaxHashedState)
		{
			auto& state = sync_state.scratch;
			state.resize(size);
			if (CoreNetStateSave(state.data(), size))
			{
				NetplayApplyStateMask(state.data(), size, sync_state.state_mask);
				const uint32_t h = NetplayHashState(state.data(), size);
				sync_state.local_hashes.push_back({ frame, h });
				sync_state.hashed.swap(state);
				sync_state.hashed_frame = frame;
				SendSimple(NET_TYPE_HASH, frame, h);
				CompareHashes();
			}
		}
	}

	NetInput mine, theirs;
	if (!ls.Get(frame, mine, theirs)) return 0;
	port1 = net_role == NETPLAY_HOST ? mine : theirs;
	port2 = net_role == NETPLAY_HOST ? theirs : mine;

	ls.Advance();
	sync_state.stats.frames++;
	return 1;
}

bool NetplayLockstepFrame(const NetInput& local, NetInput& port1, NetInput& port2)
{
	int r;
	while ((r = LockstepFrameOnce(local, port1, port2)) < 0) {}
	return r == 1;
}

// Two real processes, a real socket and a real core: the proof the session works
// that no unit test can give. Each presses its own pseudo-random buttons, so the
// games only stay equal if each really plays the other's input on the same frame.
int NetplaySelfTest(const char* role, const char* core, const char* rom, const char* ip, int port,
                    unsigned frames, const char* result_path)
{
	const bool host = strcmp(role, "host") == 0;
	NetplayInit();
	if (host)
	{
		if (!NetplayStartHost(port)) return 4;
		if (!CoreRequestLoad(rom, core)) return 4;
	}
	else if (!NetplayStartClient(ip, port))
		return 4;

	NetplayStats last;
	uint32_t rnd = host ? 0x12345678u : 0x9ABCDEF0u;
	const auto started = std::chrono::steady_clock::now();
	auto seconds = [&started]() {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	};
	double done_at = 0;
	const double limit = 60.0 + frames / 20.0;
	while (seconds() < limit)
	{
		NetplayUpdate();
		OsdDisable();
		const NetplayStats now = NetplayGetStats();
		if (now.frames >= last.frames) last = now;
		for (int b = 0; b < 16; b++)
		{
			// Start and Select out of it: a pause menu is a dull thing to compare.
			rnd = rnd * 1664525u + 1013904223u;
			const bool pressed = b != 2 && b != 3 && ((rnd >> 24) < 48);
			CoreSetButtonState(0, b, pressed);
		}
		if (last.frames >= frames && done_at == 0) done_at = seconds();
		// A moment more, so the last comparisons of both sides get across.
		if (done_at != 0 && seconds() - done_at > 3.0) break;
		if (done_at == 0 && last.frames > 0 && net_state == NETPLAY_DISCONNECTED) break;
		SleepMs(16);
	}
	FILE* f = fopen(result_path, "w");
	if (f)
	{
		fprintf(f, "role=%s frames=%u hash_ok=%u hash_bad=%u streak=%u learned=%u resyncs=%u state_bytes=%u delay=%u stalls=%u seconds=%.1f\n",
		        role, last.frames, last.hash_ok, last.hash_bad, last.hash_streak, last.learned, last.resyncs, last.state_bytes, last.delay, last.stalls, seconds());
		fclose(f);
	}
	// Differences while it learns a core's clock are normal. What may not happen
	// is ending on a difference, or needing the state sent again and again.
	const bool ok = last.frames >= frames && last.hash_streak >= 2 && last.resyncs <= 2;
	// The exit code is what the script reads; a clean shutdown of a core here is
	// not what this test is about.
	if (ok) return 0;
	if (last.frames == 0) return 2;
	// Ending in the middle of a correction is not a failure; most comparisons
	// disagreeing, with no correction taking hold, is.
	return (last.frames >= frames && last.hash_ok >= last.hash_bad) ? 5 : 3;
}

int SocialSelfTest(const char* role, int chat_port, int match_port, const char* result_path)
{
	const bool is_a = strcmp(role, "a") == 0;
	NetplayInit();
	DesktopChatInit();
	DesktopChatSetNickname(is_a ? "SelfTestA" : "SelfTestB");
	DesktopChatSetKarameloTag(is_a ? "SelfTestA#1111" : "SelfTestB#2222");
	// Each on its own port, addressed directly: broadcast between two copies on
	// one PC depends on the drivers. What it does on the real card is reported.
	DesktopChatSetExtraPeer("127.0.0.1", is_a ? chat_port + 1 : chat_port);
	if (!DesktopChatStartNetwork(is_a ? chat_port : chat_port + 1)) return 1;

	const std::string mine = is_a ? "hello from a & friends" : "hello from b & friends";
	const std::string theirs = is_a ? "hello from b & friends" : "hello from a & friends";
	const std::string other_sender = is_a ? "SelfTestB" : "SelfTestA";
	const std::string other_tag = is_a ? "SelfTestB#2222" : "SelfTestA#1111";

	bool got_message = false, got_presence = false, invited = false, accepted = false;
	bool connected = false, saw_invite = false, wrong_host = false;
	unsigned copies = 0;
	const auto started = std::chrono::steady_clock::now();
	auto seconds = [&started]() {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	};
	double last_post = -10, connected_at = 0;
	while (seconds() < 45)
	{
		NetplayUpdate();
		// Keeps going after the match connects: the other side may have started
		// later and still be waiting for its first message.
		if (seconds() - last_post > 1.0)
		{
			DesktopChatPostMessage(mine.c_str());
			last_post = seconds();
		}
		unsigned seen = 0;
		for (const auto& m : DesktopChatGetRoomMessages())
		{
			if (m.sender != other_sender) continue;
			if (m.text == theirs) seen++;
			if (!m.game_challenge.empty())
			{
				saw_invite = true;
				if (m.room_code.empty()) wrong_host = true;
			}
		}
		if (seen > 0) got_message = true;
		if (seen > copies) copies = seen;
		for (const auto& p : DesktopChatGetActivePeers())
			if (p.tag == other_tag) got_presence = true;

		if (is_a && got_message && got_presence && !invited)
			invited = DesktopChatSendChallenge("SelfTest Match");
		// The fixed match port is a choice of the real flow; the test runs the
		// same code on its own port so it cannot collide with a real session.
		if (!is_a && saw_invite && !accepted)
			accepted = DesktopChatAcceptLastChallenge();

		if (NetplayGetState() == NETPLAY_CONNECTED && !connected)
		{
			connected = true;
			connected_at = seconds();
		}
		// Longer than one presence round, so the side that started later gets ours too.
		if (connected && got_message && got_presence && seconds() - connected_at > 7.0) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	(void)match_port;

	FILE* f = fopen(result_path, "w");
	if (f)
	{
		fprintf(f, "role=%s message=%d copies=%u presence=%d invite=%d connected=%d local_ip=%s broadcast_cards=%d seconds=%.1f\n",
		        role, got_message ? 1 : 0, copies, got_presence ? 1 : 0, (is_a ? invited : saw_invite) ? 1 : 0,
		        connected ? 1 : 0, NetplayGetLocalIp(), DesktopChatGetBroadcastCount(), seconds());
		fclose(f);
	}
	const bool nics_exist = !NetplayGetLocalInterfaces().empty();
	const bool ok = got_message && got_presence && connected && !wrong_host &&
	                (!nics_exist || DesktopChatGetBroadcastCount() > 0);
	DesktopChatStopNetwork();
	NetplayDisconnect();
	return ok ? 0 : 1;
}

int InternetChatSelfTest(const char* role, const char* server_url, const char* id_token, int chat_port, const char* result_path)
{
	const bool is_a = strcmp(role, "a") == 0;
	const bool is_bad = strcmp(role, "bad") == 0;
	// The app has moved to its own folder by now, and each side keeps its login in
	// Config/: without this they would share one, and with the player's own.
	std::error_code dir_error;
	std::filesystem::current_path(std::filesystem::path(result_path).parent_path(), dir_error);
	if (dir_error) return 1;
	NetplayInit();
	DesktopChatInit();
	DesktopChatInternetSetServer(server_url);
	DesktopChatSetNickname(is_a ? "NetA" : "NetB");
	// Each on its own UDP port and no extra peer: nothing here can arrive by the
	// LAN road, so whatever arrives came through the server.
	if (!DesktopChatStartNetwork(chat_port)) return 1;

	const bool logged_in = DesktopChatInternetLoginWithIdToken(id_token);
	if (is_bad)
	{
		// A token the server refuses: no session, on screen or on disk.
		const bool clean = !logged_in && !DesktopChatInternetIsOnline() && DesktopChatInternetGetTag().empty() &&
		                   !std::filesystem::exists("Config/chat_session.bin");
		DesktopChatStopNetwork();
		return clean ? 0 : 1;
	}
	if (!logged_in) return 1;

	const std::string mine = is_a ? "internet hello from a" : "internet hello from b";
	const std::string theirs = is_a ? "internet hello from b" : "internet hello from a";
	const std::string other_nick = is_a ? "NetB#" : "NetA#";

	bool got_message = false, got_presence = false, invited = false, accepted = false;
	bool connected = false, saw_invite = false, wrong_host = false;
	unsigned copies_of_mine = 0, posts = 0;
	const auto started = std::chrono::steady_clock::now();
	auto seconds = [&started]() {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	};
	double last_post = -10, connected_at = 0;
	while (seconds() < 45)
	{
		NetplayUpdate();
		// Under the server's limit of five in ten seconds.
		if (DesktopChatInternetIsOnline() && seconds() - last_post > 2.5)
		{
			DesktopChatPostMessage(mine.c_str());
			last_post = seconds();
			posts++;
		}
		unsigned mine_now = 0;
		for (const auto& m : DesktopChatGetRoomMessages())
		{
			if (m.text == mine) mine_now++;
			if (m.sender.rfind(other_nick, 0) != 0) continue;
			if (m.text == theirs) got_message = true;
			if (!m.game_challenge.empty())
			{
				saw_invite = true;
				// The room must not learn the host's address.
				if (!m.host_ip.empty() || !m.room_code.empty() || m.invite_id.empty()) wrong_host = true;
			}
		}
		if (mine_now > copies_of_mine) copies_of_mine = mine_now;
		for (const auto& p : DesktopChatGetActivePeers())
			if (p.tag.rfind(other_nick, 0) == 0 && p.host_ip.empty()) got_presence = true;

		if (is_a && got_message && got_presence && !invited)
			invited = DesktopChatSendChallenge("Internet Match");
		if (!is_a && saw_invite && !accepted)
			accepted = DesktopChatAcceptLastChallenge();

		if (NetplayGetState() == NETPLAY_CONNECTED && !connected)
		{
			connected = true;
			connected_at = seconds();
		}
		// Long enough for the other side to finish its own checks.
		if (connected && got_message && got_presence && seconds() - connected_at > 5.0) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	const bool session_kept = std::filesystem::exists("Config/chat_session.bin");
	FILE* f = fopen(result_path, "w");
	if (f)
	{
		fprintf(f, "role=%s tag=%s message=%d presence=%d invite=%d connected=%d posts=%u copies_of_mine=%u session_kept=%d wrong_host=%d seconds=%.1f\n",
		        role, DesktopChatInternetGetTag().c_str(), got_message ? 1 : 0, got_presence ? 1 : 0,
		        (is_a ? invited : saw_invite) ? 1 : 0, connected ? 1 : 0, posts, copies_of_mine,
		        session_kept ? 1 : 0, wrong_host ? 1 : 0, seconds());
		fclose(f);
	}
	// The server sends our own messages back: they must not show up twice.
	const bool ok = got_message && got_presence && connected && !wrong_host && session_kept && copies_of_mine <= posts;
	DesktopChatStopNetwork();
	NetplayDisconnect();
	return ok ? 0 : 1;
}

int VoiceSelfTest(int port, int seconds, const char* result_path)
{
	NetplayInit();
	VoiceChatInit();
	VoiceChatConfig cfg = VoiceChatGetConfig();
	cfg.enabled = true;
	cfg.vad_enabled = false; // silence must also travel, or a quiet room reads as broken
	VoiceChatSetConfig(cfg);
	VoiceChatSetMicMuted(false);
	if (!VoiceChatStartP2P("127.0.0.1", port)) return 1;

	SOCKET other = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in to = { 0 };
	to.sin_family = AF_INET;
	to.sin_port = htons((u_short)port);
	inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);

	std::vector<int16_t> tone(320);
	for (size_t i = 0; i < tone.size(); i++)
		tone[i] = (int16_t)((i % 40 < 20) ? 8000 : -8000);
	std::vector<uint8_t> packet;
	bool peer_seen = false;
	const unsigned total = (unsigned)seconds * 50;
	for (unsigned i = 0; i < total; i++)
	{
		// A sequence far ahead of the session's own, so its loop-back never hides it.
		if (VoiceChatBuildPacket(0x40000000u + i, true, tone.data(), tone.size(), packet))
			sendto(other, (const char*)packet.data(), (int)packet.size(), 0, (const sockaddr*)&to, sizeof(to));
		if (VoiceChatIsPeerSpeaking()) peer_seen = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	closesocket(other);

	const VoiceChatCounters c = VoiceChatGetCounters();
	if (FILE* f = fopen(result_path, "w"))
	{
		fprintf(f, "mic_frames=%u mic_peak=%d sent=%u received=%u peer_speaking=%d\n",
		        c.mic_frames, c.mic_peak, c.sent, c.received, peer_seen ? 1 : 0);
		fclose(f);
	}
	VoiceChatStopP2P();
	// The microphone delivered, the session sent, and a player's voice reached the speaker path.
	return (c.mic_frames > 0 && c.sent > 0 && c.received > 0 && peer_seen) ? 0 : 1;
}
