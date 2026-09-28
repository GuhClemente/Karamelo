// Native Xbox 360 disc-image extraction and XEX2 decoding - see x360_dump.h
// for why this exists. Format notes are from the public Free60 / Xenia
// documentation of XDVDFS and XEX2; the LZX decoder is libmspack's, the same
// one Xenia uses for exactly this payload.
#include "x360_dump.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <stdarg.h>

#include "mspack.h"
#include "lzx.h"

namespace fs = std::filesystem;

namespace x360 {

// -------------------------------------------------------------
// Byte helpers
// -------------------------------------------------------------

static uint32_t Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static uint16_t Be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
static void PutBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
static void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
static uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

static std::string Hex(const uint8_t* d, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += k[d[i] >> 4]; s += k[d[i] & 15]; }
    return s;
}

// -------------------------------------------------------------
// BCrypt: SHA-256 / SHA-1 (incremental, duplicable) and AES-128
// -------------------------------------------------------------

namespace {

struct Hasher {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = false;

    explicit Hasher(LPCWSTR algorithm = BCRYPT_SHA256_ALGORITHM) {
        if (BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0) < 0) return;
        ok = BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;
    ~Hasher() {
        if (h) BCryptDestroyHash(h);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void Update(const uint8_t* d, size_t n) {
        // BCryptHashData takes a ULONG - feed large buffers in slices.
        while (ok && n) {
            ULONG part = ULONG(n > 0x40000000 ? 0x40000000 : n);
            ok = BCryptHashData(h, const_cast<PUCHAR>(d), part, 0) >= 0;
            d += part; n -= part;
        }
    }
    void UpdateZeros(size_t n) {
        static const uint8_t zeros[4096] = {};
        while (n) { size_t p = n > sizeof(zeros) ? sizeof(zeros) : n; Update(zeros, p); n -= p; }
    }
    // Digest of everything fed so far, without disturbing the running state -
    // lets one pass over an image yield the digest of every prefix length.
    std::string PeekHex() const {
        if (!ok) return "";
        BCRYPT_HASH_HANDLE dup = nullptr;
        if (BCryptDuplicateHash(h, &dup, nullptr, 0, 0) < 0) return "";
        uint8_t out[32];
        bool fin = BCryptFinishHash(dup, out, sizeof(out), 0) >= 0;
        BCryptDestroyHash(dup);
        return fin ? Hex(out, 32) : "";
    }
    bool Finish(uint8_t* out, ULONG n) { return ok && BCryptFinishHash(h, out, n, 0) >= 0; }
};

bool AesDecrypt(const uint8_t key[16], bool cbc, uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE k = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0) {
        const wchar_t* mode = cbc ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)mode,
                              ULONG((wcslen(mode) + 1) * sizeof(wchar_t)), 0) >= 0 &&
            BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, (PUCHAR)key, 16, 0) >= 0) {
            uint8_t iv[16] = {};
            size_t whole = size & ~size_t(15);
            ok = true;
            // Chunked so a ULONG length never overflows; the IV buffer
            // carries the CBC chain from one call to the next.
            for (size_t off = 0; ok && off < whole;) {
                ULONG part = ULONG((whole - off) > 0x10000000 ? 0x10000000 : (whole - off));
                ULONG got = 0;
                ok = BCryptDecrypt(k, data + off, part, nullptr, cbc ? iv : nullptr, cbc ? 16 : 0,
                                   data + off, part, &got, 0) >= 0 && got == part;
                off += part;
            }
        }
    }
    if (k) BCryptDestroyKey(k);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

} // namespace

std::string Sha256Hex(const uint8_t* data, size_t size) {
    Hasher s;
    s.Update(data, size);
    return s.PeekHex();
}

// -------------------------------------------------------------
// XDVDFS / GDFX
// -------------------------------------------------------------

