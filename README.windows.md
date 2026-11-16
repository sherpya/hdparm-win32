# Windows port

## Builds

Cross build on Debian Bookworm (GCC 12, mingw-w64 10, MSVCRT, win32 threads):

```sh
cmake -S . -B build-win32 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
cmake --build build-win32
cmake -S . -B build-win64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake
cmake --build build-win64
```

The x86 profile targets NT4 and Windows 98/98SE/ME APIs and subsystem 4.0.
Use the MSVCRT-based Bookworm compiler for this profile. UCRT and newer
MinGW runtimes may introduce imports absent on those systems. The x64
profile requires a 64-bit Windows NT system.

Native VS2022 (Developer Command Prompt, C17-capable MSVC):

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A Win32
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Use `-A x64` in a separate build directory for x64. MSVC builds require the
Windows version supported by the selected Microsoft CRT; they are not the
legacy NT4/Win9x binaries. Windows installation places the executable in `bin`.

## Names, transports and limits

`hdparm --list` enumerates accessible disk and optical device names. On NT,
use `\\.\PhysicalDrive0`, `/dev/sda` or `/dev/hda` (same disk index),
`\\.\CdRom0` or `/dev/scd0`, and `C:` or `\\.\C:` for a volume.
Do not assume that a physical disk index is a BIOS number or an IDE position.

| Operation | NT transport / limitation |
| --- | --- |
| ATA 28/48-bit, input/output, non-data | ATA_PASS_THROUGH_EX; driver dependent |
| ATA over SCSI/USB | SAT12/16, requires valid ATA return descriptor |
| Older IDE stack | IDE_PASS_THROUGH, 28-bit, at most 512-byte input, no output |
| SMART interface | IDENTIFY only, no arbitrary ATA commands |
| SCSI | SCSI_PASS_THROUGH, CDB up to 16 bytes |
| Geometry and capacity | Disk IOCTLs; conservative CHS bound on old stacks |
| Volume reads | One extent mapped to physical disk with offset and bounds |
| Multi-disk volumes | Rejected, no automatic translation |
| Linux driver settings, FIEMAP, sysfs, RAID detection, APT-specific transport | Unsupported on Windows |

Commands need the appropriate device permissions (usually administrator).
A method is retried through another transport only on an explicit unsupported
result. Timeouts, short/malformed replies, ATA errors and ambiguous Windows
errors stop the request. `--verbose` prints methods and original Windows error
numbers. Capability failure caches belong to each open device and request class.
ATA/SCSI commands on volume handles are rejected; choose the whole disk explicitly.
On NT4 without extent queries the volume itself is read within partition bounds.

## Timing semantics

`-T` reads a 2 MiB block once before starting the timer, then measures observable
RAM copies. Its label is **application RAM cached reads**. This differs from
Linux's kernel buffer-cache benchmark; it measures memory copying, including the
small checksum sampling cost, not the disk or the Windows file cache.

`-t` issues successive device reads into aligned buffers, stops at the device
boundary, and reports the selected transport. NT reads use a separate
`FILE_FLAG_NO_BUFFERING` handle. `--direct` applies to device reads (already
unbuffered on NT); it does not change `-T`. Device/controller RAM may still cache
physical reads. Offsets are 64-bit; `--offset` retains upstream's GiB units.
The timer uses QPC, or wrap-safe GetTickCount if QPC is unavailable. An invalid
interval or incomplete read produces an error, not a throughput value.

## Verification and Windows checklist

The `windows_tests` executable replaces the OS I/O boundary. It never opens a
real disk. Tests exercise >4 GiB seeks, 512/4096-byte sectors, LBA48 registers,
output payloads, malformed/short replies, timeout and ATA error handling,
per-device fallback, single/multiple extents and benchmark read counts.

```sh
wine build-win32/windows_tests.exe
wine build-win64/windows_tests.exe
python3 tools/check_pe.py build-win32/hdparm.exe --legacy
```

Linux regression build and both MinGW builds are checked locally; simulated
Windows tests run under Wine. Native MSVC compilation, OS loader compatibility,
and real driver behavior still need testing on Windows. Suggested read-only
smoke test, using a name returned by `--list`:

```bat
hdparm --list
hdparm --verbose -I -g \\.\PhysicalDrive0 > identify.log 2>&1
hdparm --verbose -T -t \\.\PhysicalDrive0 > timing.log 2>&1
hdparm --verbose --offset 5 -t \\.\PhysicalDrive0 > offset.log 2>&1
```

The last test requires sufficient capacity. Check model/serial against the
intended disk, geometry, one initial RAM-cache read vs sequential disk reads,
and repeat with an optical device and a single-extent volume when available.
Test both administrator and unprivileged error reporting. Record OS/service
pack, compiler, controller driver, device and logs. Run the mock tests on both
MSVC architectures too. No test requires destructive ATA commands.

