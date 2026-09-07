#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chd_reader.h"

extern "C" {
#include "libchdr/chd.h"
#include "libchdr/cdrom.h"
#include "rc_hash.h"
}

// ---------------------------------------------------------------------------
// CHD support for RetroAchievements hashing.
//
// RetroAchievements does not hash the disc image file. It reads *inside* the
// disc: on PlayStation it opens SYSTEM.CNF, finds the main executable and
// hashes that; on Saturn it reads the boot header. rcheevos does this through a
// tiny sector-reading interface, and its built-in reader understands cue/bin
// and iso - plain formats it can seek around in.
//
// A CHD is compressed, so none of that works: rcheevos could not open the file
// at all, and every CHD came back "not in the database". This bridges libchdr
// to that interface so a compressed image reads like any other disc.
// ---------------------------------------------------------------------------

#define MAX_TRACKS 99

struct ChdTrack
{
	uint32_t number;
	uint32_t frames;        // data frames, excluding pregap
	uint32_t pregap;
	uint32_t chd_start;     // first frame of this track inside the CHD
	uint32_t lba_start;     // first sector of this track in disc addressing
	uint32_t sector_size;   // 2048 or 2352, depending on the track type
	uint32_t data_offset;   // where the user data begins inside the frame
};

#define CHD_HANDLE_MAGIC 0x43484452u  /* "CHDR" */

struct ChdTrackHandle
{
	uint32_t magic;
	chd_file* chd;
	const chd_header* header;

	ChdTrack tracks[MAX_TRACKS];
	uint32_t track_count;
	uint32_t active;        // index into tracks[]

	uint8_t* hunk_buffer;
	uint32_t hunk_loaded;   // which hunk is in the buffer, or 0xFFFFFFFF
};

static rc_hash_cdreader_t g_default_cdreader;
static bool g_installed = false;

static void ChdLog(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	FILE* f = fopen("mister4all.log", "a");
	if (f) { fprintf(f, "[INFO] [CHD] %s\n", buf); fclose(f); }
}

static bool PathIsChd(const char* path)
{
	if (!path) return false;
	size_t n = strlen(path);
	if (n < 4) return false;
	return _stricmp(path + n - 4, ".chd") == 0;
}

// Track types map to how much of each 2352-byte frame is user data, and where
// that data starts. Getting this wrong shifts every byte rcheevos reads.
static void ClassifyTrack(const char* type, uint32_t* sector_size, uint32_t* data_offset)
{
	if (!strcmp(type, "MODE1") || !strcmp(type, "MODE2_FORM1"))
	{
		*sector_size = 2048; *data_offset = 0;
	}
	else if (!strcmp(type, "MODE1_RAW"))
	{
		*sector_size = 2048; *data_offset = 16;   // sync + header
	}
	else if (!strcmp(type, "MODE2_RAW") || !strcmp(type, "MODE2_FORM_MIX"))
	{
		*sector_size = 2048; *data_offset = 24;   // sync + header + subheader
	}
	else if (!strcmp(type, "MODE2_FORM2"))
	{
		*sector_size = 2324; *data_offset = 24;
	}
	else if (!strcmp(type, "MODE2"))
	{
		*sector_size = 2336; *data_offset = 16;
	}
	else // AUDIO and anything unexpected
	{
		*sector_size = 2352; *data_offset = 0;
	}
}

// Bounded local copies of libchdr's CDROM_TRACK_METADATA*_FORMAT macros
// (third_party/libchdr/include/libchdr/chd.h - vendored, not ours to edit).
// The vendored formats use unbounded %s, which lets a crafted/corrupt CHD's
// TYPE/SUBTYPE/PGTYPE/PGSUB metadata field overflow the fixed 64-byte
// buffers below; %63s caps each write to what type/subtype/pgtype/pgsub can
// actually hold (63 chars + NUL).
#define M4A_CDROM_TRACK_METADATA_FORMAT   "TRACK:%d TYPE:%63s SUBTYPE:%63s FRAMES:%d"
#define M4A_CDROM_TRACK_METADATA2_FORMAT  "TRACK:%d TYPE:%63s SUBTYPE:%63s FRAMES:%d PREGAP:%d PGTYPE:%63s PGSUB:%63s POSTGAP:%d"
#define M4A_GDROM_TRACK_METADATA_FORMAT   "TRACK:%d TYPE:%63s SUBTYPE:%63s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%63s PGSUB:%63s POSTGAP:%d"