namespace {

constexpr uint64_t kSector = 2048;
constexpr uint64_t kVolumeDescSector = 32;
const char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// Game partition offsets seen in real dumps: trimmed/"game partition only",
// XGD2 (most 360 games, The Darkness included), XGD3, and original Xbox XGD1.
const uint64_t kPartitionBases[] = { 0x0ull, 0xFD90000ull, 0x2080000ull, 0x18300000ull };

struct Source {
    virtual ~Source() = default;
    virtual bool Read(uint64_t off, void* dst, size_t n) = 0;
    virtual uint64_t Size() const = 0;
};

struct FileSource : Source {
    FILE* f = nullptr;
    uint64_t size = 0;
    explicit FileSource(const std::string& path) {
        f = _wfopen(fs::path(path).wstring().c_str(), L"rb");
        if (f && _fseeki64(f, 0, SEEK_END) == 0) size = uint64_t(_ftelli64(f));
    }
    ~FileSource() override { if (f) fclose(f); }
    bool Read(uint64_t off, void* dst, size_t n) override {
        if (!f || off + n > size || off + n < off) return false;
        if (_fseeki64(f, int64_t(off), SEEK_SET) != 0) return false;
        return fread(dst, 1, n, f) == n;
    }
    uint64_t Size() const override { return size; }
};

struct MemSource : Source {
    const std::vector<uint8_t>& m;
    explicit MemSource(const std::vector<uint8_t>& v) : m(v) {}
    bool Read(uint64_t off, void* dst, size_t n) override {
        if (off + n > m.size() || off + n < off) return false;
        memcpy(dst, m.data() + off, n);
        return true;
    }
    uint64_t Size() const override { return m.size(); }
};

bool FindBase(Source& src, uint64_t& base) {
    for (uint64_t b : kPartitionBases) {
        char magic[20];
        if (!src.Read(b + kVolumeDescSector * kSector, magic, sizeof(magic))) continue;
        if (memcmp(magic, kMagic, 20) == 0) { base = b; return true; }
    }
    return false;
}

// Rejects anything that could step outside the output folder or that
// Windows cannot create - disc names are plain ASCII in practice, so this
// never trips on a real dump.
bool SafeName(const std::string& n) {
    if (n.empty() || n == "." || n == "..") return false;
    for (unsigned char c : n)
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') return false;
    return true;
}

bool WalkDir(Source& src, uint64_t base, uint32_t sector, uint32_t size, const std::string& prefix,
             int depth, std::vector<XisoFile>& out, std::string& err) {
    if (size == 0) return true;
    if (depth > 32) { err = "arvore de diretorios profunda demais"; return false; }
    if (size > 64u * 1024 * 1024) { err = "tabela de diretorio invalida"; return false; }

    std::vector<uint8_t> table(size);
    if (!src.Read(base + uint64_t(sector) * kSector, table.data(), size)) {
        err = "leitura fora da imagem (diretorio)";
        return false;
    }

    // Each directory is a binary tree of entries; left/right are offsets in
    // 4-byte units from the start of the table, 0 meaning "no child" (the
    // root entry is the only one ever at offset 0).
    std::vector<uint32_t> stack{ 0 };
    std::set<uint32_t> seen;
    while (!stack.empty()) {
        uint32_t o = stack.back();
        stack.pop_back();
        if (!seen.insert(o).second) continue;
        if (uint64_t(o) + 14 > size) continue;
        const uint8_t* e = table.data() + o;
        uint16_t left = Le16(e), right = Le16(e + 2);
        if (left == 0xFFFF && right == 0xFFFF) continue; // sector padding / empty dir
        uint32_t f_sector = Le32(e + 4), f_size = Le32(e + 8);
        uint8_t attr = e[12], name_len = e[13];
        if (uint64_t(o) + 14 + name_len > size) { err = "entrada de diretorio truncada"; return false; }
        std::string name(reinterpret_cast<const char*>(e + 14), name_len);
        if (left && left != 0xFFFF) stack.push_back(uint32_t(left) * 4);
        if (right && right != 0xFFFF) stack.push_back(uint32_t(right) * 4);
        if (!SafeName(name)) { err = "nome de arquivo invalido na imagem: " + name; return false; }

        std::string path = prefix.empty() ? name : prefix + "/" + name;
        if (attr & 0x10) {
            if (!WalkDir(src, base, f_sector, f_size, path, depth + 1, out, err)) return false;
        } else {
            uint64_t off = base + uint64_t(f_sector) * kSector;
            if (off + f_size > src.Size()) { err = "arquivo fora dos limites da imagem: " + path; return false; }
            out.push_back({ path, off, f_size });
        }
    }
    return true;
}

bool ListAll(Source& src, std::vector<XisoFile>& out, std::string& err) {
    uint64_t base = 0;
    if (!FindBase(src, base)) { err = "imagem sem sistema de arquivos XDVDFS (nao e um ISO de Xbox 360?)"; return false; }
    uint8_t vd[28];
    if (!src.Read(base + kVolumeDescSector * kSector, vd, sizeof(vd))) { err = "descritor de volume ilegivel"; return false; }
    return WalkDir(src, base, Le32(vd + 20), Le32(vd + 24), "", 0, out, err);
}

} // namespace

