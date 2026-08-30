#ifndef MISTER_MATH_H_INCLUDED
#define MISTER_MATH_H_INCLUDED

// Pure helpers, deliberately free of Windows and of any global state.
//
// These lived as `static` copies inside main_win32.cpp, menu.cpp and
// core_runner.cpp, which made them impossible to link into a test. The test
// suite worked around that by keeping its own copies, and the copies had
// already drifted out of sync with the originals. Extracting them here gives
// both the app and the tests one definition to share.

// Clamp v into [lo, hi].
int ClampInt(int v, int lo, int hi);

// Snap a measured or reported refresh rate onto the nearest standard rate.
// Returns the input unchanged when nothing is close enough, so an implausible
// reading stays visibly implausible instead of being rounded into a lie.
double SnapToStandardRate(double rate);

// Where the emulated picture goes inside a destination surface.
//   aspect_mode 0 or 1 -> 4:3, pillarboxed (or letterboxed on a narrow window)
//   aspect_mode 2      -> fill the surface
void ComputeViewport(int aspect_mode, int dest_w, int dest_h,
                     int* out_x, int* out_y, int* out_w, int* out_h);

#endif
