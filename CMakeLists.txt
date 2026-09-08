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
#
# .gitmodules itself is intentionally not a tracked file in this repo
# (see .gitignore and scripts/init-submodules.sh for why: KDE Invent,
# our canonical host, rejects any pushed commit containing a file
# literally named .gitmodules). The real submodule config lives in
# cmake/dependency-submodules.txt instead; this step regenerates the
# real .gitmodules from it and initializes the submodules below on
# every configure, so a fresh checkout just works with plain `cmake`.
find_package(Git QUIET)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/cmake/dependency-submodules.txt
            ${CMAKE_SOURCE_DIR}/.gitmodules
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule sync --recursive
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE _seabass_submodule_result
    )
    if(NOT _seabass_submodule_result EQUAL 0)
        message(FATAL_ERROR "Failed to initialize git submodules -- see scripts/init-submodules.sh")
    endif()
else()
    message(WARNING "git not found -- skipping automatic submodule init; run scripts/init-submodules.sh manually, or ensure third_party/libdjinterop and third_party/kaitai_struct_cpp_stl_runtime are already populated")
endif()

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

# Independent ZIP implementations for the stick-backup cross-validation
# test (tests/backup_archive_crossvalidation_test.cpp). All optional: the
# test skips (exit 77) without python3 and skips the individual tool
# cases without unzip / 7z.
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(UNZIP_EXECUTABLE unzip)
find_program(SEVENZIP_EXECUTABLE NAMES 7z 7zz 7za)

# Only needed by stick_write_lock_test, which spawns a real std::thread to
# prove StickWriteLock actually excludes a concurrent locker.
find_package(Threads REQUIRED)

# TagLib: reads artist/title/duration/bitrate straight out of an audio
# file, for files no catalog references and for catalog rows that left
# fields empty. Optional, exactly like Qt6::Multimedia below -- without
# it every caller falls back to application::NullTrackMetadataProbe and
# behaves correctly, just less usefully.
#
# find_package(TagLib) only works with TagLib 2.x, which ships a CMake
# config; 1.x (what Debian/Ubuntu's libtag1-dev and MSYS2's
# mingw-w64-ucrt-x86_64-taglib still provide) ships only taglib.pc, so
# pkg-config is tried first and is the path that actually resolves on
# both platforms today.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB QUIET IMPORTED_TARGET taglib)
endif()
if(NOT TAGLIB_FOUND)
    find_package(TagLib QUIET)
endif()

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
    src/domain/camelot_key.cpp
    src/domain/cross_source_sync_conflict.cpp
    src/domain/disk_usage.cpp
    src/domain/duplicate_cleanup.cpp
    src/domain/library_fingerprint.cpp
    src/application/use_cases/advise_stick_backup.cpp
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
    src/domain/track_scope.cpp
    src/domain/usb_filesystem.cpp
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
    src/infrastructure/local/app_data_directory.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/local/file_library_edit_lock_store.cpp
    src/infrastructure/system/rekordbox_process_detector.cpp
    src/infrastructure/system/process_liveness.cpp
    src/infrastructure/system/stick_hardware_info.cpp
    src/infrastructure/zip_archive_writer.cpp
    src/infrastructure/hashing/sha256.cpp
    src/infrastructure/stick_backup/posix_archive_file.cpp
    src/infrastructure/stick_backup/zip_format.cpp
    src/infrastructure/stick_backup/zip64_writer.cpp
    src/infrastructure/stick_backup/zip64_reader.cpp
    src/infrastructure/stick_backup/backup_manifest.cpp
    src/infrastructure/stick_backup/archive_journal.cpp
    src/infrastructure/stick_backup/archive_updater.cpp
    src/infrastructure/stick_backup/archive_recovery.cpp
    src/infrastructure/long_paths.cpp
    src/infrastructure/stick_backup/stick_tree_walker.cpp
    src/infrastructure/stick_backup/stat_diff.cpp
    src/infrastructure/stick_backup/sqlite_db_set.cpp
    src/infrastructure/stick_backup/library_catalog_mtime.cpp
    src/infrastructure/stick_backup/archive_compactor.cpp
    src/infrastructure/stick_backup/restore_path_sanitizer.cpp
    src/infrastructure/engine/engine_restore_check.cpp
    src/application/use_cases/backup_stick.cpp
    src/application/use_cases/compact_stick_backup.cpp
    src/application/use_cases/restore_stick_backup.cpp
    src/application/use_cases/clone_stick.cpp
    src/application/use_cases/fill_missing_durations.cpp
    src/infrastructure/local/duration_cache.cpp
    src/infrastructure/local/metadata_cache.cpp
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
        src/infrastructure/media/windows_usb_formatter.cpp
        src/infrastructure/process/run_command_windows.cpp
    )
