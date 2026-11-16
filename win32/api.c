/* SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#define HDPARM_WIN_NO_HOOKS
#include "device.h"
struct win_api win_api = {DeviceIoControl,
                          CreateFileA,
                          CloseHandle,
                          ReadFile,
                          SetFilePointer,
                          FlushFileBuffers,
                          QueryPerformanceCounter,
                          QueryPerformanceFrequency,
                          GetTickCount,
                          GetVersion,
                          Sleep};
