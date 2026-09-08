// Copyright (c) 2026 Gustavo Clemente (mister4all.com | @GuhClemente).
// All rights reserved.
//
// Clean-room implementation of the OSD raster used to draw this app's menu.
// This file used to be a near-literal port of Main_MiSTer's own osd.cpp; it
// was rewritten from scratch (see docs/FRONTEND.md for why and what that
// means in practice) and is original work, not GPL-derived.

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "osd.h"
#include "charrom.h"

typedef unsigned int uint;

// ============================================================================
// OSD raster
// ============================================================================
// Every renderer in this project (SDL, GL, D3D11, Vulkan) draws the menu by
// reading a flat byte buffer that looks like the strip a real MiSTer feeds
// its HDMI OSD overlay: one fixed-size "row" per line of text, and inside a
// row, one 8-byte run per on-screen glyph - byte k of that run is column k
// of the glyph, and bit b of that byte is row b of the glyph (the same
// column-major, bit-is-row convention charfont[] itself uses; see
// charrom.cpp). That raster shape is a contract the renderers already rely
// on, so it stays exactly as it is - only the code that fills it in below
// was written from scratch, structured around a small RowEncoder instead of
// manual pointer bumping.

#define OSD_FIXED_ROWS 15   // the OSD card never grows or shrinks; see OsdSetSize().
#define OSD_ROW_SLOTS  32   // number of independently addressable rows
#define OSD_ROW_STRIDE OSDLINELEN

static int  s_visibleRows = OSD_FIXED_ROWS;
static bool s_enabled     = true;
static int  s_arrowFlags  = 0;

static uint8_t s_raster[OSD_ROW_SLOTS * OSD_ROW_STRIDE];
static uint8_t s_rowInverted[OSD_ROW_SLOTS]; // which row is currently drawn highlighted
static unsigned char s_titleStrip[256];      // vertical title text, pre-rotated

void OsdSetSize(int n)
{
	// Kept purely so callers can still declare which mode they think they're
	// in; the card height itself never changes.
	(void)n;
	s_visibleRows = OSD_FIXED_ROWS;
}

int OsdGetSize()
{
	return s_visibleRows;
}

// ---- starfield behind the menu --------------------------------------------

#define STAR_COUNT 128
static StarPoint s_stars[STAR_COUNT];
static int s_fieldWidth  = 640;
static int s_fieldHeight = 360;

static void RespawnStar(StarPoint& star, int fieldWidth, int fieldHeight, bool atRandomX)
{
	star.x = atRandomX ? (rand() % fieldWidth) : (fieldWidth - 1);
	star.y = rand() % fieldHeight;
	star.speed = 1 + (rand() % 4);
	star.brightness = (uint8_t)(100 + star.speed * 38);
}

void StarsInit(int screen_w, int screen_h)
{
	s_fieldWidth = screen_w;
	s_fieldHeight = screen_h;
	srand((unsigned int)time(NULL));

	for (int i = 0; i < STAR_COUNT; ++i)
		RespawnStar(s_stars[i], s_fieldWidth, s_fieldHeight, true);
}

void StarsUpdate(int screen_w, int screen_h)
{
	s_fieldWidth = screen_w;
	s_fieldHeight = screen_h;

	for (int i = 0; i < STAR_COUNT; ++i)
	{
		s_stars[i].x -= s_stars[i].speed;
		if (s_stars[i].x < 0)
			RespawnStar(s_stars[i], s_fieldWidth, s_fieldHeight, false);
	}
}

const StarPoint* OsdGetStars(int* count)
{
	if (count) *count = STAR_COUNT;
	return s_stars;
}

// ---- glyph rotation ---------------------------------------------------------
// The title strip runs sideways along the OSD's spine, so a normal upright
// 8x8 glyph has to be turned 90 degrees before it can be stacked into it:
// pixel column `col` of the source, read top-to-bottom, becomes output row
// `col`, read left-to-right.

static void RotateGlyphQuarterTurn(const unsigned char glyphRows[8], unsigned char rotatedRows[8])
{
	for (int col = 0; col < 8; ++col)
	{
		unsigned bits = 0;
		for (int row = 0; row < 8; ++row)
			bits = (bits << 1) | ((glyphRows[row] >> col) & 1u);
		rotatedRows[col] = (unsigned char)bits;
	}
}

// ---- row encoding ------------------------------------------------------------

class RowEncoder
{
public:
	explicit RowEncoder(uint8_t* rowBase) : m_dst(rowBase) {}

