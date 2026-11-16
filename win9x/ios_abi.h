/* Minimal Windows 98 IOS ABI declarations. See SOURCES.md. SPDX-License-Identifier: GPL-3.0-only */
#ifndef HDPARM_IOS_ABI_H
#define HDPARM_IOS_ABI_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#pragma pack(push, 1)
struct ios_bdd {
    u32 next;
    u8 major, minor, subtype, bios;
    u32 flags, name;
    u32 sectors_lo, sectors_hi, sector_size, heads, cylinders, spt;
    u8 rest[64];
};
struct ios_dcb {
    u32 physical, expansion, calldown, next, next_logical;
    u8 letter, unit;
    u16 tsd_flags;
    u32 vrp, demand, flags, flags2, partition, track, bds, reserved1, identify;
    u8 shift, partition_type;
    u16 signature;
    u8 type;
    u8 common_tail[15];
    u32 max_transfer, actual_sectors[2], actual_sector_size, actual_heads, actual_cylinders,
        actual_spt;
    u32 next_ddb, devnode;
    u8 bus, channel, freeze, max_sg, pending, locks;
    u16 scsi_flags;
    u8 target, lun, hba, sense_len;
    u16 srb_size;
    u8 inquiry[8];
    char vendor[8], product[16], revision[4], port[8];
    u8 current_unit;
    u32 blocked_iop, unlock_timer;
    u8 access_timer, vol_flags, queue_algo, unit_on_controller;
    u32 port_specific, spindown_timer;
    struct ios_bdd bdd;
};
struct ios_ior {
    u32 next;
    u16 function, status;
    u32 flags, callback, start[2], count, buffer;
    u32 client, private_ios, private_port, usage[5], handle, volume, sg;
    u8 num_sg, unit;
    u16 private1;
    u32 reserved[2];
};
struct ios_iop {
    u32 physical, physical_dcb, original_dcb;
    u16 timer, original_timer;
    u32 calldown, callback, voltrk, thread, srb, reserved[2];
    u32 callbacks[12];
    u8 format_head, format_rate;
    u16 format_track;
    u32 format_count;
    struct ios_ior ior;
};
struct ios_alloc {
    u16 function, result, size;
    u32 delta, pointer;
    u8 flags, pad;
};
struct ios_free {
    u16 function, result;
    u32 pointer;
};
struct ios_next {
    u16 function, result;
    u32 previous, found;
    u8 type, pad[3];
};
struct ios_ilb {
    void (*service)(void *);
    u32 print, wait, internal, criteria, int_criteria, dvt;
    u8 tail[24];
};
struct ios_drp {
    char eye[8];
    u32 layer, event, ilb;
    char name[16];
    u8 revision;
    u32 features;
    u16 requirements;
    u8 bus;
    u16 result;
    u32 reference;
    u8 reserved[6];
};
struct dioc {
    u32 internal, vm, ddb, code, input, in_size, output, out_size, returned, overlapped, handle,
        process;
};
#pragma pack(pop)
#define ABI_ASSERT(name, expr) typedef char abi_##name[(expr) ? 1 : -1]
ABI_ASSERT(dcb, sizeof(struct ios_dcb) == 299);
ABI_ASSERT(bdd, offsetof(struct ios_dcb, bdd) == 195);
ABI_ASSERT(ior, sizeof(struct ios_ior) == 88);
ABI_ASSERT(iop, offsetof(struct ios_iop, ior) == 100);
ABI_ASSERT(alloc, sizeof(struct ios_alloc) == 16);
ABI_ASSERT(drp, sizeof(struct ios_drp) == 56);
ABI_ASSERT(ilb, sizeof(struct ios_ilb) == 52);
#define PTR(x) ((void *)(uintptr_t)(x))
#define ADDR(x) ((u32)(uintptr_t)(x))
void ios_register(struct ios_drp *);
void ios_deregister(u32 dvt);
void ios_send(struct ios_ior *, struct ios_bdd *);
void ios_callback(void);
u32 page_check(u32 page, u32 count, u32 flags);
u32 page_lock(u32 page, u32 count, u32 flags);
u32 page_unlock(u32 page, u32 count, u32 flags);
u32 gate_claim(volatile u32 *);
int vxd_init(void);
int vxd_exit(void);
u32 vxd_ioctl(struct dioc *);
void vxd_event(void *);
extern volatile u32 ios_completed;
#endif
