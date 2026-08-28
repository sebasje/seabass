#pragma once

#include <string>

namespace djconvert::infrastructure
{

// Atomically (same-directory temp file + rename) and durably (the temp
// file is fsync'd / FlushFileBuffers'd before the rename; the
// containing directory is fsync'd after the rename on POSIX -- the
// standard "durable rename" pattern) replaces the file at `path` with
// `data`. Works whether or not a file already exists at `path` -- no
// partial/truncated file is ever visible there, and a crash right after
// this returns true can't lose the write to a write-back cache, which
// matters for this project's actual medium: a USB stick that can be
// pulled at any moment.
//
// Returns false, leaving whatever was at `path` before (if anything)
// completely untouched, if the write, fsync, or rename failed.
bool writeFileDurablyAtomic(const std::string &path, const std::string &data);

}  // namespace djconvert::infrastructure
