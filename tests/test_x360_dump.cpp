#include "test_framework.h"
#include "x360_dump.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// Synthetic fixtures only: no real disc image or XEX is (or may be) in the
// repository. They pin the parts of x360_dump.cpp that are pure format
// plumbing - the XDVDFS tree walk, key unwrap + CBC, basic and LZX payloads,
// the digest gate - so a regression shows up here and not as "the game will
// not start" on a user's machine.

namespace {

void PutLe16(std::vector<uint8_t>& v, size_t o, uint16_t x) { v[o] = uint8_t(x); v[o + 1] = uint8_t(x >> 8); }
void PutLe32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[o + i] = uint8_t(x >> (8 * i));
}
void PutBe32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[o + i] = uint8_t(x >> (24 - 8 * i));
}
void PutBe16(std::vector<uint8_t>& v, size_t o, uint16_t x) { v[o] = uint8_t(x >> 8); v[o + 1] = uint8_t(x); }

// XDVDFS entry: left, right (dword offsets), sector, size, attr, name.
size_t PutEntry(std::vector<uint8_t>& img, size_t at, uint16_t left, uint16_t right,
                uint32_t sector, uint32_t size, uint8_t attr, const char* name) {
    size_t n = strlen(name);
    PutLe16(img, at, left);
    PutLe16(img, at + 2, right);
    PutLe32(img, at + 4, sector);
    PutLe32(img, at + 8, size);
    img[at + 12] = attr;
    img[at + 13] = uint8_t(n);
    memcpy(&img[at + 14], name, n);
    return (14 + n + 3) & ~size_t(3);
}

std::vector<uint8_t> MakeXiso(const char* bad_name = nullptr) {
    const size_t S = 2048;
    std::vector<uint8_t> img(48 * S, 0);
    memcpy(&img[32 * S], "MICROSOFT*XBOX*MEDIA", 20);
    PutLe32(img, 32 * S + 20, 34);   // root dir sector
    PutLe32(img, 32 * S + 24, S);    // root dir size
    // Root: "default.xex" at 0 with right child "Content" (dir) at dword 8.
    size_t e0 = PutEntry(img, 34 * S, 0, 8, 40, 5, 0x20, bad_name ? bad_name : "default.xex");
    (void)e0;
    PutEntry(img, 34 * S + 32, 0, 0, 36, S, 0x10, "Content");
    memset(&img[34 * S + 64], 0xFF, S - 64);
    // Content/: one file.
    PutEntry(img, 36 * S, 0, 0, 41, 3, 0x20, "a.bin");
    memset(&img[36 * S + 20], 0xFF, S - 20);
    memcpy(&img[40 * S], "XEX2!", 5);
    memcpy(&img[41 * S], "abc", 3);
    return img;
}

bool AesEncrypt(const uint8_t key[16], bool cbc, uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE k = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0) {
        const wchar_t* mode = cbc ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)mode, ULONG((wcslen(mode) + 1) * 2), 0) >= 0 &&
            BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, (PUCHAR)key, 16, 0) >= 0) {
            uint8_t iv[16] = {};
            ULONG got = 0;
            ok = BCryptEncrypt(k, data, ULONG(size), nullptr, cbc ? iv : nullptr, cbc ? 16 : 0,
                               data, ULONG(size), &got, 0) >= 0;
        }
    }
    if (k) BCryptDestroyKey(k);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

const uint8_t kRetail[16] = { 0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                              0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91 };

// 32 KiB "memory image": a minimal MZ/PE header, some data, trailing zeros.
std::vector<uint8_t> MakeImage() {
    std::vector<uint8_t> im(0x8000, 0);
    im[0] = 'M'; im[1] = 'Z';
    PutLe32(im, 0x3C, 0x80);
    memcpy(&im[0x80], "PE\0\0", 4);
    for (size_t i = 0x100; i < 0x6000; ++i) im[i] = uint8_t(i * 7 + 3);
    return im;
}

// XEX2 skeleton: format info at 0x80, exec info at 0x60, security at 0x100,
// payload at 0x1000.
std::vector<uint8_t> MakeXexHeader(uint16_t enc, uint16_t comp, uint32_t info_size) {
    std::vector<uint8_t> x(0x1000, 0);
    memcpy(&x[0], "XEX2", 4);
    PutBe32(x, 0x08, 0x1000);
    PutBe32(x, 0x10, 0x100);
    PutBe32(x, 0x14, 2);
    PutBe32(x, 0x18, 0x3FF);  PutBe32(x, 0x1C, 0x80);
    PutBe32(x, 0x20, 0x40006); PutBe32(x, 0x24, 0x60);
    PutBe32(x, 0x60, 0x0F213645); PutBe32(x, 0x64, 1); PutBe32(x, 0x6C, 0x545407EE);
    PutBe32(x, 0x80, info_size); PutBe16(x, 0x84, enc); PutBe16(x, 0x86, comp);
    PutBe32(x, 0x100, 0x184);                 // security info size, no page descriptors
    PutBe32(x, 0x100 + 0x004, 0x8000);
    PutBe32(x, 0x100 + 0x110, 0x82000000);
    return x;
}