else()
    target_sources(seabass_core PRIVATE
        src/infrastructure/media/linux_removable_media_locator.cpp
        src/infrastructure/media/linux_udev_media_monitor.cpp
        src/infrastructure/media/udisksctl_media_mounter.cpp
        src/infrastructure/media/linux_usb_formatter.cpp
        src/infrastructure/process/run_command_posix.cpp
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

# Test/QuickTest are OPTIONAL: they're packaged separately from the core
# Qt6 modules on several distros (e.g. Debian/Ubuntu's libqt6quicktest6-dev
# vs libqt6qml6-dev), and find_package(Qt6 COMPONENTS ...) requires every
# listed component -- putting them in the required list would silently
# fail Qt6_FOUND (and so skip the whole GUI build, not just the QML tests)
# on any install that has the mandatory modules but not QuickTest.
find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick QuickControls2 QuickLayouts QuickEffects QuickDialogs2 QuickShapes Multimedia Concurrent Svg
    OPTIONAL_COMPONENTS Test QuickTest)
# --- QtMultimedia duration probe (see TrackDurationProbe) ---
#
# Deliberately its own tiny target rather than sources in seabass_core:
# the core library, seabass-cli and the Qt-free corpus_test must keep
# linking no Qt at all. Only this target sees Qt6::Multimedia, and both
# seabass-cli and the GUI link it when Qt was found. Where it was not,
# callers fall back to application::NullTrackDurationProbe and the build
# is unchanged.
#
# No AUTOMOC: QtMultimediaDurationProbe has no Q_OBJECT of its own (it
# connects with a context object + lambda), so nothing needs moc'ing.
# TagLib-backed metadata probe. Same optional shape as seabass_audio_qt
# below, and independent of it: a build can have either, both or
# neither.
if(TAGLIB_FOUND OR TARGET TagLib::tag)
    add_library(seabass_taglib STATIC src/infrastructure/audio/taglib_metadata_probe.cpp)
    target_include_directories(seabass_taglib PUBLIC src)
    if(TARGET PkgConfig::TAGLIB)
        target_link_libraries(seabass_taglib PUBLIC PkgConfig::TAGLIB)
    else()
        target_link_libraries(seabass_taglib PUBLIC TagLib::tag)
    endif()
    target_compile_definitions(seabass_taglib PUBLIC SEABASS_HAVE_TAGLIB)
    # seabass-cli is declared above this point, so it is linked here
    # rather than at its own definition, same as seabass_audio_qt.
    target_link_libraries(seabass-cli PRIVATE seabass_taglib)
endif()

if(TARGET Qt6::Multimedia)
    add_library(seabass_audio_qt STATIC src/infrastructure/audio/qt_multimedia_duration_probe.cpp)
    target_include_directories(seabass_audio_qt PUBLIC src)
    target_link_libraries(seabass_audio_qt PUBLIC Qt6::Core Qt6::Multimedia)
    target_compile_definitions(seabass_audio_qt PUBLIC SEABASS_HAVE_QT_AUDIO)
    # seabass-cli is declared above this point, so it is linked here
    # rather than at its own definition.
    target_link_libraries(seabass-cli PRIVATE seabass_audio_qt)
