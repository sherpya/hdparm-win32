# IOS bridge sources

The optional VxD code is GPL-3.0-only (COPYING). `protocol.h` is BSD-2-Clause
and can be used independently by the application.

The DDB layout in `glue.asm` and the Clang/NASM/LLD/PE-to-LE build procedure
are adapted from ages2001/WINSIMD, revision
`1bcadb374ad5c08c7e5ae80a22b253cd89b9f133`. The converter tools retain separate
provenance in `tools/vxd/SOURCES.md`. No WINDISK controller driver is included.

`ios_abi.h` describes only the ABI needed by this driver, using Windows 98 DDK
IOS/ISP/IOR/ILB/DRP/DCB/IOP/DIOC field layouts. DDK source files are not bundled.
Driver logic is new. Reference documentation:

- Microsoft Q196550, “Access IDE Controller Registers Using IDE Passthrough”
  ([archived article](https://www.betaarchive.com/wiki/index.php/Microsoft_KB_Archive/196550)).
- Microsoft Windows 98 DDK `src/block/fsd` (IOS allocation and SendCommand),
  `src/block/vsd/helpvsd` (IOP allocation), `src/base/asyncw32` (buffer locking).
- [smartmontools RELEASE_5_40 os_win32.cpp](https://github.com/smartmontools/smartmontools/blob/RELEASE_5_40/smartmontools/os_win32.cpp)
  for SMARTVSD and ASPI interface behavior, not implementation code.

No assumption relates IOS enumeration indices to BIOS, SMARTVSD or ASPI indices.
The VxD only requests service from IOS, without programming controller ports.
