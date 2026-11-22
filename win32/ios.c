/* Optional IOS bridge client. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "device.h"
#include "../win9x/protocol.h"
extern int verbose;
static HANDLE open_bridge(void) {
    HANDLE h = CreateFileA("\\\\.\\HDPARM", 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        char path[MAX_PATH], vxd[MAX_PATH];
        char *p;
        DWORD n, error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return h;
        n = GetModuleFileNameA(NULL, path, sizeof(path));
        if (!n || n >= sizeof(path) || !(p = strrchr(path, '\\')))
            return h;
        p[1] = 0;
        if (strlen(path) + sizeof("\\\\.\\HDPARM.VXD") > sizeof(vxd))
            return h;
        snprintf(vxd, sizeof(vxd), "\\\\.\\%sHDPARM.VXD", path);
        h = CreateFileA(vxd, 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    }
    return h;
}
static void request_init(struct hdios_request *r, unsigned op, const struct win_device *d) {
    memset(r, 0, sizeof(*r));
    r->magic = HDIOS_MAGIC;
    r->version = HDIOS_VERSION;
    r->size = sizeof(*r);
    r->operation = op;
    r->device = d->unit;
    r->generation = d->generation;
}
static int exchange(struct win_device *d, struct hdios_request *r, void *data,
                    struct hdios_reply *info) {
    unsigned char *input, *output;
    struct hdios_reply *s;
    DWORD got = 0, start = GetTickCount();
    unsigned inbytes = sizeof(*r) + (r->flags == HDIOS_OUT ? r->bytes : 0);
    unsigned outbytes = sizeof(*s) + (r->flags == HDIOS_IN ? r->bytes : 0);
    int rc = -1;
    unsigned token = 0, operation = r->operation;
    if (r->bytes > HDIOS_MAX_DATA)
        return win_unsupported();
    input = calloc(1, inbytes);
    output = calloc(1, outbytes);
    if (!input || !output) {
        free(input);
        free(output);
        errno = ENOMEM;
        return -1;
    }
    memcpy(input, r, sizeof(*r));
    if (inbytes > sizeof(*r))
        memcpy(input + sizeof(*r), data, r->bytes);
    s = (struct hdios_reply *)output;
    for (;;) {
        memset(output, 0, outbytes);
        if (!DeviceIoControl(d->handle, HDIOS_IOCTL, input, inbytes, output, outbytes, &got,
                             NULL)) {
            win_error("HDPARM.VXD", GetLastError());
            break;
        }
        if (got < sizeof(*s) || s->magic != HDIOS_MAGIC || s->version != HDIOS_VERSION ||
            s->size != sizeof(*s) || s->device != r->device ||
            (operation != HDIOS_ENUM && s->generation != r->generation) ||
            (token && s->token != token) || s->bytes > r->bytes || got != sizeof(*s) + s->bytes) {
            errno = EIO;
            break;
        }
        if (s->status != HDIOS_PENDING) {
            if (verbose && (s->status || operation != HDIOS_BENCH_READ))
                fprintf(stderr, "IOS status=%lu native=%lu bytes=%lu token=%lu\n",
                        (unsigned long)s->status, (unsigned long)s->ios_status,
                        (unsigned long)s->bytes, (unsigned long)s->token);
            if (s->status) {
                win_error("IOS request", s->status);
                break;
            }
            if (r->flags == HDIOS_IN && s->bytes != r->bytes) {
                errno = EIO;
                break;
            }
            if (s->bytes)
                memcpy(data, output + sizeof(*s), s->bytes);
            if (info)
                *info = *s;
            rc = 0;
            break;
        }
        if (!s->token || operation == HDIOS_ENUM || operation == HDIOS_BENCH_READ) {
            errno = EIO;
            break;
        }
        token = s->token;
        if ((DWORD)(GetTickCount() - start) / 1000u >= (r->timeout ? r->timeout : 30)) {
            /* Kernel owns its request and all data; closing our handle cannot free them. */
            fprintf(stderr,
                    "IOS request timed out; driver retains pending I/O until completion.\n");
            errno = EIO;
            break;
        }
        ((struct hdios_request *)input)->operation = HDIOS_POLL;
        ((struct hdios_request *)input)->token = token;
        inbytes = sizeof(*r);
        Sleep(1);
    }
    free(input);
    free(output);
    return rc;
}
int win_ios_open(struct win_device *d) {
    struct hdios_request r;
    struct hdios_reply s;
    __u64 sectors;
    d->handle = open_bridge();
    if (d->handle == INVALID_HANDLE_VALUE)
        return win_error("Open HDPARM.VXD", GetLastError());
    request_init(&r, HDIOS_ENUM, d);
    if (exchange(d, &r, NULL, &s))
        return -1;
    if (s.sector_size < 512 || s.sector_size > 65536 || (s.sector_size & (s.sector_size - 1)) ||
        !s.max_transfer || s.max_transfer > HDIOS_MAX_DATA || s.max_transfer % s.sector_size) {
        errno = EIO;
        return -1;
    }
    sectors = (__u64)s.sectors_lo | ((__u64)s.sectors_hi << 32);
    if (!sectors || sectors > 0x7fffffffffffffffULL / s.sector_size) {
        errno = EIO;
        return -1;
    }
    d->size = sectors * s.sector_size;
    d->sector = d->physical_sector = s.sector_size;
    d->capabilities = s.capabilities;
    d->generation = s.generation;
    d->max_transfer = s.max_transfer;
    d->cylinders = s.cylinders;
    d->heads = s.heads;
    d->sectors = s.sectors_per_track;
    d->optical = s.type == 5;
    d->transport = "Win9x IOS (DONT_CACHE)";
    return 0;
}
int win_ios_read(struct win_device *d, void *buf, unsigned bytes, __u64 offset) {
    unsigned done = 0;
    if (!(d->capabilities & HDIOS_CAP_READ))
        return win_unsupported();
    if (!d->sector || offset > d->size || bytes > d->size - offset || offset % d->sector ||
        bytes % d->sector) {
        errno = EINVAL;
        return -1;
    }
    while (done < bytes) {
        struct hdios_request r;
        __u64 lba = (offset + done) / d->sector;
        request_init(&r, HDIOS_READ, d);
        r.lba_lo = (uint32_t)lba;
        r.lba_hi = (uint32_t)(lba >> 32);
        r.flags = HDIOS_IN;
        r.bytes = bytes - done < d->max_transfer ? bytes - done : d->max_transfer;
        r.timeout = 30;
        if (exchange(d, &r, (char *)buf + done, NULL))
            return -1;
        done += r.bytes;
    }
    return (int)done;
}
int win_ios_benchmark_block(struct win_device *d) {
    struct hdios_request r;
    struct hdios_bench_result bench;
    __u64 lba = d->position / d->sector;
    request_init(&r, HDIOS_BENCH_READ, d);
    r.flags = HDIOS_IN;
    r.bytes = sizeof(bench);
    r.lba_lo = (uint32_t)lba;
    r.lba_hi = (uint32_t)(lba >> 32);
    if (exchange(d, &r, &bench, NULL))
        return -1;
    if (bench.bytes_read != HDIOS_BENCH_BYTES ||
        bench.reads != (HDIOS_BENCH_BYTES + d->max_transfer - 1) / d->max_transfer) {
        errno = EIO;
        return -1;
    }
    d->position += bench.bytes_read;
    return (int)bench.bytes_read;
}
int win_ios_ata(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data,
                unsigned bytes, unsigned timeout) {
    struct hdios_request r;
    struct hdios_reply s;
    if (!(d->capabilities & HDIOS_CAP_ATA28) || tf->is_lba48 || dma || bytes > HDIOS_MAX_DATA)
        return win_unsupported();
    request_init(&r, HDIOS_ATA28, d);
    r.flags = bytes ? (rw == SG_WRITE ? HDIOS_OUT : HDIOS_IN) : 0;
    r.bytes = bytes;
    r.timeout = timeout;
    r.regs[0] = tf->lob.feat;
    r.regs[1] = tf->lob.nsect;
    r.regs[2] = tf->lob.lbal;
    r.regs[3] = tf->lob.lbam;
    r.regs[4] = tf->lob.lbah;
    r.regs[5] = tf->dev;
    r.regs[6] = tf->command;
    if (exchange(d, &r, data, &s))
        return -1;
    tf->error = s.regs[0];
    tf->lob.nsect = s.regs[1];
    tf->lob.lbal = s.regs[2];
    tf->lob.lbam = s.regs[3];
    tf->lob.lbah = s.regs[4];
    tf->dev = s.regs[5];
    tf->status = s.regs[6];
    if (tf->status & 0xa9) {
        errno = EIO;
        return -1;
    }
    return 0;
}
int win_ios_list(void) {
    struct win_device d;
    struct hdios_request r;
    struct hdios_reply s;
    int count = 0;
    memset(&d, 0, sizeof(d));
    d.handle = open_bridge();
    if (d.handle == INVALID_HANDLE_VALUE) {
        if (verbose)
            win_error("Open HDPARM.VXD", GetLastError());
        return 0;
    }
    for (d.unit = 0; d.unit < 256; d.unit++) {
        request_init(&r, HDIOS_ENUM, &d);
        if (exchange(&d, &r, NULL, &s))
            break;
        printf("ios:%u  IOS %.8s %.16s %.4s port=%.8s bus=%lu unit=%lu sector=%lu %s\n", d.unit,
               s.vendor, s.product, s.revision, s.port, (unsigned long)s.bus, (unsigned long)s.unit,
               (unsigned long)s.sector_size,
               s.capabilities & HDIOS_CAP_ATA28 ? "read + ATA28 (port dependent)" : "read");
        count++;
    }
    CloseHandle(d.handle);
    return count;
}
