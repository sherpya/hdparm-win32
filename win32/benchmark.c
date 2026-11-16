/* Windows timing: explicit RAM copies vs actual device reads. SPDX-License-Identifier: BSD-2-Clause
 */
#define HDPARM_WIN_INTERNAL
#include "device.h"
#define BLOCK (2u * 1024u * 1024u)
struct timer {
    LARGE_INTEGER frequency, start;
    DWORD tick, last;
    __u64 ticks;
    int qpc;
};
static void timer_start(struct timer *t) {
    memset(t, 0, sizeof(*t));
    t->qpc = QueryPerformanceFrequency(&t->frequency) && t->frequency.QuadPart > 0 &&
             QueryPerformanceCounter(&t->start);
    t->tick = t->last = GetTickCount();
}
static double timer_elapsed(struct timer *t) {
    LARGE_INTEGER now;
    DWORD tick;
    if (t->qpc) {
        if (!QueryPerformanceCounter(&now) || now.QuadPart < t->start.QuadPart)
            return -1;
        return (double)(now.QuadPart - t->start.QuadPart) / (double)t->frequency.QuadPart;
    }
    tick = GetTickCount();
    t->ticks += (DWORD)(tick - t->last);
    t->last = tick;
    return (double)t->ticks / 1000.0;
}
int win_benchmark(int fd, int cached, __u64 offset) {
    struct win_device *d = win_device(fd);
    struct timer timer;
    unsigned char *buf = NULL, *source = NULL;
    __u64 total = 0;
    double elapsed = 0;
    unsigned bytes = BLOCK;
    volatile unsigned checksum = 0;
    unsigned i;
    int rc = -1;
    if (!d)
        return -1;
    if (offset > d->size || bytes > d->size - offset || offset % d->sector) {
        errno = EINVAL;
        return -1;
    }
    buf = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!buf) {
        errno = ENOMEM;
        goto done;
    }
    if (cached) {
        source = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!source) {
            errno = ENOMEM;
            goto done;
        }
        if (win_seek(fd, (__s64)offset, SEEK_SET) < 0 || win_read(fd, source, bytes) != (int)bytes)
            goto done;
    } else if (win_seek(fd, (__s64)offset, SEEK_SET) < 0)
        goto done;
    timer_start(&timer);
    do {
        if (cached) {
            memcpy(buf, source, bytes);
            /* Observable reads prevent elimination of repeated copies. */
            for (i = 0; i < bytes; i += 512)
                checksum += buf[i];
        } else {
            if (bytes > d->size - d->position)
                break;
            if (win_read(fd, buf, bytes) != (int)bytes)
                goto done;
        }
        total += bytes;
        elapsed = timer_elapsed(&timer);
        if (elapsed < 0) {
            errno = EIO;
            goto done;
        }
    } while (elapsed < (cached ? 2.0 : 3.0));
    if (elapsed <= 0 || !total) {
        errno = EIO;
        goto done;
    }
    printf(" Timing %s%s: %.0f MiB in %.2f seconds = %.2f MiB/sec\n",
           cached ? "application RAM cached reads" : "disk reads via ", cached ? "" : d->transport,
           (double)total / 1048576.0, elapsed, (double)total / 1048576.0 / elapsed);
    rc = 0;
done:
    if (rc)
        perror("Windows benchmark");
    if (source)
        VirtualFree(source, 0, MEM_RELEASE);
    if (buf)
        VirtualFree(buf, 0, MEM_RELEASE);
    return rc;
}
