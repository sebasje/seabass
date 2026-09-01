cmake_minimum_required(VERSION 3.16)
# C, not just CXX - libdjinterop's bundled SQLite/date fallback (used
# when cross-compiling, see the WIN32 branch below) is plain C
# (sqlite3.c). Declaring it here at the top level, rather than relying on
# a subdirectory's own project() call to add it implicitly, avoids a real
# cross-compile gotcha hit while adding Windows support: enabling C only
# from within third_party/libdjinterop's own project() call left
# CMAKE_C_COMPILE_OBJECT unset for the Unix Makefiles generator when
# cross-compiling, breaking the Generate step.
project(seabass CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- vendored dependencies (git submodules under third_party/) ---

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
if(WIN32)
    # No SQLite3-for-mingw package exists for Ubuntu - libdjinterop
    # already has its own bundled-amalgamation fallback for exactly this
    # (see third_party/libdjinterop/CMakeLists.txt's SYSTEM_SQLITE
    # option); seabass_core's own direct SQLite3 use (LocalCueStore)
    # gets the identical amalgamation compiled straight into it below,
    # instead of going through find_package(SQLite3) at all.
    set(SYSTEM_SQLITE OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(third_party/libdjinterop)

# System SQLite3, already required to build libdjinterop (Engine's
# library format is itself SQLite), so LocalCueStore reuses it rather
# than vendoring a JSON library for the same job. seabass_core itself
# doesn't use this on WIN32 (it compiles libdjinterop's bundled
# amalgamation directly instead, see the WIN32 branch below). This
# find_package call now runs unconditionally purely so local_cue_store_test
# (which links SQLite::SQLite3 directly, see tests/ below) has something
# to link against on Windows too. MSYS2's UCRT64 environment ships a real
# sqlite3 package with proper find_package support, unlike the old
# Ubuntu-hosted mingw cross-compile setup this project used before.
find_package(SQLite3 REQUIRED)

# System zlib, used to compress local cue-backup snapshots and full
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
    # `char**`, which C++ refuses to implicitly convert. This empties
    # win-iconv's own WINICONV_CONST macro (its documented escape hatch
    # for exactly this) to match glibc's signature instead.
    target_compile_definitions(kaitai_cpp_stl_runtime PRIVATE WINICONV_CONST=)
    # win-iconv-mingw-w64-dev ships iconv.h but (unlike glibc) needs
    # explicitly linking - PUBLIC so it also reaches whatever links this
    # static library's .a (seabass-cli.exe's final link), not just this
    # target's own compilation. Bare "iconv" resolves to libiconv.dll.a
    # (the *import* library for iconv.dll) by mingw ld's default
    # preference, silently making the exe depend on iconv.dll at runtime
    # instead of bundling it - find_library forces the real static
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

# --- seabass_core: domain -> application -> infrastructure, shared by ---
# --- both the CLI (src/cli/) and the Qt GUI (src/gui/) composition roots ---

add_library(seabass_core STATIC
    src/domain/cross_source_sync_conflict.cpp
    src/domain/disk_usage.cpp
    src/domain/duplicate_cleanup.cpp
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/filesystem_compatibility.cpp
    src/domain/fuzzy_matcher.cpp
    src/domain/junk_cue.cpp
    src/domain/library_consistency.cpp
    src/domain/library_statistics.cpp
    src/domain/local_restore.cpp
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    src/domain/track_queries.cpp
    src/application/ports/progress_reporter.cpp
    src/application/use_cases/anonymize_library.cpp
    src/infrastructure/benchmark/stick_benchmark_history.cpp
    src/infrastructure/benchmark/stick_speed_benchmark.cpp
    src/infrastructure/bulk_write_strategy.cpp
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/anlz_cue_codec.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/rekordbox_cleanup_writer.cpp
    src/infrastructure/rekordbox/rekordbox_cue_writer.cpp
    src/infrastructure/rekordbox/kaitai_rekordbox_reader.cpp
    src/infrastructure/rekordbox/rekordbox_waveform_reader.cpp
    src/infrastructure/rekordbox/rekordbox_settings_fields.cpp
    src/infrastructure/rekordbox/rekordbox_settings_reader.cpp
    src/infrastructure/rekordbox/rekordbox_settings_writer.cpp
    src/infrastructure/rekordbox/rekordbox_library_anonymizer.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/libdjinterop_engine_library_creator.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    src/infrastructure/engine/libdjinterop_waveform_reader.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/engine/libdjinterop_engine_cleanup_writer.cpp
    src/infrastructure/engine/libdjinterop_engine_anonymizer.cpp
    src/infrastructure/media/media_factory.cpp
    src/infrastructure/media/stick_root_scan.cpp
    src/infrastructure/backup/filesystem_backup_store.cpp
    src/infrastructure/backup/stick_write_lock.cpp
    src/infrastructure/cleanup/pending_deletion_manifest.cpp
    src/infrastructure/cleanup/pending_deletion_resolver.cpp
    src/infrastructure/cleanup/pending_deletion_applier.cpp
    src/infrastructure/compression/zlib_compressor.cpp
    src/infrastructure/logging/file_operation_log.cpp
    src/infrastructure/local/local_cue_store.cpp
    src/infrastructure/system/rekordbox_process_detector.cpp
    src/infrastructure/system/stick_hardware_info.cpp
    src/infrastructure/zip_archive_writer.cpp
)
target_include_directories(seabass_core PUBLIC src)
target_link_libraries(seabass_core PUBLIC rekordbox_format DjInterop ZLIB::ZLIB)

if(WIN32)
    # See the SYSTEM_SQLITE note above - same bundled amalgamation
    # libdjinterop uses privately, compiled straight into seabass_core
    # instead of going through find_package(SQLite3).
    target_sources(seabass_core PRIVATE
        third_party/libdjinterop/ext/sqlite-amalgamation/sqlite3.c)
    target_include_directories(seabass_core PRIVATE
        third_party/libdjinterop/ext/sqlite-amalgamation)
    target_compile_definitions(seabass_core PRIVATE SQLITE_OMIT_LOAD_EXTENSION)
else()
    target_link_libraries(seabass_core PUBLIC SQLite::SQLite3)
endif()

# --- platform-specific removable-media adapters (see media_factory.cpp) ---
# --- -- the only part of seabass_core with no single cross-platform ---
# --- implementation, since "how do I see/mount/eject a USB stick" is ---
# --- genuinely OS-specific (udev+udisksctl on Linux, drive letters + ---
# --- DeviceIoControl on Windows). ---

if(WIN32)
    target_sources(seabass_core PRIVATE
        src/infrastructure/media/windows_removable_media_locator.cpp
        src/infrastructure/media/windows_removable_media_monitor.cpp
        src/infrastructure/media/windows_removable_media_mounter.cpp
    )
else()
    target_sources(seabass_core PRIVATE
        src/infrastructure/media/linux_removable_media_locator.cpp
        src/infrastructure/media/linux_udev_media_monitor.cpp
        src/infrastructure/media/udisksctl_media_mounter.cpp
    )
    target_link_libraries(seabass_core PUBLIC udev)
endif()

# --- OneLibrary/Device Library Plus (exportLibrary.db) cue writer -- ---
# --- Cross-platform: talks to libsqlcipher at runtime (LoadLibrary/ ---
# --- GetProcAddress on Windows, dlopen/dlsym elsewhere) rather than ---
# --- linking it, specifically to avoid a real symbol collision (see ---
# --- sqlcipher_dyn.hpp's own doc comment) with the plain, unencrypted ---
# --- SQLite3 seabass_core already links above. See ---
# --- docs/onelibrary-format.md. Needs libsqlcipher's runtime library ---
# --- present on the machine actually running seabass (Windows: the ---
# --- mingw-w64-ucrt-x86_64-sqlcipher package's DLL; Linux: the ---
# --- libsqlcipher1 package or equivalent) -- OneLibraryCueWriter's own ---
# --- callers already treat it as a best-effort secondary write target ---
# --- and degrade gracefully if it's missing. ---

target_sources(seabass_core PRIVATE
    src/infrastructure/onelibrary/onelibrary_key.cpp
    src/infrastructure/onelibrary/sqlcipher_dyn.cpp
    src/infrastructure/onelibrary/onelibrary_cue_writer.cpp
    src/infrastructure/onelibrary/onelibrary_reader.cpp
)
if(NOT WIN32)
    target_link_libraries(seabass_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# --- seabass-cli: the terminal composition root ---

add_executable(seabass-cli
    src/cli/console.cpp
    src/cli/terminal_progress_reporter.cpp
    src/cli/main.cpp
)
target_include_directories(seabass-cli PRIVATE src)
target_link_libraries(seabass-cli PRIVATE seabass_core)
if(WIN32)
    # A portable single-file .exe - without these, mingw links
    # libgcc_s_seh-1.dll/libstdc++-6.dll dynamically, which aren't
    # present next to the exe (or anywhere in a real Windows install)
    # unless separately deployed. -static-libgcc/-static-libstdc++
    # aren't enough on their own with the posix-threading mingw
    # variant (required to match Qt's prebuilt win64_mingw binaries,
    # see cmake/mingw-w64-toolchain.cmake). That variant also pulls
    # in libwinpthread-1.dll dynamically unless -static covers it too.
    target_link_options(seabass-cli PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# --- seabass: the Qt Quick composition root (opt-in: only built ---
# --- when Qt6 is available, so machines without Qt still get the CLI) ---

# Controls whether the experimental-features opt-in itself (the Settings
# toggle AppSettingsController::experimentalFeaturesEnabled) exists at
# all, not per-feature inclusion of code -- QML has no conditional-
# compilation mechanism, so a feature's own page/controller can't be
# compiled out this way. See docs/experimental-features.md. ON by
# default: a normal build still defaults every experimental feature to
# hidden until the user opts in at runtime; turning this OFF instead
# produces a real "stable-only build" where that opt-in can't be reached
# at all, regardless of anything already stored in QSettings.
option(SEABASS_EXPERIMENTAL "Compile in the experimental-features opt-in (Settings toggle)" ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick QuickControls2 QuickLayouts QuickEffects QuickDialogs2 QuickShapes Multimedia Concurrent Svg)
if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)
    if(WIN32)
        # Needed to compile src/gui/win/app_icon.rc (the .exe's icon
        # resource), not enabled by the top-level project(seabass CXX
        # C) call above since only the Windows GUI build needs it.
        enable_language(RC)
    endif()
    # BUILD_SHARED_LIBS was forced OFF above for our own vendored static
    # libs; shadow it back to ON (non-cache, scoped to this subdirectory)
    # so Qt's QML tooling doesn't assume a static Qt build and go looking
    # for static QML plugin targets that don't exist in this (dynamic)
    # system Qt install.
    set(BUILD_SHARED_LIBS ON)
    add_subdirectory(src/gui)
    target_compile_definitions(seabass PRIVATE
        $<$<BOOL:${SEABASS_EXPERIMENTAL}>:SEABASS_EXPERIMENTAL_BUILD>)
else()
    message(STATUS "Qt6 not found, skipping seabass (CLI and tests still build)")
endif()

# --- domain-layer unit tests (no infrastructure deps, no fixtures needed) ---

enable_testing()
add_executable(duplicate_cue_consolidation_test
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/track_matching.cpp
    tests/duplicate_cue_consolidation_test.cpp
)
target_include_directories(duplicate_cue_consolidation_test PRIVATE src)
add_test(NAME duplicate_cue_consolidation_test COMMAND duplicate_cue_consolidation_test)

add_executable(duplicate_cleanup_test
    src/domain/duplicate_cleanup.cpp
    src/domain/local_restore.cpp
    tests/duplicate_cleanup_test.cpp
)
target_include_directories(duplicate_cleanup_test PRIVATE src)
add_test(NAME duplicate_cleanup_test COMMAND duplicate_cleanup_test)

add_executable(library_consistency_test
    src/domain/library_consistency.cpp
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/track_matching.cpp
    tests/library_consistency_test.cpp
)
target_include_directories(library_consistency_test PRIVATE src)
add_test(NAME library_consistency_test COMMAND library_consistency_test)

add_executable(junk_cue_test
    src/domain/junk_cue.cpp
    tests/junk_cue_test.cpp
)
target_include_directories(junk_cue_test PRIVATE src)
add_test(NAME junk_cue_test COMMAND junk_cue_test)

add_executable(library_statistics_test
    src/domain/library_statistics.cpp
    tests/library_statistics_test.cpp
)
target_include_directories(library_statistics_test PRIVATE src)
add_test(NAME library_statistics_test COMMAND library_statistics_test)

add_executable(disk_usage_test
    src/domain/disk_usage.cpp
    tests/disk_usage_test.cpp
)
target_include_directories(disk_usage_test PRIVATE src)
add_test(NAME disk_usage_test COMMAND disk_usage_test)

add_executable(filesystem_compatibility_test
    src/domain/filesystem_compatibility.cpp
    tests/filesystem_compatibility_test.cpp
)
target_include_directories(filesystem_compatibility_test PRIVATE src)
add_test(NAME filesystem_compatibility_test COMMAND filesystem_compatibility_test)

add_executable(track_matching_test
    src/domain/track_matching.cpp
    tests/track_matching_test.cpp
)
target_include_directories(track_matching_test PRIVATE src)
add_test(NAME track_matching_test COMMAND track_matching_test)

add_executable(rekordbox_key_parser_test
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/rekordbox_key_parser_test.cpp
)
target_include_directories(rekordbox_key_parser_test PRIVATE src)
target_link_libraries(rekordbox_key_parser_test PRIVATE DjInterop)
add_test(NAME rekordbox_key_parser_test COMMAND rekordbox_key_parser_test)

add_executable(sync_planning_test
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    tests/sync_planning_test.cpp
)
target_include_directories(sync_planning_test PRIVATE src)
add_test(NAME sync_planning_test COMMAND sync_planning_test)

add_executable(cross_source_sync_conflict_test
    src/domain/cross_source_sync_conflict.cpp
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    tests/cross_source_sync_conflict_test.cpp
)
target_include_directories(cross_source_sync_conflict_test PRIVATE src)
add_test(NAME cross_source_sync_conflict_test COMMAND cross_source_sync_conflict_test)

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

add_executable(cue_color_normalization_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/rekordbox/kaitai_rekordbox_reader.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    tests/cue_color_normalization_test.cpp
)
target_include_directories(cue_color_normalization_test PRIVATE src)
target_link_libraries(cue_color_normalization_test PRIVATE DjInterop rekordbox_format kaitai_cpp_stl_runtime)
add_test(NAME cue_color_normalization_test COMMAND cue_color_normalization_test)

add_executable(anlz_cue_codec_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/anlz_cue_codec.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    tests/anlz_cue_codec_test.cpp
)
target_include_directories(anlz_cue_codec_test PRIVATE src)
add_test(NAME anlz_cue_codec_test COMMAND anlz_cue_codec_test)

add_executable(anlz_file_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    tests/anlz_file_test.cpp
)
target_include_directories(anlz_file_test PRIVATE src)
add_test(NAME anlz_file_test COMMAND anlz_file_test)

add_executable(local_restore_test
    src/domain/local_restore.cpp
    tests/local_restore_test.cpp
)
target_include_directories(local_restore_test PRIVATE src)
add_test(NAME local_restore_test COMMAND local_restore_test)

add_executable(durable_file_write_test
    src/infrastructure/durable_file_write.cpp
    tests/durable_file_write_test.cpp
)
target_include_directories(durable_file_write_test PRIVATE src)
add_test(NAME durable_file_write_test COMMAND durable_file_write_test)

add_executable(scratch_dir_guard_test
    tests/scratch_dir_guard_test.cpp
)
target_include_directories(scratch_dir_guard_test PRIVATE src)
add_test(NAME scratch_dir_guard_test COMMAND scratch_dir_guard_test)

add_executable(bulk_write_strategy_test
    src/infrastructure/bulk_write_strategy.cpp
    tests/bulk_write_strategy_test.cpp
)
target_include_directories(bulk_write_strategy_test PRIVATE src)
add_test(NAME bulk_write_strategy_test COMMAND bulk_write_strategy_test)

add_executable(filesystem_backup_store_test
    src/infrastructure/durable_file_write.cpp
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

add_executable(stick_speed_benchmark_test
    src/infrastructure/benchmark/stick_speed_benchmark.cpp
    tests/stick_speed_benchmark_test.cpp
)
target_include_directories(stick_speed_benchmark_test PRIVATE src)
add_test(NAME stick_speed_benchmark_test COMMAND stick_speed_benchmark_test)

add_executable(stick_benchmark_history_test
    src/infrastructure/benchmark/stick_benchmark_history.cpp
    tests/stick_benchmark_history_test.cpp
)
target_include_directories(stick_benchmark_history_test PRIVATE src)
target_link_libraries(stick_benchmark_history_test PRIVATE SQLite::SQLite3)
add_test(NAME stick_benchmark_history_test COMMAND stick_benchmark_history_test)

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

add_executable(pdb_row_writer_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    tests/pdb_row_writer_test.cpp
)
target_include_directories(pdb_row_writer_test PRIVATE src)
target_link_libraries(pdb_row_writer_test PRIVATE rekordbox_format)
add_test(NAME pdb_row_writer_test COMMAND pdb_row_writer_test)

add_executable(pdb_row_writer_string_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    tests/pdb_row_writer_string_test.cpp
)
target_include_directories(pdb_row_writer_string_test PRIVATE src)
target_link_libraries(pdb_row_writer_string_test PRIVATE rekordbox_format)
add_test(NAME pdb_row_writer_string_test COMMAND pdb_row_writer_string_test)

add_executable(rekordbox_library_anonymizer_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    src/infrastructure/rekordbox/rekordbox_library_anonymizer.cpp
    src/application/ports/progress_reporter.cpp
    tests/rekordbox_library_anonymizer_test.cpp
)
target_include_directories(rekordbox_library_anonymizer_test PRIVATE src)
target_link_libraries(rekordbox_library_anonymizer_test PRIVATE rekordbox_format)
add_test(NAME rekordbox_library_anonymizer_test COMMAND rekordbox_library_anonymizer_test)

add_executable(rekordbox_cleanup_writer_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/rekordbox_cleanup_writer.cpp
    tests/rekordbox_cleanup_writer_test.cpp
)
target_include_directories(rekordbox_cleanup_writer_test PRIVATE src)
target_link_libraries(rekordbox_cleanup_writer_test PRIVATE rekordbox_format)
add_test(NAME rekordbox_cleanup_writer_test COMMAND rekordbox_cleanup_writer_test)

add_executable(libdjinterop_engine_library_creator_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/engine/libdjinterop_engine_library_creator.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_library_creator_test.cpp
)
target_include_directories(libdjinterop_engine_library_creator_test PRIVATE src)
target_link_libraries(libdjinterop_engine_library_creator_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_library_creator_test COMMAND libdjinterop_engine_library_creator_test)

add_executable(libdjinterop_engine_sync_scratch_replace_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/engine/libdjinterop_engine_library_creator.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_sync_scratch_replace_test.cpp
)
target_include_directories(libdjinterop_engine_sync_scratch_replace_test PRIVATE src)
target_link_libraries(libdjinterop_engine_sync_scratch_replace_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_sync_scratch_replace_test COMMAND libdjinterop_engine_sync_scratch_replace_test)

add_executable(libdjinterop_engine_cleanup_writer_test
    src/infrastructure/engine/libdjinterop_engine_cleanup_writer.cpp
    tests/libdjinterop_engine_cleanup_writer_test.cpp
)
target_include_directories(libdjinterop_engine_cleanup_writer_test PRIVATE src)
target_link_libraries(libdjinterop_engine_cleanup_writer_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_cleanup_writer_test COMMAND libdjinterop_engine_cleanup_writer_test)

add_executable(duplicate_cleanup_interruption_test
    src/infrastructure/engine/libdjinterop_engine_cleanup_writer.cpp
    src/infrastructure/backup/filesystem_backup_store.cpp
    src/infrastructure/durable_file_write.cpp
    tests/duplicate_cleanup_interruption_test.cpp
)
target_include_directories(duplicate_cleanup_interruption_test PRIVATE src)
target_link_libraries(duplicate_cleanup_interruption_test PRIVATE DjInterop)
add_test(NAME duplicate_cleanup_interruption_test COMMAND duplicate_cleanup_interruption_test)

add_executable(libdjinterop_engine_anonymizer_test
    src/infrastructure/engine/libdjinterop_engine_anonymizer.cpp
    src/application/ports/progress_reporter.cpp
    tests/libdjinterop_engine_anonymizer_test.cpp
)
target_include_directories(libdjinterop_engine_anonymizer_test PRIVATE src)
target_link_libraries(libdjinterop_engine_anonymizer_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_anonymizer_test COMMAND libdjinterop_engine_anonymizer_test)

add_executable(anonymize_library_test
    src/application/use_cases/anonymize_library.cpp
    src/application/ports/progress_reporter.cpp
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    src/infrastructure/rekordbox/rekordbox_library_anonymizer.cpp
    src/infrastructure/engine/libdjinterop_engine_anonymizer.cpp
    src/infrastructure/zip_archive_writer.cpp
    tests/anonymize_library_test.cpp
)
target_include_directories(anonymize_library_test PRIVATE src)
target_link_libraries(anonymize_library_test PRIVATE rekordbox_format DjInterop ZLIB::ZLIB)
add_test(NAME anonymize_library_test COMMAND anonymize_library_test)

add_executable(libdjinterop_engine_cue_writer_propagate_test
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_cue_writer_propagate_test.cpp
)
target_include_directories(libdjinterop_engine_cue_writer_propagate_test PRIVATE src)
target_link_libraries(libdjinterop_engine_cue_writer_propagate_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_cue_writer_propagate_test COMMAND libdjinterop_engine_cue_writer_propagate_test)

add_executable(pending_deletion_manifest_test
    src/infrastructure/cleanup/pending_deletion_manifest.cpp
    tests/pending_deletion_manifest_test.cpp
)
target_include_directories(pending_deletion_manifest_test PRIVATE src)
add_test(NAME pending_deletion_manifest_test COMMAND pending_deletion_manifest_test)

add_executable(pending_deletion_applier_test
    src/infrastructure/cleanup/pending_deletion_manifest.cpp
    src/infrastructure/cleanup/pending_deletion_applier.cpp
    tests/pending_deletion_applier_test.cpp
)
target_include_directories(pending_deletion_applier_test PRIVATE src)
add_test(NAME pending_deletion_applier_test COMMAND pending_deletion_applier_test)

add_executable(zip_archive_writer_test
    src/infrastructure/zip_archive_writer.cpp
    tests/zip_archive_writer_test.cpp
)
target_include_directories(zip_archive_writer_test PRIVATE src)
target_link_libraries(zip_archive_writer_test PRIVATE ZLIB::ZLIB)
add_test(NAME zip_archive_writer_test COMMAND zip_archive_writer_test)

add_executable(pending_deletion_resolver_test
    src/infrastructure/cleanup/pending_deletion_resolver.cpp
    tests/pending_deletion_resolver_test.cpp
)
target_include_directories(pending_deletion_resolver_test PRIVATE src)
add_test(NAME pending_deletion_resolver_test COMMAND pending_deletion_resolver_test)

# Cross-platform now, talks to libsqlcipher via runtime LoadLibrary/
# dlopen, same as seabass_core's own onelibrary sources above. Needs
# libsqlcipher's runtime library actually present on whichever machine
# runs this test (see the target_sources block above for package names).
add_executable(onelibrary_cue_writer_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/onelibrary/onelibrary_key.cpp
    src/infrastructure/onelibrary/sqlcipher_dyn.cpp
    src/infrastructure/onelibrary/onelibrary_cue_writer.cpp
    src/infrastructure/onelibrary/onelibrary_reader.cpp
    tests/onelibrary_cue_writer_test.cpp
)
target_include_directories(onelibrary_cue_writer_test PRIVATE src)
target_link_libraries(onelibrary_cue_writer_test PRIVATE ZLIB::ZLIB)
if(NOT WIN32)
    target_link_libraries(onelibrary_cue_writer_test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_test(NAME onelibrary_cue_writer_test COMMAND onelibrary_cue_writer_test)

add_executable(onelibrary_cue_writer_adapter_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/onelibrary/onelibrary_key.cpp
    src/infrastructure/onelibrary/sqlcipher_dyn.cpp
    src/infrastructure/onelibrary/onelibrary_cue_writer.cpp
    src/infrastructure/onelibrary/onelibrary_reader.cpp
    tests/onelibrary_cue_writer_adapter_test.cpp
)
target_include_directories(onelibrary_cue_writer_adapter_test PRIVATE src)
target_link_libraries(onelibrary_cue_writer_adapter_test PRIVATE ZLIB::ZLIB)
if(NOT WIN32)
    target_link_libraries(onelibrary_cue_writer_adapter_test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_test(NAME onelibrary_cue_writer_adapter_test COMMAND onelibrary_cue_writer_adapter_test)

# Covers OneLibraryReader::readAll(), the read side (content/artist/key/
# image/playlist/cue joins), independent of the write-path fixture above.
add_executable(onelibrary_reader_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/onelibrary/onelibrary_key.cpp
    src/infrastructure/onelibrary/sqlcipher_dyn.cpp
    src/infrastructure/onelibrary/onelibrary_cue_writer.cpp
    src/infrastructure/onelibrary/onelibrary_reader.cpp
    tests/onelibrary_reader_test.cpp
)
target_include_directories(onelibrary_reader_test PRIVATE src)
target_link_libraries(onelibrary_reader_test PRIVATE ZLIB::ZLIB)
if(NOT WIN32)
    target_link_libraries(onelibrary_reader_test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_test(NAME onelibrary_reader_test COMMAND onelibrary_reader_test)

# --- Integration suite: real use cases against tests/fixtures/anonymized_library ---
# --- (a committed, de-identified copy of a real ~1,400-track library) ---
# --- -- slower, gated with CTest LABEL "integration" so a default ---
# --- `ctest` run (or `ctest -LE integration`) skips them; run before a ---
# --- larger change or release with `ctest -L integration`. See ---
# --- docs/testing.md and BRAINSTORM.md's "For Developers, Maintainers ---
# --- and Testing". Links the whole seabass_core (unlike every test ---
# --- above, which links just the handful of .cpp files it needs) ---
# --- since this genuinely exercises most of the real app's read/write/ ---
# --- sync/stats/consistency machinery together, the same way seabass- ---
# --- cli itself does. ---

add_executable(anonymized_fixture_integration_test
    tests/anonymized_fixture_integration_test.cpp
)
target_include_directories(anonymized_fixture_integration_test PRIVATE src)
target_link_libraries(anonymized_fixture_integration_test PRIVATE seabass_core)
target_compile_definitions(anonymized_fixture_integration_test PRIVATE
    SEABASS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME anonymized_fixture_integration_test COMMAND anonymized_fixture_integration_test)
set_tests_properties(anonymized_fixture_integration_test PROPERTIES LABELS "integration")
