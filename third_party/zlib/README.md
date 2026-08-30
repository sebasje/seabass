# Vendored zlib

Core library sources from zlib 1.3.2, upstream:
https://github.com/madler/zlib/releases/tag/v1.3.2

zlib license (see `LICENSE`).

## Why this exists

Seabass's Linux build always uses the system's own zlib via
`find_package(ZLIB REQUIRED)` -- this vendored copy is **not** used
there. It exists only to satisfy the same `find_package(ZLIB REQUIRED)`
call (both Seabass's own and `third_party/libdjinterop`'s own internal,
unconditional one -- unlike SQLite3, libdjinterop has no bundled-zlib
fallback option) when cross-compiling for Windows with mingw-w64, where
no packaged zlib-for-mingw development files exist for Ubuntu
(`libz-mingw-w64` ships only the runtime DLL, no headers or import
library).

## How it's used

This is a deliberately standalone CMake project, **not** added via
`add_subdirectory()` from the main build. Build and install it into a
prefix first, then point the main cross-compile configure's
`CMAKE_PREFIX_PATH` at that prefix so `find_package(ZLIB)` finds it via
CMake's normal module-mode search -- this needs zero changes to
`third_party/libdjinterop`'s own `CMakeLists.txt`. See the Windows
cross-compile instructions in the top-level project notes.

## What was trimmed from the upstream release tarball

Only the core library's `.c`/`.h` files needed to build `libz`, plus
`zlib.h`/`zconf.h` (the public API) and `LICENSE`. Not included: build
system files (`configure`, `Makefile.in`, `CMakeLists.txt` was replaced
with Seabass's own minimal one), tests, examples, contrib/,
documentation (`zlib.3`, `zlib.3.pdf`, `ChangeLog`, `README`), and
platform-specific build files for platforms this project doesn't target
(`msdos/`, `qnx/`, `amiga/`, `os400/`, `watcom/`).
