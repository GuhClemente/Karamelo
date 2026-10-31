#include "cloud_sync.h"
#include "app_info.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <set>
#include "json_mini.h"
#include "core_runner.h"
#include "menu.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <map>

namespace fs = std::filesystem;

// =============================================================
// Built-in FIPS 180-4 SHA-256 implementation (zero external deps)
// =============================================================
namespace
{
struct Sha256Ctx
{
	uint32_t state[8];
	uint64_t bitlen;
	uint8_t  data[64];
	uint32_t datalen;
};

static inline uint32_t Ror32(uint32_t a, uint32_t b)
{
	return (a >> b) | (a << (32u - b));
}

static const uint32_t kSha256K[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfc1u & 0xffffffffu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffa4u & 0xffffffffu, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

// Exact FIPS 180-4 constants for index 34 (0x4d2c6dfc) and 60 (0x90befffa)
static const uint32_t kSha256Table[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void Sha256Transform(Sha256Ctx* ctx, const uint8_t data[64])
{
	uint32_t m[64];
	for (int i = 0, j = 0; i < 16; ++i, j += 4)
	{
		m[i] = ((uint32_t)data[j] << 24) |
		       ((uint32_t)data[j + 1] << 16) |
		       ((uint32_t)data[j + 2] << 8) |
		       ((uint32_t)data[j + 3]);
	}
	for (int i = 16; i < 64; ++i)
	{
		uint32_t s0 = Ror32(m[i - 15], 7) ^ Ror32(m[i - 15], 18) ^ (m[i - 15] >> 3);
		uint32_t s1 = Ror32(m[i - 2], 17) ^ Ror32(m[i - 2], 19) ^ (m[i - 2] >> 10);
		m[i] = m[i - 16] + s0 + m[i - 7] + s1;
	}

	uint32_t a = ctx->state[0];
	uint32_t b = ctx->state[1];
	uint32_t c = ctx->state[2];
	uint32_t d = ctx->state[3];
	uint32_t e = ctx->state[4];
	uint32_t f = ctx->state[5];
	uint32_t g = ctx->state[6];
	uint32_t h = ctx->state[7];

	for (int i = 0; i < 64; ++i)
	{
		uint32_t S1 = Ror32(e, 6) ^ Ror32(e, 11) ^ Ror32(e, 25);
		uint32_t ch = (e & f) ^ ((~e) & g);
		uint32_t temp1 = h + S1 + ch + kSha256Table[i] + m[i];
		uint32_t S0 = Ror32(a, 2) ^ Ror32(a, 13) ^ Ror32(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t temp2 = S0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

static void Sha256Init(Sha256Ctx* ctx)
{
	ctx->datalen = 0;
	ctx->bitlen = 0;
	ctx->state[0] = 0x6a09e667u;
	ctx->state[1] = 0xbb67ae85u;
	ctx->state[2] = 0x3c6ef372u;
	ctx->state[3] = 0xa54ff53au;
	ctx->state[4] = 0x510e527fu;
	ctx->state[5] = 0x9b05688cu;
	ctx->state[6] = 0x1f83d9abu;
	ctx->state[7] = 0x5be0cd19u;
	(void)kSha256K;
}

static void Sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len)
{
	for (size_t i = 0; i < len; ++i)
	{
		ctx->data[ctx->datalen] = data[i];
		ctx->datalen++;
		if (ctx->datalen == 64)
		{
			Sha256Transform(ctx, ctx->data);
			ctx->bitlen += 512;
			ctx->datalen = 0;
		}
	}
}

static void Sha256Final(Sha256Ctx* ctx, uint8_t hash[32])
{
	uint32_t i = ctx->datalen;
	if (ctx->datalen < 56)
	{
		ctx->data[i++] = 0x80;
		while (i < 56) ctx->data[i++] = 0x00;
	}
	else
	{
		ctx->data[i++] = 0x80;
		while (i < 64) ctx->data[i++] = 0x00;
		Sha256Transform(ctx, ctx->data);
		memset(ctx->data, 0, 56);
	}

	ctx->bitlen += (uint64_t)ctx->datalen * 8u;
	ctx->data[63] = (uint8_t)(ctx->bitlen);
	ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
	ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
	ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
	ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
	ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
	ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
	ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
	Sha256Transform(ctx, ctx->data);

	for (i = 0; i < 4; ++i)
	{
		hash[i]      = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xffu);
		hash[i + 4]  = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xffu);
		hash[i + 8]  = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xffu);
		hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xffu);
		hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xffu);
		hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xffu);
		hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xffu);
		hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xffu);
	}
}

static bool SecureRandomBytes(uint8_t* buf, size_t len)
{
#ifdef _WIN32
	return BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) return false;
	size_t total = 0;
	while (total < len)
	{
		ssize_t r = read(fd, buf + total, len - total);
		if (r <= 0) { close(fd); return false; }
		total += (size_t)r;
	}
	close(fd);
	return true;
#endif
}

static std::string GenerateRandomHex(size_t num_bytes)
{
	std::vector<uint8_t> raw(num_bytes, 0);
	if (!SecureRandomBytes(raw.data(), raw.size()))
	{
		return "";
	}
	static const char kHex[] = "0123456789abcdef";
	std::string out;
	out.reserve(num_bytes * 2);
	for (uint8_t b : raw)
	{
		out.push_back(kHex[(b >> 4) & 0xF]);
		out.push_back(kHex[b & 0xF]);
	}
	return out;
}
} // namespace

// =============================================================
// Crypto & Encoding Public Helpers
// =============================================================

std::string CloudSyncComputeSha256Bytes(const uint8_t* data, size_t len)
{
	Sha256Ctx ctx;
	Sha256Init(&ctx);
	if (data && len > 0)
		Sha256Update(&ctx, data, len);
	uint8_t digest[32];
	Sha256Final(&ctx, digest);

	char hex[65];
	for (int i = 0; i < 32; ++i)
		snprintf(hex + i * 2, 3, "%02x", digest[i]);
	hex[64] = '\0';
	return std::string(hex);
}

std::string CloudSyncComputeFileSha256(const std::string& filepath)
{
	FILE* f = fopen(filepath.c_str(), "rb");
	if (!f) return "";

	Sha256Ctx ctx;
	Sha256Init(&ctx);
	uint8_t buf[16384];
	size_t n = 0;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
	{
		Sha256Update(&ctx, buf, n);
	}
	fclose(f);

	uint8_t digest[32];
	Sha256Final(&ctx, digest);

	char hex[65];
	for (int i = 0; i < 32; ++i)
		snprintf(hex + i * 2, 3, "%02x", digest[i]);
	hex[64] = '\0';
	return std::string(hex);
}

std::string CloudSyncBase64UrlEncode(const uint8_t* data, size_t len)
{
	static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	out.reserve(((len + 2) / 3) * 4);

	size_t i = 0;
	while (i + 2 < len)
	{
		uint32_t triple = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
		out.push_back(kTable[(triple >> 18) & 0x3F]);
		out.push_back(kTable[(triple >> 12) & 0x3F]);
		out.push_back(kTable[(triple >> 6) & 0x3F]);
		out.push_back(kTable[triple & 0x3F]);
		i += 3;
	}

	if (i < len)
	{
		uint32_t rem = (uint32_t)data[i] << 16;
		if (i + 1 < len) rem |= ((uint32_t)data[i + 1] << 8);

		out.push_back(kTable[(rem >> 18) & 0x3F]);
		out.push_back(kTable[(rem >> 12) & 0x3F]);
		if (i + 1 < len)
			out.push_back(kTable[(rem >> 6) & 0x3F]);
	}

	return out;
}

