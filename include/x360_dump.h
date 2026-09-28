#ifndef X360_DUMP_H_INCLUDED
#define X360_DUMP_H_INCLUDED

// Native replacements for the two third-party tools an Xbox 360 static
// recomp (The Darkness Recomp is the first) asks its users to run by hand:
//
//   * "Xbox 360 Image Browser" - pulls the game files out of a disc image.
//     XisoExtract() reads the XDVDFS/GDFX filesystem directly.
//   * xorloser's XexTool - turns the retail default.xex into the two inputs
//     the recomp's runtime actually loads: basefile.exe (-b, the decrypted
//     and decompressed memory image) and _uncrypted.xex (-e u -c u).
//     XexDecodeImage() + X360BuildRecompInputs() do the same work.
//
// Nothing here is emulation - it is file-format plumbing, and every output
// is gated by a SHA-256 the recomp itself checks at startup, so a wrong guess
// about a byte layout can never reach the game: it is reported instead.
//
// Windows-only (AES/SHA-256 come from BCrypt): every port that needs this
// ships Windows builds only today.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace x360 {

// done/total in bytes of file data copied so far.
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

// Offset of the game partition inside the image (0 for an already-trimmed
// "game partition only" ISO, 0xFD90000 for a full XGD2 dump, 0x2080000 for
// XGD3, 0x18300000 for an original-Xbox XGD1). Returns false if the image
// has no XDVDFS volume descriptor at any of them.
bool XisoFindPartition(const std::string& iso_path, uint64_t& out_base, std::string& err);

// Extracts every file of the image into out_dir, keeping the directory tree.
// A file that already exists in out_dir with the same size is skipped, so an
// interrupted extraction resumes instead of starting over.
bool XisoExtract(const std::string& iso_path, const std::string& out_dir,
                 std::string& err, const ProgressFn& progress = {});

// Same parser, operating on an in-memory image - used by the unit tests to
// exercise the directory-tree walk without a multi-gigabyte fixture.
struct XisoFile { std::string path; uint64_t offset; uint64_t size; };
bool XisoListFromMemory(const std::vector<uint8_t>& image, std::vector<XisoFile>& out, std::string& err);

struct XexInfo {
    uint32_t header_size = 0;       // start of the PE payload in the file
    uint32_t security_offset = 0;
    uint32_t image_size = 0;        // decoded memory image size
    uint32_t load_address = 0;
    uint16_t encryption_type = 0;   // 0 none, 1 normal (AES-128-CBC)
    uint16_t compression_type = 0;  // 0 none, 1 basic (zero runs), 2 normal (LZX), 3 delta
    uint32_t format_info_offset = 0;
    uint32_t format_info_size = 0;
    uint32_t title_id = 0;
    uint32_t media_id = 0;
    uint32_t version = 0;
};

bool XexParse(const std::vector<uint8_t>& xex, XexInfo& info, std::string& err);

// Decrypts (retail key, then devkit key) and decompresses the payload into
// the memory image the console would map at load_address. out_produced is
// how many bytes the payload actually covered - anything past it is the
// implicit zero fill the loader supplies.
bool XexDecodeImage(const std::vector<uint8_t>& xex, std::vector<uint8_t>& out_image,
                    size_t& out_produced, std::string& err);

// Builds basefile.exe and _uncrypted.xex from default.xex and accepts them
// only when both SHA-256 digests are in accepted_sha256 (lowercase hex).
// _uncrypted.xex reproduces XexTool 6.3's header rebuild byte for byte (the
// layout XexTool-RE reverse-engineered - see x360_dump.cpp); the few choices
// it leaves open (keep the file's own basic runs or re-cut them, pad the
// payload or not) are all generated and the one whose digest matches wins.
// On failure, out_report says which digests were produced, for the log.
bool X360BuildRecompInputs(const std::vector<uint8_t>& xex,
                           const std::vector<std::string>& accepted_sha256,
                           std::vector<uint8_t>& out_basefile,
                           std::vector<uint8_t>& out_uncrypted,
                           std::string& out_report, std::string& err);

std::string Sha256Hex(const uint8_t* data, size_t size);

// Scans a binary (the recomp's own executable) for 64-char lowercase hex
// strings - that is how the recomp embeds the digests it will enforce, so
// reading them back keeps Karamelo in step with whatever game revision the
// installed release was built against, without a hardcoded table going stale.
std::vector<std::string> FindEmbeddedSha256(const std::string& binary_path);

} // namespace x360

#endif
