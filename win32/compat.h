/* Windows portability for hdparm. SPDX-License-Identifier: BSD-2-Clause */
#ifndef HDPARM_WIN_COMPAT_H
#define HDPARM_WIN_COMPAT_H
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;
typedef signed long long __s64;
#ifdef _MSC_VER
typedef intptr_t ssize_t;
#define strdup _strdup
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define inline __inline
#endif
#ifndef ENOTSUP
#define ENOTSUP ENOSYS
#endif
#ifndef EBADE
#define EBADE 1000
#endif
#ifndef ENOMSG
#define ENOMSG 1001
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_DIRECT
#define O_DIRECT 0x40000000
#endif
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#define SG_IO 0x2285
#define SG_GET_VERSION_NUM 0x2282
#define BLKGETSIZE64 0x80081272
#define BLKGETSIZE 0x1260
#define BLKFLSBUF 0x1261
#define BLKROSET 0x125d
#define BLKROGET 0x125e
#define BLKRRPART 0x125f
#define BLKRASET 0x1262
#define BLKRAGET 0x1263
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & _S_IFMT) == _S_IFCHR)
#endif
#define __cpu_to_le64(v) (v)
#define __le16_to_cpu(v) (v)
#define __le32_to_cpu(v) (v)
#define __le64_to_cpu(v) (v)
#define __BYTE_ORDER 1234
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define MAP_FAILED ((void *)-1)
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 4
#define MAP_POPULATE 0
#define MAP_LOCKED 0
#define PROT_READ 1
#define PROT_WRITE 2

typedef __s64 off64_t;
struct ata_tf;
int win_open_device(const char *name, unsigned flags);
int win_close(int fd);
int win_read(int fd, void *buf, unsigned bytes);
__s64 win_seek(int fd, __s64 offset, int origin);
int win_fstat(int fd, struct stat *st);
int win_ioctl(int fd, unsigned code, uintptr_t arg);
int win_flush(int fd);
void *win_mmap(void *addr, size_t size, int prot, int flags, int fd, __s64 offset);
int win_munmap(void *addr, size_t size);
void win_sleep(unsigned seconds);
char *win_strndup(const char *s, size_t n);
char *win_getpass(const char *prompt);
int win_ata(int fd, int rw, int dma, struct ata_tf *tf, void *data, unsigned bytes,
            unsigned timeout);
int win_benchmark(int fd, int cached, __u64 offset);
int win_list(void);
int win_is_whole_disk(int fd);
int win_identify_command(int fd);
#ifndef HDPARM_WIN_INTERNAL
#define open(...) _open(__VA_ARGS__)
#define write _write
#define close win_close
#define read win_read
#define lseek win_seek
#define lseek64 win_seek
#define fstat win_fstat
#define ioctl(fd, code, arg) win_ioctl(fd, code, (uintptr_t)(arg))
#define fsync win_flush
#define fdatasync win_flush
#define sync() ((void)0)
#define mmap win_mmap
#define munmap win_munmap
#define munlock(p, n) ((void)0)
#define sleep win_sleep
#define getpass win_getpass
#define strndup win_strndup
#endif
#endif
