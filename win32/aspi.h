/* Win32 ASPI ABI (Adaptec SCSI request blocks). SPDX-License-Identifier: BSD-2-Clause */
#ifndef HDPARM_ASPI_H
#define HDPARM_ASPI_H
#include "device.h"
#pragma pack(push, 1)
struct aspi_head {
    unsigned char command;
    volatile unsigned char status;
    unsigned char adapter, flags;
    DWORD reserved;
};
struct aspi_query {
    struct aspi_head h;
    unsigned char adapters, target;
    char manager[16], name[16];
    unsigned char parameters[16];
};
struct aspi_type {
    struct aspi_head h;
    unsigned char target, lun, type, reserved;
};
struct aspi_io {
    struct aspi_head h;
    unsigned char target, lun, reserved[2];
    DWORD bytes;
    void *buffer;
    unsigned char sense_bytes, cdb_bytes, host_status, target_status;
    void *post;
    unsigned char workspace[20], cdb[16], sense[64];
};
#pragma pack(pop)
struct aspi_api {
    DWORD(__cdecl *send)(void *);
    DWORD(__cdecl *info)(void);
};
extern struct aspi_api aspi_api;
int win_aspi_open(struct win_device *d);
int win_aspi_list(void);
int win_aspi_read(struct win_device *d, void *buf, unsigned bytes, __u64 offset);
int win_aspi_scsi(struct win_device *d, const unsigned char *cdb, unsigned cdb_len, int rw,
                  void *data, unsigned bytes, unsigned timeout, unsigned char *sense,
                  unsigned *sense_len, unsigned char *status);
#endif
