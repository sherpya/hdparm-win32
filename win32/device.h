/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef HDPARM_WIN_DEVICE_H
#define HDPARM_WIN_DEVICE_H
#define WIN32_LEAN_AND_MEAN
#include "../sgio.h"
#include "api.h"
#include "compat.h"
#include <stddef.h>
#include <windows.h>
#include <winioctl.h>
#define WIN_FD_BASE 10000
#define WIN_MAX_DEVICES 32
#define WIN_MAX_TRANSFER (32u * 1024u * 1024u)
struct win_device {
    HANDLE handle;
    HANDLE read_handle;
    __u64 size, base, position;
    unsigned sector, physical_sector;
    unsigned cylinders, heads, sectors;
    int volume, optical, writable;
    unsigned unavailable[6]; /* per request class: 28/48-bit x none/in/out */
    char name[MAX_PATH];
    const char *transport;
    unsigned kind, unit, adapter, target, lun;
};
struct win_device *win_device(int fd);
int win_error(const char *operation, DWORD error);
int win_unsupported(void);
int win_nt_open(struct win_device *d, const char *name);
int win_nt_read(struct win_device *d, void *buf, unsigned bytes, __u64 offset);
int win_nt_ata(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
               unsigned timeout);
int win_nt_scsi(struct win_device *d, const unsigned char *cdb, unsigned cdb_len, int rw,
                void *data, unsigned bytes, unsigned timeout, unsigned char *sense,
                unsigned *sense_len, unsigned char *status);
int win_sat(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
            unsigned timeout);
int win_scsi(struct win_device *d, const unsigned char *cdb, unsigned cdb_len, int rw, void *data,
             unsigned bytes, unsigned timeout, unsigned char *sense, unsigned *sense_len,
             unsigned char *status);
int win_is_9x(void);
#endif
