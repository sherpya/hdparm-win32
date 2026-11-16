# hdparm

## Building

The sources support Linux and Windows, with a C compiler and CMake 3.16 or newer.
See [Windows builds and testing](README.windows.md) for MinGW and Visual Studio.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/hdparm -V
```

Use `-DCMAKE_BUILD_TYPE=Debug` for a debug build. For multi-configuration
generators, select the configuration with `cmake --build build --config Release`.
Ninja and Unix Makefiles are both supported.

For a statically linked executable (requires static C library development files):

```sh
cmake -S . -B build-static -DCMAKE_BUILD_TYPE=Release -DHDPARM_STATIC=ON
cmake --build build-static --parallel
```

## Installing

```sh
cmake --install build --strip
```

The default prefix is `/usr/local`, with the executable in `sbin` and the man
page in `share/man/man8`. Set `CMAKE_INSTALL_PREFIX`, `CMAKE_INSTALL_SBINDIR`,
or `CMAKE_INSTALL_MANDIR` at configure time to change these locations.
Stripping is optional; omit `--strip` to retain debugging symbols.

To retain the original Makefile's `/sbin/hdparm` and
`/usr/share/man/man8/hdparm.8` layout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_SBINDIR=/sbin
cmake --build build --parallel
DESTDIR="$PWD/stage" cmake --install build --strip
```

`DESTDIR` stages the installation for packaging; omit it to install into the
configured system directories.
