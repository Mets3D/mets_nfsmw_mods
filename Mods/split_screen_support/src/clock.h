// A clock for per frame times. GetTickCount ticks every 15.6 ms, so at 60 frames a second the time between two frames comes out as 0 or 31 ms about as often
// as 16 ms: a camera that smooths or blurs by that time jerks from frame to frame.
#pragma once

#include <windows.h>

namespace split_screen_support {

inline double seconds_now() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency;
}

}  // namespace split_screen_support