void Encrypt(std::vector<uint8_t>& xex) {
    uint8_t session[16];
    for (int i = 0; i < 16; ++i) session[i] = uint8_t(0x11 * i + 5);
    uint8_t wrapped[16];
    memcpy(wrapped, session, 16);
    AesEncrypt(kRetail, false, wrapped, 16);
    memcpy(&xex[0x100 + 0x150], wrapped, 16);
    AesEncrypt(session, true, xex.data() + 0x1000, xex.size() - 0x1000);
}

void Sha1(const uint8_t* a, size_t an, const uint8_t* b, size_t bn, uint8_t out[20]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(h, const_cast<PUCHAR>(a), ULONG(an), 0);
    BCryptHashData(h, const_cast<PUCHAR>(b), ULONG(bn), 0);
    BCryptFinishHash(h, out, 20, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
}

} // namespace

TEST_CASE(X360XisoTreeWalk)
{
    std::vector<x360::XisoFile> files;
    std::string err;
    ASSERT_TRUE(x360::XisoListFromMemory(MakeXiso(), files, err));
    ASSERT_EQ(files.size(), (size_t)2);
    bool saw_xex = false, saw_nested = false;
    for (const auto& f : files) {
        if (f.path == "default.xex" && f.size == 5 && f.offset == 40 * 2048) saw_xex = true;
        if (f.path == "Content/a.bin" && f.size == 3 && f.offset == 41 * 2048) saw_nested = true;
    }
    ASSERT_TRUE(saw_xex);
    ASSERT_TRUE(saw_nested);
}

TEST_CASE(X360XisoRejectsTraversalNames)
{
    std::vector<x360::XisoFile> files;
    std::string err;
    ASSERT_FALSE(x360::XisoListFromMemory(MakeXiso(".."), files, err));
    ASSERT_FALSE(x360::XisoListFromMemory(std::vector<uint8_t>(80 * 1024, 0), files, err));
}

TEST_CASE(X360XexBasicEncrypted)
{
    std::vector<uint8_t> image = MakeImage();
    // Basic: data 0x6000, zero run 0x2000.
    std::vector<uint8_t> xex = MakeXexHeader(1, 1, 16);
    PutBe32(xex, 0x88, 0x6000);
    PutBe32(xex, 0x8C, 0x2000);
    xex.insert(xex.end(), image.begin(), image.begin() + 0x6000);
    Encrypt(xex);

    x360::XexInfo info;
    std::string err;
    ASSERT_TRUE(x360::XexParse(xex, info, err));
    ASSERT_EQ(info.title_id, 0x545407EEu);
    ASSERT_EQ(info.media_id, 0x0F213645u);

    std::vector<uint8_t> out;
    size_t produced = 0;
    ASSERT_TRUE(x360::XexDecodeImage(xex, out, produced, err));
    ASSERT_EQ(produced, (size_t)0x8000);
    ASSERT_TRUE(out == image);
}

TEST_CASE(X360XexLzxUncompressedBlock)
{
    std::vector<uint8_t> image = MakeImage();
    // One LZX frame holding a single "uncompressed" block. Header bits,
    // MSB-first in 16-bit little-endian words: 0 (no E8 translation), 011
    // (type 3), 24-bit block length 0x008000, then padding to the word
    // boundary: 0011 0000 0000 1000 | 0000 0000 0000 0000 -> 0x3008, 0x0000.
    // Then R0..R2 (12 bytes) and the raw bytes.
    std::vector<uint8_t> lzx = { 0x08, 0x30, 0x00, 0x00 };
    for (int r = 0; r < 3; ++r) { lzx.push_back(1); lzx.push_back(0); lzx.push_back(0); lzx.push_back(0); }
    lzx.insert(lzx.end(), image.begin(), image.end());

    std::vector<uint8_t> xex = MakeXexHeader(0, 2, 36);
    PutBe32(xex, 0x88, 0x8000);            // window size
    // Blocks: 24-byte header (next size 0, SHA-1) + chunks of <= 0xFFFF.
    std::vector<uint8_t> block(24, 0);
    size_t pos = 0;
    while (pos < lzx.size()) {
        size_t n = lzx.size() - pos > 0x7FF0 ? 0x7FF0 : lzx.size() - pos;
        block.push_back(uint8_t(n >> 8));
        block.push_back(uint8_t(n));
        block.insert(block.end(), lzx.begin() + pos, lzx.begin() + pos + n);
        pos += n;
    }
    block.push_back(0); block.push_back(0);
    PutBe32(xex, 0x8C, uint32_t(block.size()));
    xex.insert(xex.end(), block.begin(), block.end());

    std::vector<uint8_t> out;
    size_t produced = 0;
    std::string err;
    ASSERT_TRUE(x360::XexDecodeImage(xex, out, produced, err));
    ASSERT_EQ(produced, (size_t)0x8000);
    ASSERT_TRUE(out == image);
}

