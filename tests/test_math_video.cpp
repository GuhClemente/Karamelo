#include "test_framework.h"

// The real thing, not a copy. These used to be private reimplementations that
// had already drifted from production: the local rate table was missing five
// entries the app had, and the local viewport routine treated aspect_mode 0 as
// "fill" while the app treats it as 4:3. Both tests passed anyway, because they
// were testing themselves.
#include "mister_math.h"

#include <cmath>

TEST_CASE(ClampIntBounds)
{
	ASSERT_EQ(0, ClampInt(-5, 0, 9));
	ASSERT_EQ(9, ClampInt(50, 0, 9));
	ASSERT_EQ(4, ClampInt(4, 0, 9));
	ASSERT_EQ(7, ClampInt(7, 7, 7));
}

TEST_CASE(SnapRefreshRateToStandard)
{
	// DWM reports the exact ratio; it must land on the NTSC rate, not on 60.
	ASSERT_TRUE(std::fabs(SnapToStandardRate(59.957) - 59.94005994) < 0.001);
	ASSERT_TRUE(std::fabs(SnapToStandardRate(59.94) - 59.94005994) < 0.001);

	// 59.000 is what a virtual display adapter once reported here. At the old
	// 1% window it slipped through unsnapped and the core was aimed at an
	// impossible rate.
	ASSERT_TRUE(std::fabs(SnapToStandardRate(59.000) - 59.94005994) < 0.001);

	ASSERT_TRUE(std::fabs(SnapToStandardRate(60.0) - 60.0) < 0.001);
	ASSERT_TRUE(std::fabs(SnapToStandardRate(143.9) - 143.8561439) < 0.001);
	ASSERT_TRUE(std::fabs(SnapToStandardRate(240.1) - 240.0) < 0.001);

	// Nothing plausible nearby: leave it alone rather than round it into a lie.
	ASSERT_TRUE(std::fabs(SnapToStandardRate(31.5) - 31.5) < 0.001);
}

TEST_CASE(ViewportFourThreePillarboxed)
{
	int x, y, w, h;
	ComputeViewport(0, 1280, 720, &x, &y, &w, &h);
	ASSERT_EQ(960, w);
	ASSERT_EQ(720, h);
	ASSERT_EQ(160, x);
	ASSERT_EQ(0, y);

	// aspect_mode 1 must behave identically to 0 - the old test asserted the
	// opposite for mode 0 and still passed.
	int x1, y1, w1, h1;
	ComputeViewport(1, 1280, 720, &x1, &y1, &w1, &h1);
	ASSERT_EQ(w, w1);
	ASSERT_EQ(h, h1);
	ASSERT_EQ(x, x1);
	ASSERT_EQ(y, y1);
}

TEST_CASE(ViewportLetterboxesNarrowWindow)
{
	// Narrower than 4:3, so it must letterbox instead of running off the edge.
	int x, y, w, h;
	ComputeViewport(0, 640, 720, &x, &y, &w, &h);
	ASSERT_TRUE(w <= 640);
	ASSERT_EQ(640, w);
	ASSERT_EQ(480, h);
	ASSERT_EQ(0, x);
	ASSERT_EQ(120, y);
}

TEST_CASE(ViewportWidescreenFills)
{
	int x, y, w, h;
	ComputeViewport(2, 1280, 720, &x, &y, &w, &h);
	ASSERT_EQ(1280, w);
	ASSERT_EQ(720, h);
	ASSERT_EQ(0, x);
	ASSERT_EQ(0, y);
}

TEST_CASE(ViewportNeverExceedsSurface)
{
	// Whatever the window shape, the picture has to fit inside it.
	const int sizes[][2] = { {320,240},{640,360},{800,600},{1280,720},{1920,1080},{2560,1440},{100,900} };
	for (int mode = 0; mode <= 2; mode++)
	{
		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
		{
			int dw = sizes[i][0], dh = sizes[i][1];
			int x, y, w, h;
			ComputeViewport(mode, dw, dh, &x, &y, &w, &h);
			ASSERT_TRUE(x >= 0);
			ASSERT_TRUE(y >= 0);
			ASSERT_TRUE(x + w <= dw);
			ASSERT_TRUE(y + h <= dh);
		}
	}
}
