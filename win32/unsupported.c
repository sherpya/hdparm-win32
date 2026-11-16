/* Linux kernel facilities have no equivalent on Windows. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "../hdparm.h"
#include "device.h"
int sysfs_get_attr(int fd, const char *a, const char *f, void *v, void *w, int noisy) {
    (void)fd;
    (void)a;
    (void)f;
    (void)v;
    (void)w;
    (void)noisy;
    return win_unsupported();
}
int sysfs_get_attr_recursive(int fd, const char *a, const char *f, void *v, void *w, int noisy) {
    return sysfs_get_attr(fd, a, f, v, w, noisy);
}
int sysfs_set_attr(int fd, const char *a, const char *f, void *v, int noisy) {
    return sysfs_get_attr(fd, a, f, v, NULL, noisy);
}
int do_filemap(const char *p) {
    (void)p;
    fprintf(stderr, "FIEMAP/FIBMAP are not supported on Windows.\n");
    return ENOSYS;
}
int do_fallocate_syscall(const char *p, __u64 bytes) {
    (void)p;
    (void)bytes;
    fprintf(stderr, "fallocate is not supported on Windows.\n");
    return ENOSYS;
}
int fd_is_raid(int fd) {
    (void)fd;
    return 0;
}
int dev_has_sgio(int fd) { return win_device(fd) != NULL; }
int apt_detect(int fd, int v) {
    (void)fd;
    (void)v;
    return 0;
}
int apt_is_apt(void) { return 0; }
int set_dvdspeed(int fd, int speed) {
    struct win_device *d = win_device(fd);
    unsigned char cdb[12] = {0xbb}, sense[64], status;
    unsigned n = sizeof(sense);
    unsigned rate = speed ? (unsigned)speed * 177 : 65535;
    if (!d)
        return -1;
    if (rate > 65535)
        rate = 65535;
    cdb[2] = (unsigned char)(rate >> 8);
    cdb[3] = (unsigned char)rate;
    cdb[4] = cdb[5] = 255;
    if (win_scsi(d, cdb, 12, SG_READ, NULL, 0, 15, sense, &n, &status))
        return -1;
    if (status) {
        errno = EIO;
        return -1;
    }
    return 0;
}