static bool ReadTrackTable(ChdTrackHandle* h)
{
	h->track_count = 0;

	uint32_t chd_frame = 0;
	uint32_t lba = 0;

	for (uint32_t i = 0; i < MAX_TRACKS; i++)
	{
		// Zero-initialized and read with sizeof(meta)-1 so meta[511] is
		// always left as the zero-init NUL, guaranteeing termination even if
		// chd_get_metadata() fills every requested byte with no NUL of its
		// own (it makes no such guarantee - it just copies raw file bytes).
		char meta[512] = { 0 };
		uint32_t len = 0;
		int num = 0, frames = 0, pregap = 0, postgap = 0;
		uint32_t padding = 0;
		char type[64] = { 0 }, subtype[64] = { 0 }, pgtype[64] = { 0 }, pgsub[64] = { 0 };

		if (chd_get_metadata(h->chd, CDROM_TRACK_METADATA2_TAG, i,
				meta, sizeof(meta) - 1, &len, NULL, NULL) == CHDERR_NONE)
		{
			if (sscanf(meta, M4A_CDROM_TRACK_METADATA2_FORMAT, &num, type, subtype,
					&frames, &pregap, pgtype, pgsub, &postgap) != 8)
				break;
		}
		else if (chd_get_metadata(h->chd, CDROM_TRACK_METADATA_TAG, i,
				meta, sizeof(meta) - 1, &len, NULL, NULL) == CHDERR_NONE)
		{
			if (sscanf(meta, M4A_CDROM_TRACK_METADATA_FORMAT, &num, type, subtype, &frames) != 4)
				break;
			pregap = 0;
		}
		else if (chd_get_metadata(h->chd, GDROM_TRACK_METADATA_TAG, i,
				meta, sizeof(meta) - 1, &len, NULL, NULL) == CHDERR_NONE)
		{
			// Dreamcast discs are GD-ROMs and store their tracks under a
			// different tag with an extra PAD field. Reading only the CD tags
			// made every Dreamcast CHD report "sem metadados de faixa".
			int pad = 0;
			if (sscanf(meta, M4A_GDROM_TRACK_METADATA_FORMAT, &num, type, subtype,
					&frames, &pad, &pregap, pgtype, pgsub, &postgap) != 9)
				break;
			if (pad < 0) break;
			padding = (uint32_t)pad;
		}
		else if (chd_get_metadata(h->chd, GDROM_OLD_METADATA_TAG, i,
				meta, sizeof(meta) - 1, &len, NULL, NULL) == CHDERR_NONE)
		{
			if (sscanf(meta, M4A_CDROM_TRACK_METADATA_FORMAT, &num, type, subtype, &frames) != 4)
				break;
			pregap = 0;
		}
		else
		{
			break;
		}

		// A crafted/corrupt CHD can put a negative value in any of these -
		// FRAMES:-1 cast straight to uint32_t would wrap to 0xFFFFFFFF and
		// defeat ChdReadSector's only bounds check (offset_in_track >=
		// t.frames), letting it read whatever frame that wraps to instead of
		// cleanly rejecting the file. Reject the whole track table instead.
		if (num < 0 || frames < 0 || pregap < 0 || postgap < 0)
			break;

		ChdTrack& t = h->tracks[h->track_count];
		t.number = (uint32_t)num;
		t.frames = (uint32_t)frames;
		t.pregap = (uint32_t)pregap;
		t.chd_start = chd_frame;
		t.lba_start = lba + (uint32_t)pregap;
		ClassifyTrack(type, &t.sector_size, &t.data_offset);

		h->track_count++;

		lba += (uint32_t)frames + (uint32_t)pregap;

		// CD tracks are padded up to a 4-frame boundary; GD-ROM tracks state
		// their padding explicitly. Skipping either makes every track after
		// the first read from the wrong place.
		chd_frame += (uint32_t)frames;
		if (padding) chd_frame += padding;
		else chd_frame = (chd_frame + 3) & ~3u;
	}

	return h->track_count > 0;
}

