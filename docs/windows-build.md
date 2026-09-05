# Building Seabass on Windows

Seabass builds **natively** on Windows via MSYS2's UCRT64 mingw-w64
toolchain — not by cross-compiling from Linux. `cmake/mingw-w64-toolchain.cmake`
is for that old Linux-cross-compile setup and is **not used** here; building
from inside the UCRT64 environment needs no `CMAKE_TOOLCHAIN_FILE` at all,
since the toolchain already targets Windows directly.

## One-time setup

Install [MSYS2](https://www.msys2.org/), then from an elevated or normal
PowerShell:

```powershell
& "C:\msys64\usr\bin\bash.exe" -lc "pacman -Sy --noconfirm"
& "C:\msys64\usr\bin\bash.exe" -lc "pacman -S --noconfirm --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-multimedia mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-sqlcipher mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libiconv mingw-w64-ucrt-x86_64-imagemagick"
```

(`imagemagick` is only needed if you plan to regenerate `src/gui/win/app_icon.ico`.
`sqlcipher` is needed to run the OneLibrary tests -- without it they fail
with "could not load libsqlcipher-0.dll" at test time, even though the
main build itself doesn't need it at build time, see `sqlcipher_dyn.hpp`'s
doc comment for why it's loaded dynamically instead of linked.)

Note: MSYS2's `bash.exe -lc "..."` wrapper can behave oddly with nested
quoting/redirection when driven from PowerShell. It's fine for `pacman`.
For actually running `cmake`/`ninja`/the built binaries, prefer driving them
directly from PowerShell with UCRT64's `bin` on `PATH` (see below) rather
than wrapping every command through `bash -lc`.

## Configure & build

PowerShell does not persist environment variables between separate tool
invocations/sessions, so set `PATH` in the same command block you build with:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build-win --target seabass-cli     # CLI
cmake --build build-win --target seabass          # GUI (only configured if Qt6 was found)
```

Unit tests are unconditional (`enable_testing()`/every `add_test()` call
runs regardless of platform) and configure, build, and run fine on
Windows via `ctest --test-dir build-win`. **Build them Debug, not
Release, for now**: several tests wrap the actual call under test in a
bare `assert()`, and CMake adds `-DNDEBUG` for every non-Debug
`CMAKE_BUILD_TYPE`, which compiles the call itself out entirely (not
just the check) -- a Release ctest run can report a near-full pass count
while having verified almost nothing for those cases. Confirmed
concretely on Windows: `anonymize_library_test`'s zlib inflate call and
`local_cue_store_test`'s `sqlite3_open` call both silently never
executed under Release, the second one also hiding a `fs::path::c_str()`
type mismatch that only Debug ever actually compiled. Most call sites
have since been fixed to hoist the call into a named variable and
assert on that (see git history for `tests/anonymize_library_test.cpp`/
`tests/local_cue_store_test.cpp`), but this is a systemic pattern
worth sweeping the rest of `tests/` for rather than assuming fixed.

## Deploying the GUI as a standalone .exe

`windeployqt` bundles Qt's own DLLs and QML plugins, but **not** the
mingw-w64 compiler runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
`libwinpthread-1.dll`) and not non-Qt dependencies Seabass itself pulls in
(e.g. `zlib1.dll`) or Qt's own transitive non-Qt dependencies (harfbuzz,
freetype, icu, pcre2, zstd, glib, ...). All of those must be copied
alongside the exe separately, or the built .exe only runs on machines that
happen to have UCRT64's `bin` on `PATH` (i.e. only on a dev machine with
MSYS2 installed).

```powershell
.\tools\deploy-windows.ps1
```

This runs `windeployqt`, then walks the full transitive DLL closure itself
(`objdump -p <dll> | Select-String "DLL Name"`, recursively, starting from
every `.dll`/`.exe` already in `build-win\`) and copies across anything
missing from `C:\msys64\ucrt64\bin` -- including plugins' own dependencies
`windeployqt` doesn't know about, which turned out to matter a lot in
practice: the JPEG image-format plugin needs `libjpeg-8.dll` (without it,
cover art silently fails to decode -- no crash, no error, just a blank
image), and the FFmpeg-based multimedia backend plugin needs its *entire*
codec dependency graph resolvable before Windows will load it at all
(`LoadLibraryEx` fails with error 126 otherwise) -- MSYS2's ffmpeg package
pulls in dozens of optional codec libraries (`libx264`, `libvpx`,
`libmp3lame`, `libopus`, `libbluray`, ...) as hard DLL dependencies, none
of which are needed for ordinary MP3/AAC playback but all of which must
still be *present* for the plugin's import table to resolve. Re-run this
script after any MSYS2 package update -- the exact closure drifts with
Qt/MSYS2 versions, so don't treat any past run's file list as exhaustive.

One DLL the closure walk can never find on its own: `libsqlcipher-0.dll`
(needed by the OneLibrary cue writer, see docs/onelibrary-format.md).
It's loaded at runtime via `LoadLibrary`, deliberately not linked at
build time (see `sqlcipher_dyn.hpp`'s doc comment for why), so it never
appears in any binary's import table for the walk to discover -- the
script copies it as an explicit special case. Without it, the exe still
builds and runs fine, but every OneLibrary write silently fails (throws,
caught as the best-effort failure it's designed to be) with "could not
load libsqlcipher-0.dll".

This deploy step still isn't wired into the CMake build itself or packaged
into a real installer (no CPack/NSIS/WiX step)
— real follow-up work, tracked here rather than silently skipped.

## Known gaps

- Unit tests: build and run fine, but see the Debug-vs-Release
  `assert()`/`NDEBUG` caveat above -- a real, only-partially-fixed gap,
  not a solved one.
- Three genuine Windows-specific test failures found so far, all
  pre-existing and unrelated to any one feature:
  `pending_deletion_applier_test` (simulates an undeletable file via
  POSIX permission bits `fs::permissions()` sets, which Windows doesn't
  enforce the same way -- a test-portability issue, not a product bug);
  `onelibrary_cue_writer_test` (the staleness guard, meant to refuse a
  write if the database changed externally, does not throw on Windows --
  possibly a real product bug in an mtime-based check, given Windows
  timestamp granularity/caching differs from Linux; flagged as the one
  to prioritize since it guards a real safety property); `onelibrary_reader_test`
  (compares a path built with Windows backslashes against one the reader
  builds with forward slashes from the DB's stored `/Contents/...` value --
  likely test-only, but worth confirming the product itself never emits
  mixed separators on Windows).
- No CI (`.github/` doesn't exist).
- No installer/packaging step (CPack, NSIS, WiX, MSIX, ...) — only the
  `tools\deploy-windows.ps1` script above.
- `seabass-cli.exe` (the CLI) links `-static -static-libgcc -static-libstdc++`
  but its dependency on `zlib1.dll` (found via MSYS2's `libz.dll.a` import
  library, not a static `libz.a`) is still dynamic — so despite those flags
  it isn't actually a fully self-contained single-file exe yet. Needs either
  a static zlib build or shipping `zlib1.dll` alongside it.