endif()

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
    if(TARGET seabass_audio_qt)
        target_link_libraries(seabass PRIVATE seabass_audio_qt)
    endif()
    if(TARGET seabass_taglib)
        target_link_libraries(seabass PRIVATE seabass_taglib)
    endif()
    target_compile_definitions(seabass PRIVATE
        $<$<BOOL:${SEABASS_EXPERIMENTAL}>:SEABASS_EXPERIMENTAL_BUILD>)

    # --- QtQuickTest: QML-level UI tests (component behavior, not the ---
    # --- domain/controller logic the C++ tests above already cover) ---
    #
    # Gated on Qt6Test_FOUND/Qt6QuickTest_FOUND (set by the OPTIONAL_COMPONENTS
    # find_package call above regardless of whether they were found) -- an
    # install with the mandatory Qt6 modules but not QuickTest still gets
    # the full seabass GUI build, just without this one test target.
    if(Qt6Test_FOUND AND Qt6QuickTest_FOUND)
        # A separate, test-only "SeabassGui" module registration (URI, and
        # the exact QML files under test, listed again here) rather than
        # reusing seabass's own qt_add_qml_module() call: that one is
        # scoped to the `seabass` executable target, not a separately
        # linkable library, so there's nothing for a second binary to
        # import. Both targets registering the same URI is fine -- they're
        # two independent, self-contained binaries, no shared linkage,
        # same as how every C++ domain test below lists just the .cpp
        # files it needs instead of linking one shared library.
        #
        # Test *.qml files themselves are NOT compiled in here --
        # QUICK_TEST_MAIN scans a real directory for tst_*.qml at run time
        # (see the -input arg on the add_test() call below), so editing/
        # adding a test doesn't need a rebuild, only the components-under-
        # test do.
        qt_add_executable(seabass_qml_tests tests/qml/qml_test_main.cpp)
        # Must run before qt_add_qml_module() -- it reads this property at
        # call time (see src/gui/CMakeLists.txt's own comment on the exact
        # same ordering requirement for the real seabass target). Set
        # after instead, Theme.qml compiles as a plain (non-singleton)
        # type, and every bare `Theme.xxx` reference elsewhere silently
        # evaluates to undefined rather than erroring -- confusing to
        # track down the first time.
        set_source_files_properties(src/gui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        # Same ordering requirement as Theme.qml above -- must run before
        # qt_add_qml_module() or CamelotWheelPosition silently registers as
        # a plain (non-singleton) type and every `CamelotWheelPosition.x`
        # style access in CamelotWheelPopup.qml goes undefined.
        set_source_files_properties(src/gui/qml/CamelotWheelPosition.qml
            PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

        # The same module the app registers (see src/gui/CMakeLists.txt's
        # SEABASS_GUI_* lists): every page with its real controller, so
        # tests/qml-live/ can drive the real code against a real stick.
        qt_add_qml_module(seabass_qml_tests
            URI SeabassGui
            VERSION 1.0
            QML_FILES
                ${SEABASS_GUI_QML_FILES_FROM_ROOT}
            RESOURCES
                src/gui/qml/icons/seabass_soundbass.svg
            SOURCES
                ${SEABASS_GUI_SOURCES_FROM_ROOT}
        )
        target_link_libraries(seabass_qml_tests PRIVATE
            seabass_core
            Qt6::QuickTest
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::QuickLayouts
            Qt6::QuickEffects
            Qt6::QuickDialogs2
            Qt6::QuickShapes
            Qt6::Multimedia
            Qt6::Concurrent
            Qt6::Svg
        )
        # The generated qmltyperegistrations.cpp #includes each QML_ELEMENT
        # header via <angle brackets> (see e.g. seabass_qmltyperegistrations.cpp
        # in the build dir), resolved against the include path -- src/gui/
        # itself needs to be on it for <system_font_metrics.hpp> to resolve.
        target_include_directories(seabass_qml_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/gui
            ${CMAKE_SOURCE_DIR}/src/gui/edit)

        add_test(NAME seabass_qml_tests
            COMMAND seabass_qml_tests -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/qml)
        # No real display needed -- Qt's "offscreen" platform plugin renders
        # without one, which is both faster and far more reliable in a
        # headless/CI/agent environment than anything Xvfb-dependent.
        set_tests_properties(seabass_qml_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        message(STATUS "Qt6::QuickTest not found, skipping seabass_qml_tests (GUI build unaffected)")
    endif()
else()
    message(STATUS "Qt6 not found, skipping seabass (CLI and tests still build)")
endif()

# --- domain-layer unit tests (no infrastructure deps, no fixtures needed) ---

enable_testing()
add_executable(duration_cache_test tests/duration_cache_test.cpp)
target_include_directories(duration_cache_test PRIVATE src)
target_link_libraries(duration_cache_test PRIVATE seabass_core)
add_test(NAME duration_cache_test COMMAND duration_cache_test)

add_executable(metadata_cache_test tests/metadata_cache_test.cpp)
target_include_directories(metadata_cache_test PRIVATE src)
target_link_libraries(metadata_cache_test PRIVATE seabass_core)
add_test(NAME metadata_cache_test COMMAND metadata_cache_test)

# Only built when TagLib was found, same as the probe it covers.
if(TARGET seabass_taglib)
    add_executable(taglib_metadata_probe_test tests/taglib_metadata_probe_test.cpp)
    target_link_libraries(taglib_metadata_probe_test PRIVATE seabass_taglib)
    add_test(NAME taglib_metadata_probe_test COMMAND taglib_metadata_probe_test)
endif()

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

add_executable(camelot_key_test
    src/domain/camelot_key.cpp
    tests/camelot_key_test.cpp
)
target_include_directories(camelot_key_test PRIVATE src)
add_test(NAME camelot_key_test COMMAND camelot_key_test)

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

add_executable(format_usb_stick_test
    src/domain/usb_filesystem.cpp
    src/application/ports/progress_reporter.cpp
    tests/format_usb_stick_test.cpp
)
target_include_directories(format_usb_stick_test PRIVATE src)
add_test(NAME format_usb_stick_test COMMAND format_usb_stick_test)

add_executable(cross_source_sync_conflict_test
    src/domain/cross_source_sync_conflict.cpp
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    tests/cross_source_sync_conflict_test.cpp
)
target_include_directories(cross_source_sync_conflict_test PRIVATE src)
add_test(NAME cross_source_sync_conflict_test COMMAND cross_source_sync_conflict_test)

add_executable(track_scope_test
    src/domain/track_scope.cpp
    tests/track_scope_test.cpp
)
target_include_directories(track_scope_test PRIVATE src)
add_test(NAME track_scope_test COMMAND track_scope_test)

# library_catalog_cache.cpp lives in src/gui/ (it's the format-string ->
# concrete-reader dispatch every controller's own scan-task function
# already did, just consolidated) but needs no Qt itself -- realScan()/
# realMtime() reference the rekordbox/engine/onelibrary readers directly,
# so this links the full seabass_core (which already PUBLIC-links
# rekordbox_format/DjInterop/SQLite3) rather than hand-picking individual
# infrastructure .cpp files. The test itself only exercises the injected-
# fake constructor, never realScan()/realMtime(), but the linker still
# needs those symbols to resolve since they're compiled into the same
# translation unit.
add_executable(library_catalog_cache_test
    src/gui/library_catalog_cache.cpp
    tests/library_catalog_cache_test.cpp
)
target_include_directories(library_catalog_cache_test PRIVATE src)
target_link_libraries(library_catalog_cache_test PRIVATE seabass_core Threads::Threads)
add_test(NAME library_catalog_cache_test COMMAND library_catalog_cache_test)

add_executable(library_fingerprint_test
    tests/library_fingerprint_test.cpp
)
target_include_directories(library_fingerprint_test PRIVATE src)
target_link_libraries(library_fingerprint_test PRIVATE seabass_core)
add_test(NAME library_fingerprint_test COMMAND library_fingerprint_test)

add_executable(advise_stick_backup_test
    tests/advise_stick_backup_test.cpp
)
target_include_directories(advise_stick_backup_test PRIVATE src)
target_link_libraries(advise_stick_backup_test PRIVATE seabass_core)
add_test(NAME advise_stick_backup_test COMMAND advise_stick_backup_test)

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
    src/infrastructure/work_counters.cpp
    tests/cue_color_normalization_test.cpp
)
target_include_directories(cue_color_normalization_test PRIVATE src)
target_link_libraries(cue_color_normalization_test PRIVATE DjInterop rekordbox_format kaitai_cpp_stl_runtime)
add_test(NAME cue_color_normalization_test COMMAND cue_color_normalization_test)

add_executable(anlz_cue_codec_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/anlz_cue_codec.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    tests/anlz_cue_codec_test.cpp
)
target_include_directories(anlz_cue_codec_test PRIVATE src)
add_test(NAME anlz_cue_codec_test COMMAND anlz_cue_codec_test)

add_executable(anlz_file_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    tests/anlz_file_test.cpp
)
target_include_directories(anlz_file_test PRIVATE src)
add_test(NAME anlz_file_test COMMAND anlz_file_test)

add_executable(rekordbox_loop_cue_test
    tests/rekordbox_loop_cue_test.cpp
)
target_include_directories(rekordbox_loop_cue_test PRIVATE src)
target_link_libraries(rekordbox_loop_cue_test PRIVATE rekordbox_format)
add_test(NAME rekordbox_loop_cue_test COMMAND rekordbox_loop_cue_test)

add_executable(local_restore_test
    src/domain/local_restore.cpp
    tests/local_restore_test.cpp
)
target_include_directories(local_restore_test PRIVATE src)
add_test(NAME local_restore_test COMMAND local_restore_test)

add_executable(durable_file_write_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
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
    src/infrastructure/work_counters.cpp
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

add_executable(stick_identity_test tests/stick_identity_test.cpp)
target_include_directories(stick_identity_test PRIVATE src)
add_test(NAME stick_identity_test COMMAND stick_identity_test)

add_executable(stick_presence_diff_test tests/stick_presence_diff_test.cpp)
target_include_directories(stick_presence_diff_test PRIVATE src)
add_test(NAME stick_presence_diff_test COMMAND stick_presence_diff_test)

# Forks a child process on POSIX to prove the pid-liveness staleness rule
# against a real second process, not just an injected fake.
add_executable(library_edit_lock_store_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/local/app_data_directory.cpp
    src/infrastructure/local/file_library_edit_lock_store.cpp
    src/infrastructure/system/process_liveness.cpp
    tests/library_edit_lock_store_test.cpp
)
target_include_directories(library_edit_lock_store_test PRIVATE src)
add_test(NAME library_edit_lock_store_test COMMAND library_edit_lock_store_test)

# The edit-session save loop and its per-format write target live in
# src/gui/edit but are plain Qt Core code (QString, no widgets), so they
# are testable headless.
if(TARGET Qt6::Core)
    add_executable(edit_session_save_loop_test
        src/gui/edit/save_context.cpp
        src/gui/edit/save_loop.cpp
        tests/edit_session_save_loop_test.cpp
    )
    target_include_directories(edit_session_save_loop_test PRIVATE src)
    target_link_libraries(edit_session_save_loop_test PRIVATE seabass_core Qt6::Core)
    add_test(NAME edit_session_save_loop_test COMMAND edit_session_save_loop_test)

    add_executable(format_write_session_test
        src/gui/edit/save_context.cpp
        src/gui/edit/format_write_session.cpp
        tests/format_write_session_test.cpp
    )
    target_include_directories(format_write_session_test PRIVATE src)
    target_link_libraries(format_write_session_test PRIVATE seabass_core Qt6::Core)
    add_test(NAME format_write_session_test COMMAND format_write_session_test)
endif()

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
    src/infrastructure/local/app_data_directory.cpp
    src/infrastructure/local/local_cue_store.cpp
    tests/local_cue_store_test.cpp
)
target_include_directories(local_cue_store_test PRIVATE src)
target_link_libraries(local_cue_store_test PRIVATE SQLite::SQLite3 ZLIB::ZLIB)
add_test(NAME local_cue_store_test COMMAND local_cue_store_test)

add_executable(pdb_row_writer_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    tests/pdb_row_writer_test.cpp
)
target_include_directories(pdb_row_writer_test PRIVATE src)
target_link_libraries(pdb_row_writer_test PRIVATE rekordbox_format ZLIB::ZLIB)
add_test(NAME pdb_row_writer_test COMMAND pdb_row_writer_test)

# Uses the committed real-library fixture rather than a synthetic .pdb:
# the staleness guard compares the whole file's bytes, so exercising it
# against a real 1.5MB export.pdb is closer to what it actually protects
# than a hand-built two-page buffer would be. SEABASS_SOURCE_DIR is how
# it finds that fixture, same as anonymized_fixture_integration_test.
add_executable(pdb_row_writer_staleness_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    tests/pdb_row_writer_staleness_test.cpp
)
target_include_directories(pdb_row_writer_staleness_test PRIVATE src)
target_link_libraries(pdb_row_writer_staleness_test PRIVATE rekordbox_format ZLIB::ZLIB)
target_compile_definitions(pdb_row_writer_staleness_test PRIVATE
    SEABASS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME pdb_row_writer_staleness_test COMMAND pdb_row_writer_staleness_test)

add_executable(pdb_row_writer_string_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    tests/pdb_row_writer_string_test.cpp
)
target_include_directories(pdb_row_writer_string_test PRIVATE src)
target_link_libraries(pdb_row_writer_string_test PRIVATE rekordbox_format ZLIB::ZLIB)
add_test(NAME pdb_row_writer_string_test COMMAND pdb_row_writer_string_test)

add_executable(rekordbox_library_anonymizer_test
    src/infrastructure/long_paths.cpp
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/pdb_lookup.cpp
    src/infrastructure/rekordbox/anlz_file.cpp
    src/infrastructure/rekordbox/rekordbox_library_anonymizer.cpp
    src/application/ports/progress_reporter.cpp
    tests/rekordbox_library_anonymizer_test.cpp
)
target_include_directories(rekordbox_library_anonymizer_test PRIVATE src)
target_link_libraries(rekordbox_library_anonymizer_test PRIVATE rekordbox_format ZLIB::ZLIB)
add_test(NAME rekordbox_library_anonymizer_test COMMAND rekordbox_library_anonymizer_test)

add_executable(rekordbox_cleanup_writer_test
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/rekordbox/pdb_row_writer.cpp
    src/infrastructure/rekordbox/rekordbox_cleanup_writer.cpp
    tests/rekordbox_cleanup_writer_test.cpp
)
target_include_directories(rekordbox_cleanup_writer_test PRIVATE src)
target_link_libraries(rekordbox_cleanup_writer_test PRIVATE rekordbox_format ZLIB::ZLIB)
add_test(NAME rekordbox_cleanup_writer_test COMMAND rekordbox_cleanup_writer_test)

add_executable(libdjinterop_engine_library_creator_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/engine/libdjinterop_engine_library_creator.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/work_counters.cpp
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
    src/infrastructure/work_counters.cpp
    src/infrastructure/engine/libdjinterop_engine_library_creator.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_sync_scratch_replace_test.cpp
)
target_include_directories(libdjinterop_engine_sync_scratch_replace_test PRIVATE src)
target_link_libraries(libdjinterop_engine_sync_scratch_replace_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_sync_scratch_replace_test COMMAND libdjinterop_engine_sync_scratch_replace_test)

add_executable(libdjinterop_engine_loop_cue_test
    src/application/ports/progress_reporter.cpp
    src/infrastructure/engine/libdjinterop_engine_library_creator.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_loop_cue_test.cpp
)
target_include_directories(libdjinterop_engine_loop_cue_test PRIVATE src)
target_link_libraries(libdjinterop_engine_loop_cue_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_loop_cue_test COMMAND libdjinterop_engine_loop_cue_test)

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
    src/infrastructure/work_counters.cpp
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
    src/infrastructure/long_paths.cpp
    src/application/use_cases/anonymize_library.cpp
    src/application/ports/progress_reporter.cpp
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
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
    src/infrastructure/work_counters.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_cue_writer_propagate_test.cpp
)
target_include_directories(libdjinterop_engine_cue_writer_propagate_test PRIVATE src)
target_link_libraries(libdjinterop_engine_cue_writer_propagate_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_cue_writer_propagate_test COMMAND libdjinterop_engine_cue_writer_propagate_test)

add_executable(libdjinterop_engine_cue_writer_main_cue_test
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/engine/rekordbox_key_parser.cpp
    tests/libdjinterop_engine_cue_writer_main_cue_test.cpp
)
target_include_directories(libdjinterop_engine_cue_writer_main_cue_test PRIVATE src)
target_link_libraries(libdjinterop_engine_cue_writer_main_cue_test PRIVATE DjInterop)
add_test(NAME libdjinterop_engine_cue_writer_main_cue_test COMMAND libdjinterop_engine_cue_writer_main_cue_test)

add_executable(pending_deletion_manifest_test
    src/infrastructure/cleanup/pending_deletion_manifest.cpp
    tests/pending_deletion_manifest_test.cpp
)
target_include_directories(pending_deletion_manifest_test PRIVATE src)
add_test(NAME pending_deletion_manifest_test COMMAND pending_deletion_manifest_test)

add_executable(pending_deletion_applier_test
    src/infrastructure/long_paths.cpp
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

# --- stick backup (docs/stick-backup-plan.md) ---

add_executable(sha256_test
    src/infrastructure/hashing/sha256.cpp
    tests/sha256_test.cpp
)
target_include_directories(sha256_test PRIVATE src)
add_test(NAME sha256_test COMMAND sha256_test)

add_executable(archive_file_test
    src/infrastructure/stick_backup/posix_archive_file.cpp
    tests/archive_file_test.cpp
)
target_include_directories(archive_file_test PRIVATE src)
add_test(NAME archive_file_test COMMAND archive_file_test)

set(SEABASS_STICK_BACKUP_FORMAT_SOURCES
    src/infrastructure/hashing/sha256.cpp
    src/infrastructure/stick_backup/posix_archive_file.cpp
    src/infrastructure/stick_backup/zip_format.cpp
    src/infrastructure/stick_backup/zip64_writer.cpp
    src/infrastructure/stick_backup/zip64_reader.cpp
    src/infrastructure/stick_backup/backup_manifest.cpp
)

add_executable(backup_archive_roundtrip_test
    ${SEABASS_STICK_BACKUP_FORMAT_SOURCES}
    tests/backup_archive_roundtrip_test.cpp
)
target_include_directories(backup_archive_roundtrip_test PRIVATE src)
target_link_libraries(backup_archive_roundtrip_test PRIVATE ZLIB::ZLIB)
add_test(NAME backup_archive_roundtrip_test COMMAND backup_archive_roundtrip_test)

add_executable(backup_archive_crossvalidation_test
    ${SEABASS_STICK_BACKUP_FORMAT_SOURCES}
    tests/backup_archive_crossvalidation_test.cpp
)
target_include_directories(backup_archive_crossvalidation_test PRIVATE src)
target_link_libraries(backup_archive_crossvalidation_test PRIVATE ZLIB::ZLIB)
add_test(NAME backup_archive_crossvalidation_test COMMAND backup_archive_crossvalidation_test)
set_tests_properties(backup_archive_crossvalidation_test PROPERTIES
    ENVIRONMENT "SEABASS_PYTHON3=${Python3_EXECUTABLE};SEABASS_UNZIP=${UNZIP_EXECUTABLE};SEABASS_SEVENZIP=${SEVENZIP_EXECUTABLE}"
    SKIP_RETURN_CODE 77
)

set(SEABASS_STICK_BACKUP_UPDATE_SOURCES
    ${SEABASS_STICK_BACKUP_FORMAT_SOURCES}
    src/infrastructure/stick_backup/archive_journal.cpp
    src/infrastructure/stick_backup/archive_updater.cpp
    src/infrastructure/stick_backup/archive_recovery.cpp
)

add_executable(backup_archive_incremental_update_test
    ${SEABASS_STICK_BACKUP_UPDATE_SOURCES}
    tests/backup_archive_incremental_update_test.cpp
)
target_include_directories(backup_archive_incremental_update_test PRIVATE src)
target_link_libraries(backup_archive_incremental_update_test PRIVATE ZLIB::ZLIB)
add_test(NAME backup_archive_incremental_update_test COMMAND backup_archive_incremental_update_test)

add_executable(backup_archive_truncation_fuzz_test
    ${SEABASS_STICK_BACKUP_UPDATE_SOURCES}
    tests/backup_archive_truncation_fuzz_test.cpp
)
target_include_directories(backup_archive_truncation_fuzz_test PRIVATE src)
target_link_libraries(backup_archive_truncation_fuzz_test PRIVATE ZLIB::ZLIB)
add_test(NAME backup_archive_truncation_fuzz_test COMMAND backup_archive_truncation_fuzz_test)

set(SEABASS_STICK_BACKUP_STICK_SOURCES
    ${SEABASS_STICK_BACKUP_UPDATE_SOURCES}
    src/infrastructure/long_paths.cpp
    src/infrastructure/stick_backup/stick_tree_walker.cpp
    src/infrastructure/stick_backup/stat_diff.cpp
    src/infrastructure/stick_backup/sqlite_db_set.cpp
    src/infrastructure/stick_backup/library_catalog_mtime.cpp
    src/infrastructure/backup/stick_write_lock.cpp
)

add_executable(library_catalog_mtime_test
    src/infrastructure/long_paths.cpp
    src/infrastructure/stick_backup/stick_tree_walker.cpp
    src/infrastructure/stick_backup/library_catalog_mtime.cpp
    tests/library_catalog_mtime_test.cpp
)
target_include_directories(library_catalog_mtime_test PRIVATE src)
add_test(NAME library_catalog_mtime_test COMMAND library_catalog_mtime_test)

add_executable(long_paths_test
    src/infrastructure/long_paths.cpp
    src/infrastructure/hashing/sha256.cpp
    src/infrastructure/stick_backup/backup_manifest.cpp
    src/infrastructure/stick_backup/stick_tree_walker.cpp
    tests/long_paths_test.cpp
)
target_include_directories(long_paths_test PRIVATE src)
add_test(NAME long_paths_test COMMAND long_paths_test)

add_executable(stat_diff_test
    src/infrastructure/hashing/sha256.cpp
    src/infrastructure/stick_backup/backup_manifest.cpp
    src/infrastructure/long_paths.cpp
    src/infrastructure/stick_backup/stick_tree_walker.cpp
    src/infrastructure/stick_backup/stat_diff.cpp
    tests/stat_diff_test.cpp
)
target_include_directories(stat_diff_test PRIVATE src)
add_test(NAME stat_diff_test COMMAND stat_diff_test)

add_executable(backup_database_capture_test
    ${SEABASS_STICK_BACKUP_STICK_SOURCES}
    tests/backup_database_capture_test.cpp
)
target_include_directories(backup_database_capture_test PRIVATE src)
target_link_libraries(backup_database_capture_test PRIVATE ZLIB::ZLIB SQLite::SQLite3)
add_test(NAME backup_database_capture_test COMMAND backup_database_capture_test)

add_executable(backup_stick_test
    ${SEABASS_STICK_BACKUP_STICK_SOURCES}
    src/application/ports/progress_reporter.cpp
    src/application/use_cases/backup_stick.cpp
    tests/backup_stick_test.cpp
)
target_include_directories(backup_stick_test PRIVATE src)
target_link_libraries(backup_stick_test PRIVATE ZLIB::ZLIB SQLite::SQLite3)
add_test(NAME backup_stick_test COMMAND backup_stick_test)

add_executable(backup_archive_concurrent_reader_test
    ${SEABASS_STICK_BACKUP_STICK_SOURCES}
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/stick_backup/archive_compactor.cpp
    src/infrastructure/stick_backup/restore_path_sanitizer.cpp
    src/application/ports/progress_reporter.cpp
    src/application/use_cases/backup_stick.cpp
    src/application/use_cases/restore_stick_backup.cpp
    src/infrastructure/work_counters.cpp
    tests/backup_archive_concurrent_reader_test.cpp
)
target_include_directories(backup_archive_concurrent_reader_test PRIVATE src)
target_link_libraries(backup_archive_concurrent_reader_test PRIVATE ZLIB::ZLIB SQLite::SQLite3 Threads::Threads)
add_test(NAME backup_archive_concurrent_reader_test COMMAND backup_archive_concurrent_reader_test)

add_executable(backup_archive_compaction_test
    ${SEABASS_STICK_BACKUP_UPDATE_SOURCES}
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/stick_backup/archive_compactor.cpp
    src/infrastructure/backup/stick_write_lock.cpp
    src/application/use_cases/compact_stick_backup.cpp
    tests/backup_archive_compaction_test.cpp
)
target_include_directories(backup_archive_compaction_test PRIVATE src)
target_link_libraries(backup_archive_compaction_test PRIVATE ZLIB::ZLIB)
add_test(NAME backup_archive_compaction_test COMMAND backup_archive_compaction_test)

add_executable(backup_restore_test
    ${SEABASS_STICK_BACKUP_STICK_SOURCES}
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/stick_backup/archive_compactor.cpp
    src/infrastructure/stick_backup/restore_path_sanitizer.cpp
    src/application/ports/progress_reporter.cpp
    src/application/use_cases/backup_stick.cpp
    src/application/use_cases/compact_stick_backup.cpp
    src/application/use_cases/restore_stick_backup.cpp
    tests/backup_restore_test.cpp
)
target_include_directories(backup_restore_test PRIVATE src)
target_link_libraries(backup_restore_test PRIVATE ZLIB::ZLIB SQLite::SQLite3)
add_test(NAME backup_restore_test COMMAND backup_restore_test)

add_executable(archive_write_lock_test
    ${SEABASS_STICK_BACKUP_STICK_SOURCES}
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/stick_backup/archive_compactor.cpp
    src/infrastructure/stick_backup/restore_path_sanitizer.cpp
    src/application/ports/progress_reporter.cpp
    src/application/use_cases/backup_stick.cpp
    src/application/use_cases/compact_stick_backup.cpp
    src/application/use_cases/restore_stick_backup.cpp
    tests/archive_write_lock_test.cpp
)
target_include_directories(archive_write_lock_test PRIVATE src)
target_link_libraries(archive_write_lock_test PRIVATE ZLIB::ZLIB SQLite::SQLite3 Threads::Threads)
add_test(NAME archive_write_lock_test COMMAND archive_write_lock_test)

add_executable(clone_stick_test
    ${SEABASS_STICK_BACKUP_STICK_SOURCES}
    src/infrastructure/durable_file_write.cpp
    src/infrastructure/work_counters.cpp
    src/infrastructure/stick_backup/archive_compactor.cpp
    src/infrastructure/stick_backup/restore_path_sanitizer.cpp
    src/application/ports/progress_reporter.cpp
    src/application/use_cases/backup_stick.cpp
    src/application/use_cases/compact_stick_backup.cpp
    src/application/use_cases/restore_stick_backup.cpp
    src/application/use_cases/clone_stick.cpp
    tests/clone_stick_test.cpp
)
target_include_directories(clone_stick_test PRIVATE src)
target_link_libraries(clone_stick_test PRIVATE ZLIB::ZLIB SQLite::SQLite3)
add_test(NAME clone_stick_test COMMAND clone_stick_test)

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
    src/infrastructure/work_counters.cpp
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
    src/infrastructure/work_counters.cpp
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
    src/infrastructure/work_counters.cpp
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

# The corpus runner: the committed fixture plus every set under
# $SEABASS_CORPUS (real, un-anonymized libraries extracted with
# tools/extract_testdata -- see docs/real-data-testing.md). Qt-free, runs
# from local disk, asserts read-backs and work counts rather than seconds.
add_executable(corpus_test tests/corpus_test.cpp)
target_include_directories(corpus_test PRIVATE src)
target_link_libraries(corpus_test PRIVATE seabass_core)
add_test(NAME corpus_test COMMAND corpus_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(corpus_test PROPERTIES LABELS "integration")