std::string CloudSyncBuildPkceChallenge(const std::string& verifier)
{
	Sha256Ctx ctx;
	Sha256Init(&ctx);
	if (!verifier.empty())
		Sha256Update(&ctx, (const uint8_t*)verifier.data(), verifier.size());
	uint8_t digest[32];
	Sha256Final(&ctx, digest);
	return CloudSyncBase64UrlEncode(digest, 32);
}

// =============================================================
// Path Validation & Local Save Discovery
// =============================================================

bool CloudSyncIsAllowedSyncPath(const std::string& rel_path)
{
	if (rel_path.empty() || rel_path.size() > 240) return false;
	if (rel_path[0] == '/' || rel_path[0] == '\\') return false;
	if (rel_path.find("..") != std::string::npos) return false;
	if (rel_path.find(':') != std::string::npos) return false;
	if (rel_path.find('\\') != std::string::npos) return false;

	for (unsigned char c : rel_path)
	{
		if (c < 0x20 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
			return false;
	}

	// Explicitly allowed configuration files
	if (rel_path == "Config/karamelo.cfg" ||
	    rel_path == "Config/favorites.txt" ||
	    rel_path == "Config/recent.txt")
	{
		return true;
	}

	// Battery saves & virtual memory cards under saves/<System>/<File>
	if (rel_path.rfind("saves/", 0) != 0)
		return false;

	size_t slash1 = 5; // after "saves"
	size_t slash2 = rel_path.find('/', slash1 + 1);
	if (slash2 == std::string::npos || slash2 == slash1 + 1)
		return false;
	// Do not allow arbitrary deep nested paths
	if (rel_path.find('/', slash2 + 1) != std::string::npos)
		return false;

	std::string filename = rel_path.substr(slash2 + 1);
	if (filename.empty() || filename[0] == '.')
		return false;

	size_t dot = filename.rfind('.');
	if (dot == std::string::npos) return false;
	std::string ext = filename.substr(dot);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });

	// Allowed battery save / memory card extensions (excluding giant savestates by default)
	return (ext == ".srm" || ext == ".sav" || ext == ".eep" || ext == ".fla" ||
	        ext == ".sra" || ext == ".mcr" || ext == ".mcd" || ext == ".vms" ||
	        ext == ".bin" || ext == ".nv" || ext == ".rtc" || ext == ".ps2");
}

bool CloudSyncScanLocalFiles(const std::string& root_dir, std::vector<CloudSaveEntry>& out_entries)
{
	out_entries.clear();
	std::error_code ec;
	fs::path base = root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
	if (ec || !fs::exists(base, ec)) return false;

	auto add_candidate = [&](const std::string& rel_str, const std::string& sys_dir, const std::string& stem)
	{
		if (!CloudSyncIsAllowedSyncPath(rel_str)) return;
		fs::path full = base / fs::path(rel_str);
		std::error_code fec;
		if (!fs::exists(full, fec) || !fs::is_regular_file(full, fec)) return;

		uint64_t sz = (uint64_t)fs::file_size(full, fec);
		// Skip empty or >16 MB files (protects quota and prevents accidental ISO/state sync)
		if (fec || sz == 0 || sz > 16u * 1024u * 1024u) return;

		std::string sha = CloudSyncComputeFileSha256(full.string());
		if (sha.empty()) return;

		int64_t mod_utc = 0;
		auto ftime = fs::last_write_time(full, fec);
		if (!fec)
		{
			auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
			);
			mod_utc = (int64_t)std::chrono::system_clock::to_time_t(sctp);
		}

		CloudSaveEntry e;
		e.rel_path = rel_str;
		e.system_dir = sys_dir;
		e.game_stem = stem;
		e.sha256 = sha;
		e.size_bytes = sz;
		e.modified_utc = mod_utc;
		e.playtime_sec = 0;
		out_entries.push_back(e);
	};

	fs::path saves_root = base / "saves";
	if (fs::exists(saves_root, ec) && fs::is_directory(saves_root, ec))
	{
		for (const auto& sys_dir_entry : fs::directory_iterator(saves_root, ec))
		{
			if (!sys_dir_entry.is_directory(ec)) continue;
			std::string sys_name = sys_dir_entry.path().filename().string();
			if (sys_name.empty() || sys_name[0] == '.') continue;

			std::error_code sec;
			for (const auto& file_entry : fs::directory_iterator(sys_dir_entry.path(), sec))
			{
				if (!file_entry.is_regular_file(sec)) continue;
				std::string fname = file_entry.path().filename().string();
				std::string stem = file_entry.path().stem().string();
				std::string rel = "saves/" + sys_name + "/" + fname;
				add_candidate(rel, sys_name, stem);
			}
		}
	}

	std::sort(out_entries.begin(), out_entries.end(),
		[](const CloudSaveEntry& a, const CloudSaveEntry& b) { return a.rel_path < b.rel_path; });

	return true;
}

// =============================================================
// Manifest Serialization, Parsing & Conflict Resolution
// =============================================================

std::string CloudSyncSerializeManifest(const CloudProfileManifest& manifest)
{
	std::ostringstream oss;
	oss << "{\n";
	oss << "  \"schema_version\": " << manifest.schema_version << ",\n";
	oss << "  \"app_version\": " << JsonQuote(manifest.app_version) << ",\n";
	oss << "  \"device_name\": " << JsonQuote(manifest.device_name) << ",\n";
	oss << "  \"updated_utc\": " << manifest.updated_utc << ",\n";
	oss << "  \"entries\": [\n";
	for (size_t i = 0; i < manifest.entries.size(); ++i)
	{
		const CloudSaveEntry& e = manifest.entries[i];
		oss << "    {\n";
		oss << "      \"rel_path\": " << JsonQuote(e.rel_path) << ",\n";
		oss << "      \"system_dir\": " << JsonQuote(e.system_dir) << ",\n";
		oss << "      \"game_stem\": " << JsonQuote(e.game_stem) << ",\n";
		oss << "      \"sha256\": " << JsonQuote(e.sha256) << ",\n";
		oss << "      \"size_bytes\": " << (int64_t)e.size_bytes << ",\n";
		oss << "      \"modified_utc\": " << e.modified_utc << ",\n";
		oss << "      \"playtime_sec\": " << (int64_t)e.playtime_sec << "\n";
		oss << "    }" << (i + 1 < manifest.entries.size() ? "," : "") << "\n";
	}
	oss << "  ]\n";
	oss << "}\n";
	return oss.str();
}

bool CloudSyncParseManifest(const std::string& json_str, CloudProfileManifest& out_manifest)
{
	JsonValue root;
	std::string err;
	if (!JsonParse(json_str, root, err) || root.type != JsonValue::Object)
		return false;

	auto get_str = [](const JsonValue& obj, const char* key, const char* def_val) -> std::string {
		const JsonValue* v = obj.Find(key);
		return (v && v->type == JsonValue::String) ? v->str : std::string(def_val);
	};
	auto get_int = [](const JsonValue& obj, const char* key, int64_t def_val) -> int64_t {
		const JsonValue* v = obj.Find(key);
		return (v && v->type == JsonValue::Number) ? (int64_t)v->number : def_val;
	};

	out_manifest = CloudProfileManifest{};
	out_manifest.schema_version = (int)get_int(root, "schema_version", 1);
	out_manifest.app_version = get_str(root, "app_version", "");
	out_manifest.device_name = get_str(root, "device_name", "");
	out_manifest.updated_utc = get_int(root, "updated_utc", 0);

	const JsonValue* arr = root.Find("entries");
	if (arr && arr->type == JsonValue::Array)
	{
		for (const auto& item : arr->items)
		{
			if (item.type != JsonValue::Object) continue;
			CloudSaveEntry e;
			e.rel_path = get_str(item, "rel_path", "");
			e.system_dir = get_str(item, "system_dir", "");
			e.game_stem = get_str(item, "game_stem", "");
			e.sha256 = get_str(item, "sha256", "");
			int64_t sz = get_int(item, "size_bytes", 0);
			e.size_bytes = (uint64_t)(sz > 0 ? sz : 0);
			e.modified_utc = get_int(item, "modified_utc", 0);
			int64_t pt = get_int(item, "playtime_sec", 0);
			e.playtime_sec = (uint64_t)(pt > 0 ? pt : 0);

			if (CloudSyncIsAllowedSyncPath(e.rel_path) && e.sha256.size() == 64)
			{
				out_manifest.entries.push_back(e);
			}
		}
	}
	return true;
}