bool XisoFindPartition(const std::string& iso_path, uint64_t& out_base, std::string& err) {
    FileSource src(iso_path);
    if (!src.f) { err = "nao foi possivel abrir " + iso_path; return false; }
    if (!FindBase(src, out_base)) { err = "imagem sem sistema de arquivos XDVDFS"; return false; }
    return true;
}

bool XisoListFromMemory(const std::vector<uint8_t>& image, std::vector<XisoFile>& out, std::string& err) {
    MemSource src(image);
    return ListAll(src, out, err);
}

bool XisoExtract(const std::string& iso_path, const std::string& out_dir,
                 std::string& err, const ProgressFn& progress) {
    FileSource src(iso_path);
    if (!src.f) { err = "nao foi possivel abrir " + iso_path; return false; }
    std::vector<XisoFile> files;
    if (!ListAll(src, files, err)) return false;

    uint64_t total = 0, done = 0;
    for (const auto& f : files) total += f.size;

    std::vector<uint8_t> buf(4u * 1024 * 1024);
    std::error_code ec;
    for (const auto& f : files) {
        fs::path dst = fs::path(out_dir) / fs::path(std::u8string(f.path.begin(), f.path.end()));
        fs::create_directories(dst.parent_path(), ec);
        if (fs::exists(dst, ec) && fs::file_size(dst, ec) == f.size) {
            done += f.size;
            if (progress) progress(done, total);
            continue;
        }
        // Written under a temporary name and renamed at the end, so an
        // interrupted copy never leaves a same-sized-but-wrong file that the
        // resume check above would then trust.
        fs::path part = dst;
        part += ".part";
        FILE* o = _wfopen(part.wstring().c_str(), L"wb");
        if (!o) { err = "nao foi possivel criar " + dst.string(); return false; }
        uint64_t left = f.size, off = f.offset;
        bool ok = true;
        while (left && ok) {
            size_t n = size_t(left > buf.size() ? buf.size() : left);
            ok = src.Read(off, buf.data(), n) && fwrite(buf.data(), 1, n, o) == n;
            off += n; left -= n; done += n;
            if (progress) progress(done, total);
        }
        ok = (fclose(o) == 0) && ok;
        if (!ok) { fs::remove(part, ec); err = "falha ao extrair " + f.path + " (disco cheio?)"; return false; }
        fs::rename(part, dst, ec);
        if (ec) { err = "falha ao finalizar " + dst.string(); return false; }
    }
    return true;
}

// -------------------------------------------------------------
// XEX2
// -------------------------------------------------------------

namespace {

// AES-128 keys that wrap each XEX's per-file key. Both are public (Free60,
// Xenia's xex_module.cc): the retail key for every shipped disc, the all-zero
// devkit key for development builds.
const uint8_t kRetailKey[16] = { 0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                                 0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91 };
const uint8_t kDevkitKey[16] = {};

constexpr uint32_t kHeaderFileFormat = 0x000003FF;
constexpr uint32_t kHeaderExecutionInfo = 0x00040006;
constexpr uint32_t kSecImageSize = 0x004;
constexpr uint32_t kSecLoadAddress = 0x110;
constexpr uint32_t kSecAesKey = 0x150;

// --- libmspack glue: in-memory mspack_file over a byte range -------------

struct MemFile {
    uint8_t* data;
    size_t size;
    size_t pos;
};

int MsRead(mspack_file* f, void* buf, int bytes) {
    auto* m = reinterpret_cast<MemFile*>(f);
    if (bytes < 0) return -1;
    size_t n = m->size - m->pos;
    if (size_t(bytes) < n) n = size_t(bytes);
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return int(n);
}
int MsWrite(mspack_file* f, void* buf, int bytes) {
    auto* m = reinterpret_cast<MemFile*>(f);
    if (bytes < 0) return -1;
    size_t n = m->size - m->pos;
    if (size_t(bytes) < n) n = size_t(bytes);
    memcpy(m->data + m->pos, buf, n);
    m->pos += n;
    return int(n);
}
void* MsAlloc(mspack_system*, size_t n) { return malloc(n); }
void MsFree(void* p) { free(p); }
void MsCopy(void* src, void* dst, size_t n) { memmove(dst, src, n); }
void MsMessage(mspack_file*, const char*, ...) {}

bool LzxDecompress(uint8_t* in, size_t in_size, uint8_t* out, size_t out_size,
                   uint32_t window_size, size_t& produced) {
    int window_bits = 0;
    while ((1u << window_bits) < window_size && window_bits < 31) ++window_bits;

    mspack_system sys = {};
    sys.read = MsRead;
    sys.write = MsWrite;
    sys.message = MsMessage;
    sys.alloc = MsAlloc;
    sys.free = MsFree;
    sys.copy = MsCopy;

    MemFile src{ in, in_size, 0 };
    MemFile dst{ out, out_size, 0 };
    lzxd_stream* lzx = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&src),
                                 reinterpret_cast<mspack_file*>(&dst), window_bits, 0, 0x8000,
                                 off_t(out_size), 0);
    if (!lzx) return false;
    int rc = lzxd_decompress(lzx, off_t(out_size));
    lzxd_free(lzx);
    produced = dst.pos;
    // A stream that simply ends early (the loader zero-fills the rest) still
    // yields every frame it finished - the digest check downstream decides
    // whether that was the whole image or a corrupt payload.
    return rc == MSPACK_ERR_OK || produced > 0;
}

