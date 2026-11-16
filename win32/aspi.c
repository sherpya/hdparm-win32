/* Dynamically loaded ASPI. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "aspi.h"
extern int verbose;
struct aspi_api aspi_api;
#ifndef _WIN64
static HMODULE module;
typedef char aspi_layout[(sizeof(struct aspi_head) == 8 && sizeof(struct aspi_query) == 58 &&
                          offsetof(struct aspi_io, cdb) == 48)
                             ? 1
                             : -1];
static int load_aspi(void) {
    char path[MAX_PATH];
    UINT n;
    FARPROC fn;
    if (aspi_api.send && aspi_api.info)
        return 0;
    /* Resolve only the installed system ASPI manager, not a DLL in the current directory. */
    n = GetSystemDirectoryA(path, sizeof(path));
    if (!n || n + sizeof("\\WNASPI32.DLL") > sizeof(path))
        return win_unsupported();
    memcpy(path + n, "\\WNASPI32.DLL", sizeof("\\WNASPI32.DLL"));
    if (!module)
        module = LoadLibraryA(path);
    if (!module)
        return win_error("LoadLibrary(WNASPI32)", GetLastError());
    fn = GetProcAddress(module, "SendASPI32Command");
    memcpy(&aspi_api.send, &fn, sizeof(fn));
    fn = GetProcAddress(module, "GetASPI32SupportInfo");
    memcpy(&aspi_api.info, &fn, sizeof(fn));
    if (!aspi_api.send || !aspi_api.info)
        return win_unsupported();
    /* ASPI may own worker threads: keep its module loaded for process lifetime. */
    return 0;
}
static int submit(struct aspi_head *h, unsigned timeout) {
    DWORD start = GetTickCount();
    int expired = 0;
    aspi_api.send(h);
    while (h->status == 0) {
        if (!expired && (DWORD)(GetTickCount() - start) / 1000u >= timeout) {
            expired = 1;
            fprintf(stderr,
                    "ASPI timed out; waiting for completion before releasing its buffers.\n");
        }
        /* No abort/replay, stack SRB, or early buffer release while the manager owns I/O. */
        Sleep(10);
    }
    if (verbose)
        fprintf(stderr, "ASPI completion status=%02x%s\n", h->status, expired ? " (late)" : "");
    if (expired) {
        errno = EIO;
        return -1;
    }
    return 0;
}
static int type_query(unsigned adapter, unsigned target, unsigned lun, unsigned *type) {
    struct aspi_type *p = calloc(1, sizeof(*p));
    int rc = -1;
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    p->h.command = 1;
    p->h.adapter = (unsigned char)adapter;
    p->target = (unsigned char)target;
    p->lun = (unsigned char)lun;
    if (!submit(&p->h, 15)) {
        if (p->h.status == 1) {
            *type = p->type;
            rc = 0;
        } else {
            errno = p->h.status == 0x82 ? ENOENT : EIO;
        }
    }
    free(p);
    return rc;
}
#endif
int win_aspi_scsi(struct win_device *d, const unsigned char *cdb, unsigned cdb_len, int rw,
                  void *data, unsigned bytes, unsigned timeout, unsigned char *sense,
                  unsigned *sense_len, unsigned char *status) {
#ifdef _WIN64
    (void)d;
    (void)cdb;
    (void)cdb_len;
    (void)rw;
    (void)data;
    (void)bytes;
    (void)timeout;
    (void)sense;
    (void)sense_len;
    (void)status;
    return win_unsupported();
#else
    struct aspi_io *p;
    int rc = -1;
    if (cdb_len != 6 && cdb_len != 10 && cdb_len != 12 && cdb_len != 16)
        return win_unsupported();
    if (bytes > 65536 || (bytes && !data))
        return win_unsupported();
    if (load_aspi())
        return -1;
    p = calloc(1, sizeof(*p) + bytes);
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    p->h.command = 2;
    p->h.adapter = (unsigned char)d->adapter;
    p->h.flags = bytes ? (rw == SG_WRITE ? 0x10 : 0x08) : 0x18;
    p->target = (unsigned char)d->target;
    p->lun = (unsigned char)d->lun;
    p->bytes = bytes;
    p->buffer = bytes ? (void *)(p + 1) : NULL;
    p->sense_bytes = sizeof(p->sense);
    p->cdb_bytes = (unsigned char)cdb_len;
    memcpy(p->cdb, cdb, cdb_len);
    if (bytes && rw == SG_WRITE)
        memcpy(p + 1, data, bytes);
    if (submit(&p->h, timeout ? timeout : 15))
        goto done;
    if (p->h.status == 0x80) {
        rc = win_unsupported();
        goto done;
    }
    if ((p->h.status == 1 || p->h.status == 4) && !p->host_status &&
        p->sense_bytes <= sizeof(p->sense) &&
        win_scsi_unsupported(p->sense, p->sense_bytes, p->target_status)) {
        rc = win_unsupported();
        goto done;
    }
    if ((p->h.status != 1 && p->h.status != 4) || p->host_status || p->bytes != bytes ||
        p->sense_bytes > sizeof(p->sense)) {
        errno = EIO;
        goto done;
    }
    *status = p->target_status;
    if (p->h.status == 4 && !*status) {
        errno = EIO;
        goto done;
    }
    if (*sense_len > p->sense_bytes)
        *sense_len = p->sense_bytes;
    memcpy(sense, p->sense, *sense_len);
    if (bytes && rw == SG_READ)
        memcpy(data, p + 1, bytes);
    rc = 0;
done:
    free(p);
    return rc;
#endif
}
#ifndef _WIN64
static __u64 be(const unsigned char *p, unsigned n) {
    __u64 value = 0;
    while (n--)
        value = (value << 8) | *p++;
    return value;
}
#endif
static int command(struct win_device *d, unsigned char *cdb, unsigned clen, void *data,
                   unsigned bytes) {
    unsigned char sense[64], status = 0;
    unsigned len = sizeof(sense);
    if (win_aspi_scsi(d, cdb, clen, SG_READ, data, bytes, 30, sense, &len, &status))
        return -1;
    if (status) {
        errno = EIO;
        return -1;
    }
    return 0;
}
int win_aspi_open(struct win_device *d) {
#ifdef _WIN64
    (void)d;
    return win_unsupported();
#else
    unsigned char cdb[16] = {0x25}, capacity[32] = {0};
    __u64 last;
    unsigned type;
    DWORD info;
    if (load_aspi())
        return -1;
    info = aspi_api.info();
    if (((info >> 8) & 255) != 1 || d->adapter >= (info & 255)) {
        errno = ENOENT;
        return -1;
    }
    if (type_query(d->adapter, d->target, d->lun, &type))
        return -1;
    if (type != 0 && type != 5 && type != 7)
        return win_unsupported();
    d->optical = type == 5;
    if (command(d, cdb, 10, capacity, 8))
        return -1;
    last = be(capacity, 4);
    d->sector = (unsigned)be(capacity + 4, 4);
    if (last == 0xffffffffULL) {
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = 0x9e;
        cdb[1] = 0x10;
        cdb[13] = 32;
        if (command(d, cdb, 16, capacity, 32))
            return -1;
        last = be(capacity, 8);
        d->sector = (unsigned)be(capacity + 8, 4);
    }
    if (d->sector < 512 || d->sector > 65536 || (d->sector & (d->sector - 1)) ||
        last >= 0x7fffffffffffffffULL / d->sector) {
        errno = EIO;
        return -1;
    }
    d->physical_sector = d->sector;
    d->size = (last + 1) * d->sector;
    d->transport = "Win9x ASPI READ(10/16)";
    return 0;
#endif
}
int win_aspi_read(struct win_device *d, void *buf, unsigned bytes, __u64 offset) {
    unsigned done = 0;
    if (!d->sector || offset > d->size || bytes > d->size - offset || offset % d->sector ||
        bytes % d->sector) {
        errno = EINVAL;
        return -1;
    }
    while (done < bytes) {
        unsigned char cdb[16] = {0};
        unsigned count = (bytes - done > 65536 ? 65536 : bytes - done) / d->sector, i;
        __u64 lba = (offset + done) / d->sector, t = lba;
        int ext = lba + count > 0x100000000ULL;
        cdb[0] = ext ? 0x88 : 0x28;
        for (i = 0; i < (ext ? 8u : 4u); i++) {
            cdb[(ext ? 9 : 5) - i] = (unsigned char)t;
            t >>= 8;
        }
        cdb[ext ? 12 : 7] = (unsigned char)(count >> 8);
        cdb[ext ? 13 : 8] = (unsigned char)count;
        if (command(d, cdb, ext ? 16 : 10, (char *)buf + done, count * d->sector))
            return -1;
        done += count * d->sector;
    }
    return (int)done;
}
int win_aspi_list(void) {
#ifdef _WIN64
    return 0;
#else
    unsigned a, t, l, type;
    DWORD info;
    int count = 0;
    if (load_aspi())
        return 0;
    info = aspi_api.info();
    if (((info >> 8) & 255) != 1)
        return 0;
    for (a = 0; a < (info & 255); a++) {
        struct aspi_query *q = calloc(1, sizeof(*q));
        unsigned targets;
        if (!q)
            return count;
        q->h.adapter = (unsigned char)a;
        if (submit(&q->h, 15) || q->h.status != 1) {
            free(q);
            continue;
        }
        targets = (q->parameters[0] & 0x10) && q->parameters[1] ? q->parameters[1] : 8;
        if (targets > 16)
            targets = 16;
        for (t = 0; t < targets; t++) {
            if (t == q->target)
                continue;
            for (l = 0; l < 8; l++)
                if (!type_query(a, t, l, &type)) {
                    printf("aspi:%u:%u:%u  ASPI SCSI type=%u (SAT depends on target)\n", a, t, l,
                           type);
                    count++;
                }
        }
        free(q);
    }
    return count;
#endif
}
