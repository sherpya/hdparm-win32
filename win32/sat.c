/* ATA over SCSI. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "device.h"
extern int prefer_ata12;
/* A target rejecting the pass-through opcode has not executed the ATA command. */
int win_scsi_unsupported(const unsigned char *s, unsigned n, unsigned char status) {
    if (status != 2 || n < 8 || 8u + s[7] > n)
        return 0;
    if ((s[0] & 0x7e) == 0x70)
        return n >= 14 && s[7] >= 6 && (s[2] & 15) == 5 && s[12] == 0x20 && !s[13];
    return (s[0] & 0x7e) == 0x72 && (s[1] & 15) == 5 && s[2] == 0x20 && !s[3];
}
int win_sat(struct win_device *d, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
            unsigned timeout) {
    unsigned char cdb[16] = {0}, sense[64] = {0}, status = 0;
    unsigned slen = sizeof(sense), i, end;
    int use12 = prefer_ata12 && !tf->is_lba48 && tf->command != ATA_OP_PIDENTIFY;
    int identify = rw == SG_READ && !tf->is_lba48 && bytes == 512 &&
                   (tf->command == ATA_OP_IDENTIFY || tf->command == ATA_OP_PIDENTIFY);
    cdb[0] = use12 ? 0xa1 : 0x85;
    cdb[1] = (unsigned char)((!bytes ? 3 : dma ? 6 : rw == SG_WRITE ? 5 : 4) << 1);
    cdb[2] = identify ? 0 : 0x20; /* IDENTIFY can succeed with data alone. */
    if (bytes)
        cdb[2] |= 4 | 2 | (rw == SG_READ ? 8 : 0);
    if (use12) {
        cdb[3] = tf->lob.feat;
        cdb[4] = tf->lob.nsect;
        cdb[5] = tf->lob.lbal;
        cdb[6] = tf->lob.lbam;
        cdb[7] = tf->lob.lbah;
        cdb[8] = tf->dev;
        cdb[9] = tf->command;
    } else {
        cdb[1] |= tf->is_lba48 ? 1 : 0;
        cdb[3] = tf->hob.feat;
        cdb[4] = tf->lob.feat;
        cdb[5] = tf->hob.nsect;
        cdb[6] = tf->lob.nsect;
        cdb[7] = tf->hob.lbal;
        cdb[8] = tf->lob.lbal;
        cdb[9] = tf->hob.lbam;
        cdb[10] = tf->lob.lbam;
        cdb[11] = tf->hob.lbah;
        cdb[12] = tf->lob.lbah;
        cdb[13] = tf->dev;
        cdb[14] = tf->command;
    }
    if (win_scsi(d, cdb, use12 ? 12 : 16, rw, data, bytes, timeout, sense, &slen, &status))
        return -1;
    if (win_scsi_unsupported(sense, slen, status))
        return win_unsupported();
    if (identify && status == 0) {
        tf->status = 0;
        tf->error = 0;
        return 0;
    }
    if (status != 2 || slen < 8 || (sense[0] & 0x7e) != 0x72) {
        errno = EIO;
        return -1;
    }
    end = 8u + sense[7];
    if (end > slen) {
        errno = EIO;
        return -1;
    }
    for (i = 8; i + 2 <= end; i += 2u + sense[i + 1]) {
        if (i + 2u + sense[i + 1] > end) {
            errno = EIO;
            return -1;
        }
        if (sense[i] == 9 && sense[i + 1] >= 12) {
            const unsigned char *s = sense + i;
            if (tf->is_lba48 && !(s[2] & 1)) {
                errno = EIO;
                return -1;
            }
            tf->error = s[3];
            tf->hob.nsect = s[4];
            tf->lob.nsect = s[5];
            tf->hob.lbal = s[6];
            tf->lob.lbal = s[7];
            tf->hob.lbam = s[8];
            tf->lob.lbam = s[9];
            tf->hob.lbah = s[10];
            tf->lob.lbah = s[11];
            tf->dev = s[12];
            tf->status = s[13];
            if ((tf->status & 0xa9) || ((sense[1] & 15) != 0 && (sense[1] & 15) != 1)) {
                errno = EIO;
                return -1;
            }
            return 0;
        }
    }
    errno = EIO;
    return -1;
}
