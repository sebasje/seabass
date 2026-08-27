# Cross-compile djconvert's CLI (djconvert_core + the `djconvert` target)
# for 64-bit Windows using mingw-w64. GUI target is not attempted: Qt6 has
# no packaged mingw build for Ubuntu, so find_package(Qt6) simply won't
# find anything under this toolchain and djconvert-gui is skipped, same
# as any machine without Qt6 -- see the top-level CMakeLists.txt.
#
# Usage:
#   cmake -S . -B build-mingw \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
#       -DCMAKE_PREFIX_PATH=<path to an installed vendored zlib prefix -- see third_party/zlib/README.md>
#   cmake --build build-mingw --target djconvert

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# BOTH, not ONLY -- ONLY restricted find_library/find_path to just
# CMAKE_FIND_ROOT_PATH itself, silently ignoring an explicit
# CMAKE_PREFIX_PATH entry (e.g. the vendored zlib install prefix, see
# third_party/zlib/README.md) that doesn't happen to live under it.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
