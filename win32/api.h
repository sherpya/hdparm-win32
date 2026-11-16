/* Injectable OS boundary for transport tests. SPDX-License-Identifier: BSD-2-Clause */
#ifndef HDPARM_WIN_API_H
#define HDPARM_WIN_API_H
#include <windows.h>
struct win_api {
    BOOL(WINAPI *ioctl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    HANDLE(WINAPI *create)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    BOOL(WINAPI *close)(HANDLE);
    BOOL(WINAPI *read)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    DWORD(WINAPI *seek)(HANDLE, LONG, PLONG, DWORD);
    BOOL(WINAPI *flush)(HANDLE);
    BOOL(WINAPI *counter)(LARGE_INTEGER *);
    BOOL(WINAPI *frequency)(LARGE_INTEGER *);
    DWORD(WINAPI *ticks)(void);
    DWORD(WINAPI *version)(void);
};
extern struct win_api win_api;
#ifndef HDPARM_WIN_NO_HOOKS
#define DeviceIoControl win_api.ioctl
#define CreateFileA win_api.create
#define CloseHandle win_api.close
#define ReadFile win_api.read
#define SetFilePointer win_api.seek
#define FlushFileBuffers win_api.flush
#define QueryPerformanceCounter win_api.counter
#define QueryPerformanceFrequency win_api.frequency
#define GetTickCount win_api.ticks
#define GetVersion win_api.version
#endif
#endif
