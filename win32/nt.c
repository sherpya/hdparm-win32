/* NT storage transport. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "../hdparm.h"
#include "device.h"
#include <ntddscsi.h>
extern int verbose;

static int ctl(HANDLE h, DWORD code, void *in, DWORD insz, void *out, DWORD outsz, DWORD *got) {
    *got = 0;
    if (DeviceIoControl(h, code, in, insz, out, outsz, got, NULL))
        return 0;
    return win_error("DeviceIoControl", GetLastError());
}
static int geometry(struct win_device *d) {
    DISK_GEOMETRY g;
    GET_LENGTH_INFORMATION length;
    DWORD got;
    memset(&g, 0, sizeof(g));
    if (ctl(d->handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &g, sizeof(g), &got))
        return -1;
    if (got < sizeof(g) || !g.BytesPerSector || (g.BytesPerSector & (g.BytesPerSector - 1)) ||
        g.BytesPerSector > 65536) {
        errno = EIO;
        return -1;
    }
    d->sector = d->physical_sector = g.BytesPerSector;
    d->cylinders = (unsigned)g.Cylinders.QuadPart;
    d->heads = g.TracksPerCylinder;
    d->sectors = g.SectorsPerTrack;
    if (ctl(d->handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &length, sizeof(length), &got)) {
        if (errno != ENOSYS)
            return -1;
        /* Older stacks expose only CHS. This is a conservative read bound. */
        if (g.Cylinders.QuadPart <= 0 || !g.TracksPerCylinder || !g.SectorsPerTrack ||
            (unsigned long long)g.Cylinders.QuadPart >
                0x7fffffffffffffffULL / g.TracksPerCylinder / g.SectorsPerTrack / g.BytesPerSector)
            return win_unsupported();
        d->size = (__u64)g.Cylinders.QuadPart * g.TracksPerCylinder * g.SectorsPerTrack *
                  g.BytesPerSector;
    } else {
        if (got < sizeof(length) || length.Length.QuadPart <= 0) {
            errno = EIO;
            return -1;
        }
        d->size = (__u64)length.Length.QuadPart;
    }
    return 0;
}
int win_nt_open(struct win_device *d, const char *name) {
    DWORD error;
    char raw[MAX_PATH];
    strcpy(raw, name);
    d->handle = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    d->writable = d->handle != INVALID_HANDLE_VALUE;
    if (!d->writable)
        d->handle = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
    if (d->handle == INVALID_HANDLE_VALUE)
        return win_error("CreateFile", GetLastError());
    if (geometry(d))
        return -1;
    if (d->volume) {
        VOLUME_DISK_EXTENTS e;
        DWORD got;
        memset(&e, 0, sizeof(e));
        if (ctl(d->handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &e, sizeof(e), &got)) {
            if (errno != ENOSYS)
                return -1;
            /* NT4 has no extent query: read the volume itself, with partition bounds. */
            PARTITION_INFORMATION pi;
            if (ctl(d->handle, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0, &pi, sizeof(pi), &got))
                return -1;
            if (got < sizeof(pi) || pi.PartitionLength.QuadPart <= 0) {
                errno = EIO;
                return -1;
            }
            d->size = (__u64)pi.PartitionLength.QuadPart;
        } else {
            if (got < sizeof(e) || e.NumberOfDiskExtents != 1 ||
                e.Extents[0].StartingOffset.QuadPart < 0 || e.Extents[0].ExtentLength.QuadPart <= 0)
                return win_unsupported();
            d->base = (__u64)e.Extents[0].StartingOffset.QuadPart;
            d->size = (__u64)e.Extents[0].ExtentLength.QuadPart;
            snprintf(raw, sizeof(raw), "\\\\.\\PhysicalDrive%lu", e.Extents[0].DiskNumber);
        }
    }
    d->read_handle = CreateFileA(raw, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (d->read_handle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return win_error("CreateFile(unbuffered)", error);
    }
    d->transport = "NT unbuffered";
    return 0;
}
int win_nt_read(struct win_device *d, void *buf, unsigned bytes, __u64 offset) {
    LONG high;
    DWORD low, got;
    __u64 absolute;
    if (offset > d->size || bytes > d->size - offset || offset > 0x7fffffffffffffffULL - d->base ||
        bytes % d->sector || offset % d->sector) {
        errno = EINVAL;
        return -1;
    }
    absolute = d->base + offset;
    high = (LONG)(absolute >> 32);
    SetLastError(NO_ERROR);
    low = SetFilePointer(d->read_handle, (LONG)absolute, &high, FILE_BEGIN);
    if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
        return win_error("SetFilePointer", GetLastError());
    if (!ReadFile(d->read_handle, buf, bytes, &got, NULL))
        return win_error("ReadFile", GetLastError());
    if (got != bytes) {
        errno = EIO;
        return -1;
    }
    return (int)got;
}
static void tf_regs(unsigned char r[8], const struct ata_tf *tf, int high) {
    const struct ata_lba_regs *p = high ? &tf->hob : &tf->lob;
    r[0] = p->feat;
    r[1] = p->nsect;
    r[2] = p->lbal;
    r[3] = p->lbam;
    r[4] = p->lbah;
    r[5] = high ? 0 : tf->dev;
    r[6] = high ? 0 : tf->command;
    r[7] = 0;
}
static void regs_tf(struct ata_tf *tf, const unsigned char r[8], int high) {
    struct ata_lba_regs *p = high ? &tf->hob : &tf->lob;
    p->feat = r[0];
    p->nsect = r[1];
    p->lbal = r[2];
    p->lbam = r[3];
    p->lbah = r[4];
    if (!high) {
        tf->error = r[0];
        tf->dev = r[5];
        tf->status = r[6];
    }
}
static int ata_ex(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data,
                  unsigned bytes, unsigned timeout) {
    size_t offset = (sizeof(ATA_PASS_THROUGH_EX) + 15u) & ~15u;
    DWORD size = (DWORD)(offset + bytes), got;
    ATA_PASS_THROUGH_EX *p = calloc(1, size);
    int rc = -1;
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    p->Length = sizeof(*p);
    p->TimeOutValue = timeout;
    p->DataTransferLength = bytes;
    p->DataBufferOffset = bytes ? offset : 0;
    p->AtaFlags = ATA_FLAGS_DRDY_REQUIRED | (tf->is_lba48 ? ATA_FLAGS_48BIT_COMMAND : 0);
    if (bytes)
        p->AtaFlags |= rw == SG_WRITE ? ATA_FLAGS_DATA_OUT : ATA_FLAGS_DATA_IN;
    if (dma)
        p->AtaFlags |= ATA_FLAGS_USE_DMA;
    tf_regs(p->CurrentTaskFile, tf, 0);
    tf_regs(p->PreviousTaskFile, tf, 1);
    if (bytes && rw == SG_WRITE)
        memcpy((char *)p + offset, data, bytes);
    if (ctl(d->handle, IOCTL_ATA_PASS_THROUGH, p, size, p, size, &got))
        goto done;
    if (got < sizeof(*p) || p->Length != sizeof(*p)) {
        errno = EIO;
        goto done;
    }
    regs_tf(tf, p->CurrentTaskFile, 0);
    if (tf->is_lba48)
        regs_tf(tf, p->PreviousTaskFile, 1);
    if (tf->status & 0xa9) {
        errno = EIO;
        goto done;
    } /* BSY, DF, DRQ, ERR */
    if (bytes && (p->DataTransferLength != bytes ||
                  (rw == SG_READ && (got < offset || got - offset < bytes)))) {
        errno = EIO;
        goto done;
    }
    if (bytes && rw == SG_READ)
        memcpy(data, (char *)p + offset, bytes);
    rc = 0;
done:
    free(p);
    return rc;
}
#pragma pack(push, 1)
struct legacy_ide {
    unsigned char regs[8];
    DWORD bytes;
    unsigned char data[1];
};
#pragma pack(pop)
static int ata_legacy(struct win_device *d, int rw, struct ata_tf *tf, void *data, unsigned bytes) {
    struct legacy_ide *p;
    DWORD got, size = (DWORD)offsetof(struct legacy_ide, data) + bytes;
    int rc = -1;
    if (tf->is_lba48 || rw == SG_WRITE || bytes > 512)
        return win_unsupported();
    p = calloc(1, size);
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    tf_regs(p->regs, tf, 0);
    p->bytes = bytes;
    if (ctl(d->handle, 0x0004d028, p, size, p, size, &got))
        goto done;
    if (got < offsetof(struct legacy_ide, data)) {
        errno = EIO;
        goto done;
    }
    regs_tf(tf, p->regs, 0);
    if ((tf->status & 0xa9) || (bytes && (got < size || p->bytes != bytes))) {
        errno = EIO;
        goto done;
    }
    if (bytes)
        memcpy(data, p->data, bytes);
    rc = 0;
done:
    free(p);
    return rc;
}
static int ata_smart(struct win_device *d, struct ata_tf *tf, void *data, unsigned bytes) {
    SENDCMDINPARAMS in;
    unsigned char out[sizeof(SENDCMDOUTPARAMS) + 512];
    SENDCMDOUTPARAMS *p = (SENDCMDOUTPARAMS *)out;
    DWORD got;
    if (tf->is_lba48 || (tf->command != ATA_OP_IDENTIFY && tf->command != ATA_OP_PIDENTIFY) ||
        bytes != 512)
        return win_unsupported();
    memset(&in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    tf_regs((unsigned char *)&in.irDriveRegs, tf, 0);
    in.irDriveRegs.bDriveHeadReg = 0xa0;
    in.cBufferSize = 512;
    if (ctl(d->handle, SMART_RCV_DRIVE_DATA, &in, sizeof(in) - 1, out, sizeof(out), &got))
        return -1;
    if (got < offsetof(SENDCMDOUTPARAMS, bBuffer) + 512 || p->cBufferSize != 512 ||
        p->DriverStatus.bDriverError || p->DriverStatus.bIDEError) {
        errno = EIO;
        return -1;
    }
    memcpy(data, p->bBuffer, 512);
    tf->status = 0x50;
    tf->error = 0;
    return 0;
}
int win_nt_ata(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
               unsigned timeout) {
    unsigned cls = (tf->is_lba48 ? 3 : 0) + (bytes ? (rw == SG_WRITE ? 2 : 1) : 0), i;
    struct ata_tf original = *tf;
    if (d->volume)
        return win_unsupported();
    for (i = 0; i < 4; i++) {
        int rc;
        if (d->unavailable[cls] & (1u << i))
            continue;
        if (i == 2 && (original.is_lba48 || rw == SG_WRITE || bytes > 512))
            continue;
        if (i == 3 &&
            (rw == SG_WRITE || original.is_lba48 || bytes != 512 ||
             (original.command != ATA_OP_IDENTIFY && original.command != ATA_OP_PIDENTIFY)))
            continue;
        *tf = original;
        if (verbose)
            fprintf(stderr, "Windows ATA method=%s timeout=%u bytes=%u\n",
                    i == 0   ? "ATA_PASS_THROUGH"
                    : i == 1 ? "SAT"
                    : i == 2 ? "IDE_PASS_THROUGH"
                             : "SMART",
                    timeout, bytes);
        if (i == 0)
            rc = ata_ex(d, rw, dma, tf, data, bytes, timeout);
        else if (i == 1)
            rc = win_sat(d, rw, dma, tf, data, bytes, timeout);
        else if (i == 2)
            rc = ata_legacy(d, rw, tf, data, bytes);
        else
            rc = ata_smart(d, tf, data, bytes);
        if (rc == 0 || errno != ENOSYS)
            return rc;
        d->unavailable[cls] |= 1u << i;
    }
    *tf = original;
    return win_unsupported();
}
int win_nt_scsi(struct win_device *d, const unsigned char *cdb, unsigned cdb_len, int rw,
                void *data, unsigned bytes, unsigned timeout, unsigned char *sense,
                unsigned *sense_len, unsigned char *status) {
    size_t offset = (sizeof(SCSI_PASS_THROUGH) + 64u + 15u) & ~15u;
    DWORD size = (DWORD)(offset + bytes), got;
    SCSI_PASS_THROUGH *p;
    int rc = -1;
    if (d->volume)
        return win_unsupported();
    if (cdb_len > 16 || !cdb_len || bytes > WIN_MAX_TRANSFER)
        return win_unsupported();
    p = calloc(1, size);
    if (!p) {
        errno = ENOMEM;
        return -1;
    }
    p->Length = sizeof(*p);
    p->CdbLength = (UCHAR)cdb_len;
    p->SenseInfoLength = 64;
    p->SenseInfoOffset = sizeof(*p);
    p->DataIn = bytes ? (rw == SG_WRITE ? SCSI_IOCTL_DATA_OUT : SCSI_IOCTL_DATA_IN)
                      : SCSI_IOCTL_DATA_UNSPECIFIED;
    p->DataTransferLength = bytes;
    p->TimeOutValue = timeout;
    p->DataBufferOffset = bytes ? offset : 0;
    memcpy(p->Cdb, cdb, cdb_len);
    if (bytes && rw == SG_WRITE)
        memcpy((char *)p + offset, data, bytes);
    if (ctl(d->handle, IOCTL_SCSI_PASS_THROUGH, p, size, p, size, &got))
        goto done;
    if (got < sizeof(*p) || p->Length != sizeof(*p) || p->SenseInfoOffset != sizeof(*p) ||
        p->SenseInfoLength > 64 || (p->SenseInfoLength && got < sizeof(*p) + p->SenseInfoLength)) {
        errno = EIO;
        goto done;
    }
    *status = p->ScsiStatus;
    if (*sense_len > p->SenseInfoLength)
        *sense_len = p->SenseInfoLength;
    memcpy(sense, (char *)p + sizeof(*p), *sense_len);
    if (bytes && (p->DataTransferLength != bytes || (rw == SG_READ && got < offset + bytes))) {
        errno = EIO;
        goto done;
    }
    if (bytes && rw == SG_READ)
        memcpy(data, (char *)p + offset, bytes);
    rc = 0;
done:
    free(p);
    return rc;
}
