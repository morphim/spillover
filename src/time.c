/*
Copyright (c) 2015 drugaddicted - c17h19no3 AT openmailbox DOT org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "time.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>

#define SPO_TIME_TO_MSECS(time) (uint32_t)((time).tv_sec * 1000 + (time).tv_nsec / 1000000)
#endif

uint32_t spo_time_current()
{
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return SPO_TIME_TO_MSECS(ts);
    return 0;
#endif
}

uint32_t spo_time_elapsed(uint32_t from_time)
{
#ifdef _WIN32
    return ((uint32_t)GetTickCount() - from_time); /* unsigned arithmetic does all the magic */
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (SPO_TIME_TO_MSECS(ts) - from_time); /* unsigned arithmetic does all the magic */
    return 0;
#endif
}
