cmake_minimum_required(VERSION 3.16)
project(vendored_zlib C)

# Deliberately not integrated into the main Seabass build -- see
# README.md in this directory. Configure/build/install this standalone
# project into a prefix, then point the main build's CMAKE_PREFIX_PATH
# at it so find_package(ZLIB) succeeds normally, for both Seabass's own
# CMakeLists.txt and libdjinterop's internal (unconditional, no bypass
# option) find_package(ZLIB REQUIRED) call.

add_library(z STATIC
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    infback.c
    inffast.c
    inflate.c
    inftrees.c
    trees.c
    uncompr.c
    zutil.c
)
target_include_directories(z PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

include(GNUInstallDirs)
install(TARGETS z DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES zlib.h zconf.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
