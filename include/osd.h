#ifndef OSD_H_INCLUDED
#define OSD_H_INCLUDED

#include <stdint.h>

#define DISABLE_KEYBOARD 0x02
#define OSD_INFO         0x04
#define OSD_MSG          0x08

#define REPEATDELAY      500
#define REPEATRATE       50

#define OSD_ARROW_LEFT   1
#define OSD_ARROW_RIGHT  2

#define OSDLINELEN       256

/* Functions ported from Main_MiSTer */
void OsdSetTitle(const char *s, int arrow = 0);
void OsdSetArrow(int arrow);
void OsdWrite(unsigned char n, const char *s="", unsigned char inver=0, unsigned char stipple=0, char usebg = 0, int maxinv = 32, int mininv = 0);
void OsdWriteOffset(unsigned char n, const char *s, unsigned char inver, unsigned char stipple, char offset, char leftchar, char usebg = 0, int maxinv = 32, int mininv = 0);
void OsdClear();
void OsdEnable(unsigned char mode = 0);
void OsdDisable();
void OsdUpdate();
void ScrollText(char n, const char *str, int off, int len, int max_len, unsigned char invert, int idx = 0);
void ScrollReset(int idx = 0);
void StarsInit(int screen_w = 640, int screen_h = 360);
void StarsUpdate(int screen_w = 640, int screen_h = 360);
void OsdSetSize(int n);
int  OsdGetSize();

/* Platform buffer access for Windows renderer */
const uint8_t* OsdGetBuffer();
const uint8_t* OsdGetInvertMap();
bool           OsdIsEnabled();

struct StarPoint
{
	int x, y;
	int speed;
	uint8_t brightness;
};

const StarPoint* OsdGetStars(int* count);

#define OsdIsBig (OsdGetSize()>8)

#endif