CloudConflictAction CloudSyncEvaluateConflict(const CloudSaveEntry& local_e, const CloudSaveEntry& remote_e, int64_t last_synced_utc)
{
	if (local_e.sha256 == remote_e.sha256 && !local_e.sha256.empty())
		return CLOUD_CONFLICT_NONE;

	if (remote_e.sha256.empty())
		return CLOUD_CONFLICT_UPLOAD_LOCAL;
	if (local_e.sha256.empty())
		return CLOUD_CONFLICT_DOWNLOAD_REMOTE;

	// If both files were modified after the last known sync point and their hashes differ, flag divergence
	if (last_synced_utc > 0 &&
	    local_e.modified_utc > last_synced_utc &&
	    remote_e.modified_utc > last_synced_utc)
	{
		return CLOUD_CONFLICT_DIVERGED;
	}

	if (local_e.modified_utc >= remote_e.modified_utc)
		return CLOUD_CONFLICT_UPLOAD_LOCAL;
	else
		return CLOUD_CONFLICT_DOWNLOAD_REMOTE;
}

// =============================================================
// Runtime State, OAuth 2.0 Loopback & Google Drive REST Worker
// =============================================================

static std::mutex                  g_cloud_mutex;
static std::atomic<int>            g_cloud_state{ CLOUD_DISCONNECTED };
static std::atomic<bool>           g_cloud_auto_sync{ true };
static std::atomic<int>            g_cloud_synced_count{ 0 };
static std::atomic<bool>           g_cloud_shutdown{ false };
static std::thread                 g_cloud_worker;
static std::thread                 g_auth_worker;
static std::string                 g_cloud_status_str = "Desconectado";
static std::string                 g_cloud_last_sync_str = "Nunca";
static const char*                 kDefaultGoogleClientId = "";
static const char*                 kDefaultGoogleClientSecret = "";
static std::string                 g_client_id = kDefaultGoogleClientId;
static std::string                 g_client_secret = kDefaultGoogleClientSecret;
static std::string                 g_access_token;
static std::string                 g_refresh_token;
static int64_t                     g_last_sync_utc = 0;
static std::map<std::string, uint64_t> g_playtime_map;

static void SetCloudStatus(CloudSyncState st, const std::string& msg)
{
	g_cloud_state.store((int)st);
	std::lock_guard<std::mutex> lk(g_cloud_mutex);
	g_cloud_status_str = msg;
}

static std::string GetLocalDeviceName()
{
#ifdef _WIN32
	char buf[128] = { 0 };
	DWORD sz = sizeof(buf) - 1;
	if (GetComputerNameA(buf, &sz) && sz > 0)
		return std::string(buf);
	return "Windows-PC";
#else
	char buf[128] = { 0 };
	if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0] != '\0')
		return std::string(buf);
	return "POSIX-Host";
#endif
}

static void LoadCloudConfigAndTokens()
{
	std::error_code ec;
	fs::create_directories("Config", ec);

	const char* env_cid = getenv("KARAMELO_GOOGLE_CLIENT_ID");
	if (env_cid && env_cid[0] != '\0')
	{
		g_client_id = env_cid;
	}

	const char* env_sec = getenv("KARAMELO_GOOGLE_CLIENT_SECRET");
	if (env_sec && env_sec[0] != '\0')
	{
		g_client_secret = env_sec;
	}

	fs::path cfg_path = "Config/cloud.cfg";
	if (fs::exists(cfg_path, ec))
	{
		std::ifstream in(cfg_path);
		std::string line;
		while (std::getline(in, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
				line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string k = line.substr(0, eq);
			std::string v = line.substr(eq + 1);
			if (k == "google_client_id" && !v.empty()) g_client_id = v;
			else if (k == "google_client_secret" && !v.empty()) g_client_secret = v;
			else if (k == "auto_sync") g_cloud_auto_sync.store(v != "0");
			else if (k == "last_sync_utc")
			{
				try { g_last_sync_utc = std::stoll(v); } catch (...) {}
			}
		}
	}

	// Try reading Config/google_credentials.json if client_id is not custom
	fs::path cred_path = "Config/google_credentials.json";
	if (fs::exists(cred_path, ec))
	{
		std::ifstream cr(cred_path);
		std::string cr_str((std::istreambuf_iterator<char>(cr)), std::istreambuf_iterator<char>());
		JsonValue croot;
		std::string jerr;
		if (JsonParse(cr_str, croot, jerr) && croot.type == JsonValue::Object)
		{
			const JsonValue* inst = croot.Find("installed");
			if (inst && inst->type == JsonValue::Object)
			{
				const JsonValue* cid = inst->Find("client_id");
				if (cid && cid->type == JsonValue::String && !cid->str.empty())
					g_client_id = cid->str;
				const JsonValue* csec = inst->Find("client_secret");
				if (csec && csec->type == JsonValue::String && !csec->str.empty())
					g_client_secret = csec->str;
			}
		}
	}

	if (g_client_id.empty()) g_client_id = kDefaultGoogleClientId;
	if (g_client_secret.empty()) g_client_secret = kDefaultGoogleClientSecret;
	else
	{
		std::ofstream out(cfg_path);
		if (out.is_open())
		{
			out << "# Karamelo Cloud Configuration (Google Drive appDataFolder)\n";
			out << "# Zero user data is ever sent to karamelo-emu.com.\n";
			out << "# Configure your Google OAuth 2.0 Desktop Client ID below:\n";
			out << "google_client_id=\n";
			out << "auto_sync=1\n";
			out << "last_sync_utc=0\n";
		}
	}

	// Load encrypted/protected refresh token if present
	g_refresh_token = CloudSyncLoadSecret("Config/cloud_token.bin");

	// Earlier builds saved a made-up token when the login failed.
	if (g_refresh_token.rfind("oauth2_pkce_", 0) == 0)
		g_refresh_token.clear();

	if (!g_refresh_token.empty())
	{
		SetCloudStatus(CLOUD_IDLE, Tr("Conectado (Google Drive)", "Connected (Google Drive)"));
	}
	else
	{
		SetCloudStatus(CLOUD_DISCONNECTED, Tr("Desconectado", "Disconnected"));
	}
}

static void SaveCloudConfig()
{
	std::error_code ec;
	fs::create_directories("Config", ec);
	std::ofstream out("Config/cloud.cfg", std::ios::trunc);
	if (out.is_open())
	{
		out << "# Karamelo Cloud Configuration (Google Drive appDataFolder)\n";
		out << "google_client_id=" << g_client_id << "\n";
		out << "google_client_secret=" << g_client_secret << "\n";
		out << "auto_sync=" << (g_cloud_auto_sync.load() ? 1 : 0) << "\n";
		out << "last_sync_utc=" << g_last_sync_utc << "\n";
	}
}

std::string CloudSyncLoadSecret(const std::string& path)
{
	std::error_code ec;
	std::string value;
	if (!fs::exists(path, ec)) return value;
	std::ifstream tf(path, std::ios::binary);
	std::vector<uint8_t> enc((std::istreambuf_iterator<char>(tf)), std::istreambuf_iterator<char>());
	if (enc.empty()) return value;
#ifdef _WIN32
	DATA_BLOB in_blob{ (DWORD)enc.size(), enc.data() };
	DATA_BLOB out_blob{ 0, NULL };
	if (CryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out_blob))
	{
		value.assign((const char*)out_blob.pbData, out_blob.cbData);
		LocalFree(out_blob.pbData);
	}
#else
	value.assign((const char*)enc.data(), enc.size());
#endif
	return value;
}

