/* HDPARM.VXD user/kernel protocol, little endian. SPDX-License-Identifier: BSD-2-Clause */
#ifndef HDPARM_IOS_PROTOCOL_H
#define HDPARM_IOS_PROTOCOL_H
#include <stdint.h>
#define HDIOS_MAGIC 0x4f494448u /* HDIO */
#define HDIOS_VERSION 1u
#define HDIOS_IOCTL 1u
#define HDIOS_MAX_DATA 65536u
#define HDIOS_ENUM 1u
#define HDIOS_READ 2u
#define HDIOS_ATA28 3u
#define HDIOS_POLL 4u
#define HDIOS_IN 1u
#define HDIOS_OUT 2u
#define HDIOS_CAP_READ 1u
#define HDIOS_CAP_ATA28 2u /* transport available; individual commands depend on port driver */
#define HDIOS_PENDING 997u
#pragma pack(push, 4)
struct hdios_request {
    uint32_t magic, version, size, operation;
    uint32_t device, generation, token, flags;
    uint32_t lba_lo, lba_hi, bytes, timeout;
    uint8_t regs[8]; /* features/count/LBA low/mid/high/device/command/reserved */
    uint32_t reserved[2];
};
struct hdios_reply {
    uint32_t magic, version, size, status;
    uint32_t device, generation, token, ios_status;
    uint32_t bytes, capabilities, sector_size, sectors_lo, sectors_hi;
    uint32_t cylinders, heads, sectors_per_track;
    uint32_t type, bus, unit, max_transfer;
    uint8_t regs[8];
    char vendor[8], product[16], revision[4], port[8];
    uint32_t reserved;
};
#pragma pack(pop)
typedef char hdios_request_size[(sizeof(struct hdios_request) == 64) ? 1 : -1];
typedef char hdios_reply_size[(sizeof(struct hdios_reply) == 128) ? 1 : -1];
#endif
