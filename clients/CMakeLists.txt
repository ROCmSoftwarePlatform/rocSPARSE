# ########################################################################
# Copyright (c) 2018-2021 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# ########################################################################

# The ROCm platform requires Ubuntu 16.04 or Fedora 24, which has cmake 3.5
cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

# Consider removing this in the future
# This should appear before the project command, because it does not use FORCE
if(WIN32)
  set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/package" CACHE PATH "Install path prefix, prepended onto install directories")
else()
  set(CMAKE_INSTALL_PREFIX "/opt/rocm" CACHE PATH "Install path prefix, prepended onto install directories")
endif()

# This has to be initialized before the project() command appears
# Set the default of CMAKE_BUILD_TYPE to be release, unless user specifies with -D.  MSVC_IDE does not use CMAKE_BUILD_TYPE
if(NOT DEFINED CMAKE_CONFIGURATION_TYPES AND NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel.")
endif()

# This project may compile dependencies for clients
project(rocsparse-clients LANGUAGES CXX)

# Determine if CXX Compiler is hip-clang
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(STATUS "Using hip-clang to build for amdgpu backend")
else()
  message(FATAL_ERROR "'hipcc' compiler required to compile for ROCm platform.")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# This option only works for make/nmake and the ninja generators, but no reason it shouldn't be on all the time
# This tells cmake to create a compile_commands.json file that can be used with clang tooling or vim
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET rocsparse)
  find_package(rocsparse REQUIRED CONFIG PATHS /opt/rocm/rocsparse)

  option(BUILD_CLIENTS_TESTS "Build tests (requires googletest)" OFF)
  option(BUILD_CLIENTS_BENCHMARKS "Build benchmarks" OFF)
  option(BUILD_CLIENTS_SAMPLES "Build examples" ON)
endif()

# HIP headers required of all clients; clients use hip to allocate device memory
list(APPEND CMAKE_PREFIX_PATH /opt/rocm)
find_package(HIP REQUIRED CONFIG PATHS ${CMAKE_PREFIX_PATH})

# Build flags
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# If OpenMP is available, we can use it to speed up some tests
find_package(OpenMP QUIET)

if(OPENMP_FOUND)
  if(NOT TARGET OpenMP::OpenMP_CXX)
    # OpenMP cmake fix for cmake <= 3.9
    add_library(OpenMP::OpenMP_CXX IMPORTED INTERFACE)
    set_property(TARGET OpenMP::OpenMP_CXX PROPERTY INTERFACE_COMPILE_OPTIONS ${OpenMP_CXX_FLAGS})
    set_property(TARGET OpenMP::OpenMP_CXX PROPERTY INTERFACE_LINK_LIBRARIES ${OpenMP_CXX_FLAGS} Threads::Threads)
  endif()
endif()

if(BUILD_CLIENTS_SAMPLES)
  add_subdirectory(samples)
endif()

if(BUILD_CLIENTS_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(BUILD_CLIENTS_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

set(ROCSPARSE_COMMON "${PROJECT_BINARY_DIR}/staging/rocsparse_common.yaml")
add_custom_command(OUTPUT "${ROCSPARSE_COMMON}"
                   COMMAND ${CMAKE_COMMAND} -E copy include/rocsparse_common.yaml "${ROCSPARSE_COMMON}"
                   DEPENDS include/rocsparse_common.yaml
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

set(ROCSPARSE_TEMPLATE "${PROJECT_BINARY_DIR}/staging/rocsparse_template.yaml")
add_custom_command(OUTPUT "${ROCSPARSE_TEMPLATE}"
                   COMMAND ${CMAKE_COMMAND} -E copy include/rocsparse_template.yaml "${ROCSPARSE_TEMPLATE}"
                   DEPENDS include/rocsparse_template.yaml
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

set(ROCSPARSE_GENTEST "${PROJECT_BINARY_DIR}/staging/rocsparse_gentest.py")
add_custom_command(OUTPUT "${ROCSPARSE_GENTEST}"
                   COMMAND ${CMAKE_COMMAND} -E copy common/rocsparse_gentest.py "${ROCSPARSE_GENTEST}"
                   DEPENDS common/rocsparse_gentest.py
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_custom_target(rocsparse-common DEPENDS "${ROCSPARSE_COMMON}" "${ROCSPARSE_TEMPLATE}" "${ROCSPARSE_GENTEST}")

if(BUILD_CLIENTS_SAMPLES OR BUILD_CLIENTS_BENCHMARKS OR BUILD_CLIENTS_TESTS)
        install(
                DIRECTORY "${PROJECT_BINARY_DIR}/staging/"
                DESTINATION "rocsparse/bin/"
                COMPONENT "clients")
endif()
