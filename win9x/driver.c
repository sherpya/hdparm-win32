/* HDPARM.VXD: bounded IOS client; no controller port I/O. SPDX-License-Identifier: GPL-3.0-only */
#include "ios_abi.h"
#include "protocol.h"
#define MAX_DEVICES 256u
#define IORF_HIGH_PRIORITY 0x1u
#define IORF_DONT_CACHE 0x4u
#define IORF_BYPASS_VOLTRK 0x8u
#define IORF_DOUBLE_BUFFER 0x40u
#define IORF_SYNC_COMMAND 0x100u
#define IORF_VERSION_002 0x400u
#define IORF_DATA_IN 0x200000u
#define IORF_DATA_OUT 0x400000u
#define IORF_PHYS_CMD 0x40000000u
static struct ios_ilb ilb;
static struct ios_drp drp;
static struct ios_dcb *devices[MAX_DEVICES];
static u32 device_count, snapshot, generation = 1, registered;
static volatile u32 gate;
static volatile u32 ios_completed;
static u32 allocation, owner, process, token, next_token, orphan;
static struct ios_ior *ior;
static struct hdios_request active;
static struct hdios_reply result;
static u8 data_buffer[HDIOS_MAX_DATA];
static u8 taskfile[8];
/* IOS pushes IOR_req_req_handle and pops it after this cdecl callback returns. */
static void read_completed(u32 handle) {
    (void)handle;
    ios_completed = 1;
}
static void zero(void *p, u32 n) {
    u8 *q = p;
    while (n--)
        *q++ = 0;
}
static void copy(void *p, const void *s, u32 n) {
    u8 *q = p;
    const u8 *t = s;
    while (n--)
        *q++ = *t++;
}
void vxd_event(void *packet) {
    u16 *p = packet;
    if (p[0] == 3 || p[0] == 4 || p[0] == 15)
        ++generation;
    p[1] = 0;
}
int vxd_init(void) {
    zero(&drp, sizeof(drp));
    zero(&ilb, sizeof(ilb));
    copy(drp.eye, "XXXXXXXX", 8);
    copy(drp.name, "hdparm IOS", 10);
    drp.layer = 1u << 8;
    drp.event = ADDR(vxd_event);
    drp.ilb = ADDR(&ilb);
    drp.revision = 1;
    drp.requirements = 0xff;
    ios_register(&drp);
    registered = drp.result == 1 && ilb.service && ilb.dvt;
    return registered ? 0 : 1;
}
static void release(void) {
    if (allocation) {
        struct ios_free p = {4, 0, 0};
        p.pointer = allocation;
        ilb.service(&p);
    }
    allocation = owner = process = token = orphan = 0;
    ior = 0;
    ios_completed = 0;
}
int vxd_exit(void) {
    if (gate_claim(&gate))
        return 1;
    if (allocation && !ios_completed) {
        gate = 0;
        return 1;
    }
    release();
    if (registered)
        ios_deregister(ilb.dvt);
    registered = 0;
    gate = 0;
    return 0;
}
static int enumerate(void) {
    struct ios_next p;
    u32 n, epoch = generation;
    zero(&p, sizeof(p));
    p.function = 8;
    p.type = 0xff;
    device_count = 0;
    for (n = 0; n < 4096; n++) {
        struct ios_dcb *d;
        p.result = 0;
        p.found = 0;
        ilb.service(&p);
        if (p.result || !p.found)
            break;
        if (p.found == p.previous)
            return 31;
        d = PTR(p.found);
        if (d->flags & 0x8000) {
            if (device_count == MAX_DEVICES)
                return 8;
            devices[device_count++] = d;
        }
        p.previous = p.found;
    }
    if (n == 4096 || epoch != generation)
        return 21;
    snapshot = epoch;
    return 0;
}
static void describe(struct ios_dcb *d, struct hdios_reply *s) {
    u32 limit = d->max_transfer;
    s->sector_size = d->bdd.sector_size;
    s->sectors_lo = d->bdd.sectors_lo;
    s->sectors_hi = d->bdd.sectors_hi;
    s->heads = d->bdd.heads;
    s->cylinders = d->bdd.cylinders;
    s->sectors_per_track = d->bdd.spt;
    s->type = d->type;
    s->bus = d->bus;
    s->unit = d->unit;
    s->capabilities = HDIOS_CAP_READ | HDIOS_CAP_BENCH_READ;
    /* Only ESDI/IDE disk transports. No SCSI/BIOS/ASPI index inference. */
    if (d->bus == 0 && d->type == 0 && !(d->flags & 0x2000) && s->sector_size == 512 &&
        (!d->actual_sector_size || d->actual_sector_size == 512))
        s->capabilities |= HDIOS_CAP_ATA28;
    if (!limit || limit > HDIOS_MAX_DATA)
        limit = HDIOS_MAX_DATA;
    s->max_transfer = s->sector_size ? limit - limit % s->sector_size : 0;
    copy(s->vendor, d->vendor, 8);
    copy(s->product, d->product, 16);
    copy(s->revision, d->revision, 4);
    copy(s->port, d->port, 8);
}
static int extended_command(u8 cmd) {
    switch (cmd) {
    case 0x06:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2f:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x42:
    case 0x45:
    case 0x47:
    case 0x57:
    case 0x60:
    case 0x61:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0xea:
        return 1;
    default:
        return 0;
    }
}
static int start(const struct hdios_request *r, const void *payload, struct dioc *di,
                 int sync_read) {
    struct ios_alloc p;
    struct ios_dcb *d = devices[r->device];
    struct ios_iop *iop;
    u32 delta, size;
    uint64_t lba = ((uint64_t)r->lba_hi << 32) | r->lba_lo;
    uint64_t sectors = ((uint64_t)result.sectors_hi << 32) | result.sectors_lo;
    if (r->flags > HDIOS_OUT || r->bytes > HDIOS_MAX_DATA || (r->bytes && !r->flags) ||
        (!r->bytes && r->flags) || r->regs[7] || r->reserved[0] || r->reserved[1])
        return 87;
    if (r->operation == HDIOS_READ) {
        if (!result.sector_size || result.sector_size > HDIOS_MAX_DATA ||
            (result.sector_size & (result.sector_size - 1)) || r->flags != HDIOS_IN || !r->bytes ||
            r->bytes > result.max_transfer || r->bytes % result.sector_size || lba > sectors ||
            r->bytes / result.sector_size > sectors - lba)
            return 87;
    } else if (r->operation == HDIOS_ATA28) {
        if (!(result.capabilities & HDIOS_CAP_ATA28) || extended_command(r->regs[6]) ||
            r->bytes > result.max_transfer || r->bytes % 512)
            return 50;
    } else
        return 50;
    if (d->expansion > 60000)
        return 8;
    delta = d->expansion + offsetof(struct ios_iop, ior);
    size = delta + sizeof(struct ios_ior) + 8u * (d->max_sg ? d->max_sg : 1);
    if (size > 65535)
        return 8;
    zero(&p, sizeof(p));
    p.function = 2;
    p.size = (u16)size;
    p.delta = delta;
    ilb.service(&p);
    if (p.result || !p.pointer)
        return 8;
    allocation = p.pointer;
    iop = PTR(allocation + d->expansion);
    ior = PTR(allocation + delta);
    /* ISP_CREATE_IOP initializes IOS-owned fields; initialize only the IOR and our fields. */
    ior->next = 0;
    ior->status = 0;
    ior->start[0] = ior->start[1] = 0;
    ior->volume = 0;
    ior->num_sg = 0;
    iop->physical_dcb = iop->original_dcb = ADDR(d);
    ior->flags = IORF_HIGH_PRIORITY | IORF_VERSION_002;
    ior->unit = d->unit;
    ior->sg = ADDR(ior + 1);
    ior->buffer = r->bytes ? ADDR(data_buffer) : 0;
    zero(data_buffer, r->bytes);
    if (r->flags == HDIOS_OUT)
        copy(data_buffer, payload, r->bytes);
    if (r->operation == HDIOS_READ) {
        ior->function = 0;
        ior->start[0] = r->lba_lo;
        ior->start[1] = r->lba_hi;
        ior->count = r->bytes / result.sector_size;
        ior->flags |= IORF_PHYS_CMD | IORF_DONT_CACHE | IORF_BYPASS_VOLTRK |
                      IORF_DOUBLE_BUFFER | IORF_DATA_IN;
        if (sync_read) {
            ior->flags |= IORF_SYNC_COMMAND;
            ior->callback = ior->handle = 0;
        } else {
            ior->callback = ADDR(read_completed);
            ior->handle = ADDR(ior);
        }
    } else {
        ior->function = 0x2b;
        /* ESDI_506 takes a sector count, including for IDENTIFY (one sector).
         * SMARTVSD submits this request synchronously, with a linear data buffer. */
        ior->count = r->bytes / 512;
        ior->flags |= IORF_SYNC_COMMAND;
        ior->callback = ior->handle = 0;
        if (r->flags)
            ior->flags |= r->flags == HDIOS_IN ? IORF_DATA_IN : IORF_DATA_OUT;
        copy(taskfile, r->regs, 8);
        taskfile[5] = (u8)(0xa0 | (taskfile[5] & 0x4f) | ((d->unit_on_controller & 1) << 4));
        ior->usage[0] = ADDR(taskfile);
    }
    active = *r;
    owner = di->handle;
    process = di->process;
    orphan = 0;
    token = ++next_token;
    if (!token)
        token = ++next_token;
    result.token = token;
    ios_completed = 0;
    ios_send(ior, &d->bdd);
    if (sync_read || r->operation == HDIOS_ATA28)
        ios_completed = 1;
    return 0;
}
static void complete(void) {
    result.ios_status = ior->status;
    result.status = ior->status < 0x10 ? 0 : ior->status == 0x16 ? 50 : 31;
    if (active.operation == HDIOS_ATA28) {
        copy(result.regs, taskfile, 8);
        if (!result.status && (taskfile[6] & 0xa9))
            result.status = 31;
    }
    /* IOS completion is all-or-error, unlike Win32's byte-counted reads. */
    if (!result.status && ior->count != active.bytes / result.sector_size)
        result.status = 31;
    result.bytes = (!result.status && active.flags == HDIOS_IN) ? active.bytes : 0;
}
static u32 benchmark_block(const struct hdios_request *r, struct dioc *di) {
    struct hdios_request read = *r;
    struct hdios_bench_result bench = {0};
    uint64_t lba = ((uint64_t)r->lba_hi << 32) | r->lba_lo;
    uint64_t capacity = ((uint64_t)result.sectors_hi << 32) | result.sectors_lo;
    u32 sector = result.sector_size, limit = result.max_transfer;
    if (r->flags != HDIOS_IN || r->bytes != sizeof(bench) || r->token ||
        sector < 512 || sector > HDIOS_MAX_DATA || (sector & (sector - 1)) || !limit ||
        lba > capacity || HDIOS_BENCH_BYTES / sector > capacity - lba)
        return 87;
    read.operation = HDIOS_READ;
    while (bench.bytes_read < HDIOS_BENCH_BYTES) {
        u32 error, remaining = HDIOS_BENCH_BYTES - bench.bytes_read;
        read.bytes = remaining < limit ? remaining : limit;
        read.lba_lo = (u32)lba;
        read.lba_hi = (u32)(lba >> 32);
        error = start(&read, 0, di, 1);
        if (error)
            return error;
        complete();
        release();
        result.token = 0;
        if (generation != r->generation)
            return 21;
        if (result.status)
            return 0; /* deliver the IOS error in the reply */
        bench.reads++;
        bench.bytes_read += result.bytes;
        lba += result.bytes / sector;
    }
    copy(data_buffer, &bench, sizeof(bench));
    result.bytes = sizeof(bench);
    return 0;
}
static u32 dispatch(struct dioc *di, const struct hdios_request *r, const void *payload, void *out,
                    u32 *returned) {
    u32 error = 0;
    struct hdios_reply reply;
    if (gate_claim(&gate))
        return 170;
    if (orphan && ios_completed)
        release();
    if (r->operation == HDIOS_POLL) {
        if (!allocation || token != r->token || owner != di->handle || process != di->process ||
            active.device != r->device || active.generation != r->generation ||
            active.bytes != r->bytes || active.flags != r->flags) {
            error = 87;
            goto done;
        }
    } else {
        if (allocation) {
            error = 170;
            goto done;
        }
        if (snapshot != generation && (error = enumerate()))
            goto done;
        zero(&result, sizeof(result));
        result.magic = HDIOS_MAGIC;
        result.version = HDIOS_VERSION;
        result.size = sizeof(result);
        result.device = r->device;
        result.generation = generation;
        if (r->device >= device_count) {
            error = 18;
            goto done;
        }
        if (r->operation != HDIOS_ENUM && r->generation != generation) {
            error = 21;
            goto done;
        }
        describe(devices[r->device], &result);
        if (r->operation == HDIOS_BENCH_READ)
            error = benchmark_block(r, di);
        else if (r->operation != HDIOS_ENUM)
            error = start(r, payload, di, 0);
        if (error)
            goto done;
    }
    reply = result;
    if (allocation) {
        if (ios_completed) {
            complete();
            reply = result;
        } else {
            reply.status = HDIOS_PENDING;
            reply.bytes = 0;
        }
    }
    if (di->out_size < sizeof(reply) + reply.bytes) {
        error = 122;
        goto done;
    }
    copy(out, &reply, sizeof(reply));
    if (reply.bytes)
        copy((u8 *)out + sizeof(reply), data_buffer, reply.bytes);
    *returned = sizeof(reply) + reply.bytes;
    /* An interrupt can complete the I/O after we prepared a pending reply.
     * Keep its result and buffer for the poll that consumes the token. */
    if (allocation && reply.status != HDIOS_PENDING)
        release();
done:
    gate = 0;
    return error;
}
static void *lock_buffer(u32 address, u32 bytes) {
    u32 pages, mapped;
    if (!bytes || address < 0x400000 || address >= 0xc0000000 || bytes > 0xc0000000 - address)
        return 0;
    pages = ((address & 4095) + bytes + 4095) >> 12;
    if (page_check(address >> 12, pages, 0) < pages)
        return 0;
    mapped = page_lock(address >> 12, pages, 0x40000000);
    return mapped ? PTR(mapped + (address & 4095)) : 0;
}
static void unlock_buffer(void *p, u32 bytes) {
    u32 a = ADDR(p);
    if (p)
        page_unlock(a >> 12, ((a & 4095) + bytes + 4095) >> 12, 0x40000000);
}
u32 vxd_ioctl(struct dioc *di) {
    struct hdios_request r;
    u32 error = 87;
    void *input = 0, *output = 0;
    u32 *returned = 0;
    if (!di->code)
        return 0; /* DIOC_OPEN */
    if (di->code == 0xffffffffu) {
        if (!gate_claim(&gate)) {
            if (allocation && owner == di->handle && process == di->process) {
                orphan = 1;
                if (ios_completed)
                    release();
            }
            gate = 0;
        }
        return 0;
    }
    if (!registered || di->code != HDIOS_IOCTL)
        return 50;
    if (di->in_size < sizeof(r) || di->in_size > sizeof(r) + HDIOS_MAX_DATA ||
        di->out_size < sizeof(struct hdios_reply) ||
        di->out_size > sizeof(struct hdios_reply) + HDIOS_MAX_DATA || di->overlapped)
        return 87;
    input = lock_buffer(di->input, di->in_size);
    output = lock_buffer(di->output, di->out_size);
    returned = lock_buffer(di->returned, 4);
    if (!input || !output || !returned) {
        error = 998;
        goto done;
    }
    *returned = 0;
    copy(&r, input, sizeof(r));
    if (r.magic != HDIOS_MAGIC || r.version != HDIOS_VERSION || r.size != sizeof(r) ||
        r.bytes > HDIOS_MAX_DATA || r.operation < HDIOS_ENUM || r.operation > HDIOS_BENCH_READ ||
        r.flags > HDIOS_OUT || r.reserved[0] || r.reserved[1])
        goto done;
    if (di->in_size !=
            sizeof(r) + (r.flags == HDIOS_OUT && r.operation != HDIOS_POLL ? r.bytes : 0) ||
        di->out_size < sizeof(struct hdios_reply) + (r.flags == HDIOS_IN ? r.bytes : 0))
        goto done;
    error = dispatch(di, &r, (u8 *)input + sizeof(r), output, returned);
done:
    unlock_buffer(returned, 4);
    unlock_buffer(output, di->out_size);
    unlock_buffer(input, di->in_size);
    return error;
}
