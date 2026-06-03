# Standalone CMake recipe for the marketnull pybind11 module. This is the basis
# for the deployable manylinux wheel; for quick host iteration use build_local.sh.
#
# Requires an installed/exported libleidenalg (find_package below), pybind11, and
# igraph. Configure with, e.g.:
#   cmake -S marketnull -B build/marketnull -DCMAKE_PREFIX_PATH="<libleidenalg-install>;<pybind11>"
#   cmake --build build/marketnull
cmake_minimum_required(VERSION 3.23)
project(marketnull LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
find_package(igraph CONFIG REQUIRED)
find_package(libleidenalg CONFIG REQUIRED)

# libleidenalg exports its target under the libleidenalg:: namespace on install.
if(TARGET libleidenalg::libleidenalg)
  set(LEIDEN_TARGET libleidenalg::libleidenalg)
else()
  set(LEIDEN_TARGET libleidenalg)
endif()

pybind11_add_module(marketnull marketnull_module.cpp)
target_link_libraries(marketnull PRIVATE ${LEIDEN_TARGET} igraph::igraph)
