#!/usr/bin/env bash
# Reproducible local build of the marketnull pybind11 module (for development /
# validation on the host toolchain). For the deployable artifact see the
# manylinux wheel recipe (Phase 4); this just compiles against an already-built
# libleidenalg in builds/ninja and the host igraph.
#
# Usage (from the libleidenalg repo root, with a Python env that has pybind11):
#   bash marketnull/build_local.sh
#
# Env overrides:
#   LIB_DIR     directory holding liblibleidenalg.* (default: builds/ninja/src)
#   INC_BUILD   build-tree include dir (default: builds/ninja/include)
#   IGRAPH_PREFIX  homebrew/igraph prefix (default: /opt/homebrew)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

LIB_DIR="${LIB_DIR:-builds/ninja/src}"
INC_BUILD="${INC_BUILD:-builds/ninja/include}"
IGRAPH_PREFIX="${IGRAPH_PREFIX:-/opt/homebrew}"

if ! python3 -c "import pybind11" >/dev/null 2>&1; then
  echo "error: pybind11 not importable in this Python env. pip install pybind11." >&2
  exit 1
fi

SUFFIX="$(python3-config --extension-suffix)"
OUT="marketnull/marketnull${SUFFIX}"

UNAME="$(uname -s)"
EXTRA_LDFLAGS=()
if [ "$UNAME" = "Darwin" ]; then
  EXTRA_LDFLAGS+=(-undefined dynamic_lookup)
fi

set -x
clang++ -O2 -std=c++17 -shared -fPIC "${EXTRA_LDFLAGS[@]}" \
  $(python3 -m pybind11 --includes) \
  -I include -I "$INC_BUILD" -I "$IGRAPH_PREFIX/include" -I "$IGRAPH_PREFIX/include/igraph" \
  marketnull/marketnull_module.cpp \
  -L "$LIB_DIR" -llibleidenalg \
  -L "$IGRAPH_PREFIX/lib" -ligraph \
  -Wl,-rpath,"$IGRAPH_PREFIX/lib" -Wl,-rpath,"$REPO_ROOT/$LIB_DIR" \
  -o "$OUT"
set +x

echo "built $OUT"
