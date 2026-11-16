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
