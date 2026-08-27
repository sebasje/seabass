cmake_minimum_required(VERSION 3.16)
# C, not just CXX -- libdjinterop's bundled SQLite/date fallback (used
# when cross-compiling, see the WIN32 branch below) is plain C
# (sqlite3.c). Declaring it here at the top level, rather than relying on
# a subdirectory's own project() call to add it implicitly, avoids a real
# cross-compile gotcha hit while adding Windows support: enabling C only
# from within third_party/libdjinterop's own project() call left
# CMAKE_C_COMPILE_OBJECT unset for the Unix Makefiles generator when
# cross-compiling, breaking the Generate step.
project(djconvert CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- vendored dependencies (git submodules under third_party/) ---

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
if(WIN32)
    # No SQLite3-for-mingw package exists for Ubuntu -- libdjinterop
    # already has its own bundled-amalgamation fallback for exactly this
    # (see third_party/libdjinterop/CMakeLists.txt's SYSTEM_SQLITE
    # option); djconvert_core's own direct SQLite3 use (LocalCueStore)
    # gets the identical amalgamation compiled straight into it below,
    # instead of going through find_package(SQLite3) at all.
    set(SYSTEM_SQLITE OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(third_party/libdjinterop)

if(NOT WIN32)
    # System SQLite3 -- already required to build libdjinterop (Engine's
    # library format is itself SQLite), so LocalCueStore reuses it rather
    # than vendoring a JSON library for the same job.
    find_package(SQLite3 REQUIRED)
endif()

# System zlib -- used to compress local cue-backup snapshots and full
# metadata backups before they hit disk (see infrastructure/compression/).
find_package(ZLIB REQUIRED)

# Only needed by stick_write_lock_test, which spawns a real std::thread to
# prove StickWriteLock actually excludes a concurrent locker.
find_package(Threads REQUIRED)

add_library(kaitai_cpp_stl_runtime STATIC
    third_party/kaitai_struct_cpp_stl_runtime/kaitai/kaitaistream.cpp
)
target_include_directories(kaitai_cpp_stl_runtime PUBLIC
    third_party/kaitai_struct_cpp_stl_runtime
)
target_compile_definitions(kaitai_cpp_stl_runtime PUBLIC KS_STR_ENCODING_ICONV)
if(WIN32)
    # mingw-w64 has neither <endian.h> nor <byteswap.h> (Linux/BSD-only) --
    # see third_party/mingw-compat/endian.h for the full explanation. Only
    # this one target needs it (the only place that source file's
    # platform-detection preamble is compiled), and PRIVATE so it doesn't
    # leak onto anything else's include path.
    target_include_directories(kaitai_cpp_stl_runtime PRIVATE third_party/mingw-compat)
    # win-iconv-mingw-w64-dev's iconv() declares its second parameter
    # `const char **` by default (POSIX-2008 style); kaitaistream.cpp
    # (written against glibc's non-const `char **`) passes a plain
    # `char**`, which C++ refuses to implicitly convert -- this empties
    # win-iconv's own WINICONV_CONST macro (its documented escape hatch
    # for exactly this) to match glibc's signature instead.
    target_compile_definitions(kaitai_cpp_stl_runtime PRIVATE WINICONV_CONST=)
    # win-iconv-mingw-w64-dev ships iconv.h but (unlike glibc) needs
    # explicitly linking -- PUBLIC so it also reaches whatever links this
    # static library's .a (djconvert.exe's final link), not just this
    # target's own compilation. Bare "iconv" resolves to libiconv.dll.a
    # (the *import* library for iconv.dll) by mingw ld's default
    # preference, silently making the exe depend on iconv.dll at runtime
    # instead of bundling it -- find_library forces the real static
    # libiconv.a instead, for a self-contained portable exe.
    find_library(WIN_ICONV_STATIC_LIB NAMES libiconv.a PATHS /usr/x86_64-w64-mingw32/lib REQUIRED)
    target_link_libraries(kaitai_cpp_stl_runtime PUBLIC ${WIN_ICONV_STATIC_LIB})
endif()

# --- rekordbox format parser, generated from specs/*.ksy (see specs/README.md) ---

add_library(rekordbox_format STATIC
    src/infrastructure/rekordbox/generated/rekordbox_pdb.cpp
    src/infrastructure/rekordbox/generated/rekordbox_anlz.cpp
)
target_include_directories(rekordbox_format PUBLIC
    src/infrastructure/rekordbox/generated
)
target_link_libraries(rekordbox_format PUBLIC kaitai_cpp_stl_runtime)

# --- djconvert_core: domain -> application -> infrastructure, shared by ---
# --- both the CLI (src/cli/) and the Qt GUI (src/gui/) composition roots ---

add_library(djconvert_core STATIC
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/fuzzy_matcher.cpp
    src/domain/local_restore.cpp
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    src/domain/track_queries.cpp
    src/application/ports/progress_reporter.cpp
    src/infrastructure/rekordbox/anlz_cue_codec.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    src/infrastructure/rekordbox/rekordbox_cue_writer.cpp
    src/infrastructure/rekordbox/kaitai_rekordbox_reader.cpp
    src/infrastructure/rekordbox/rekordbox_waveform_reader.cpp
    src/infrastructure/rekordbox/rekordbox_settings_fields.cpp
    src/infrastructure/rekordbox/rekordbox_settings_reader.cpp
    src/infrastructure/rekordbox/rekordbox_settings_writer.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/libdjinterop_waveform_reader.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/media/media_factory.cpp
    src/infrastructure/media/stick_root_scan.cpp
    src/infrastructure/backup/filesystem_backup_store.cpp
    src/infrastructure/backup/stick_write_lock.cpp
    src/infrastructure/compression/zlib_compressor.cpp
    src/infrastructure/logging/file_operation_log.cpp
    src/infrastructure/local/local_cue_store.cpp
    src/infrastructure/system/rekordbox_process_detector.cpp
)
target_include_directories(djconvert_core PUBLIC src)
target_link_libraries(djconvert_core PUBLIC rekordbox_format DjInterop ZLIB::ZLIB)

if(WIN32)
    # See the SYSTEM_SQLITE note above -- same bundled amalgamation
    # libdjinterop uses privately, compiled straight into djconvert_core
    # instead of going through find_package(SQLite3).
    target_sources(djconvert_core PRIVATE
        third_party/libdjinterop/ext/sqlite-amalgamation/sqlite3.c)
    target_include_directories(djconvert_core PRIVATE
        third_party/libdjinterop/ext/sqlite-amalgamation)
    target_compile_definitions(djconvert_core PRIVATE SQLITE_OMIT_LOAD_EXTENSION)
else()
    target_link_libraries(djconvert_core PUBLIC SQLite::SQLite3)
endif()

# --- platform-specific removable-media adapters (see media_factory.cpp) ---
# --- -- the only part of djconvert_core with no single cross-platform ---
# --- implementation, since "how do I see/mount/eject a USB stick" is ---
# --- genuinely OS-specific (udev+udisksctl on Linux, drive letters + ---
# --- DeviceIoControl on Windows). ---

if(WIN32)
    target_sources(djconvert_core PRIVATE
        src/infrastructure/media/windows_removable_media_locator.cpp
        src/infrastructure/media/windows_removable_media_monitor.cpp
        src/infrastructure/media/windows_removable_media_mounter.cpp
    )
else()
    target_sources(djconvert_core PRIVATE
        src/infrastructure/media/linux_removable_media_locator.cpp
        src/infrastructure/media/linux_udev_media_monitor.cpp
        src/infrastructure/media/udisksctl_media_mounter.cpp
    )
    target_link_libraries(djconvert_core PUBLIC udev)
endif()

# --- djconvert: the terminal composition root ---

add_executable(djconvert
    src/cli/console.cpp
    src/cli/terminal_progress_reporter.cpp
    src/cli/main.cpp
)
target_include_directories(djconvert PRIVATE src)
target_link_libraries(djconvert PRIVATE djconvert_core)
if(WIN32)
    # A portable single-file .exe -- without these, mingw links
    # libgcc_s_seh-1.dll/libstdc++-6.dll dynamically, which aren't
    # present next to the exe (or anywhere in a real Windows install)
    # unless separately deployed. -static-libgcc/-static-libstdc++
    # aren't enough on their own with the posix-threading mingw
    # variant (required to match Qt's prebuilt win64_mingw binaries,
    # see cmake/mingw-w64-toolchain.cmake) -- that variant also pulls
    # in libwinpthread-1.dll dynamically unless -static covers it too.
    target_link_options(djconvert PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# --- djconvert-gui: the Qt Quick composition root (opt-in: only built ---
# --- when Qt6 is available, so machines without Qt still get the CLI) ---

find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick QuickControls2 QuickLayouts Multimedia Concurrent Svg)
if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)
    # BUILD_SHARED_LIBS was forced OFF above for our own vendored static
    # libs; shadow it back to ON (non-cache, scoped to this subdirectory)
    # so Qt's QML tooling doesn't assume a static Qt build and go looking
    # for static QML plugin targets that don't exist in this (dynamic)
    # system Qt install.
    set(BUILD_SHARED_LIBS ON)
    add_subdirectory(src/gui)
else()
    message(STATUS "Qt6 not found -- skipping djconvert-gui (CLI and tests still build)")
endif()

# --- domain-layer unit tests (no infrastructure deps, no fixtures needed) ---
# --- -- gated on NOT WIN32 for now: several link SQLite::SQLite3/ ---
# --- ZLIB::ZLIB directly (not through djconvert_core, which handles the ---
# --- WIN32 case above), and Windows test coverage is genuinely not yet ---
# --- wired up -- tracked as real follow-up work, not silently skipped. ---

if(NOT WIN32)
enable_testing()
add_executable(duplicate_cue_consolidation_test
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/track_matching.cpp
    tests/duplicate_cue_consolidation_test.cpp
)
target_include_directories(duplicate_cue_consolidation_test PRIVATE src)
add_test(NAME duplicate_cue_consolidation_test COMMAND duplicate_cue_consolidation_test)

add_executable(track_matching_test
    src/domain/track_matching.cpp
    tests/track_matching_test.cpp
)
target_include_directories(track_matching_test PRIVATE src)
add_test(NAME track_matching_test COMMAND track_matching_test)

add_executable(sync_planning_test
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    tests/sync_planning_test.cpp
)
target_include_directories(sync_planning_test PRIVATE src)
add_test(NAME sync_planning_test COMMAND sync_planning_test)

add_executable(fuzzy_matcher_test
    src/domain/fuzzy_matcher.cpp
    tests/fuzzy_matcher_test.cpp
)
target_include_directories(fuzzy_matcher_test PRIVATE src)
add_test(NAME fuzzy_matcher_test COMMAND fuzzy_matcher_test)

add_executable(track_queries_test
    src/domain/track_queries.cpp
    tests/track_queries_test.cpp
)
target_include_directories(track_queries_test PRIVATE src)
add_test(NAME track_queries_test COMMAND track_queries_test)

add_executable(anlz_cue_codec_test
    src/infrastructure/rekordbox/anlz_cue_codec.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    tests/anlz_cue_codec_test.cpp
)
target_include_directories(anlz_cue_codec_test PRIVATE src)
add_test(NAME anlz_cue_codec_test COMMAND anlz_cue_codec_test)

add_executable(local_restore_test
    src/domain/local_restore.cpp
    tests/local_restore_test.cpp
)
target_include_directories(local_restore_test PRIVATE src)
add_test(NAME local_restore_test COMMAND local_restore_test)

add_executable(filesystem_backup_store_test
    src/infrastructure/backup/filesystem_backup_store.cpp
    tests/filesystem_backup_store_test.cpp
)
target_include_directories(filesystem_backup_store_test PRIVATE src)
add_test(NAME filesystem_backup_store_test COMMAND filesystem_backup_store_test)

add_executable(stick_write_lock_test
    src/infrastructure/backup/stick_write_lock.cpp
    tests/stick_write_lock_test.cpp
)
target_include_directories(stick_write_lock_test PRIVATE src)
target_link_libraries(stick_write_lock_test PRIVATE Threads::Threads)
add_test(NAME stick_write_lock_test COMMAND stick_write_lock_test)

add_executable(rekordbox_process_detector_test
    src/infrastructure/system/rekordbox_process_detector.cpp
    tests/rekordbox_process_detector_test.cpp
)
target_include_directories(rekordbox_process_detector_test PRIVATE src)
add_test(NAME rekordbox_process_detector_test COMMAND rekordbox_process_detector_test)

add_executable(rekordbox_settings_test
    src/infrastructure/rekordbox/rekordbox_settings_fields.cpp
    src/infrastructure/rekordbox/rekordbox_settings_reader.cpp
    src/infrastructure/rekordbox/rekordbox_settings_writer.cpp
    tests/rekordbox_settings_test.cpp
)
target_include_directories(rekordbox_settings_test PRIVATE src)
add_test(NAME rekordbox_settings_test COMMAND rekordbox_settings_test)

# --- infrastructure-layer test needing a real (temp-file) SQLite db ---

add_executable(local_cue_store_test
    src/domain/track_matching.cpp
    src/application/ports/progress_reporter.cpp
    src/infrastructure/compression/zlib_compressor.cpp
    src/infrastructure/local/local_cue_store.cpp
    tests/local_cue_store_test.cpp
)
target_include_directories(local_cue_store_test PRIVATE src)
target_link_libraries(local_cue_store_test PRIVATE SQLite::SQLite3 ZLIB::ZLIB)
add_test(NAME local_cue_store_test COMMAND local_cue_store_test)

endif()  # NOT WIN32
