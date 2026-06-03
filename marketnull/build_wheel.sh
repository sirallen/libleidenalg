#!/usr/bin/env bash
# Build a self-contained manylinux wheel for the `marketnull` extension via
# cibuildwheel. The extension statically compiles the libleidenalg sources and
# links a STATIC igraph (built in CIBW_BEFORE_ALL), so the resulting .so depends
# only on system libs and auditwheel can certify it as manylinux2014 (glibc 2.17
# — compatible with AWS Glue 4.0 / Amazon Linux 2 and Glue 5.0 / AL2023).
#
# Run from the libleidenalg repo root (where pyproject.toml lives):
#   bash marketnull/build_wheel.sh
# Requires Docker and `pip install cibuildwheel`. Wheels land in wheelhouse/.
#
# Target Python versions (override via CIBW_BUILD):
#   Glue 4.0 -> Python 3.10 (cp310); Glue 5.0 -> Python 3.11 (cp311).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

IGRAPH_VERSION="${IGRAPH_VERSION:-0.10.15}"

export CIBW_BUILD="${CIBW_BUILD:-cp310-manylinux_x86_64 cp311-manylinux_x86_64}"
export CIBW_ARCHS="${CIBW_ARCHS:-x86_64}"
export CIBW_MANYLINUX_X86_64_IMAGE="${CIBW_MANYLINUX_X86_64_IMAGE:-manylinux2014}"

# Build & install a static, PIC igraph once per container, with optional features
# that pull external deps disabled (we only need the core graph algorithms).
#
# Use the release TARBALL, not a git clone: the tarball ships pre-generated
# flex/bison parser sources and vendored dependencies, so the build needs neither
# flex/bison nor network access to submodules inside the manylinux container.
export CIBW_BEFORE_ALL="
set -e
curl -fsSL -o /tmp/igraph.tar.gz https://github.com/igraph/igraph/releases/download/${IGRAPH_VERSION}/igraph-${IGRAPH_VERSION}.tar.gz
mkdir -p /tmp/igraph
tar xzf /tmp/igraph.tar.gz -C /tmp/igraph --strip-components=1
cmake -S /tmp/igraph -B /tmp/igraph/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DIGRAPH_GRAPHML_SUPPORT=OFF \
  -DIGRAPH_USE_INTERNAL_BLAS=ON \
  -DIGRAPH_USE_INTERNAL_LAPACK=ON \
  -DIGRAPH_USE_INTERNAL_ARPACK=ON \
  -DIGRAPH_USE_INTERNAL_GLPK=ON \
  -DIGRAPH_USE_INTERNAL_GMP=ON \
  -DIGRAPH_USE_INTERNAL_PLFIT=ON \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /tmp/igraph/build -j\$(nproc)
cmake --install /tmp/igraph/build
"

# Hand the static igraph install to scikit-build-core / CMake.
export CIBW_ENVIRONMENT="CMAKE_PREFIX_PATH=/usr/local"

# Sanity-check the built wheel imports and runs the validation in the container.
export CIBW_TEST_REQUIRES="numpy"
export CIBW_TEST_COMMAND="python {project}/marketnull/validate.py"

python -m cibuildwheel --output-dir wheelhouse .
echo "wheels written to $REPO_ROOT/wheelhouse/"
