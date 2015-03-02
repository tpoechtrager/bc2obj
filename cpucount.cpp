/*
  Copyright (c) 2015 Thomas Poechtrager (t.poechtrager@gmail.com)

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
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
 */

#include <cstdlib>

#ifdef __CYGWIN__
#define WIN32
#endif /* __CYGWIN__ */

#ifdef _WIN32
#include <windows.h>
#endif /* _WIN32 */

#ifdef __linux__
#undef __USE_GNU
#define __USE_GNU
#include <sched.h>
#undef __USE_GNU
#endif /* __linux__ */

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) ||     \
    defined(__APPLE__)
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#ifndef HW_AVAILCPU
#define HW_AVAILCPU 25
#endif /* HW_AVAILCPU */
#endif /* BSD */

int getCPUCount() {
#ifdef WIN32
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);

  return sysinfo.dwNumberOfProcessors;
#else
#ifdef __linux__
  cpu_set_t cs;
  int i, cpucount = 0;

  CPU_ZERO(&cs);
  sched_getaffinity(0, sizeof(cs), &cs);

  for (i = 0; i < 128; i++) {
    if (CPU_ISSET(i, &cs))
      cpucount++;
  }

  return cpucount ? cpucount : 1;
#else
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) ||     \
    defined(__APPLE__)
  int cpucount = 0;
  int mib[4];
  size_t len = sizeof(cpucount);

  mib[0] = CTL_HW;
  mib[1] = HW_AVAILCPU;

  sysctl(mib, 2, &cpucount, &len, NULL, 0);

  if (cpucount < 1) {
    mib[1] = HW_NCPU;
    sysctl(mib, 2, &cpucount, &len, NULL, 0);
  }

  return cpucount ? cpucount : 1;
#else
#warning unknown platform
  return 1;
#endif /* BSD */
#endif /* __linux__ */
#endif /* WIN32 */
}
