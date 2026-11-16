/* No real devices: all I/O is replaced at the OS boundary. */
#define HDPARM_WIN_INTERNAL
#include "hdparm.h"
#include "win32/device.h"
#include <assert.h>
#include <ntddscsi.h>
int verbose = 0, prefer_ata12 = 0;
static unsigned calls, reads, ata_calls, sat_calls;
static unsigned scenario;
static __u64 seek_position;
static LONGLONG clock_value;
static BOOL WINAPI fake_close(HANDLE h) {
    (void)h;
    return TRUE;
}
static BOOL WINAPI fake_flush(HANDLE h) {
    (void)h;
    return TRUE;
}
static DWORD WINAPI fake_version(void) { return 4; }
static HANDLE WINAPI fake_open(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES x, DWORD c,
                               DWORD f, HANDLE h) {
    (void)p;
    (void)a;
    (void)s;
    (void)x;
    (void)c;
    (void)f;
    (void)h;
    return (HANDLE)(uintptr_t)0x12345678;
}
static DWORD WINAPI fake_seek(HANDLE h, LONG lo, PLONG hi, DWORD origin) {
    (void)h;
    assert(origin == FILE_BEGIN);
    seek_position = ((__u64)(DWORD)*hi << 32) | (DWORD)lo;
    return (DWORD)lo;
}
static BOOL WINAPI fake_read(HANDLE h, LPVOID p, DWORD n, LPDWORD got, LPOVERLAPPED o) {
    (void)h;
    (void)o;
    reads++;
    memset(p, 0x5a, n);
    *got = scenario == 5 ? n - 1 : n;
    return TRUE;
}
static BOOL WINAPI fake_counter(LARGE_INTEGER *v) {
    v->QuadPart = clock_value;
    clock_value += 1000;
    return TRUE;
}
static BOOL WINAPI fake_frequency(LARGE_INTEGER *v) {
    v->QuadPart = 1000;
    return TRUE;
}
static DWORD WINAPI fake_ticks(void) { return 100; }
static BOOL WINAPI fake_ioctl(HANDLE h, DWORD code, LPVOID in, DWORD insz, LPVOID out, DWORD outsz,
                              LPDWORD got, LPOVERLAPPED ov) {
    (void)h;
    (void)insz;
    (void)outsz;
    (void)ov;
    calls++;
    *got = 0;
    if (code == IOCTL_DISK_GET_DRIVE_GEOMETRY) {
        DISK_GEOMETRY *g = out;
        memset(g, 0, sizeof(*g));
        g->BytesPerSector = scenario == 6 ? 4096 : 512;
        g->Cylinders.QuadPart = 10000;
        g->TracksPerCylinder = 255;
        g->SectorsPerTrack = 63;
        *got = sizeof(*g);
        return TRUE;
    }
    if (code == IOCTL_DISK_GET_LENGTH_INFO) {
        ((GET_LENGTH_INFORMATION *)out)->Length.QuadPart = 16LL * 1024 * 1024 * 1024;
        *got = sizeof(GET_LENGTH_INFORMATION);
        return TRUE;
    }
    if (code == IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) {
        VOLUME_DISK_EXTENTS *e = out;
        if (scenario == 7) {
            SetLastError(ERROR_MORE_DATA);
            return FALSE;
        }
        memset(e, 0, sizeof(*e));
        e->NumberOfDiskExtents = 1;
        e->Extents[0].DiskNumber = 3;
        e->Extents[0].StartingOffset.QuadPart = 0x140000000LL;
        e->Extents[0].ExtentLength.QuadPart = 0x10000000LL;
        *got = sizeof(*e);
        return TRUE;
    }
    if (code == IOCTL_ATA_PASS_THROUGH) {
        ATA_PASS_THROUGH_EX *p = in;
        unsigned char command = p->CurrentTaskFile[6];
        ata_calls++;
        if (scenario == 1 || scenario == 8) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
        if (scenario == 2) {
            SetLastError(ERROR_SEM_TIMEOUT);
            return FALSE;
        }
        if (scenario == 3) {
            *got = 2;
            return TRUE;
        }
        if (command == ATA_OP_READ_PIO_EXT) {
            assert(p->AtaFlags & ATA_FLAGS_48BIT_COMMAND);
            assert(p->PreviousTaskFile[2] == 0x34);
            assert(p->CurrentTaskFile[2] == 0x78);
        }
        if (p->AtaFlags & ATA_FLAGS_DATA_OUT)
            assert(((unsigned char *)p)[p->DataBufferOffset] == 0xa5);
        else if (p->DataTransferLength)
            memset((char *)p + p->DataBufferOffset, 0x5a, p->DataTransferLength);
        p->CurrentTaskFile[6] = scenario == 4 ? 0x51 : 0x50;
        p->CurrentTaskFile[0] = scenario == 4 ? 4 : 0;
        *got = (DWORD)(p->DataTransferLength ? p->DataBufferOffset + p->DataTransferLength
                                             : sizeof(*p));
        return TRUE;
    }
    if (code == IOCTL_SCSI_PASS_THROUGH) {
        SCSI_PASS_THROUGH *p = out;
        unsigned char *s = (unsigned char *)p + p->SenseInfoOffset;
        sat_calls++;
        memset(s, 0, 64);
        s[0] = 0x72;
        s[1] = 1;
        s[7] = 14;
        s[8] = 9;
        s[9] = scenario == 8 ? 30 : 12;
        s[21] = 0x50;
        p->ScsiStatus = 2;
        p->SenseInfoLength = 22;
        if (p->DataTransferLength)
            memset((char *)p + p->DataBufferOffset, 0x5a, p->DataTransferLength);
        *got = (DWORD)(p->DataTransferLength ? p->DataBufferOffset + p->DataTransferLength
                                             : sizeof(*p) + 22);
        return TRUE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
static int open_disk(void) {
    int fd = win_open_device("/dev/sda", 0);
    assert(fd >= WIN_FD_BASE);
    return fd;
}
int main(void) {
    struct ata_tf tf;
    int fd, fd2;
    unsigned char *buf =
        VirtualAlloc(NULL, 2 * 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(buf);
    assert(sizeof(IDEREGS) == 8);
    assert(offsetof(SENDCMDINPARAMS, bBuffer) == 32);
    assert(offsetof(SENDCMDOUTPARAMS, bBuffer) == 16);
    assert(sizeof(ATA_PASS_THROUGH_EX) == (sizeof(void *) == 8 ? 48 : 40));
    assert(sizeof(SCSI_PASS_THROUGH) == (sizeof(void *) == 8 ? 56 : 44));
    win_api.ioctl = fake_ioctl;
    win_api.create = fake_open;
    win_api.close = fake_close;
    win_api.read = fake_read;
    win_api.seek = fake_seek;
    win_api.flush = fake_flush;
    win_api.counter = fake_counter;
    win_api.frequency = fake_frequency;
    win_api.ticks = fake_ticks;
    win_api.version = fake_version;
    fd = open_disk();
    assert(win_seek(fd, 0xffffffffLL + 1, SEEK_SET) == 0x100000000LL);
    assert(win_read(fd, buf, 512) == 512 && seek_position == 0x100000000ULL);
    assert(win_seek(fd, -1, SEEK_SET) == -1);
    assert(win_seek(fd, 16LL * 1024 * 1024 * 1024 - 512, SEEK_SET) >= 0);
    assert(win_read(fd, buf, 1024) == -1);
    memset(&tf, 0, sizeof(tf));
    tf.command = ATA_OP_READ_PIO_EXT;
    tf.is_lba48 = 1;
    tf.hob.lbal = 0x34;
    tf.lob.lbal = 0x78;
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 15) == 0 && buf[0] == 0x5a);
    memset(&tf, 0, sizeof(tf));
    tf.command = ATA_OP_SECURITY_SET_PASS;
    memset(buf, 0xa5, 512);
    assert(win_ata(fd, SG_WRITE, 0, &tf, buf, 512, 15) == 0);
    scenario = 1;
    ata_calls = sat_calls = 0;
    tf.command = ATA_OP_IDENTIFY;
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 15) == 0 && ata_calls == 1 && sat_calls == 1);
    tf.command = ATA_OP_IDENTIFY;
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 15) == 0 && ata_calls == 1 && sat_calls == 2);
    scenario = 0;
    fd2 = open_disk();
    tf.command = ATA_OP_IDENTIFY;
    assert(win_ata(fd2, SG_READ, 0, &tf, buf, 512, 15) == 0 && ata_calls == 2);
    win_close(fd);
    win_close(fd2);
    for (scenario = 2; scenario <= 4; scenario++) {
        fd = open_disk();
        ata_calls = sat_calls = 0;
        tf.command = ATA_OP_IDENTIFY;
        assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 15) == -1 && errno == EIO);
        assert(ata_calls == 1 && sat_calls == 0);
        win_close(fd);
    }
    scenario = 8;
    fd = open_disk();
    ata_calls = sat_calls = 0;
    tf.command = ATA_OP_IDENTIFY;
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 15) == -1 && errno == EIO);
    assert(ata_calls == 1 && sat_calls == 1);
    win_close(fd);
    scenario = 5;
    fd = open_disk();
    assert(win_read(fd, buf, 512) == -1 && errno == EIO);
    win_close(fd);
    scenario = 6;
    fd = open_disk();
    assert(win_read(fd, buf, 512) == -1);
    assert(win_read(fd, buf, 4096) == 4096);
    win_close(fd);
    scenario = 7;
    assert(win_open_device("C:", 0) == -1); /* no multi-disk fallback */
    scenario = 0;
    fd = win_open_device("C:", 0);
    assert(fd >= WIN_FD_BASE);
    assert(win_read(fd, buf, 512) == 512 && seek_position == 0x140000000ULL);
    assert(win_seek(fd, 0x10000000LL, SEEK_SET) >= 0 && win_read(fd, buf, 512) == -1);
    assert(win_ata(fd, SG_READ, 0, &tf, buf, 512, 15) == -1 && errno == ENOSYS);
    win_close(fd);
    fd = open_disk();
    reads = 0;
    clock_value = 0;
    assert(win_benchmark(fd, 1, 0) == 0 && reads == 1);
    reads = 0;
    clock_value = 0;
    assert(win_benchmark(fd, 0, 0) == 0 && reads == 3);
    win_close(fd);
    VirtualFree(buf, 0, MEM_RELEASE);
    puts("Windows transport and benchmark tests passed (mock I/O).");
    return 0;
}
