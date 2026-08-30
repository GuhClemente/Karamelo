#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "osd.h"
#include "charrom.h"

typedef unsigned int uint;

// The real MiSTer OSD is a fixed-height card: a page with four entries shows
// four entries and twelve blank rows, it does not shrink. Sizing the card to
// its contents made it grow and shrink as you moved between pages, which is
// the one thing the hardware never does.
#define OSD_FIXED_LINES 15

static int osd_size = OSD_FIXED_LINES;
static bool osd_enabled = true;

void OsdSetSize(int n)
{
	// Kept as the callers' statement of intent, but the card height is fixed.
	// Only the vertical title text still needs to know the real line count.
	(void)n;
	osd_size = OSD_FIXED_LINES;
}

int OsdGetSize()
{
	return osd_size;
}

#define MAX_STARS 128
static StarPoint star_list[MAX_STARS];
static int star_screen_w = 640;
static int star_screen_h = 360;

static uint8_t osdbuf[256 * 32];
static uint8_t osd_invert_map[32]; // tracks which row is currently highlighted/inverted
static int  osdbufpos = 0;
static int  osdset = 0;

void StarsInit(int screen_w, int screen_h)
{
	star_screen_w = screen_w;
	star_screen_h = screen_h;
	srand((unsigned int)time(NULL));

	for (int i = 0; i < MAX_STARS; ++i)
	{
		star_list[i].x = rand() % star_screen_w;
		star_list[i].y = rand() % star_screen_h;
		star_list[i].speed = 1 + (rand() % 4);
		star_list[i].brightness = (uint8_t)(100 + (star_list[i].speed * 38));
	}
}

void StarsUpdate(int screen_w, int screen_h)
{
	star_screen_w = screen_w;
	star_screen_h = screen_h;

	for (int i = 0; i < MAX_STARS; ++i)
	{
		star_list[i].x -= star_list[i].speed;
		if (star_list[i].x < 0)
		{
			star_list[i].x = star_screen_w - 1;
			star_list[i].y = rand() % star_screen_h;
			star_list[i].speed = 1 + (rand() % 4);
			star_list[i].brightness = (uint8_t)(100 + (star_list[i].speed * 38));
		}
	}
}

const StarPoint* OsdGetStars(int* count)
{
	if (count) *count = MAX_STARS;
	return star_list;
}

static int arrow = 0;
static unsigned char titlebuffer[256];

static void rotatechar(unsigned char *in, unsigned char *out)
{
	int a, b, c;
	for (b = 0; b < 8; ++b)
	{
		a = 0;
		for (c = 0; c < 8; ++c)
		{
			a <<= 1;
			a |= (in[c] >> b) & 1;
		}
		out[b] = a;
	}
}

#define OSDHEIGHT ((uint)(osd_size * 8))

void OsdSetTitle(const char *s, int a)
{
	arrow = a;
	int zeros = 0;
	uint i = 0, j = 0;
	uint outp = 0;

	while (1)
	{
		int c = s[i++];
		if (c && (outp < OSDHEIGHT - 8))
		{
			unsigned char *p = &charfont[(unsigned char)c][0];
			for (j = 0; j < 8; ++j)
			{
				unsigned char nc = *p++;
				if (nc)
				{
					zeros = 0;
					titlebuffer[outp++] = nc;
				}
				else if (zeros == 0 || (c == ' ' && zeros < 5))
				{
					titlebuffer[outp++] = 0;
					zeros++;
				}
				if (outp >= sizeof(titlebuffer)) break;
			}
		}
		else break;
	}

	for (i = outp; i < OSDHEIGHT; i++)
	{
		titlebuffer[i] = 0;
	}

	uint c = (OSDHEIGHT > outp) ? (OSDHEIGHT - 1 - outp) / 2 : 0;
	if (c > 0 && outp > 0)
	{
		memmove(titlebuffer + c, titlebuffer, outp);
		for (i = 0; i < c; ++i) titlebuffer[i] = 0;
	}

	for (i = 0; i < OSDHEIGHT; i += 8)
	{
		unsigned char tmp[8];
		rotatechar(&titlebuffer[i], tmp);
		for (c = 0; c < 8; ++c)
		{
			titlebuffer[i + c] = tmp[c];
		}
	}
}

void OsdSetArrow(int a)
{
	arrow = a;
}

static void osd_start(int line)
{
	line = line & 0x1F;
	osdset |= 1 << line;
	osdbufpos = line * 256;
}

