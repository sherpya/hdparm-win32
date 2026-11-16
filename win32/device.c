/* Device contexts and CRT file separation. SPDX-License-Identifier: BSD-2-Clause */
#define HDPARM_WIN_INTERNAL
#include "device.h"
#include "../hdparm.h"
#include <conio.h>
extern int verbose;
static struct win_device devices[WIN_MAX_DEVICES];
static unsigned char used[WIN_MAX_DEVICES];
int win_is_9x(void) { return (GetVersion() & 0x80000000UL) != 0; }
int win_unsupported(void) {
    errno = ENOSYS;
    return -1;
}
int win_error(const char *operation, DWORD e) {
    if (verbose)
        fprintf(stderr, "%s: Windows error %lu\n", operation, e);
    switch (e) {
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
        errno = ENOSYS;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        errno = EACCES;
        break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        errno = ENOMEM;
        break;
    case ERROR_INVALID_PARAMETER:
        errno = EINVAL;
        break;
    default:
        errno = EIO;
        break;
    }
    return -1;
}
struct win_device *win_device(int fd) {
    if (fd < WIN_FD_BASE || fd >= WIN_FD_BASE + WIN_MAX_DEVICES || !used[fd - WIN_FD_BASE]) {
        errno = EBADF;
        return NULL;
    }
    return &devices[fd - WIN_FD_BASE];
}
static int device_name(const char *name, char *out, int *volume, int *optical) {
    unsigned n;
    int end = 0;
    if (!strncmp(name, "/dev/", 5))
        name += 5;
    if (strlen(name) == 3 && (name[0] == 'h' || name[0] == 's') && name[1] == 'd' &&
        name[2] >= 'a' && name[2] <= 'z')
        snprintf(out, MAX_PATH, "\\\\.\\PhysicalDrive%u", (unsigned)(name[2] - 'a'));
    else if (sscanf(name, "scd%u%n", &n, &end) == 1 && name[end] == 0 && n < 256) {
        snprintf(out, MAX_PATH, "\\\\.\\CdRom%u", n);
        *optical = 1;
    } else if (strlen(name) == 2 && name[1] == ':' &&
               ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) {
        snprintf(out, MAX_PATH, "\\\\.\\%s", name);
        *volume = 1;
    } else if (!strncmp(name, "\\\\.\\", 4) && strlen(name) < MAX_PATH) {
        const char *p = name + 4;
        end = 0;
        if (sscanf(p, "PhysicalDrive%u%n", &n, &end) == 1 && p[end] == 0)
            strcpy(out, name);
        else if ((end = 0, sscanf(p, "CdRom%u%n", &n, &end)) == 1 && p[end] == 0) {
            strcpy(out, name);
            *optical = 1;
        } else if (strlen(p) == 2 && p[1] == ':' &&
                   ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))) {
            strcpy(out, name);
            *volume = 1;
        } else {
            errno = EINVAL;
            return -1;
        }
    } else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