bool DecodeWithKey(const std::vector<uint8_t>& xex, const XexInfo& info, const uint8_t* wrap_key,
                   std::vector<uint8_t>& image, size_t& produced, std::string& err) {
    const uint8_t* fmt = xex.data() + info.format_info_offset;
    std::vector<uint8_t> payload(xex.begin() + info.header_size, xex.end());

    if (info.encryption_type == 1) {
        uint8_t session[16];
        memcpy(session, xex.data() + info.security_offset + kSecAesKey, 16);
        if (!AesDecrypt(wrap_key, false, session, 16) ||
            !AesDecrypt(session, true, payload.data(), payload.size())) {
            err = "falha na decifragem AES";
            return false;
        }
    } else if (info.encryption_type != 0) {
        err = "tipo de criptografia XEX desconhecido";
        return false;
    }

    image.assign(info.image_size, 0);
    produced = 0;

    switch (info.compression_type) {
    case 0: {
        size_t n = payload.size() < image.size() ? payload.size() : image.size();
        memcpy(image.data(), payload.data(), n);
        produced = n;
        return true;
    }
    case 1: {
        // Basic: runs of (data_size bytes present in the file, zero_size
        // bytes the loader fills in), the file holding only the data parts.
        uint32_t blocks = (info.format_info_size - 8) / 8;
        size_t in = 0, out = 0;
        for (uint32_t i = 0; i < blocks; ++i) {
            uint32_t data = Be32(fmt + 8 + i * 8), zero = Be32(fmt + 12 + i * 8);
            if (in + data > payload.size() || out + data + zero > image.size()) {
                err = "blocos basicos do XEX fora dos limites";
                return false;
            }
            memcpy(image.data() + out, payload.data() + in, data);
            in += data;
            out += data + zero;
        }
        produced = out;
        return true;
    }
    case 2: {
        // Normal: a chain of blocks, each starting with the next block's size
        // and SHA-1, then 16-bit-length-prefixed LZX chunks. The chunks are
        // concatenated into one LZX stream.
        uint32_t window = Be32(fmt + 8);
        uint32_t block_size = Be32(fmt + 12);
        std::vector<uint8_t> stream;
        stream.reserve(payload.size());
        size_t p = 0;
        while (block_size) {
            if (p + block_size > payload.size() || block_size < 24) {
                err = "cadeia de blocos LZX fora dos limites";
                return false;
            }
            size_t next = p + block_size;
            uint32_t next_size = Be32(payload.data() + p);
            size_t q = p + 24;
            while (q + 2 <= next) {
                uint16_t chunk = Be16(payload.data() + q);
                q += 2;
                if (!chunk) break;
                if (q + chunk > next) { err = "chunk LZX fora dos limites"; return false; }
                stream.insert(stream.end(), payload.begin() + q, payload.begin() + q + chunk);
                q += chunk;
            }
            p = next;
            block_size = next_size;
        }
        if (!LzxDecompress(stream.data(), stream.size(), image.data(), image.size(), window, produced)) {
            err = "falha na descompressao LZX";
            return false;
        }
        return true;
    }
    default:
        err = "compressao XEX delta nao suportada";
        return false;
    }
}