TEST_CASE(X360RecompInputsDigestGate)
{
    std::vector<uint8_t> image = MakeImage();
    std::vector<uint8_t> xex = MakeXexHeader(1, 1, 16);
    PutBe32(xex, 0x88, 0x6000);
    PutBe32(xex, 0x8C, 0x2000);
    xex.insert(xex.end(), image.begin(), image.begin() + 0x6000);
    Encrypt(xex);

    // Expected outputs: basefile trimmed to 0x6000 (the last data byte is
    // at 0x5FFF); _uncrypted = XexTool's rebuilt header - security info
    // moved to 0x18 + 2*8 + 0x80 = 0xA8, blobs packed in key order after it
    // (format info at 0x22C keeping the file's own basic runs, unencrypted;
    // execution info at 0x23C), payload at 0x1000, signature zeroed, header
    // digest recomputed - and only the data run stored, in the clear.
    std::vector<uint8_t> want_base(image.begin(), image.begin() + 0x6000);
    std::vector<uint8_t> want_xex(0x1000, 0);
    memcpy(&want_xex[0], &xex[0], 0x18);
    PutBe32(want_xex, 0x08, 0x1000);
    PutBe32(want_xex, 0x10, 0xA8);
    PutBe32(want_xex, 0x18, 0x3FF);   PutBe32(want_xex, 0x1C, 0x22C);
    PutBe32(want_xex, 0x20, 0x40006); PutBe32(want_xex, 0x24, 0x23C);
    memcpy(&want_xex[0xA8], &xex[0x100], 0x184);
    PutBe32(want_xex, 0x22C, 16); PutBe16(want_xex, 0x230, 0); PutBe16(want_xex, 0x232, 1);
    PutBe32(want_xex, 0x234, 0x6000); PutBe32(want_xex, 0x238, 0x2000);
    memcpy(&want_xex[0x23C], &xex[0x60], 24);
    Sha1(&want_xex[0xA8 + 0x17C], 0x1000 - (0xA8 + 0x17C), &want_xex[0], 0xA8 + 8, &want_xex[0xA8 + 0x164]);
    want_xex.insert(want_xex.end(), image.begin(), image.begin() + 0x6000);

    std::vector<std::string> accepted = {
        x360::Sha256Hex(want_base.data(), want_base.size()),
        x360::Sha256Hex(want_xex.data(), want_xex.size()),
    };
    std::vector<uint8_t> base, unc;
    std::string report, err;
    ASSERT_TRUE(x360::X360BuildRecompInputs(xex, accepted, base, unc, report, err));
    ASSERT_TRUE(base == want_base);
    ASSERT_TRUE(unc == want_xex);

    // A digest the dump cannot produce (another game revision) is refused.
    std::vector<std::string> other = { std::string(64, 'a') };
    ASSERT_FALSE(x360::X360BuildRecompInputs(xex, other, base, unc, report, err));
}

TEST_CASE(X360FindsEmbeddedDigests)
{
    const std::string h = "180b7fc8f57462f6bac3404ecab061a8d79914c7a3e9a12c238bd3449e72d049";
    std::string blob = std::string("junk\x01", 5) + h + std::string(1, '\0') +
                       std::string(65, 'b') + std::string(1, '\0') + "deadbeef";
    auto path = std::filesystem::temp_directory_path() / "karamelo_x360_digest_test.bin";
    FILE* f = fopen(path.string().c_str(), "wb");
    ASSERT_TRUE(f != nullptr);
    fwrite(blob.data(), 1, blob.size(), f);
    fclose(f);
    std::vector<std::string> got = x360::FindEmbeddedSha256(path.string());
    std::filesystem::remove(path);
    ASSERT_EQ(got.size(), (size_t)1);
    ASSERT_TRUE(got[0] == h);
}

#endif