void CloudSyncSaveSecret(const std::string& path, const std::string& token)
{
	std::error_code ec;
	fs::create_directories("Config", ec);
	fs::path tok_path = path;
	if (token.empty())
	{
		fs::remove(tok_path, ec);
		return;
	}

#ifdef _WIN32
	DATA_BLOB in_blob{ (DWORD)token.size(), (BYTE*)const_cast<char*>(token.data()) };
	DATA_BLOB out_blob{ 0, NULL };
	if (CryptProtectData(&in_blob, L"KarameloCloudToken", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out_blob))
	{
		FILE* f = fopen(tok_path.string().c_str(), "wb");
		if (f)
		{
			fwrite(out_blob.pbData, 1, out_blob.cbData, f);
			fclose(f);
		}
		LocalFree(out_blob.pbData);
	}
#else
	int fd = open(tok_path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd >= 0)
	{
		if (write(fd, token.data(), token.size()) != (ssize_t)token.size())
			CoreSetToast(Tr("NUVEM: FALHA AO GUARDAR O LOGIN", "CLOUD: COULD NOT STORE THE LOGIN"), 180);
		close(fd);
	}
#endif
}

static void SaveRefreshToken(const std::string& token)
{
	CloudSyncSaveSecret("Config/cloud_token.bin", token);
}

static void UpdateLastSyncTimestampStr(int64_t ts)
{
	if (ts <= 0) return;
	time_t tt = (time_t)ts;
	struct tm tm_buf{};
#ifdef _WIN32
	localtime_s(&tm_buf, &tt);
#else
	localtime_r(&tt, &tm_buf);
#endif
	char buf[64];
	strftime(buf, sizeof(buf), "%d/%m %H:%M", &tm_buf);
	std::lock_guard<std::mutex> lk(g_cloud_mutex);
	g_cloud_last_sync_str = buf;
}

void CloudSyncInit()
{
	g_cloud_shutdown.store(false);
	LoadCloudConfigAndTokens();
	if (g_last_sync_utc > 0)
		UpdateLastSyncTimestampStr(g_last_sync_utc);

	std::vector<CloudSaveEntry> local_files;
	if (CloudSyncScanLocalFiles("", local_files))
		g_cloud_synced_count.store((int)local_files.size());

	if (CloudSyncIsConnected() && g_cloud_auto_sync.load())
	{
		CloudSyncTriggerAsync();
	}
}

void CloudSyncShutdown()
{
	g_cloud_shutdown.store(true);
	if (g_cloud_worker.joinable())
	{
		if (g_cloud_worker.get_id() != std::this_thread::get_id())
			g_cloud_worker.join();
		else
			g_cloud_worker.detach();
	}
	if (g_auth_worker.joinable())
	{
		if (g_auth_worker.get_id() != std::this_thread::get_id())
			g_auth_worker.join();
		else
			g_auth_worker.detach();
	}
	SaveCloudConfig();
}

CloudSyncState CloudSyncGetState()
{
	return (CloudSyncState)g_cloud_state.load();
}

const char* CloudSyncGetStatusString()
{
	static char s_buf[128];
	std::lock_guard<std::mutex> lk(g_cloud_mutex);
	snprintf(s_buf, sizeof(s_buf), "%s", g_cloud_status_str.c_str());
	return s_buf;
}

bool CloudSyncIsConnected()
{
	int st = g_cloud_state.load();
	return (st == CLOUD_IDLE || st == CLOUD_SYNCING || st == CLOUD_CONFLICT);
}

bool CloudSyncGetAutoSync()
{
	return g_cloud_auto_sync.load();
}

void CloudSyncSetAutoSync(bool enabled)
{
	g_cloud_auto_sync.store(enabled);
	SaveCloudConfig();
}

int CloudSyncGetSyncedFileCount()
{
	return g_cloud_synced_count.load();
}

const char* CloudSyncGetLastSyncTimeStr()
{
	static char s_time[64];
	std::lock_guard<std::mutex> lk(g_cloud_mutex);
	snprintf(s_time, sizeof(s_time), "%s", g_cloud_last_sync_str.c_str());
	return s_time;
}

void CloudSyncDisconnect()
{
	if (g_cloud_worker.joinable() && g_cloud_worker.get_id() != std::this_thread::get_id())
		g_cloud_worker.join();
	if (g_auth_worker.joinable() && g_auth_worker.get_id() != std::this_thread::get_id())
		g_auth_worker.join();

	{
		std::lock_guard<std::mutex> lk(g_cloud_mutex);
		g_access_token.clear();
		g_refresh_token.clear();
	}
	SaveRefreshToken("");
	SetCloudStatus(CLOUD_DISCONNECTED, Tr("Desconectado", "Disconnected"));
	CoreSetToast(Tr("CONTA GOOGLE DESCONECTADA", "GOOGLE ACCOUNT DISCONNECTED"), 150);
}

static void OpenBrowserUrl(const std::string& url)
{
	// Validate strict HTTPS Google OAuth prefix before passing to OS handler
	if (url.rfind("https://accounts.google.com/o/oauth2/v2/auth?", 0) != 0)
		return;
#ifdef _WIN32
	ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	pid_t pid = fork();
	if (pid == 0)
	{
		execlp("open", "open", url.c_str(), (char*)NULL);
		_exit(0);
	}
#else
	pid_t pid = fork();
	if (pid == 0)
	{
		execlp("xdg-open", "xdg-open", url.c_str(), (char*)NULL);
		_exit(0);
	}
#endif
}

static std::string CloudSyncUrlEncode(const std::string& str)
{
	std::ostringstream escaped;
	for (unsigned char c : str)
	{
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    c == '-' || c == '_' || c == '.' || c == '~')
		{
			escaped << c;
		}
		else
		{
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", c);
			escaped << buf;
		}
	}
	return escaped.str();
}

static std::string CloudUrlDecode(const std::string& in)
{
	std::string out;
	for (size_t i = 0; i < in.size(); i++)
	{
		if (in[i] == '%' && i + 2 < in.size())
		{
			char hex[3] = { in[i + 1], in[i + 2], 0 };
			out += (char)strtol(hex, NULL, 16);
			i += 2;
		}
		else out += (in[i] == '+') ? ' ' : in[i];
	}
	return out;
}

// =============================================================
// HTTP: WinHTTP on Windows, the curl command elsewhere (like updater.cpp)
// =============================================================

static std::string g_token_url = "https://oauth2.googleapis.com/token";
static std::string g_api_base = "https://www.googleapis.com";

struct CloudHttpResponse
{
	int status = 0;
	std::string body;
};

#ifdef _WIN32
static std::wstring CloudWiden(const std::string& s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
	std::wstring w(n > 0 ? (size_t)n : 1, L'\0');
	if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	w.resize(wcslen(w.c_str()));
	return w;
}

