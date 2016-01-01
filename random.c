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

#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "random.h"

/* we don't use synchronization here as this results in more random numbers */
static uint32_t spo_rand_x[55];
static uint32_t spo_rand_y[256];
static uint32_t spo_rand_z;
static uint32_t spo_rand_i;
static uint32_t spo_rand_j;

static uint32_t spo_internal_get_seed()
{
#ifdef WIN32
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return time.LowPart;
#else /* we need to replace the next code to make it safer */
    struct timeval time;
    gettimeofday(&time, NULL);
    return 1000000 * time.tv_sec + time.tv_usec;
#endif
}

static uint32_t spo_internal_rand()
{
    if (spo_rand_i > 0)
        --spo_rand_i;
    else
        spo_rand_i = 54;
    if (spo_rand_j > 0)
        --spo_rand_j;
    else
        spo_rand_j = 54;

	return spo_rand_x[spo_rand_j] += spo_rand_x[spo_rand_i];
}

void spo_random_init()
{
	int i;

	spo_rand_x[0] = 1;
	spo_rand_x[1] = spo_internal_get_seed();

	for (i = 2; i < 55; ++i)
		spo_rand_x[i] = spo_rand_x[i - 1] + spo_rand_x[i - 2];

	spo_rand_i = 23;
	spo_rand_j = 54;

	for (i = 255; i >= 0; --i)
        spo_internal_rand();
	for (i = 255; i >= 0; --i)
        spo_rand_y[i] = spo_internal_rand();

	spo_rand_z = spo_internal_rand();
}

uint32_t spo_random_next()
{
    uint32_t index = spo_rand_z >> 24;

	spo_rand_z = spo_rand_y[index];
	spo_rand_y[index] = spo_internal_rand();

	return spo_rand_z;
}