static void draw_title(const unsigned char *p)
{
	osdbuf[osdbufpos++] = 0xff;
	osdbuf[osdbufpos++] = 0xff;
	osdbuf[osdbufpos++] = 0xff;

	for (int i = 0; i < 8; i++)
	{
		osdbuf[osdbufpos++] = 255 ^ *p;
		osdbuf[osdbufpos++] = 255 ^ *p++;
	}

	osdbuf[osdbufpos++] = 0xff;
	osdbuf[osdbufpos++] = 0;
	osdbuf[osdbufpos++] = 0;
}

void OsdWriteOffset(unsigned char n, const char *s, unsigned char invert, unsigned char stipple, char offset, char leftchar, char usebg, int maxinv, int mininv)
{
	(void)usebg; // Keep OSD box clean of background stars
	unsigned short i;
	unsigned char b;
	const unsigned char *p;
	unsigned char stipplemask = 0xff;
	int linelimit = OSDLINELEN;
	int arrowmask = arrow;

	if (n < 32)
	{
		osd_invert_map[n] = invert;
	}

	if (n == (osd_size - 1) && (arrow & OSD_ARROW_RIGHT))
		linelimit -= 22;

	if (n && n < OsdGetSize() - 1) leftchar = 0;

	if (stipple) {
		stipplemask = 0x55;
		stipple = 0xff;
	}
	else
		stipple = 0;

	osd_start(n);

	unsigned char xormask = 0;
	unsigned char xorchar = 0;

	i = 0;
	while (1)
	{
		if (invert && i / 8 >= mininv) xormask = 255;
		if (invert && i / 8 >= maxinv) xormask = 0;

		if (i == 0 && (n < osd_size))
		{
			unsigned char tmp[8];

			if (leftchar)
			{
				unsigned char tmp2[8];
				memcpy(tmp2, charfont[(uint)(unsigned char)leftchar], 8);
				rotatechar(tmp2, tmp);
				p = tmp;
			}
			else
			{
				p = &titlebuffer[(osd_size - 1 - n) * 8];
			}

			draw_title(p);
			i += 22;
		}
		else if (n == (osd_size - 1) && (arrowmask & OSD_ARROW_LEFT))
		{
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			p = &charfont[0x10][0];
			for (b = 0; b < 8; b++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
			p = &charfont[0x14][0];
			for (b = 0; b < 8; b++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;

			i += 24;
			arrowmask &= ~OSD_ARROW_LEFT;
			if (*s++ == 0) break;
			if (*s++ == 0) break;
			if (*s++ == 0) break;
		}
		else
		{
			b = *s++;
			if (!b) break;

			if (b == 0xb)
			{
				stipplemask ^= 0xAA;
				stipple ^= 0xff;
			}
			else if (b == 0xc)
			{
				xorchar ^= 0xff;
			}
			else if (b == 0x0d || b == 0x0a)
			{
				if (++n >= linelimit) n = 0;
				osd_start(n);
			}
			else if (i < (linelimit - 8))
			{
				p = &charfont[(unsigned char)b][0];
				for (unsigned char c = 0; c < 8; c++) {
					osdbuf[osdbufpos++] = (((*p++ << offset) & stipplemask) ^ xormask ^ xorchar);
					stipplemask ^= stipple;
				}
				i += 8;
			}
		}
	}

	for (; i < linelimit; i++)
	{
		osdbuf[osdbufpos++] = xormask;
	}

	if (n == (osd_size - 1) && (arrowmask & OSD_ARROW_RIGHT))
	{
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		p = &charfont[0x15][0];
		for (unsigned char c = 0; c < 8; c++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
		p = &charfont[0x11][0];
		for (unsigned char c = 0; c < 8; c++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		i += 22;
	}
}

void OsdWrite(unsigned char n, const char *s, unsigned char inver, unsigned char stipple, char usebg, int maxinv, int mininv)
{
	OsdWriteOffset(n, s, inver, stipple, 0, 0, usebg, maxinv, mininv);
}

void OsdClear()
{
	memset(osdbuf, 0, sizeof(osdbuf));
	memset(osd_invert_map, 0, sizeof(osd_invert_map));
	osdset = 0;
}

void OsdEnable(unsigned char mode)
{
	(void)mode;
	osd_enabled = true;
}

void OsdDisable()
{
	osd_enabled = false;
}

void OsdUpdate()
{
}

const uint8_t* OsdGetBuffer()
{
	return osdbuf;
}

const uint8_t* OsdGetInvertMap()
{
	return osd_invert_map;
}

bool OsdIsEnabled()
{
	return osd_enabled;
}