static void* ChdOpenTrack(const char* path, uint32_t track)
{
	chd_file* chd = NULL;
	if (chd_open(path, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE)
	{
		ChdLog("nao consegui abrir: %s", path);
		return NULL;
	}

	ChdTrackHandle* h = (ChdTrackHandle*)calloc(1, sizeof(ChdTrackHandle));
	if (!h) { chd_close(chd); return NULL; }

	h->magic = CHD_HANDLE_MAGIC;
	h->chd = chd;
	h->header = chd_get_header(chd);

	if (!ReadTrackTable(h))
	{
		ChdLog("sem metadados de faixa: %s", path);
		chd_close(chd);
		free(h);
		return NULL;
	}

	// track 0 means "the first data track", which is what a hash needs.
	h->active = 0;
	if (track == RC_HASH_CDTRACK_FIRST_DATA || track == 0)
	{
		for (uint32_t i = 0; i < h->track_count; i++)
		{
			if (h->tracks[i].sector_size != 2352) { h->active = i; break; }
		}
	}
	else
	{
		for (uint32_t i = 0; i < h->track_count; i++)
			if (h->tracks[i].number == track) { h->active = i; break; }
	}

	h->hunk_buffer = (uint8_t*)malloc(h->header->hunkbytes);
	h->hunk_loaded = 0xFFFFFFFFu;

	if (!h->hunk_buffer) { chd_close(chd); free(h); return NULL; }

	ChdLog("%s: %u faixas, usando faixa %u (setor %u bytes, offset %u)",
		path, h->track_count, h->tracks[h->active].number,
		h->tracks[h->active].sector_size, h->tracks[h->active].data_offset);

	return h;
}

static size_t ChdReadSector(void* track_handle, uint32_t sector, void* buffer, size_t requested)
{
	ChdTrackHandle* h = (ChdTrackHandle*)track_handle;
	if (!h || !h->chd || !buffer) return 0;

	const ChdTrack& t = h->tracks[h->active];

	// Sector numbers arrive in disc addressing; translate into the frame this
	// track actually occupies inside the CHD.
	if (sector < t.lba_start) return 0;
	uint32_t offset_in_track = sector - t.lba_start;
	if (offset_in_track >= t.frames) return 0;

	uint32_t frame = t.chd_start + offset_in_track;

	const uint32_t frames_per_hunk = h->header->hunkbytes / CD_FRAME_SIZE;
	if (frames_per_hunk == 0) return 0;

	uint32_t hunk = frame / frames_per_hunk;
	uint32_t frame_in_hunk = frame % frames_per_hunk;

	if (hunk != h->hunk_loaded)
	{
		if (chd_read(h->chd, hunk, h->hunk_buffer) != CHDERR_NONE) return 0;
		h->hunk_loaded = hunk;
	}

	const uint8_t* src = h->hunk_buffer + (size_t)frame_in_hunk * CD_FRAME_SIZE + t.data_offset;

	size_t avail = t.sector_size;
	size_t n = requested < avail ? requested : avail;
	memcpy(buffer, src, n);
	return n;
}

static void ChdCloseTrack(void* track_handle)
{
	ChdTrackHandle* h = (ChdTrackHandle*)track_handle;
	if (!h) return;

	if (h->hunk_buffer) free(h->hunk_buffer);
	if (h->chd) chd_close(h->chd);
	free(h);
}

static uint32_t ChdFirstTrackSector(void* track_handle)
{
	ChdTrackHandle* h = (ChdTrackHandle*)track_handle;
	if (!h) return 0;
	return h->tracks[h->active].lba_start;
}

// The built-in reader still handles cue/bin/iso; we only take over when the
// path is a CHD, so nothing that worked before changes.
static void* DispatchOpenTrack(const char* path, uint32_t track)
{
	if (PathIsChd(path)) return ChdOpenTrack(path, track);
	return g_default_cdreader.open_track ? g_default_cdreader.open_track(path, track) : NULL;
}

// Handles opened by the default reader must go back to it. A CHD handle always
// starts with its chd_file pointer, which the default reader never produces, so
// tagging our own handles is the reliable way to tell them apart.
static bool IsOurHandle(void* handle)
{
	ChdTrackHandle* h = (ChdTrackHandle*)handle;
	return h && h->magic == CHD_HANDLE_MAGIC;
}

static size_t DispatchReadSector(void* handle, uint32_t sector, void* buffer, size_t requested)
{
	if (IsOurHandle(handle)) return ChdReadSector(handle, sector, buffer, requested);
	return g_default_cdreader.read_sector
		? g_default_cdreader.read_sector(handle, sector, buffer, requested) : 0;
}

static void DispatchCloseTrack(void* handle)
{
	if (IsOurHandle(handle)) { ChdCloseTrack(handle); return; }
	if (g_default_cdreader.close_track) g_default_cdreader.close_track(handle);
}

static uint32_t DispatchFirstTrackSector(void* handle)
{
	if (IsOurHandle(handle)) return ChdFirstTrackSector(handle);
	return g_default_cdreader.first_track_sector
		? g_default_cdreader.first_track_sector(handle) : 0;
}

// rcheevos 12 prefers this entry point over open_track when both are set.
// Overriding only open_track meant the default reader kept being used and
// every CHD came back "Could not open track".
static void* DispatchOpenTrackIterator(const char* path, uint32_t track,
                                       const struct rc_hash_iterator* iterator)
{
	if (PathIsChd(path)) return ChdOpenTrack(path, track);

	if (g_default_cdreader.open_track_iterator)
		return g_default_cdreader.open_track_iterator(path, track, iterator);
	if (g_default_cdreader.open_track)
		return g_default_cdreader.open_track(path, track);
	return NULL;
}

void ChdReaderInstall()
{
	if (g_installed) return;

	rc_hash_get_default_cdreader(&g_default_cdreader);

	rc_hash_cdreader_t reader = g_default_cdreader;
	reader.open_track = DispatchOpenTrack;
	reader.read_sector = DispatchReadSector;
	reader.close_track = DispatchCloseTrack;
	reader.first_track_sector = DispatchFirstTrackSector;
	reader.open_track_iterator = DispatchOpenTrackIterator;

	rc_hash_init_custom_cdreader(&reader);
	g_installed = true;

	ChdLog("leitor de CHD instalado");
}
