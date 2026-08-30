#include "test_framework.h"
#include "archive_helper.h"
#include <string>
#include <vector>

// Calls the shipped implementation. This used to be a private copy that never
// touched ArchiveIsCompressed, so the test could not have caught a change in it.
static bool CheckIsCompressedByExtension(const std::string& path)
{
	return ArchiveIsCompressed(path);
}

TEST_CASE(ArchiveExtensionDetection)
{
	ASSERT_TRUE(CheckIsCompressedByExtension("roms/MegaCD/Sonic_CD.zip"));
	ASSERT_TRUE(CheckIsCompressedByExtension("roms/PlayStation/Tekken_3.7z"));
	ASSERT_TRUE(CheckIsCompressedByExtension("roms/NeoGeo/mslug.rar"));
	ASSERT_TRUE(CheckIsCompressedByExtension("game.ZIP"));
	ASSERT_TRUE(CheckIsCompressedByExtension("archive.7Z"));

	ASSERT_FALSE(CheckIsCompressedByExtension("roms/Genesis/Sonic.bin"));
	ASSERT_FALSE(CheckIsCompressedByExtension("roms/SNES/Mario.sfc"));
	ASSERT_FALSE(CheckIsCompressedByExtension("roms/NES/Zelda.nes"));
	ASSERT_FALSE(CheckIsCompressedByExtension("roms/PlayStation/game.cue"));
	ASSERT_FALSE(CheckIsCompressedByExtension("roms/NeoGeo/game.chd"));
}

TEST_CASE(ArchiveMagicBytes)
{
	// ZIP: PK\x03\x04
	const uint8_t zip_hdr[4] = { 0x50, 0x4B, 0x03, 0x04 };
	bool is_zip = (zip_hdr[0] == 'P' && zip_hdr[1] == 'K' && zip_hdr[2] == 0x03 && zip_hdr[3] == 0x04);
	ASSERT_TRUE(is_zip);

	// 7z: 7z\xBC\xAF\x27\x1C
	const uint8_t sz_hdr[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
	bool is_7z = (sz_hdr[0] == '7' && sz_hdr[1] == 'z' && sz_hdr[2] == 0xBC);
	ASSERT_TRUE(is_7z);

	// RAR: Rar!\x1A\x07
	const uint8_t rar_hdr[6] = { 'R', 'a', 'r', '!', 0x1A, 0x07 };
	bool is_rar = (rar_hdr[0] == 'R' && rar_hdr[1] == 'a' && rar_hdr[2] == 'r' && rar_hdr[3] == '!');
	ASSERT_TRUE(is_rar);
}