int win_open_device(const char *name, unsigned flags) {
    unsigned i;
    struct win_device *d;
    (void)flags;
    for (i = 0; i < WIN_MAX_DEVICES && used[i]; i++)
        ;
    if (i == WIN_MAX_DEVICES) {
        errno = EMFILE;
        return -1;
    }
    d = &devices[i];
    memset(d, 0, sizeof(*d));
    d->handle = d->read_handle = INVALID_HANDLE_VALUE;
    if (device_name(name, d->name, &d->volume, &d->optical))
        return -1;
    if (win_is_9x())
        return win_unsupported();
    if (win_nt_open(d, d->name)) {
        int error = errno;
        if (d->handle != INVALID_HANDLE_VALUE)
            CloseHandle(d->handle);
        if (d->read_handle != INVALID_HANDLE_VALUE)
            CloseHandle(d->read_handle);
        errno = error;
        return -1;
    }
    used[i] = 1;
    if (verbose)
        fprintf(stderr, "%s: %s, bytes=%llu sector=%u writable=%d\n", d->name, d->transport,
                d->size, d->sector, d->writable);
    return (int)i + WIN_FD_BASE;
}
int win_close(int fd) {
    struct win_device *d;
    if (fd < WIN_FD_BASE)
        return _close(fd);
    d = win_device(fd);
    if (!d)
        return -1;
    if (d->handle != INVALID_HANDLE_VALUE)
        CloseHandle(d->handle);
    if (d->read_handle != INVALID_HANDLE_VALUE)
        CloseHandle(d->read_handle);
    used[fd - WIN_FD_BASE] = 0;
    return 0;
}
int win_read(int fd, void *buf, unsigned bytes) {
    struct win_device *d;
    int result;
    if (fd < WIN_FD_BASE)
        return _read(fd, buf, bytes);
    d = win_device(fd);
    if (!d)
        return -1;
    result = win_nt_read(d, buf, bytes, d->position);
    if (result > 0)
        d->position += (unsigned)result;
    return result;
}
__s64 win_seek(int fd, __s64 offset, int origin) {
    struct win_device *d;
    __u64 base, pos;
    if (fd < WIN_FD_BASE)
        return _lseeki64(fd, offset, origin);
    d = win_device(fd);
    if (!d)
        return -1;
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? d->position : d->size;
    if ((offset < 0 && (__u64)(-(offset + 1)) + 1 > base) ||
        (offset >= 0 && (__u64)offset > 0x7fffffffffffffffULL - base)) {
        errno = EINVAL;
        return -1;
    }
    pos = base + offset;
    if (pos > d->size) {
        errno = EINVAL;
        return -1;
    }
    d->position = pos;
    return (__s64)pos;
}
int win_fstat(int fd, struct stat *st) {
    if (fd < WIN_FD_BASE)
        return fstat(fd, st);
    if (!win_device(fd))
        return -1;
    memset(st, 0, sizeof(*st));
    st->st_mode = _S_IFCHR;
    return 0;
}
int win_is_whole_disk(int fd) {
    struct win_device *d = win_device(fd);
    return d && !d->volume && !d->optical;
}
int win_flush(int fd) {
    struct win_device *d;
    if (fd < WIN_FD_BASE)
        return _commit(fd);
    d = win_device(fd);
    if (!d)
        return -1;
    if (!FlushFileBuffers(d->handle))
        return win_error("FlushFileBuffers", GetLastError());
    return 0;
}
int win_ata(int fd, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
            unsigned timeout) {
    struct win_device *d = win_device(fd);
    if (!d)
        return -1;
    if (bytes > WIN_MAX_TRANSFER || (bytes && !data)) {
        errno = EINVAL;
        return -1;
    }
    if (!timeout)
        timeout = 15;
    return win_nt_ata(d, rw, dma, tf, data, bytes, timeout);
}
int win_scsi(struct win_device *d, const unsigned char *cdb, unsigned cdb_len, int rw, void *data,
             unsigned bytes, unsigned timeout, unsigned char *sense, unsigned *sense_len,
             unsigned char *status) {
    return win_nt_scsi(d, cdb, cdb_len, rw, data, bytes, timeout, sense, sense_len, status);
}
int win_ioctl(int fd, unsigned code, uintptr_t arg) {
    struct win_device *d = win_device(fd);
    void *p = (void *)arg;
    if (!d)
        return -1;
    switch (code) {
    case BLKGETSIZE64:
        if (!p)
            break;
        *(__u64 *)p = d->size;
        return 0;
    case BLKGETSIZE:
        if (!p)
            break;
        if (d->size / 512 > ULONG_MAX) {
            errno = ERANGE;
            return -1;
        }
        *(unsigned long *)p = (unsigned long)(d->size / 512);
        return 0;
    case BLKFLSBUF:
        return win_flush(fd);
    case HDIO_GET_IDENTITY: {
        struct ata_tf tf;
        if (!p)
            break;
        memset(&tf, 0, sizeof(tf));
        tf.command = d->optical ? ATA_OP_PIDENTIFY : ATA_OP_IDENTIFY;
        tf.lob.nsect = 1;
        return win_ata(fd, SG_READ, SG_PIO, &tf, p, 512, 15);
    }
    case HDIO_DRIVE_CMD:
        if (!arg)
            return win_flush(fd);
        break;
    default:
        break;
    }
    return win_unsupported();
}
int get_dev_geometry(int fd, __u32 *c, __u32 *h, __u32 *s, __u64 *start, __u64 *sectors) {
    struct win_device *d = win_device(fd);
    if (!d)
        return errno;
    if (c)
        *c = d->cylinders;
    if (h)
        *h = d->heads;
    if (s)
        *s = d->sectors;
    if (start)
        *start = d->volume ? START_LBA_UNKNOWN : 0;
    if (sectors)
        *sectors = d->size / 512;
    return 0;
}
int win_list(void) {
    unsigned i, count = 0;
    char path[64];
    if (win_is_9x()) {
        fprintf(stderr, "Win9x backend not available in this build\n");
        return ENOSYS;
    }
    for (i = 0; i < 256; i++) {
        HANDLE h;
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%u", i);
        h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            printf("%s  NT disk (ATA/SAT/SMART capabilities depend on driver)\n", path);
            CloseHandle(h);
            count++;
        }
    }
    for (i = 0; i < 32; i++) {
        HANDLE h;
        snprintf(path, sizeof(path), "\\\\.\\CdRom%u", i);
        h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            printf("%s  NT optical device\n", path);
            CloseHandle(h);
            count++;
        }
    }
    if (!count)
        fprintf(stderr, "No accessible physical disks found.\n");
    return 0;
}
void *win_mmap(void *addr, size_t size, int prot, int flags, int fd, __s64 offset) {
    void *p;
    (void)addr;
    (void)prot;
    (void)flags;
    p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (fd >= 0 && (size > INT_MAX || win_seek(fd, offset, SEEK_SET) < 0 ||
                    win_read(fd, p, (unsigned)size) != (int)size)) {
        VirtualFree(p, 0, MEM_RELEASE);
        errno = EIO;
        return MAP_FAILED;
    }
    return p;
}
int win_munmap(void *p, size_t size) {
    (void)size;
    return VirtualFree(p, 0, MEM_RELEASE) ? 0 : -1;
}
void win_sleep(unsigned seconds) { Sleep(seconds * 1000u); }
char *win_getpass(const char *prompt) {
    static char password[256];
    unsigned n = 0;
    int c;
    fputs(prompt, stderr);
    while ((c = _getch()) != '\r' && c != '\n') {
        if (c == 3) {
            memset(password, 0, sizeof(password));
            exit(EINTR);
        }
        if (c == 8) {
            if (n)
                n--;
        } else if (c && c != 224 && n < sizeof(password) - 1)
            password[n++] = (char)c;
        else if (c == 0 || c == 224)
            (void)_getch();
    }
    password[n] = 0;
    fputc('\n', stderr);
    return password;
}

char *win_strndup(const char *s, size_t n) {
    size_t len = 0;
    char *p;
    while (len < n && s[len])
        len++;
    p = malloc(len + 1);
    if (!p)
        return NULL;
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}
