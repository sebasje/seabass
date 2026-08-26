#!/usr/bin/env bash
# Regenerates the C++ rekordbox PDB/ANLZ parser from specs/*.ksy.
# Requires: java, and kaitai-struct-compiler on PATH (or set KSC below).
#
# https://github.com/kaitai-io/kaitai_struct_compiler/releases

set -euo pipefail

KSC="${KSC:-kaitai-struct-compiler}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/src/infrastructure/rekordbox/generated"

"$KSC" -t cpp_stl --cpp-standard 11 --outdir "$OUT" \
  "$ROOT/specs/rekordbox_pdb.ksy" \
  "$ROOT/specs/rekordbox_anlz.ksy"

echo "Regenerated parser in $OUT"