bool LooksLikePe(const std::vector<uint8_t>& image, size_t produced) {
    if (produced < 0x40 || image[0] != 'M' || image[1] != 'Z') return false;
    uint32_t pe = Le32(image.data() + 0x3C);
    return uint64_t(pe) + 4 <= image.size() && memcmp(image.data() + pe, "PE\0\0", 4) == 0;
}

} // namespace

bool XexParse(const std::vector<uint8_t>& xex, XexInfo& info, std::string& err) {
    info = XexInfo{};
    if (xex.size() < 0x18 || memcmp(xex.data(), "XEX2", 4) != 0) { err = "arquivo nao e um XEX2"; return false; }
    info.header_size = Be32(&xex[8]);
    info.security_offset = Be32(&xex[0x10]);
    uint32_t count = Be32(&xex[0x14]);
    if (info.header_size > xex.size() || uint64_t(count) * 8 + 0x18 > info.header_size ||
        uint64_t(info.security_offset) + 0x184 > info.header_size) {
        err = "cabecalho XEX invalido";
        return false;
    }
    const uint8_t* sec = xex.data() + info.security_offset;
    info.image_size = Be32(sec + kSecImageSize);
    info.load_address = Be32(sec + kSecLoadAddress);
    if (info.image_size == 0 || info.image_size > 0x10000000) { err = "tamanho de imagem XEX invalido"; return false; }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = Be32(&xex[0x18 + i * 8]);
        uint32_t val = Be32(&xex[0x18 + i * 8 + 4]);
        if (key == kHeaderFileFormat) {
            if (uint64_t(val) + 8 > info.header_size) { err = "file format info invalido"; return false; }
            info.format_info_offset = val;
            info.format_info_size = Be32(&xex[val]);
            info.encryption_type = Be16(&xex[val + 4]);
            info.compression_type = Be16(&xex[val + 6]);
            if (info.format_info_size < 8 || uint64_t(val) + info.format_info_size > info.header_size) {
                err = "file format info invalido";
                return false;
            }
        } else if (key == kHeaderExecutionInfo && uint64_t(val) + 16 <= info.header_size) {
            info.media_id = Be32(&xex[val]);
            info.version = Be32(&xex[val + 4]);
            info.title_id = Be32(&xex[val + 12]);
        }
    }
    if (!info.format_info_offset) { err = "XEX sem file format info"; return false; }
    if (info.compression_type == 2 && info.format_info_size < 36) { err = "file format info LZX curto"; return false; }
    return true;
}

bool XexDecodeImage(const std::vector<uint8_t>& xex, std::vector<uint8_t>& out_image,
                    size_t& out_produced, std::string& err) {
    XexInfo info;
    if (!XexParse(xex, info, err)) return false;
    std::string retail_err;
    if (DecodeWithKey(xex, info, kRetailKey, out_image, out_produced, retail_err) &&
        LooksLikePe(out_image, out_produced))
        return true;
    if (info.encryption_type == 1) {
        std::string dev_err;
        if (DecodeWithKey(xex, info, kDevkitKey, out_image, out_produced, dev_err) &&
            LooksLikePe(out_image, out_produced))
            return true;
    }
    err = retail_err.empty() ? "imagem decodificada nao e um PE valido (dump corrompido?)" : retail_err;
    return false;
}

// -------------------------------------------------------------
// Recomp inputs (XexTool -b and -e u -c u)
// -------------------------------------------------------------

