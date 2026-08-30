#include "mister_math.h"

#include <math.h>
#include <stddef.h>

int ClampInt(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

double SnapToStandardRate(double rate)
{
	// The NTSC-derived rates are the exact 1000/1001 ratios, not the rounded
	// integers, because that is what the hardware actually runs at.
	static const double kKnownRates[] = {
		50.0, 59.94005994, 60.0, 72.0, 74.925, 75.0, 85.0, 90.0,
		100.0, 119.8801198, 120.0, 143.8561439, 144.0,
		164.8351648, 165.0, 175.0, 180.0, 200.0,
		239.7602398, 240.0, 360.0
	};

	double best_diff = 1e9;
	double best_rate = rate;

	for (size_t k = 0; k < sizeof(kKnownRates) / sizeof(kKnownRates[0]); k++)
	{
		double diff = fabs(rate - kKnownRates[k]);

		// 2% rather than 1%: a virtual display adapter in the chain once made
		// DWM report 59.000Hz, which is 1.57% away from 59.94 and so slipped
		// through unsnapped - and a core aimed at an impossible rate is worse
		// than one left alone.
		if (diff < best_diff && (diff / kKnownRates[k] < 0.02))
		{
			best_diff = diff;
			best_rate = kKnownRates[k];
		}
	}

	return best_rate;
}

void ComputeViewport(int aspect_mode, int dest_w, int dest_h,
                     int* out_x, int* out_y, int* out_w, int* out_h)
{
	int target_w = dest_w;
	int target_h = dest_h;
	int offset_x = 0;
	int offset_y = 0;

	if (aspect_mode == 0 || aspect_mode == 1) // 4:3, pillarboxed
	{
		target_w = (dest_h * 4) / 3;
		target_h = dest_h;
		offset_x = (dest_w - target_w) / 2;
		offset_y = 0;

		// The window can be narrower than 4:3; letterbox instead of overflowing.
		if (target_w > dest_w)
		{
			target_w = dest_w;
			target_h = (dest_w * 3) / 4;
			offset_x = 0;
			offset_y = (dest_h - target_h) / 2;
		}
	}

	if (out_x) *out_x = offset_x;
	if (out_y) *out_y = offset_y;
	if (out_w) *out_w = target_w;
	if (out_h) *out_h = target_h;
}