	void Push(unsigned value) { *m_dst++ = (uint8_t)value; ++m_bytesWritten; }

	void PushRepeated(unsigned value, int count)
	{
		while (count-- > 0) Push(value);
	}

	// One byte per glyph row: `(row << shift) & mask`, then XORed with
	// `xorWith`. `mask` is 0xff (a no-op besides the implicit truncation to
	// a byte) for arrow glyphs, and the current stipple mask for text.
	void PushGlyphRows(const unsigned char* glyphRows, int shift, unsigned mask, unsigned xorWith)
	{
		for (int row = 0; row < 8; ++row)
			Push(((unsigned)(glyphRows[row] << shift) & mask) ^ xorWith);
	}

	// Lets a cell-writing function report exactly how many bytes it pushed
	// (see Emit*Cell below) instead of the caller separately hardcoding that
	// count - two numbers that have to be kept in sync by hand otherwise.
	int BytesWritten() const { return (int)m_bytesWritten; }

private:
	uint8_t* m_dst;
	size_t m_bytesWritten = 0;
};

static uint8_t* RowStart(unsigned char n)
{
	return &s_raster[(n & (OSD_ROW_SLOTS - 1)) * OSD_ROW_STRIDE];
}

// A title cell is the left-most 22 bytes of a row: a solid 3-byte border,
// then 8 title-strip rows drawn twice as wide (each row byte written twice)
// and inverted so the title text shows light-on-dark, then a 3-byte trailer.
static int EmitTitleCell(RowEncoder& row, const unsigned char* titleColumn)
{
	int before = row.BytesWritten();
	row.PushRepeated(0xff, 3);
	for (int i = 0; i < 8; ++i)
	{
		unsigned inverted = 255 ^ titleColumn[i];
		row.Push(inverted);
		row.Push(inverted);
	}
	row.Push(0xff);
	row.Push(0);
	row.Push(0);
	return row.BytesWritten() - before;
}

// A paging-arrow cell stacks two glyphs (an arrowhead over a chevron) with
// filler bytes on either side; the left and right arrows use different
// amounts of filler, which just mirrors how the arrows have always looked.
static int EmitArrowCell(RowEncoder& row, const unsigned char* glyphTop, const unsigned char* glyphBottom,
                          int shift, unsigned xorWith, int leadFiller, int trailFiller)
{
	int before = row.BytesWritten();
	row.PushRepeated(xorWith, leadFiller);
	row.PushGlyphRows(glyphTop, shift, 0xff, xorWith);
	row.PushGlyphRows(glyphBottom, shift, 0xff, xorWith);
	row.PushRepeated(xorWith, trailFiller);
	return row.BytesWritten() - before;
}

// A text-glyph cell: 8 bytes, one per pixel row, each optionally shifted,
// masked by the current stipple pattern, and XORed for inversion/forced
// invert. `stippleMask` keeps alternating (when stippling is on) so the
// dithered look continues smoothly from one glyph into the next.
static int EmitTextGlyphCell(RowEncoder& row, const unsigned char* glyphRows, int shift, unsigned xorWith,
                              unsigned& stippleMask, unsigned stippleToggle)
{
	int before = row.BytesWritten();
	for (int r = 0; r < 8; ++r)
	{
		row.Push(((unsigned)(glyphRows[r] << shift) & stippleMask) ^ xorWith);
		stippleMask ^= stippleToggle;
	}
	return row.BytesWritten() - before;
}

// ---- title text --------------------------------------------------------------

void OsdSetTitle(const char *s, int a)
{
	s_arrowFlags = a;

	const uint stripHeight = (uint)s_visibleRows * 8;
	uint written = 0;
	uint blankRun = 0;

	for (int idx = 0; s[idx] != 0 && written < stripHeight - 8; ++idx)
	{
		const unsigned char ch = (unsigned char)s[idx];
		const unsigned char* glyph = charfont[ch];

		for (int row = 0; row < 8 && written < sizeof(s_titleStrip); ++row)
		{
			const unsigned char pixels = glyph[row];
			if (pixels != 0)
			{
				s_titleStrip[written++] = pixels;
				blankRun = 0;
			}
			else if (blankRun == 0 || (ch == ' ' && blankRun < 5))
			{
				s_titleStrip[written++] = 0;
				++blankRun;
			}
		}
	}

	for (uint i = written; i < stripHeight; ++i)
		s_titleStrip[i] = 0;

	// Center whatever we managed to fit within the full strip height.
	const uint pad = (stripHeight > written) ? (stripHeight - 1 - written) / 2 : 0;
	if (pad > 0 && written > 0)
	{
		memmove(s_titleStrip + pad, s_titleStrip, written);
		memset(s_titleStrip, 0, pad);
	}

	for (uint i = 0; i < stripHeight; i += 8)
	{
		unsigned char rotated[8];
		RotateGlyphQuarterTurn(&s_titleStrip[i], rotated);
		memcpy(&s_titleStrip[i], rotated, 8);
	}
}