The local hdparm 6.9 Win32 port and smartmontools RELEASE_5_40 were interface
references. New transport code does not reuse the old HANDLE-to-int casts,
32-bit seek or global device state. Volume mapping follows
[dd-backdoor](http://www.chrysocome.net/dd-backdoor).

## Windows 98/98SE/ME

The executable selects Win9x transports at runtime. Their device namespaces
are deliberately independent:

| Name from `--list` | Implemented functionality | Limit |
| --- | --- | --- |
| `smart:0` … `smart:3`, `/dev/hda` … `/dev/hdd` | SMARTVSD IDENTIFY, SMART read data/thresholds/status | No sector reads or arbitrary ATA; driver must already be installed/loaded |
| `aspi:adapter:target:lun` | ASPI SCSI, READ(10/16), capacity, SAT12/16 | Installed system WNASPI32.DLL required; SAT depends on the target's sense support |
| `ios:index` | IOS enumeration, geometry, sector reads, ATA28 PIO/non-data | Optional HDPARM.VXD and an IOS controller driver that implements the requested operation |

`/dev/sda` and `PhysicalDriveN` have no automatic Win9x mapping. Do not substitute
an ASPI index or a BIOS number for an IOS index. `--list` prints IOS vendor,
product, revision, port, bus, native unit and sector size to help identify the
device. Recheck the model with `-I`. Device-generation changes invalidate open
IOS contexts, so reopen after changing hardware.

SMARTVSD is used only for its documented command subset. It is not a generic
ATA transport. The ASPI manager is loaded from the Windows system directory.
ASPI is compiled only into the x86 path; the x64 executable cannot run on Win9x.
No DLL, Win16 thunk, or replacement controller driver is bundled.

### Building and loading HDPARM.VXD

Install Clang, NASM, LLD (`lld-link`) and Python 3 on the Linux build host:

```sh
cmake -S . -B build-vxd -G Ninja -DHDPARM_BUILD_VXD=ON
cmake --build build-vxd --target hdparm_vxd
python3 tools/check_vxd.py build-vxd/vxd/HDPARM.VXD
```

Use `HDPARM_VXD_CLANG`, `HDPARM_VXD_NASM` and `HDPARM_VXD_LLD` to select explicit
executables. The build tested here uses Clang/LLD 19.1.7, NASM 3.01 and
Python 3.14. The LLD executable was extracted from Debian's package locally;
it was not installed into the system. The VxD uses an i386 instruction baseline,
no C runtime, no PE imports, no controller I/O ports, and a single preloaded
LE code/data object. PE-to-LE tools and adapted DDB glue retain WINSIMD's GPLv3
license and provenance; see [win9x/SOURCES.md](win9x/SOURCES.md).

Copy `build-vxd/vxd/HDPARM.VXD` beside the **x86** `hdparm.exe` on Windows 9x.
Opening `ios:index` or running `--list` opens an already loaded `HDPARM` VxD or
loads that exact adjacent file dynamically. No SYSTEM.INI edit is needed.
SMARTVSD and ASPI continue to work when the optional helper is absent.
The helper unregisters from IOS when unloaded. Unload is refused while I/O
is pending; request memory and data remain owned by the driver, even when the
client closes or times out. Completed orphan requests are reclaimed on a later
call or unload. The protocol is versioned, uses fixed-size fields and no user
buffer pointers, and serializes outstanding IOS requests.

The IOS ATA transport advertises **28-bit only**, with PIO or non-data commands,
up to the controller's transfer limit (at most 64 KiB). Extended ATA commands
and DMA taskfiles are rejected. Sector reads use IOS's 64-bit starting sector;
capacity and controller limits still govern what the installed port can read.
`-t` requests IOS DONT_CACHE or ASPI READ commands. Neither promises to bypass
the device's own cache. `-T` retains the same application RAM-copy semantics.

For ASPI, timeout reporting is followed by draining the outstanding request
before returning failure: the ASPI manager must finish using its SRB and data.
A stuck manager can therefore keep the process waiting. No automatic command
replay or forced release of its buffers occurs.

### Verification status and Win9x checklist

HDPARM.VXD is **experimental until tested in the real Win9x IOS stack**. The
local checks validate the build, binary format and logic with mocked services;
they do not establish controller compatibility or successful VMM loading.

| Check | Local result | Still required on Windows |
| --- | --- | --- |
| Linux regression | Build and `-V` pass | — |
| MinGW x86/x64 | Builds pass; mocked NT and client tests pass under Wine | NT4/98 loader and real device tests |
| MSVC x86/x64 | CMake profiles and C17 portability provided | VS2022 compilation and mock tests |
| IOS helper | LE/DDB/fixups valid; no CRT imports; clean builds identical | VMM load/unload, IOS callbacks, controller transfer behavior |
| Error paths | Simulated short replies, bad sense, timeout, ATA errors, unsupported fallback, separate devices | Real driver error behavior |
| Benchmark | Single seed read for `-T`; successive `-t` reads; QPC failure and tick wrap tested | Real disk and memory performance |

Run the non-destructive tests first:

```bat
windows_tests.exe
win9x_tests.exe
ios_tests.exe
hdparm --verbose --list > devices.log 2>&1
hdparm --verbose -I -g ios:0 > ios-identify.log 2>&1
hdparm --verbose -T -t ios:0 > ios-timing.log 2>&1
```

Choose the actual `ios:index` from the list. Then repeat IDENTIFY with its
SMARTVSD name, and read tests with an appropriate ASPI device when available.
Test `--list` without the VxD as well as with it. Check both 512-byte and
4096-byte emulated disks where available, >4 GiB offsets, empty/unsupported
optical media, missing SMARTVSD/ASPI, and permission/driver failure logs.
Do not infer throughput ratios: RAM copies and device reads measure different
work. The automated tests never send commands to real disks.

The reproducibility check uses two separate, initially empty build directories,
builds `hdparm_vxd` in each, then runs `cmp` on their VXD files. The tested
artifact SHA-256 is
`10c908b96f1a605ecea1ce2e8798975ac482f2c22b0c83a5100c10787536c103`.
The PE import check includes CRT imports and rejects additions to the reviewed
Bookworm baseline. It is a regression check, not a substitute for running the
binary with the actual NT4/Win98 versions of KERNEL32/MSVCRT.
