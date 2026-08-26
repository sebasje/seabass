cmake_minimum_required(VERSION 3.16)
project(djconvert CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- vendored dependencies (git submodules under third_party/) ---

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/libdjinterop)

add_library(kaitai_cpp_stl_runtime STATIC
    third_party/kaitai_struct_cpp_stl_runtime/kaitai/kaitaistream.cpp
)
target_include_directories(kaitai_cpp_stl_runtime PUBLIC
    third_party/kaitai_struct_cpp_stl_runtime
)
target_compile_definitions(kaitai_cpp_stl_runtime PUBLIC KS_STR_ENCODING_ICONV)

# --- rekordbox format parser, generated from specs/*.ksy (see specs/README.md) ---

add_library(rekordbox_format STATIC
    src/infrastructure/rekordbox/generated/rekordbox_pdb.cpp
    src/infrastructure/rekordbox/generated/rekordbox_anlz.cpp
)
target_include_directories(rekordbox_format PUBLIC
    src/infrastructure/rekordbox/generated
)
target_link_libraries(rekordbox_format PUBLIC kaitai_cpp_stl_runtime)

# --- djconvert itself, layered: domain -> application -> infrastructure -> cli ---

add_executable(djconvert
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/fuzzy_matcher.cpp
    src/domain/sync_planning.cpp
    src/domain/track_matching.cpp
    src/domain/track_queries.cpp
    src/application/ports/progress_reporter.cpp
    src/infrastructure/rekordbox/kaitai_rekordbox_reader.cpp
    src/infrastructure/engine/libdjinterop_engine_reader.cpp
    src/infrastructure/engine/libdjinterop_engine_cue_writer.cpp
    src/infrastructure/media/linux_removable_media_locator.cpp
    src/infrastructure/backup/filesystem_backup_store.cpp
    src/infrastructure/logging/file_operation_log.cpp
    src/cli/console.cpp
    src/cli/terminal_progress_reporter.cpp
    src/cli/main.cpp
)
target_include_directories(djconvert PRIVATE src)
target_link_libraries(djconvert PRIVATE rekordbox_format DjInterop)

# --- domain-layer unit tests (no infrastructure deps, no fixtures needed) ---

enable_testing()
add_executable(duplicate_cue_consolidation_test
    src/domain/duplicate_cue_consolidation.cpp
    src/domain/track_matching.cpp
    tests/duplicate_cue_consolidation_test.cpp
)
target_include_directories(duplicate_cue_consolidation_test PRIVATE src)
add_test(NAME duplicate_cue_consolidation_test COMMAND duplicate_cue_consolidation_test)

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