namespace {

bool Accepted(const std::vector<std::string>& ok, const std::string& h) {
    for (const auto& a : ok) if (a == h) return true;
    return false;
}

// The trailing-zero cut XexTool applies to its outputs is not documented
// (the recomp's own loader notes a 0xB0FE00-byte basefile for a 0xB10000
// image), so every plausible length is tried. All of them are prefixes of
// one image, so a single running hash yields every candidate's digest.
std::vector<size_t> LengthCandidates(const std::vector<uint8_t>& image, size_t produced) {
    size_t last = image.size();
    while (last > 0 && image[last - 1] == 0) --last;
    std::set<size_t> s{ image.size(), produced, last };
    for (size_t a : { size_t(0x10), size_t(0x200), size_t(0x1000), size_t(0x10000) }) {
        size_t r = (last + a - 1) / a * a;
        if (r <= image.size()) s.insert(r);
    }
    for (size_t r = (last + 0x1FF) / 0x200 * 0x200; r <= image.size(); r += 0x200) s.insert(r);
    s.erase(0);
    return std::vector<size_t>(s.begin(), s.end());
}

// --- _uncrypted.xex exactly as XexTool 6.3 writes it -----------------------
// The layout below was reverse-engineered by XexTool-RE (github.com/RexxColder/
// XexTool-RE, MIT, (c) 2026 Logan Greer and contributors), a clean-room rebuild
// verified byte-identical against xorloser's binary; this is a port of its
// -c u path (convert.cpp: decompress_to_basic / build_header / devkit_sign)
// trimmed to what a retail disc needs. Checked against The Darkness: it
// reproduces the digest DarkRecomp.exe expects.

constexpr uint32_t kHeaderImportLibraries = 0x000103FF;
constexpr uint32_t kGranule = 0x8000;

struct Run { uint32_t data, zero; };

// "-c u" of an LZX-compressed (or plain) file: xextool cuts the image into
// 32 KiB granules and every all-zero granule joins a zero run. A tail shorter
// than a granule is data, folded into the last run only if that run has no
// zero part (it would otherwise sit after those zeros).
std::vector<Run> GranuleRuns(const std::vector<uint8_t>& image) {
    std::vector<Run> runs;
    const size_t ng = image.size() / kGranule;
    auto zero_granule = [&](size_t g) {
        const uint8_t* p = image.data() + g * kGranule;
        for (size_t k = 0; k < kGranule; ++k) if (p[k]) return false;
        return true;
    };
    for (size_t g = 0; g < ng;) {
        size_t d = 0, z = 0;
        while (g < ng && !zero_granule(g)) { ++d; ++g; }
        while (g < ng && zero_granule(g)) { ++z; ++g; }
        if (d || z) runs.push_back({ uint32_t(d * kGranule), uint32_t(z * kGranule) });
    }
    if (runs.empty()) runs.push_back({ uint32_t(image.size()), 0 });
    size_t tail = image.size() - ng * kGranule;
    if (tail) {
        if (runs.back().zero == 0) runs.back().data += uint32_t(tail);
        else runs.push_back({ uint32_t(tail), 0 });
    }
    return runs;
}

// A file that is already "basic" keeps its own runs - that is what The
// Darkness's digest shows: xextool only decrypted the payload.
std::vector<Run> OriginalRuns(const std::vector<uint8_t>& xex, const XexInfo& info) {
    std::vector<Run> runs;
    const uint8_t* fmt = xex.data() + info.format_info_offset;
    uint64_t covered = 0;
    for (uint32_t i = 0; i < (info.format_info_size - 8) / 8; ++i) {
        Run r{ Be32(fmt + 8 + i * 8), Be32(fmt + 12 + i * 8) };
        covered += uint64_t(r.data) + r.zero;
        runs.push_back(r);
    }
    if (covered > info.image_size) runs.clear();
    return runs;
}

// XexTool rebuilds the whole header on every write: optional-header entries
// sorted by key; the security info moved to 0x18 + count*8 + 0x80; every
// data blob packed in key order right after it, except the import libraries,
// which sit flush against a 4 KiB-aligned payload offset. It then recomputes
// the header digest and, having no retail private key, zeroes the signature.
// The per-file AES key is left as it was.
bool XexToolUncrypted(const std::vector<uint8_t>& xex, const XexInfo& info,
                      const std::vector<uint8_t>& image, const std::vector<Run>& runs,
                      bool pad_payload, std::vector<uint8_t>& out) {
    struct Entry { uint32_t key, val; std::vector<uint8_t> body; };
    const uint32_t count = Be32(&xex[0x14]);
    std::vector<Entry> ents;
    for (uint32_t j = 0; j < count; ++j) {
        Entry e{ Be32(&xex[0x18 + j * 8]), Be32(&xex[0x1C + j * 8]), {} };
        const uint32_t lo = e.key & 0xFF;
        if (e.key == kHeaderFileFormat) {
            e.body.assign(8 + runs.size() * 8, 0);
            PutBe32(e.body.data(), uint32_t(e.body.size()));
            PutBe16(e.body.data() + 4, 0);   // not encrypted
            PutBe16(e.body.data() + 6, 1);   // basic
            for (size_t r = 0; r < runs.size(); ++r) {
                PutBe32(e.body.data() + 8 + r * 8, runs[r].data);
                PutBe32(e.body.data() + 12 + r * 8, runs[r].zero);
            }
        } else if (lo > 1 && e.val && uint64_t(e.val) + 4 <= info.header_size) {
            // Low byte 0/1: the value itself is the data. 0xFF: the blob
            // leads with its own size. Otherwise: that many dwords.
            uint32_t sz = lo == 0xFF ? Be32(&xex[e.val]) : lo * 4;
            if (!sz || uint64_t(e.val) + sz > info.header_size) return false;
            e.body.assign(xex.begin() + e.val, xex.begin() + e.val + sz);
        }
        ents.push_back(std::move(e));
    }
    std::stable_sort(ents.begin(), ents.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });

