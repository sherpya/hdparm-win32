/* Win9x client tests with simulated SMARTVSD, ASPI and IOS. BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "win32/device.h"
#include "win32/aspi.h"
#include "win9x/protocol.h"
#include <assert.h>
int verbose = 0, prefer_ata12 = 0;
enum { IOS_BATCH = 9, IOS_SHORT_BATCH };
static unsigned mode, starts, polls, smart_calls, sleeps;
static DWORD tick;
static __u64 position;
static struct aspi_io *pending_aspi;
static DWORD WINAPI version(void) { return 0x80000004u; }
static DWORD WINAPI ticks(void) {
    tick += 1000;
    return tick;
}
static BOOL WINAPI benchmark_frequency(LARGE_INTEGER *v) {
    v->QuadPart = 1000;
    return TRUE;
}
static BOOL WINAPI benchmark_counter(LARGE_INTEGER *v) {
    static LONGLONG now;
    v->QuadPart = now;
    now += 1600;
    return TRUE;
}
static void WINAPI sleep_fake(DWORD ms) {
    (void)ms;
    sleeps++;
    if (pending_aspi && sleeps >= 3) {
        assert(pending_aspi->buffer && pending_aspi->bytes == 512);
        pending_aspi->h.status = 1;
        pending_aspi = NULL;
    }
}
static BOOL WINAPI close_fake(HANDLE h) {
    (void)h;
    return TRUE;
}
static HANDLE WINAPI open_fake(LPCSTR s, DWORD a, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD cd,
                               DWORD fl, HANDLE tpl) {
    (void)s;
    (void)a;
    (void)share;
    (void)sa;
    (void)cd;
    (void)fl;
    (void)tpl;
    return (HANDLE)(uintptr_t)0x10203040;
}
static BOOL WINAPI control(HANDLE h, DWORD c, LPVOID in, DWORD insz, LPVOID out, DWORD outsz,
                           LPDWORD got, LPOVERLAPPED ov) {
    (void)h;
    (void)ov;
    if (c == SMART_GET_VERSION) {
        GETVERSIONINPARAMS *v = out;
        memset(v, 0, sizeof(*v));
        v->bIDEDeviceMap = 1;
        v->fCapabilities = 1;
        *got = sizeof(*v);
        return TRUE;
    }
    if (c == SMART_RCV_DRIVE_DATA) {
        SENDCMDINPARAMS *p = in;
        SENDCMDOUTPARAMS *o = out;
        unsigned char *id = o->bBuffer;
        assert(insz == 32 && outsz >= 528 && p->bDriveNumber == 0);
        assert(p->irDriveRegs.bCommandReg == 0xec);
        memset(out, 0, outsz);
        o->cBufferSize = 512;
        id[122] = 0;
        id[123] = 2; /* 0x02000000 sectors */
        *got = mode == 3 ? 16 : 528;
        smart_calls++;
        return TRUE;
    }
    if (c == HDIOS_IOCTL) {
        struct hdios_request *r = in;
        struct hdios_reply *s = out;
        assert(insz >= sizeof(*r));
        memset(out, 0, outsz);
        s->magic = HDIOS_MAGIC;
        s->version = HDIOS_VERSION;
        s->size = sizeof(*s);
        s->device = r->device;
        s->generation = 7;
        s->token = 19;
        if (r->operation == HDIOS_ENUM) {
            s->sector_size = 512;
            s->sectors_lo = 0x2000000;
            s->capabilities = HDIOS_CAP_READ | HDIOS_CAP_ATA28;
            if (mode == IOS_BATCH || mode == IOS_SHORT_BATCH)
                s->capabilities |= HDIOS_CAP_BENCH_READ;
            s->max_transfer = 65536;
            *got = sizeof(*s);
            return TRUE;
        }
        assert(r->generation == 7);
        if (r->operation == HDIOS_BENCH_READ) {
            struct hdios_bench_result *bench = (struct hdios_bench_result *)(s + 1);
            assert((mode == IOS_BATCH || mode == IOS_SHORT_BATCH) && r->flags == HDIOS_IN);
            assert(insz == sizeof(*r) && r->bytes == sizeof(*bench));
            assert(outsz == sizeof(*s) + sizeof(*bench));
            position = ((__u64)r->lba_hi << 32) | r->lba_lo;
            assert(position == 0x800000u + starts * (HDIOS_BENCH_BYTES / 512));
            starts++;
            s->bytes = sizeof(*bench);
            bench->bytes_read = HDIOS_BENCH_BYTES - (mode == IOS_SHORT_BATCH ? 512 : 0);
            bench->reads = 32;
            *got = outsz;
            return TRUE;
        }
        if (r->operation != HDIOS_POLL) {
            starts++;
            position = ((__u64)r->lba_hi << 32) | r->lba_lo;
        } else {
            polls++;
            assert(r->token == 19);
        }
        if ((mode == 1 && r->operation != HDIOS_POLL) || mode == 2)
            s->status = HDIOS_PENDING;
        else {
            s->bytes = r->flags == HDIOS_IN ? r->bytes : 0;
            memset(s + 1, 0x5a, s->bytes);
            s->regs[6] = 0x50;
        }
        *got = sizeof(*s) + s->bytes;
        if (mode == 4)
            --*got;
        if (mode == 5)
            s->token = 20;
        return TRUE;
    }
    assert(0);
    return FALSE;
}
#ifndef _WIN64
static DWORD __cdecl aspi_info(void) { return 0x101; }
static DWORD __cdecl aspi_send(void *v) {
    struct aspi_head *h = v;
    if (h->command == 1) {
        struct aspi_type *q = v;
        assert(q->target == 2 && q->lun == 1);
        q->type = 0;
    } else {
        struct aspi_io *p = v;
        unsigned char *buf = p->buffer;
        assert(h->command == 2 && p->target == 2 && p->lun == 1);
        memset(buf, 0x5a, p->bytes);
        if (p->cdb[0] == 0x25) {
            assert(p->bytes == 8);
            memset(buf, 0, 8);
            buf[0] = 1;
            buf[1] = buf[2] = buf[3] = 255;
            buf[6] = 2;
        } else if (p->cdb[0] == 0x28) {
            assert(p->cdb[2] == 0 && p->cdb[3] == 0x80); /* 4 GiB / 512 */
            if (mode == 6) {
                pending_aspi = p;
                return 0;
            }
            if (mode == 7)
                p->bytes--;
            if (mode == 8)
                p->host_status = 0x11;
        } else
            assert(0);
    }
    h->status = 1;
    return 1;
}
#endif
int main(void) {
    struct ata_tf tf = {0};
    int fd;
    unsigned char *buf = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(buf);
    win_api.ioctl = control;
    win_api.create = open_fake;
    win_api.close = close_fake;
    win_api.version = version;
    win_api.ticks = ticks;
    win_api.sleep = sleep_fake;
    fd = win_open_device("/dev/hda", 0);
    assert(fd >= WIN_FD_BASE && smart_calls == 1);
    tf.command = 0xef;
    assert(win_ata(fd, SG_READ, 0, &tf, NULL, 0, 15) == -1 && errno == ENOSYS && smart_calls == 1);
    assert(win_read(fd, buf, 512) == -1 && errno == ENOSYS);
    win_close(fd);
    mode = 3;
    assert(win_open_device("smart:0", 0) == -1 && errno == EIO);
    mode = 0;
    assert(win_open_device("/dev/sda", 0) == -1); /* no cross-namespace guessing */
    fd = win_open_device("ios:0", 0);
    assert(fd >= WIN_FD_BASE);
    assert(win_seek(fd, 0x100000000LL, SEEK_SET) >= 0 && win_read(fd, buf, 512) == 512);
    assert(position == 0x800000 && starts == 1 && !polls);
    mode = 1;
    starts = polls = 0;
    assert(win_read(fd, buf, 512) == 512 && starts == 1 && polls == 1);
    mode = 2;
    starts = polls = 0;
    tf.command = 0xec;
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 1) == -1 && errno == EIO && starts == 1);
    mode = 4;
    assert(win_read(fd, buf, 512) == -1 && errno == EIO);
    mode = 0;
    starts = 0;
    tf.is_lba48 = 1;
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 1) == -1 && errno == ENOSYS && !starts);
    win_api.frequency = benchmark_frequency;
    win_api.counter = benchmark_counter;
    assert(win_benchmark(fd, 0, 0) == 0 && starts == 64); /* older VxD, two 2 MiB blocks */
    win_close(fd);
    mode = IOS_BATCH;
    fd = win_open_device("ios:0", 0);
    assert(fd >= WIN_FD_BASE);
    starts = 0;
    assert(win_benchmark(fd, 1, 0) == 0 && starts == 32); /* seed -T with ordinary reads */
    starts = polls = sleeps = 0;
    assert(win_benchmark(fd, 0, 0x100000000ULL) == 0);
    assert(starts == 2 && !polls && !sleeps);
    assert(win_device(fd)->position == 0x100000000ULL + 2 * HDIOS_BENCH_BYTES);
    mode = IOS_SHORT_BATCH;
    starts = 0;
    assert(win_benchmark(fd, 0, 0x100000000ULL) == -1 && errno == EIO);
    assert(starts == 1 && win_device(fd)->position == 0x100000000ULL);
    mode = 0;
    win_close(fd);