void OsdSetArrow(int a)
{
	s_arrowFlags = a;
}

// ---- main line writer ----------------------------------------------------

void OsdWriteOffset(unsigned char n, const char *s, unsigned char invert, unsigned char stipple,
                     char offset, char leftchar, char usebg, int maxinv, int mininv)
{
	(void)usebg; // the box always shows our own wallpaper behind it, never a "real" background

	if (n < OSD_ROW_SLOTS)
		s_rowInverted[n] = invert;

	int lineLimit = OSDLINELEN;
	if (n == (s_visibleRows - 1) && (s_arrowFlags & OSD_ARROW_RIGHT))
		lineLimit -= 22;

	if (n && n < s_visibleRows - 1)
		leftchar = 0;

	unsigned stippleMask   = stipple ? 0x55u : 0xffu;
	unsigned stippleToggle = stipple ? 0xffu : 0u;

	RowEncoder row(RowStart(n));

	int arrowState = s_arrowFlags;
	unsigned xorInvert = 0;
	unsigned xorChar   = 0;
	int col = 0;

	for (;;)
	{
		if (invert && col / 8 >= mininv) xorInvert = 0xff;
		if (invert && col / 8 >= maxinv) xorInvert = 0;

		if (col == 0 && n < s_visibleRows)
		{
			unsigned char rotatedLeftChar[8];
			const unsigned char* titleColumn;

			if (leftchar)
			{
				unsigned char raw[8];
				memcpy(raw, charfont[(unsigned char)leftchar], 8);
				RotateGlyphQuarterTurn(raw, rotatedLeftChar);
				titleColumn = rotatedLeftChar;
			}
			else
			{
				titleColumn = &s_titleStrip[(s_visibleRows - 1 - n) * 8];
			}

			col += EmitTitleCell(row, titleColumn);
		}
		else if (n == (s_visibleRows - 1) && (arrowState & OSD_ARROW_LEFT))
		{
			col += EmitArrowCell(row, charfont[0x10], charfont[0x14], offset, xorInvert, 3, 5);
			arrowState &= ~OSD_ARROW_LEFT;
			if (*s++ == 0) break;
			if (*s++ == 0) break;
			if (*s++ == 0) break;
		}
		else
		{
			unsigned char b = (unsigned char)*s++;
			if (!b) break;

			if (b == 0x0b) // toggle stipple on/off and flip its phase
			{
				stippleMask ^= 0xAA;
				stippleToggle ^= 0xff;
			}
			else if (b == 0x0c) // toggle a forced per-character invert
			{
				xorChar ^= 0xff;
			}
			else if (b == 0x0d || b == 0x0a) // force a line wrap to the next row
			{
				if (++n >= lineLimit) n = 0;
				row = RowEncoder(RowStart(n));
			}
			else if (col < (lineLimit - 8))
			{
				col += EmitTextGlyphCell(row, charfont[b], offset, xorInvert ^ xorChar, stippleMask, stippleToggle);
			}
		}
	}

	for (; col < lineLimit; ++col)
		row.Push(xorInvert);

	if (n == (s_visibleRows - 1) && (arrowState & OSD_ARROW_RIGHT))
	{
		col += EmitArrowCell(row, charfont[0x15], charfont[0x11], offset, xorInvert, 3, 3);
	}
}

void OsdWrite(unsigned char n, const char *s, unsigned char inver, unsigned char stipple, char usebg, int maxinv, int mininv)
{
	OsdWriteOffset(n, s, inver, stipple, 0, 0, usebg, maxinv, mininv);
}

void OsdClear()
{
	memset(s_raster, 0, sizeof(s_raster));
	memset(s_rowInverted, 0, sizeof(s_rowInverted));
}

void OsdEnable(unsigned char mode)
{
	(void)mode;
	s_enabled = true;
}

void OsdDisable()
{
	s_enabled = false;
}

void OsdUpdate()
{
}

const uint8_t* OsdGetBuffer()
{
	return s_raster;
}

const uint8_t* OsdGetInvertMap()
{
	return s_rowInverted;
}

bool OsdIsEnabled()
{
	return s_enabled;
}