    const uint32_t old_sec = info.security_offset;
    const uint32_t sec_size = Be32(&xex[old_sec]);
    if (sec_size < 0x184 || uint64_t(old_sec) + sec_size > info.header_size) return false;
    const uint32_t sec = 0x18 + count * 8 + 0x80;
    uint32_t pos = sec + sec_size, import_size = 0;
    for (Entry& e : ents) {
        if (e.body.empty()) continue;
        if (e.key == kHeaderImportLibraries) { import_size = uint32_t(e.body.size()); continue; }
        e.val = pos;
        pos += uint32_t(e.body.size());
    }
    const uint32_t data_offset = (pos + import_size + 0xFFF) & ~0xFFFu;
    for (Entry& e : ents)
        if (e.key == kHeaderImportLibraries && !e.body.empty()) e.val = data_offset - import_size;

    out.assign(data_offset, 0);
    memcpy(out.data(), xex.data(), 0x18);
    PutBe32(&out[0x08], data_offset);
    PutBe32(&out[0x10], sec);
    PutBe32(&out[0x14], count);
    memcpy(&out[sec], &xex[old_sec], sec_size);
    for (uint32_t j = 0; j < count; ++j) {
        PutBe32(&out[0x18 + j * 8], ents[j].key);
        PutBe32(&out[0x1C + j * 8], ents[j].val);
        if (!ents[j].body.empty()) memcpy(&out[ents[j].val], ents[j].body.data(), ents[j].body.size());
    }

    // Signature at sec+8 (0x100 bytes), then the signed image info; the
    // header digest at sec+0x164 covers everything after the signed region
    // up to the payload, then everything before the signature.
    const uint32_t sig = sec + 8, signed_end = sec + 0x17C;
    memset(&out[sig], 0, 0x100);
    Hasher sha1(BCRYPT_SHA1_ALGORITHM);
    sha1.Update(&out[signed_end], data_offset - signed_end);
    sha1.Update(out.data(), sig);
    if (!sha1.Finish(&out[sec + 0x164], 20)) return false;

    // Payload: the data runs only, in the clear.
    size_t at = 0;
    for (const Run& r : runs) {
        if (at + r.data > image.size()) return false;
        out.insert(out.end(), image.begin() + at, image.begin() + at + r.data);
        at += size_t(r.data) + r.zero;
    }
    if (pad_payload) out.resize(data_offset + ((out.size() - data_offset + kGranule - 1) & ~size_t(kGranule - 1)), 0);
    return true;
}

} // namespace