#ifndef _WIN64
    aspi_api.send = aspi_send;
    aspi_api.info = aspi_info;
    fd = win_open_device("aspi:0:2:1", 0);
    assert(fd >= WIN_FD_BASE);
    assert(win_seek(fd, 0x100000000LL, SEEK_SET) >= 0 && win_read(fd, buf, 512) == 512);
    mode = 6;
    sleeps = 0;
    assert(win_read(fd, buf, 512) == 512 && !pending_aspi && sleeps == 3);
    /* Explicit short deadline: timed out, but waited for pending completion before free. */
    {
        unsigned char cdb[10] = {0x28, 0, 0, 0x80, 0, 0, 0, 0, 1, 0}, sense[64], status;
        unsigned len = sizeof(sense);
        sleeps = 0;
        assert(win_aspi_scsi(win_device(fd), cdb, 10, SG_READ, buf, 512, 1, sense, &len, &status) ==
               -1);
        assert(errno == EIO && !pending_aspi && sleeps == 3);
    }
    mode = 7;
    assert(win_read(fd, buf, 512) == -1 && errno == EIO);
    mode = 8;
    assert(win_read(fd, buf, 512) == -1 && errno == EIO);
    win_close(fd);
#endif
    VirtualFree(buf, 0, MEM_RELEASE);
    puts("Win9x transport tests passed (mock SMARTVSD/ASPI/IOS).");
    return 0;
}