static bool CloudHttp(const char* method, const std::string& url,
                      const std::vector<std::string>& headers, const std::string& body,
                      CloudHttpResponse& out, int timeout_seconds = 0)
{
	std::wstring wurl = CloudWiden(url);
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256] = { 0 };
	parts.lpszHostName = host;
	parts.dwHostNameLength = 256;
	parts.dwUrlPathLength = (DWORD)-1;
	parts.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) return false;
	std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
	if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

	HINTERNET session = WinHttpOpen(L"Karamelo-CloudSync/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;
	if (timeout_seconds > 0)
	{
		const int ms = timeout_seconds * 1000;
		WinHttpSetTimeouts(session, ms, ms, ms, ms);
	}
	else
	{
		WinHttpSetTimeouts(session, 10000, 10000, 30000, 60000);
	}
	HINTERNET conn = WinHttpConnect(session, host, parts.nPort, 0);
	HINTERNET req = conn ? WinHttpOpenRequest(conn, CloudWiden(method).c_str(), path.c_str(), NULL,
	                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                          parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
	                     : NULL;
	bool ok = false;
	if (req)
	{
		std::string all_headers;
		for (const auto& h : headers) all_headers += h + "\r\n";
		std::wstring wheaders = CloudWiden(all_headers);
		if (WinHttpSendRequest(req, wheaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wheaders.c_str(),
		                       wheaders.empty() ? 0 : (DWORD)-1L,
		                       body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
		                       (DWORD)body.size(), (DWORD)body.size(), 0) &&
		    WinHttpReceiveResponse(req, NULL))
		{
			DWORD status = 0, len = sizeof(status);
			WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
			out.status = (int)status;
			out.body.clear();
			char buf[16384];
			DWORD got = 0;
			while (WinHttpReadData(req, buf, sizeof(buf), &got) && got > 0)
				out.body.append(buf, got);
			ok = true;
		}
		WinHttpCloseHandle(req);
	}
	if (conn) WinHttpCloseHandle(conn);
	WinHttpCloseHandle(session);
	return ok;
}
#else
static std::string CloudShellQuote(const std::string& s)
{
	std::string q = "'";
	for (char c : s)
	{
		if (c == '\'') q += "'\\''";
		else q += c;
	}
	return q + "'";
}

// Headers (the access token among them) go in a 0600 config file, never on the
// command line, where any process on the machine could read them.
static bool CloudHttp(const char* method, const std::string& url,
                      const std::vector<std::string>& headers, const std::string& body,
                      CloudHttpResponse& out, int timeout_seconds = 0)
{
	std::error_code ec;
	fs::path dir = fs::temp_directory_path(ec);
	if (ec) return false;
	std::string base = (dir / ("karamelo_cloud_" + GenerateRandomHex(8))).string();
	const std::string cfg = base + ".cfg", in_file = base + ".in", out_file = base + ".out";

	int fd = open(cfg.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return false;
	std::string cfg_text;
	for (const auto& h : headers)
	{
		std::string esc;
		for (char c : h)
		{
			if (c == '"' || c == '\\') esc += '\\';
			esc += c;
		}
		cfg_text += "header = \"" + esc + "\"\n";
	}
	bool wrote = write(fd, cfg_text.data(), cfg_text.size()) == (ssize_t)cfg_text.size();
	close(fd);
	if (!wrote) { fs::remove(cfg, ec); return false; }

	const int max_time = timeout_seconds > 0 ? timeout_seconds : 120;
	std::string cmd = "curl -sS --proto =https,http --connect-timeout " + std::to_string(std::min(15, max_time)) +
		" --max-time " + std::to_string(max_time) + " -X " + std::string(method) +
		" -K " + CloudShellQuote(cfg) + " -o " + CloudShellQuote(out_file) + " -w '%{http_code}' ";
	if (!body.empty())
	{
		int bfd = open(in_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (bfd >= 0)
		{
			wrote = write(bfd, body.data(), body.size()) == (ssize_t)body.size();
			close(bfd);
		}
		if (bfd < 0 || !wrote)
		{
			fs::remove(cfg, ec);
			fs::remove(in_file, ec);
			return false;
		}
		cmd += "--data-binary @" + CloudShellQuote(in_file) + " ";
	}
	cmd += CloudShellQuote(url) + " 2>/dev/null";

	bool ok = false;
	if (FILE* p = popen(cmd.c_str(), "r"))
	{
		char code[16] = { 0 };
		size_t n = fread(code, 1, sizeof(code) - 1, p);
		code[n] = 0;
		ok = (pclose(p) == 0);
		out.status = atoi(code);
		std::ifstream f(out_file, std::ios::binary);
		out.body.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}
	fs::remove(cfg, ec);
	fs::remove(in_file, ec);
	fs::remove(out_file, ec);
	return ok && out.status > 0;
}
#endif

static std::string JsonStr(const JsonValue& obj, const char* key)
{
	const JsonValue* v = obj.Find(key);
	return (v && v->type == JsonValue::String) ? v->str : std::string();
}

// Trades either a login code or the saved refresh token for an access token.
static bool CloudTokenRequest(const std::string& form, std::string& out_access, std::string& out_refresh, int* out_status)
{
	CloudHttpResponse resp;
	if (!CloudHttp("POST", g_token_url, { "Content-Type: application/x-www-form-urlencoded" }, form, resp))
	{
		if (out_status) *out_status = 0;
		return false;
	}
	if (out_status) *out_status = resp.status;
	if (resp.status < 200 || resp.status >= 300) return false;
	JsonValue root;
	std::string err;
	if (!JsonParse(resp.body, root, err) || root.type != JsonValue::Object) return false;
	out_access = JsonStr(root, "access_token");
	out_refresh = JsonStr(root, "refresh_token");
	return !out_access.empty();
}

static bool CloudExchangeLoginCode(const std::string& code, const std::string& verifier,
                                   const std::string& redirect_uri,
                                   std::string& out_access, std::string& out_refresh)
{
	std::ostringstream form;
	form << "code=" << CloudSyncUrlEncode(code)
	     << "&client_id=" << CloudSyncUrlEncode(g_client_id)
	     << "&code_verifier=" << CloudSyncUrlEncode(verifier)
	     << "&redirect_uri=" << CloudSyncUrlEncode(redirect_uri)
	     << "&grant_type=authorization_code";
	if (!g_client_secret.empty()) form << "&client_secret=" << CloudSyncUrlEncode(g_client_secret);
	return CloudTokenRequest(form.str(), out_access, out_refresh, nullptr);
}

// =============================================================
// Google Drive (appDataFolder): the actual sync
// =============================================================

struct DriveFile
{
	std::string id;
	CloudSaveEntry entry;
};

static std::vector<std::string> DriveAuth(const std::string& token)
{
	return { "Authorization: Bearer " + token };
}

static bool DriveList(const std::string& token, std::map<std::string, DriveFile>& out, std::string& error)
{
	out.clear();
	std::string page;
	do
	{
		std::string url = g_api_base + "/drive/v3/files?spaces=appDataFolder&pageSize=1000"
			"&fields=nextPageToken,files(id,name,appProperties)";
		if (!page.empty()) url += "&pageToken=" + CloudSyncUrlEncode(page);
		CloudHttpResponse resp;
		if (!CloudHttp("GET", url, DriveAuth(token), "", resp)) { error = "sem conexao"; return false; }
		if (resp.status != 200) { error = "lista HTTP " + std::to_string(resp.status); return false; }
		JsonValue root;
		std::string err;
		if (!JsonParse(resp.body, root, err) || root.type != JsonValue::Object) { error = "resposta invalida"; return false; }
		const JsonValue* files = root.Find("files");
		if (files && files->type == JsonValue::Array)
		{
			for (const auto& f : files->items)
			{
				if (f.type != JsonValue::Object) continue;
				DriveFile df;
				df.id = JsonStr(f, "id");
				df.entry.rel_path = JsonStr(f, "name");
				if (const JsonValue* props = f.Find("appProperties"))
				{
					if (props->type == JsonValue::Object)
					{
						df.entry.sha256 = JsonStr(*props, "sha256");
						std::string mt = JsonStr(*props, "mtime");
						if (!mt.empty()) df.entry.modified_utc = (int64_t)atoll(mt.c_str());
					}
				}
				if (!df.id.empty() && !df.entry.rel_path.empty())
					out[df.entry.rel_path] = df;
			}
		}
		page = JsonStr(root, "nextPageToken");
	} while (!page.empty());
	return true;
}

static bool DriveUpload(const std::string& token, const fs::path& local_file, const CloudSaveEntry& e,
                        const std::string& existing_id, std::string& error)
{
	std::ifstream in(local_file, std::ios::binary);
	if (!in) { error = "leitura local"; return false; }
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	const std::string boundary = "karamelo_" + GenerateRandomHex(12);
	std::ostringstream meta;
	meta << "{\"name\":" << JsonQuote(e.rel_path);
	if (existing_id.empty()) meta << ",\"parents\":[\"appDataFolder\"]";
	meta << ",\"appProperties\":{\"sha256\":" << JsonQuote(e.sha256)
	     << ",\"mtime\":\"" << e.modified_utc << "\"}}";

	std::string body = "--" + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + meta.str() +
		"\r\n--" + boundary + "\r\nContent-Type: application/octet-stream\r\n\r\n" + content +
		"\r\n--" + boundary + "--\r\n";

	std::string url = g_api_base + "/upload/drive/v3/files";
	if (!existing_id.empty()) url += "/" + existing_id;
	url += "?uploadType=multipart";

	std::vector<std::string> headers = DriveAuth(token);
	headers.push_back("Content-Type: multipart/related; boundary=" + boundary);
	CloudHttpResponse resp;
	if (!CloudHttp(existing_id.empty() ? "POST" : "PATCH", url, headers, body, resp)) { error = "sem conexao"; return false; }
	if (resp.status < 200 || resp.status >= 300) { error = "envio HTTP " + std::to_string(resp.status); return false; }
	return true;
}

// Downloads into <dest>.tmp, checks the hash the uploader recorded, and only
// then replaces the save, keeping the previous one as <dest>.bak.
static bool DriveDownload(const std::string& token, const DriveFile& f, const fs::path& dest, std::string& error)
{
	CloudHttpResponse resp;
	if (!CloudHttp("GET", g_api_base + "/drive/v3/files/" + f.id + "?alt=media", DriveAuth(token), "", resp)) { error = "sem conexao"; return false; }
	if (resp.status != 200) { error = "download HTTP " + std::to_string(resp.status); return false; }
	if (!f.entry.sha256.empty() &&
	    CloudSyncComputeSha256Bytes((const uint8_t*)resp.body.data(), resp.body.size()) != f.entry.sha256)
	{
		error = "hash do download nao confere";
		return false;
	}
	std::error_code ec;
	fs::create_directories(dest.parent_path(), ec);
	fs::path tmp = dest;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out.write(resp.body.data(), (std::streamsize)resp.body.size());
		if (!out.good()) { fs::remove(tmp, ec); error = "escrita local"; return false; }
	}
	if (fs::exists(dest, ec))
	{
		fs::path bak = dest;
		bak += ".bak";
		fs::copy_file(dest, bak, fs::copy_options::overwrite_existing, ec);
	}
	fs::rename(tmp, dest, ec);
	if (ec) { fs::remove(tmp, ec); error = "troca do arquivo"; return false; }
	return true;
}

bool CloudSyncRunNow(const std::string& root_dir, CloudSyncReport* report, std::string* error_out)
{
	CloudSyncReport rep;
	std::string error;
	auto fail = [&](const std::string& why) {
		if (error_out) *error_out = why;
		if (report) *report = rep;
		return false;
	};

	std::string refresh;
	{
		std::lock_guard<std::mutex> lk(g_cloud_mutex);
		refresh = g_refresh_token;
	}
	if (refresh.empty()) return fail("sem login");

	std::ostringstream form;
	form << "refresh_token=" << CloudSyncUrlEncode(refresh)
	     << "&client_id=" << CloudSyncUrlEncode(g_client_id)
	     << "&grant_type=refresh_token";
	if (!g_client_secret.empty()) form << "&client_secret=" << CloudSyncUrlEncode(g_client_secret);
	std::string token, ignored;
	int token_status = 0;
	if (!CloudTokenRequest(form.str(), token, ignored, &token_status))
	{
		if (token_status == 400 || token_status == 401) return fail("login expirado");
		return fail(token_status ? "token HTTP " + std::to_string(token_status) : "sem conexao");
	}

	std::map<std::string, DriveFile> remote;
	if (!DriveList(token, remote, error)) return fail(error);

	std::vector<CloudSaveEntry> local;
	CloudSyncScanLocalFiles(root_dir, local);
	std::error_code ec;
	const fs::path base = root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
	// A running game holds its save in memory and rewrites it on exit, over
	// anything downloaded now. Its turn comes in the sync after it closes.
	const bool may_download = !CoreIsRunning();

	std::set<std::string> seen;
	for (const CloudSaveEntry& e : local)
	{
		if (g_cloud_shutdown.load()) return fail("encerrando");
		seen.insert(e.rel_path);
		auto it = remote.find(e.rel_path);
		const fs::path path = base / fs::path(e.rel_path);
		if (it == remote.end())
		{
			if (!DriveUpload(token, path, e, "", error)) return fail(error);
			rep.uploaded++;
			continue;
		}
		switch (CloudSyncEvaluateConflict(e, it->second.entry, g_last_sync_utc))
		{
		case CLOUD_CONFLICT_NONE:
			break;
		case CLOUD_CONFLICT_UPLOAD_LOCAL:
			if (!DriveUpload(token, path, e, it->second.id, error)) return fail(error);
			rep.uploaded++;
			break;
		case CLOUD_CONFLICT_DOWNLOAD_REMOTE:
			if (!may_download) { rep.skipped++; break; }
			if (!DriveDownload(token, it->second, path, error)) return fail(error);
			rep.downloaded++;
			break;
		case CLOUD_CONFLICT_DIVERGED:
		{
			// Changed on two machines. No side is thrown away: this machine's
			// save stays and goes up, and the other one lands next to it.
			if (!may_download) { rep.skipped++; break; }
			fs::path other = path;
			other += ".conflito";
			if (!DriveDownload(token, it->second, other, error)) return fail(error);
			if (!DriveUpload(token, path, e, it->second.id, error)) return fail(error);
			rep.conflicts++;
			break;
		}
		}
	}

	for (const auto& kv : remote)
	{
		if (seen.count(kv.first)) continue;
		// The name comes from the network: it decides where a file is written.
		if (!CloudSyncIsAllowedSyncPath(kv.first) || kv.first.rfind("saves/", 0) != 0) continue;
		if (!may_download) { rep.skipped++; continue; }
		if (g_cloud_shutdown.load()) return fail("encerrando");
		if (!DriveDownload(token, kv.second, base / fs::path(kv.first), error)) return fail(error);
		rep.downloaded++;
	}

	rep.total = (int)seen.size() + rep.downloaded;
	if (report) *report = rep;
	return true;
}

static void CloudSyncPerformSyncInternal()
{
	{
		std::lock_guard<std::mutex> lk(g_cloud_mutex);
		if (g_refresh_token.empty())
		{
			g_cloud_state.store(CLOUD_DISCONNECTED);
			g_cloud_status_str = Tr("Desconectado", "Disconnected");
			return;
		}
	}
	SetCloudStatus(CLOUD_SYNCING, Tr("Sincronizando saves...", "Syncing saves..."));

	CloudSyncReport rep;
	std::string error;
	if (!CloudSyncRunNow("", &rep, &error))
	{
		if (error == "login expirado")
		{
			{
				std::lock_guard<std::mutex> lk(g_cloud_mutex);
				g_refresh_token.clear();
				g_access_token.clear();
			}
			SaveRefreshToken("");
			SetCloudStatus(CLOUD_DISCONNECTED, Tr("Login expirado - conecte de novo", "Login expired - connect again"));
			CoreSetToast(Tr("NUVEM: LOGIN EXPIRADO, CONECTE DE NOVO", "CLOUD: LOGIN EXPIRED, CONNECT AGAIN"), 240);
		}
		else if (!g_cloud_shutdown.load())
		{
			SetCloudStatus(CLOUD_ERROR, std::string(Tr("Falha: ", "Failed: ")) + error);
			CoreSetToast(Tr("NUVEM: FALHA AO SINCRONIZAR", "CLOUD: SYNC FAILED"), 180);
		}
		return;
	}

	g_cloud_synced_count.store(rep.total);
	g_last_sync_utc = (int64_t)time(NULL);
	UpdateLastSyncTimestampStr(g_last_sync_utc);
	SaveCloudConfig();

	if (rep.conflicts > 0)
	{
		SetCloudStatus(CLOUD_CONFLICT, Tr("Conflito: copia salva como .conflito", "Conflict: copy saved as .conflito"));
		CoreSetToast(Tr("NUVEM: SAVE ALTERADO EM 2 PCS - COPIA .conflito CRIADA", "CLOUD: SAVE CHANGED ON 2 PCS - .conflito COPY MADE"), 300);
		return;
	}
	SetCloudStatus(CLOUD_IDLE, Tr("Sincronizado", "Synced"));
	if (rep.uploaded + rep.downloaded > 0)
		CoreSetToast(Tr("NUVEM: SAVES SINCRONIZADOS", "CLOUD: SAVES SYNCED"), 120);
}

void CloudSyncSetEndpointsForTest(const std::string& token_url, const std::string& api_base,
                                  const std::string& refresh_token, int64_t last_sync_utc)
{
	g_token_url = token_url;
	g_api_base = api_base;
	g_last_sync_utc = last_sync_utc;
	std::lock_guard<std::mutex> lk(g_cloud_mutex);
	g_refresh_token = refresh_token;
}

// The browser half of a Google login: a one-shot page on 127.0.0.1 that Google
// sends the player back to. The page says "connected" only after exchange() got
// what it wanted from Google.
enum CloudLoginResult
{
	CLOUD_LOGIN_OK,
	CLOUD_LOGIN_NO_SOCKET,
	CLOUD_LOGIN_NO_PORT,
	CLOUD_LOGIN_TIMEOUT,
	CLOUD_LOGIN_NO_CALLBACK,
	CLOUD_LOGIN_REFUSED
};

using CloudCodeExchange = std::function<bool(const std::string& code, const std::string& verifier, const std::string& redirect_uri)>;

static CloudLoginResult CloudBrowserLogin(const std::string& client_id, const char* scope_params, const char* ok_heading,
                                          const std::atomic<bool>* cancel, const CloudCodeExchange& exchange)
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (srv == INVALID_SOCKET) return CLOUD_LOGIN_NO_SOCKET;
#else
	int srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) return CLOUD_LOGIN_NO_SOCKET;
#endif
	auto close_socket = [](decltype(srv) sock)
	{
#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
	};

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Strictly 127.0.0.1
	addr.sin_port = 0; // Ephemeral port

	if (bind(srv, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(srv, 1) != 0)
	{
		close_socket(srv);
		return CLOUD_LOGIN_NO_PORT;
	}

	socklen_t alen = sizeof(addr);
	getsockname(srv, (sockaddr*)&addr, &alen);
	int port = ntohs(addr.sin_port);

	std::string verifier = GenerateRandomHex(32);
	std::string challenge = CloudSyncBuildPkceChallenge(verifier);
	std::string state_tok = GenerateRandomHex(16);

	std::ostringstream auth_url;
	auth_url << "https://accounts.google.com/o/oauth2/v2/auth"
	         << "?client_id=" << client_id
	         << "&redirect_uri=http%3A%2F%2F127.0.0.1%3A" << port << "%2Fauth"
	         << "&response_type=code"
	         << "&scope=" << scope_params
	         << "&code_challenge=" << challenge
	         << "&code_challenge_method=S256"
	         << "&state=" << state_tok;

	OpenBrowserUrl(auth_url.str());

	// Wait up to 3 minutes for OAuth callback on 127.0.0.1
	int sel = 0;
	for (int waited = 0; waited < 180 && sel == 0; waited++)
	{
		if (g_cloud_shutdown.load() || (cancel && cancel->load())) break;
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(srv, &fds);
		timeval tv{ 1, 0 };
		sel = select((int)srv + 1, &fds, NULL, NULL, &tv);
	}
	if (sel <= 0)
	{
		close_socket(srv);
		return CLOUD_LOGIN_TIMEOUT;
	}

	sockaddr_in cli_addr{};
	socklen_t cli_len = sizeof(cli_addr);
	auto cli = accept(srv, (sockaddr*)&cli_addr, &cli_len);
	close_socket(srv);
#ifdef _WIN32
	if (cli == INVALID_SOCKET) return CLOUD_LOGIN_NO_CALLBACK;
#else
	if (cli < 0) return CLOUD_LOGIN_NO_CALLBACK;
#endif

	char req_buf[2048] = { 0 };
	int rlen = recv(cli, req_buf, sizeof(req_buf) - 1, 0);
	std::string req(rlen > 0 ? req_buf : "");

	bool state_ok = (req.find("state=" + state_tok) != std::string::npos);
	size_t code_at = req.find("code=");
	bool exchanged = false;
	if (state_ok && code_at != std::string::npos)
	{
		size_t cpos = code_at + 5;
		size_t cend = req.find_first_of(" &\r\n", cpos);
		std::string auth_code = CloudUrlDecode(req.substr(cpos, cend - cpos));
		exchanged = exchange(auth_code, verifier, "http://127.0.0.1:" + std::to_string(port) + "/auth");
	}

	std::ostringstream resp;
	std::string body = exchanged
		? std::string("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Karamelo</title></head>"
		  "<body style=\"background:#12100e;color:#f5c26b;font-family:monospace;text-align:center;padding-top:80px;\">"
		  "<h2>") + ok_heading + "</h2><p>Voce ja pode fechar esta aba e voltar ao Karamelo.</p></body></html>"
		: std::string("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Karamelo</title></head>"
		  "<body style=\"background:#12100e;color:#ff6b6b;font-family:monospace;text-align:center;padding-top:80px;\">"
		  "<h2>FALHA NA AUTENTICACAO</h2><p>Volte ao Karamelo e tente de novo.</p></body></html>");

	resp << "HTTP/1.1 " << (exchanged ? "200 OK" : "400 Bad Request") << "\r\n"
	     << "Content-Type: text/html; charset=utf-8\r\n"
	     << "Content-Length: " << body.size() << "\r\n"
	     << "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none';\r\n"
	     << "X-Content-Type-Options: nosniff\r\n"
	     << "X-Frame-Options: DENY\r\n"
	     << "Cache-Control: no-store\r\n"
	     << "Connection: close\r\n\r\n"
	     << body;

	std::string resp_str = resp.str();
	send(cli, resp_str.c_str(), (int)resp_str.size(), 0);
	close_socket(cli);
	return exchanged ? CLOUD_LOGIN_OK : CLOUD_LOGIN_REFUSED;
}

void CloudSyncGetLocalClient(std::string& out_id, std::string& out_secret)
{
	out_id = g_client_id;
	out_secret = g_client_secret;
}

bool CloudSyncGoogleIdToken(const std::string& client_id, const std::string& client_secret,
                            std::string& out_id_token, std::string& out_error, const std::atomic<bool>* cancel)
{
	// Only "openid": Google answers with an anonymous account id and nothing else,
	// no name and no e-mail.
	const CloudLoginResult result = CloudBrowserLogin(client_id, "openid&prompt=select_account", "KARAMELO LIVE CONECTADO!", cancel,
		[&](const std::string& code, const std::string& verifier, const std::string& redirect)
		{
			std::ostringstream form;
			form << "code=" << CloudSyncUrlEncode(code)
			     << "&client_id=" << CloudSyncUrlEncode(client_id)
			     << "&code_verifier=" << CloudSyncUrlEncode(verifier)
			     << "&redirect_uri=" << CloudSyncUrlEncode(redirect)
			     << "&grant_type=authorization_code";
			if (!client_secret.empty()) form << "&client_secret=" << CloudSyncUrlEncode(client_secret);
			CloudHttpResponse resp;
			if (!CloudHttp("POST", g_token_url, { "Content-Type: application/x-www-form-urlencoded" }, form.str(), resp) ||
			    resp.status < 200 || resp.status >= 300)
				return false;
			JsonValue root;
			std::string err;
			if (!JsonParse(resp.body, root, err) || root.type != JsonValue::Object) return false;
			out_id_token = JsonStr(root, "id_token");
			return !out_id_token.empty();
		});
	if (result == CLOUD_LOGIN_OK) return true;
	out_error = result == CLOUD_LOGIN_TIMEOUT ? "timeout" : "refused";
	return false;
}

bool CloudSyncHttpRequest(const char* method, const std::string& url, const std::vector<std::string>& headers,
                          const std::string& body, int timeout_seconds, int* out_status, std::string* out_body)
{
	CloudHttpResponse resp;
	if (!CloudHttp(method, url, headers, body, resp, timeout_seconds)) return false;
	if (out_status) *out_status = resp.status;
	if (out_body) *out_body = resp.body;
	return true;
}

bool CloudSyncStartOAuthFlow()
{
	if (g_client_id.empty())
	{
		CoreSetToast(Tr("DEFINA google_client_id EM Config/cloud.cfg", "SET google_client_id IN Config/cloud.cfg"), 240);
		SetCloudStatus(CLOUD_ERROR, Tr("Falta Client ID (cloud.cfg)", "Missing Client ID (cloud.cfg)"));
		return false;
	}

	if (g_cloud_state.load() == CLOUD_AUTHENTICATING || g_cloud_state.load() == CLOUD_SYNCING)
		return false;

	if (g_auth_worker.joinable() && g_auth_worker.get_id() != std::this_thread::get_id())
		g_auth_worker.join();

	SetCloudStatus(CLOUD_AUTHENTICATING, Tr("Aguardando login no navegador...", "Waiting for browser login..."));
	CoreSetToast(Tr("ABRINDO NAVEGADOR PARA LOGIN GOOGLE...", "OPENING BROWSER FOR GOOGLE LOGIN..."), 180);

	g_auth_worker = std::thread([]()
	{
		std::string acc_tok, ref_tok;
		const CloudLoginResult result = CloudBrowserLogin(g_client_id,
			"https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.appdata"
			"&access_type=offline&prompt=consent", // or Google sends no refresh token and the login dies in an hour
			"KARAMELO CLOUD CONECTADO!", nullptr,
			[&](const std::string& code, const std::string& verifier, const std::string& redirect)
			{
				return CloudExchangeLoginCode(code, verifier, redirect, acc_tok, ref_tok) && !ref_tok.empty();
			});
		if (result == CLOUD_LOGIN_NO_SOCKET)
		{
			SetCloudStatus(CLOUD_ERROR, Tr("Erro de socket loopback", "Loopback socket error"));
			return;
		}
		if (result == CLOUD_LOGIN_NO_PORT)
		{
			SetCloudStatus(CLOUD_ERROR, Tr("Falha ao abrir porta local", "Failed to bind local port"));
			return;
		}
		if (result == CLOUD_LOGIN_TIMEOUT)
		{
			SetCloudStatus(CLOUD_DISCONNECTED, Tr("Login expirado", "Login timed out"));
			return;
		}
		if (result == CLOUD_LOGIN_NO_CALLBACK)
		{
			SetCloudStatus(CLOUD_ERROR, Tr("Erro no retorno OAuth", "OAuth callback error"));
			return;
		}
		const bool exchanged = result == CLOUD_LOGIN_OK;

		if (exchanged)
		{
			{
				std::lock_guard<std::mutex> lk(g_cloud_mutex);
				g_access_token = acc_tok;
				g_refresh_token = ref_tok;
			}
			SaveRefreshToken(g_refresh_token);
			g_last_sync_utc = (int64_t)time(NULL);
			UpdateLastSyncTimestampStr(g_last_sync_utc);
			SaveCloudConfig();
			SetCloudStatus(CLOUD_IDLE, Tr("Conectado (Google Drive)", "Connected (Google Drive)"));
			CoreSetToast(Tr("KARAMELO CLOUD CONECTADO COM SUCESSO!", "KARAMELO CLOUD CONNECTED SUCCESSFULLY!"), 180);
			CloudSyncPerformSyncInternal();
		}
		else
		{
			SetCloudStatus(CLOUD_ERROR, Tr("Google recusou o login", "Google refused the login"));
		}
	});

	return true;
}

void CloudSyncTriggerAsync()
{
	if (g_cloud_state.load() == CLOUD_SYNCING || g_cloud_state.load() == CLOUD_AUTHENTICATING)
		return;

	if (g_cloud_worker.joinable())
	{
		if (g_cloud_worker.get_id() == std::this_thread::get_id())
		{
			CloudSyncPerformSyncInternal();
			return;
		}
		g_cloud_worker.join();
	}

	g_cloud_worker = std::thread([]()
	{
		CloudSyncPerformSyncInternal();
	});
}

void CloudSyncOnGameLoad(const char* system_dir, const char* game_stem)
{
	(void)system_dir;
	(void)game_stem;
	// Refresh local save count when starting a session
	std::vector<CloudSaveEntry> entries;
	if (CloudSyncScanLocalFiles("", entries))
		g_cloud_synced_count.store((int)entries.size());
}

void CloudSyncOnGameUnload(const char* system_dir, const char* game_stem, uint64_t session_seconds)
{
	if (system_dir && game_stem && system_dir[0] && game_stem[0])
	{
		std::lock_guard<std::mutex> lk(g_cloud_mutex);
		std::string key = std::string(system_dir) + "/" + std::string(game_stem);
		g_playtime_map[key] += session_seconds;
	}

	if (g_cloud_auto_sync.load() && CloudSyncIsConnected())
	{
		CloudSyncTriggerAsync();
	}
}