bool X360BuildRecompInputs(const std::vector<uint8_t>& xex,
                           const std::vector<std::string>& accepted,
                           std::vector<uint8_t>& out_basefile,
                           std::vector<uint8_t>& out_uncrypted,
                           std::string& out_report, std::string& err) {
    XexInfo info;
    if (!XexParse(xex, info, err)) return false;
    std::vector<uint8_t> image;
    size_t produced = 0;
    if (!XexDecodeImage(xex, image, produced, err)) return false;

    char idbuf[160];
    snprintf(idbuf, sizeof(idbuf), "TitleID=%08X MediaID=%08X versao=%08X enc=%u comp=%u imagem=0x%X coberto=0x%zX",
             info.title_id, info.media_id, info.version, info.encryption_type, info.compression_type,
             info.image_size, produced);
    out_report = idbuf;

    std::vector<size_t> lengths = LengthCandidates(image, produced);

    // --- basefile.exe: a prefix of the image ---
    size_t base_len = 0;
    {
        Hasher h;
        size_t fed = 0;
        for (size_t L : lengths) {
            h.Update(image.data() + fed, L - fed);
            fed = L;
            if (Accepted(accepted, h.PeekHex())) { base_len = L; break; }
        }
    }
    out_report += " | basefile(imagem inteira)=" + Sha256Hex(image.data(), image.size());
    if (!base_len) { err = "basefile.exe gerado nao confere com esta versao do port"; return false; }

    // --- _uncrypted.xex: XexTool's rebuilt header + the payload in the clear ---
    // A file that is already plain is its own answer.
    if (info.encryption_type == 0 && info.compression_type == 0 && Accepted(accepted, Sha256Hex(xex.data(), xex.size()))) {
        out_basefile.assign(image.begin(), image.begin() + base_len);
        out_uncrypted = xex;
        return true;
    }

    std::vector<std::vector<Run>> layouts;
    if (info.compression_type == 1) layouts.push_back(OriginalRuns(xex, info));
    layouts.push_back(GranuleRuns(image));
    std::string first_digest;
    for (const auto& runs : layouts) {
        if (runs.empty()) continue;
        uint64_t stored = 0;
        for (const Run& r : runs) stored += r.data;
        for (bool pad : { false, true }) {
            if (pad && stored % kGranule == 0) continue;   // same bytes as unpadded
            std::vector<uint8_t> candidate;
            if (!XexToolUncrypted(xex, info, image, runs, pad, candidate)) continue;
            std::string digest = Sha256Hex(candidate.data(), candidate.size());
            if (first_digest.empty()) first_digest = digest;
            if (Accepted(accepted, digest)) {
                out_basefile.assign(image.begin(), image.begin() + base_len);
                out_uncrypted = std::move(candidate);
                return true;
            }
        }
    }
    out_report += " | _uncrypted=" + (first_digest.empty() ? std::string("(cabecalho invalido)") : first_digest);
    // Keep the basefile anyway - it matched - so a caller falling back to
    // XexTool for the .xex alone still has it.
    out_basefile.assign(image.begin(), image.begin() + base_len);
    err = "_uncrypted.xex gerado nao confere com esta versao do port";
    return false;
}

std::vector<std::string> FindEmbeddedSha256(const std::string& binary_path) {
    std::vector<std::string> found;
    FILE* f = _wfopen(fs::path(binary_path).wstring().c_str(), L"rb");
    if (!f) return found;
    std::vector<uint8_t> buf(1u << 20);
    std::string run;
    auto flush = [&]() {
        // Exactly 64 hex digits, bounded by non-hex bytes (the array's NUL
        // terminator on the right) - a longer run is something else.
        if (run.size() == 64) found.push_back(run);
        run.clear();
    };
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            char c = char(buf[i]);
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
                if (run.size() < 65) run += c;
            } else {
                flush();
            }
        }
    }
    flush();
    fclose(f);
    std::set<std::string> uniq(found.begin(), found.end());
    return std::vector<std::string>(uniq.begin(), uniq.end());
}

} // namespace x360

#else // !_WIN32

namespace x360 {
bool XisoFindPartition(const std::string&, uint64_t&, std::string& err) { err = "somente Windows"; return false; }
bool XisoExtract(const std::string&, const std::string&, std::string& err, const ProgressFn&) { err = "somente Windows"; return false; }
bool XisoListFromMemory(const std::vector<uint8_t>&, std::vector<XisoFile>&, std::string& err) { err = "somente Windows"; return false; }
bool XexParse(const std::vector<uint8_t>&, XexInfo&, std::string& err) { err = "somente Windows"; return false; }
bool XexDecodeImage(const std::vector<uint8_t>&, std::vector<uint8_t>&, size_t&, std::string& err) { err = "somente Windows"; return false; }
bool X360BuildRecompInputs(const std::vector<uint8_t>&, const std::vector<std::string>&, std::vector<uint8_t>&,
                           std::vector<uint8_t>&, std::string&, std::string& err) { err = "somente Windows"; return false; }
std::string Sha256Hex(const uint8_t*, size_t) { return ""; }
std::vector<std::string> FindEmbeddedSha256(const std::string&) { return {}; }
} // namespace x360

#endif
