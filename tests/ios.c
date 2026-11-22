/* IOS bridge tests: kernel services and devices are simulated. GPL-3.0-only */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win9x/ios_abi.h"
#include "win9x/protocol.h"
static struct ios_dcb disks[2];
static unsigned allocations, frees, sends, pending, fail_pages, registered_count;
static struct ios_ior *current;
static int read_in_flight;
static void complete_read(void) {
    void (*callback)(u32) = (void (*)(u32))(uintptr_t)current->callback;
    assert(read_in_flight && callback && current->handle == ADDR(current));
    read_in_flight = 0;
    callback(current->handle);
}
u32 gate_claim(volatile u32 *p) {
    u32 old = *p;
    *p = 1;
    return old;
}
u32 page_check(u32 page, u32 count, u32 flags) {
    (void)page;
    (void)flags;
    return fail_pages ? 0 : count;
}
u32 page_lock(u32 page, u32 count, u32 flags) {
    (void)count;
    (void)flags;
    return page << 12;
}
u32 page_unlock(u32 page, u32 count, u32 flags) {
    (void)page;
    (void)count;
    (void)flags;
    return 1;
}
static void service(void *packet) {
    u16 fn = *(u16 *)packet;
    if (fn == 8) {
        struct ios_next *p = packet;
        p->found = p->previous == 0                 ? ADDR(&disks[0])
                   : p->previous == ADDR(&disks[0]) ? ADDR(&disks[1])
                                                    : 0;
        p->result = p->found ? 0 : 1;
    } else if (fn == 2) {
        struct ios_alloc *p = packet;
        assert(p->delta == 16 + 100);
        assert(p->size >= p->delta + 88 + 8);
        p->pointer = ADDR(calloc(1, p->size));
        p->result = p->pointer ? 0 : 1;
        ((struct ios_ior *)PTR(p->pointer + p->delta))->private_ios = 0xc0dec0de;
        allocations++;
    } else if (fn == 4) {
        struct ios_free *p = packet;
        assert(!read_in_flight);
        free(PTR(p->pointer));
        frees++;
        current = NULL;
    } else
        assert(0);
}
void ios_register(struct ios_drp *p) {
    struct ios_ilb *b = PTR(p->ilb);
    p->result = 1;
    b->service = service;
    b->dvt = 1234;
    registered_count++;
}
void ios_deregister(u32 dvt) {
    assert(dvt == 1234);
    registered_count--;
}
void ios_send(struct ios_ior *p, struct ios_bdd *bdd) {
    sends++;
    current = p;
    assert(p->private_ios == 0xc0dec0de);
    assert(bdd == &disks[0].bdd || bdd == &disks[1].bdd);
    if (p->function == 0) {
        assert(p->flags & 0x40000000u); /* physical */
        assert(p->flags & 0x40);        /* IOS can double buffer reads */
        assert(!(p->flags & 0x100));
        read_in_flight = 1;
        memset(PTR(p->buffer), 0x5a, p->count * bdd->sector_size);
    } else {
        unsigned char *regs = PTR(p->usage[0]);
        assert(p->function == 0x2b && !p->callback && !p->handle);
        assert(regs[5] == 0xa0);
        if (regs[6] == 0xec) {
            /* The stock SMARTVSD request for a 512-byte IDENTIFY reply. */
            assert(p->flags == 0x200501 && p->count == 1);
            memset(PTR(p->buffer), 0xa5, p->count * 512);
        } else {
            assert(regs[6] == 0xe5 && p->flags == 0x501 && !p->count && !p->buffer);
        }
        regs[0] = 0;
        regs[6] = 0x50;
    }
    p->status = 0;
    if (read_in_flight && !pending)
        complete_read();
}
static struct dioc di;
static struct hdios_request *r;
static struct hdios_reply *s;
static u32 *got;
static void request(u32 op, u32 dev, u32 bytes) {
    memset(r, 0, sizeof(*r) + HDIOS_MAX_DATA);
    r->magic = HDIOS_MAGIC;
    r->version = HDIOS_VERSION;
    r->size = sizeof(*r);
    r->operation = op;
    r->device = dev;
    r->generation = 1;
    r->bytes = bytes;
    r->flags = bytes ? HDIOS_IN : 0;
    di.code = HDIOS_IOCTL;
    di.in_size = sizeof(*r);
    di.out_size = sizeof(*s) + bytes;
    di.handle = 27;
    di.process = 35;
    *got = 0;
}
int main(void) {
    unsigned n, token, before;
    /* Mock the Win9x user arena even on systems whose CRT heap is below 4 MiB. */
    r = VirtualAlloc((void *)0x10000000, 3 * 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(r);
    s = (void *)((unsigned char *)r + 65536 + sizeof(*r));
    got = (void *)((unsigned char *)s + 65536 + sizeof(*s));
    assert(r && s && got);
    di.input = ADDR(r);
    di.output = ADDR(s);
    di.returned = ADDR(got);
    for (n = 0; n < 2; n++) {
        disks[n].flags = 0x8000;
        disks[n].expansion = 16;
        disks[n].max_sg = 4;
        disks[n].bdd.sector_size = n ? 4096 : 512;
        disks[n].bdd.sectors_lo = 0x2000000;
        disks[n].max_transfer = 65536;
    }
    assert(vxd_init() == 0 && registered_count == 1);
    request(HDIOS_ENUM, 0, 0);
    assert(vxd_ioctl(&di) == 0 && *got == sizeof(*s) && s->sector_size == 512);
    request(HDIOS_ENUM, 1, 0);
    assert(vxd_ioctl(&di) == 0 && s->sector_size == 4096);
    request(HDIOS_ENUM, 2, 0);
    assert(vxd_ioctl(&di) == 18);
    request(HDIOS_READ, 0, 512);
    r->lba_lo = 0x800000;
    assert(vxd_ioctl(&di) == 0 && s->bytes == 512 && *((u8 *)(s + 1)) == 0x5a);
    assert(allocations == frees && sends == 1);
    request(HDIOS_READ, 1, 512);
    assert(vxd_ioctl(&di) == 87 && sends == 1);
    request(HDIOS_READ, 1, 4096);
    assert(vxd_ioctl(&di) == 0 && s->bytes == 4096);
    request(HDIOS_READ, 0, 512);
    r->lba_hi = 1;
    assert(vxd_ioctl(&di) == 87);
    request(HDIOS_ATA28, 0, 512);
    r->regs[6] = 0x24;
    assert(vxd_ioctl(&di) == 50);
    r->regs[6] = 0xec;
    assert(vxd_ioctl(&di) == 0 && s->regs[6] == 0x50 && *((u8 *)(s + 1)) == 0xa5);
    request(HDIOS_ATA28, 0, 0);
    r->regs[6] = 0xe5;
    assert(vxd_ioctl(&di) == 0 && !s->status && !s->bytes);
    request(HDIOS_ATA28, 1, 512);
    r->regs[6] = 0xec;
    assert(vxd_ioctl(&di) == 50); /* no ESDI_506 data transfers on a 4 KiB disk */
    request(HDIOS_READ, 0, 512);
    pending = 1;
    assert(vxd_ioctl(&di) == 0 && s->status == HDIOS_PENDING && !s->bytes);
    token = s->token;
    before = sends;
    assert(allocations == frees + 1 && vxd_exit() == 1);
    request(HDIOS_POLL, 0, 512);
    r->token = token;
    assert(vxd_ioctl(&di) == 0 && s->status == HDIOS_PENDING && sends == before);
    di.handle++;
    assert(vxd_ioctl(&di) == 87);
    di.handle--;
    current->status = 0x1e;
    complete_read();
    assert(vxd_ioctl(&di) == 0 && s->status == 31 && !s->bytes && s->ios_status == 0x1e);
    assert(allocations == frees);
    request(HDIOS_READ, 0, 512);
    assert(vxd_ioctl(&di) == 0 && s->status == HDIOS_PENDING);
    di.code = 0xffffffffu;
    assert(vxd_ioctl(&di) == 0); /* close while pending */
    assert(allocations == frees + 1 && vxd_exit() == 1);
    complete_read();
    pending = 0;
    request(HDIOS_ENUM, 0, 0);
    assert(vxd_ioctl(&di) == 0 && allocations == frees);
    request(HDIOS_READ, 0, 512);
    r->magic = 0;
    assert(vxd_ioctl(&di) == 87);
    request(HDIOS_READ, 0, 512);
    r->bytes = HDIOS_MAX_DATA + 1;
    assert(vxd_ioctl(&di) == 87);
    request(HDIOS_READ, 0, 512);
    di.out_size = sizeof(*s);
    assert(vxd_ioctl(&di) == 87);
    request(HDIOS_ENUM, 0, 0);
    fail_pages = 1;
    assert(vxd_ioctl(&di) == 998);
    fail_pages = 0;
    {
        u16 event[2] = {4, 0};
        vxd_event(event);
    }
    request(HDIOS_READ, 0, 512);
    assert(vxd_ioctl(&di) == 21); /* stale device generation */
    assert(vxd_exit() == 0 && allocations == frees && !registered_count);
    VirtualFree(r, 0, MEM_RELEASE);
    puts("IOS bridge tests passed (mock kernel, no real devices).");
    return 0;
}
