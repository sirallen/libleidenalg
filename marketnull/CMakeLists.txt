# Self-contained build of the marketnull pybind11 extension.
#
# Rather than depend on an installed shared libleidenalg, this compiles the
# libleidenalg sources (../src) directly into the extension as a static archive
# (LEIDENALG_STATIC => the LIBLEIDENALG_EXPORT macros expand to nothing) and links
# igraph. Built against a STATIC igraph in the manylinux wheel, the resulting .so
# depends only on system libs, so auditwheel can certify it without bundling a
# separate libleidenalg.
#
# Local dev:
#   cmake -S marketnull -B build/marketnull \
#         -DCMAKE_PREFIX_PATH="$(python -m pybind11 --cmakedir);/opt/homebrew"
#   cmake --build build/marketnull
# Wheel: driven by the root pyproject.toml via scikit-build-core (see build_wheel.sh).
cmake_minimum_required(VERSION 3.23)
project(marketnull LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# scikit-build-core provides Python; let pybind11 use the new FindPython.
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)
find_package(igraph CONFIG REQUIRED)

set(LEIDEN_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
file(GLOB LEIDEN_SOURCES CONFIGURE_DEPENDS ${LEIDEN_ROOT}/src/*.cpp)

# Static archive of the core library, with the export header generated exactly as
# upstream src/CMakeLists.txt does (BASE_NAME libleidenalg => LIBLEIDENALG_EXPORT,
# STATIC_DEFINE LEIDENALG_STATIC).
include(GenerateExportHeader)
add_library(leidenalg_static STATIC ${LEIDEN_SOURCES})
target_include_directories(leidenalg_static PUBLIC
  ${LEIDEN_ROOT}/include
  ${CMAKE_CURRENT_BINARY_DIR}/exportgen)
generate_export_header(leidenalg_static
  BASE_NAME libleidenalg
  STATIC_DEFINE LEIDENALG_STATIC
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/exportgen/libleidenalg_export.h)
target_compile_definitions(leidenalg_static PUBLIC LEIDENALG_STATIC)
target_link_libraries(leidenalg_static PUBLIC igraph::igraph)

pybind11_add_module(marketnull marketnull_module.cpp)
target_link_libraries(marketnull PRIVATE leidenalg_static)

install(TARGETS marketnull LIBRARY DESTINATION .)
