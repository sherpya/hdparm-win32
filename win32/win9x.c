/* Win9x transport selection. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "device.h"
#include "aspi.h"
#include "../hdparm.h"
#include "../win9x/protocol.h"
extern int verbose;
static int smart_version(HANDLE h, GETVERSIONINPARAMS *v) {
    DWORD got = 0;
    memset(v, 0, sizeof(*v));
    if (!DeviceIoControl(h, SMART_GET_VERSION, NULL, 0, v, sizeof(*v), &got, NULL))
        return win_error("SMART_GET_VERSION", GetLastError());
    if (got != sizeof(*v)) {
        errno = EIO;
        return -1;
    }
    return 0;
}
static int smart_ata(struct win_device *d, int rw, struct ata_tf *tf, void *data, unsigned bytes) {
    SENDCMDINPARAMS in;
    unsigned char output[offsetof(SENDCMDOUTPARAMS, bBuffer) + 512];
    SENDCMDOUTPARAMS *out = (SENDCMDOUTPARAMS *)output;
    DWORD got, code;
    unsigned need = bytes;
    int identify = tf->command == ATA_OP_IDENTIFY || tf->command == ATA_OP_PIDENTIFY;
    if (rw == SG_WRITE || tf->is_lba48)
        return win_unsupported();
    if (identify) {
        if (bytes != 512)
            return win_unsupported();
    } else if (tf->command == ATA_OP_SMART && tf->lob.lbam == 0x4f && tf->lob.lbah == 0xc2) {
        /* Only the documented SMART interface; vendor commands and logs are excluded. */
        if (tf->lob.feat == 0xd0 || tf->lob.feat == 0xd1) {
            if (bytes != 512)
                return win_unsupported();
        } else if (tf->lob.feat == 0xda) {
            if (bytes)
                return win_unsupported();
            need = sizeof(IDEREGS);
        } else
            return win_unsupported();
    } else
        return win_unsupported();
    memset(&in, 0, sizeof(in));
    memset(output, 0, sizeof(output));
    in.cBufferSize = bytes;
    in.bDriveNumber = (BYTE)d->unit;
    in.irDriveRegs.bFeaturesReg = tf->lob.feat;
    in.irDriveRegs.bSectorCountReg = tf->lob.nsect;
    in.irDriveRegs.bSectorNumberReg = tf->lob.lbal;
    in.irDriveRegs.bCylLowReg = tf->lob.lbam;
    in.irDriveRegs.bCylHighReg = tf->lob.lbah;
    in.irDriveRegs.bDriveHeadReg = (BYTE)(0xa0 | ((d->unit & 1) << 4));
    in.irDriveRegs.bCommandReg = tf->command;
    code = bytes ? SMART_RCV_DRIVE_DATA : SMART_SEND_DRIVE_COMMAND;
    if (!DeviceIoControl(d->handle, code, &in, offsetof(SENDCMDINPARAMS, bBuffer), out,
                         (DWORD)offsetof(SENDCMDOUTPARAMS, bBuffer) + need, &got, NULL))
        return win_error("SMARTVSD", GetLastError());
    if (got < offsetof(SENDCMDOUTPARAMS, bBuffer) + need || out->DriverStatus.bDriverError ||
        out->DriverStatus.bIDEError || (bytes && out->cBufferSize != bytes)) {
        errno = EIO;
        return -1;
    }
    tf->status = 0x50;
    tf->error = 0;
    if (bytes)
        memcpy(data, out->bBuffer, bytes);
    else {
        IDEREGS r;
        memcpy(&r, out->bBuffer, sizeof(r));
        if (r.bCommandReg & 0xa9) {
            errno = EIO;
            return -1;
        }
        tf->lob.lbam = r.bCylLowReg;
        tf->lob.lbah = r.bCylHighReg;
    }
    return 0;
}
static int smart_open(struct win_device *d) {
    GETVERSIONINPARAMS v;
    struct ata_tf tf = {0};
    __u16 id[256];
    __u64 sectors;
    d->handle = CreateFileA("\\\\.\\SMARTVSD", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (d->handle == INVALID_HANDLE_VALUE)
        return win_error("CreateFile(SMARTVSD)", GetLastError());
    if (smart_version(d->handle, &v))
        return -1;
    if (!(v.bIDEDeviceMap & (1u << d->unit))) {
        errno = ENOENT;
        return -1;
    }
    d->optical = (v.bIDEDeviceMap >> (4 + d->unit)) & 1;
    if (!(v.fCapabilities & (d->optical ? 2 : 1)))
        return win_unsupported();
    tf.command = d->optical ? ATA_OP_PIDENTIFY : ATA_OP_IDENTIFY;
    if (smart_ata(d, SG_READ, &tf, id, sizeof(id)))
        return -1;
    d->sector = d->physical_sector = 512;
    if ((id[106] & 0xd000) == 0x5000) {
        unsigned words = id[117] | ((unsigned)id[118] << 16);
        if (words < 256 || words > 32768 || (words & (words - 1))) {
            errno = EIO;
            return -1;
        }
        d->sector = d->physical_sector = words * 2;
    }
    sectors = (__u64)id[60] | ((__u64)id[61] << 16);
    if ((id[83] & 0xc400) == 0x4400)
        sectors = (__u64)id[100] | ((__u64)id[101] << 16) | ((__u64)id[102] << 32) |
                  ((__u64)id[103] << 48);
    if (sectors > 0x7fffffffffffffffULL / d->sector) {
        errno = EIO;
        return -1;
    }
    d->size = sectors * d->sector;
    d->cylinders = id[1];
    d->heads = id[3];
    d->sectors = id[6];
    d->transport = "Win9x SMARTVSD (IDENTIFY/SMART only, no sector reads)";
    return 0;
}
int win_9x_open(struct win_device *d, const char *name) {
    unsigned n, a, t, l;
    int end = 0;
    if (strlen(name) >= MAX_PATH) {
        errno = EINVAL;
        return -1;
    }
    strcpy(d->name, name);
    if (!strncmp(name, "/dev/", 5))
        name += 5;
    if (strlen(name) == 3 && name[0] == 'h' && name[1] == 'd' && name[2] >= 'a' && name[2] <= 'd') {
        d->unit = name[2] - 'a';
        d->kind = WIN_SMART;
    } else if (sscanf(name, "smart:%u%n", &n, &end) == 1 && !name[end] && n < 4) {
        d->unit = n;
        d->kind = WIN_SMART;
    } else if ((end = 0, sscanf(name, "aspi:%u:%u:%u%n", &a, &t, &l, &end)) == 3 && !name[end] &&
               a < 256 && t < 16 && l < 8) {
        d->adapter = a;
        d->target = t;
        d->lun = l;
        d->kind = WIN_ASPI;
    } else if ((end = 0, sscanf(name, "ios:%u%n", &n, &end)) == 1 && !name[end] && n < 256) {
        d->unit = n;
        d->kind = WIN_IOS;
    } else {
        errno = EINVAL;
        return -1;
    }
    if (d->kind == WIN_SMART)
        return smart_open(d);
    if (d->kind == WIN_ASPI)
        return win_aspi_open(d);
    return win_ios_open(d);
}
int win_9x_ata(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
               unsigned timeout) {
    if (d->kind == WIN_SMART)
        return smart_ata(d, rw, tf, data, bytes);
    if (d->kind == WIN_ASPI)
        return win_sat(d, rw, dma, tf, data, bytes, timeout);
    return win_ios_ata(d, rw, dma, tf, data, bytes, timeout);
}
int win_9x_read(struct win_device *d, void *buf, unsigned bytes, __u64 offset) {
    if (d->kind == WIN_ASPI)
        return win_aspi_read(d, buf, bytes, offset);
    if (d->kind == WIN_IOS)
        return win_ios_read(d, buf, bytes, offset);
    return win_unsupported();
}
int win_9x_list(void) {
    GETVERSIONINPARAMS v;
    unsigned n;
    int count = 0;
    HANDLE h = CreateFileA("\\\\.\\SMARTVSD", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (!smart_version(h, &v))
            for (n = 0; n < 4; n++)
                if (v.bIDEDeviceMap & (1u << n)) {
                    printf("smart:%u  /dev/hd%c  SMARTVSD %s (IDENTIFY/SMART, no raw reads)\n", n,
                           'a' + n, v.bIDEDeviceMap & (0x10u << n) ? "ATAPI" : "ATA");
                    count++;
                }
        CloseHandle(h);
    } else if (verbose)
        win_error("CreateFile(SMARTVSD)", GetLastError());
    count += win_aspi_list();
    count += win_ios_list();
    if (!count)
        fprintf(stderr, "No Win9x device transports found; see README.windows.md.\n");
    return 0;
}
