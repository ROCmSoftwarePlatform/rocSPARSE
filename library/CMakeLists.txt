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

# This option only works for make/nmake and the ninja generators, but no reason it shouldn't be on all the time
# This tells cmake to create a compile_commands.json file that can be used with clang tooling or vim
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Print verbose compiler flags
if(BUILD_VERBOSE)
  include(../cmake/Verbose.cmake)
endif()

# Configure a header file to pass the rocSPARSE version
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/rocsparse-version.h.in"
               "${PROJECT_BINARY_DIR}/include/rocsparse-version.h"
)

# Public rocSPARSE headers
set(rocsparse_headers_public
  include/rocsparse-auxiliary.h
  include/rocsparse-functions.h
  include/rocsparse-types.h
  include/rocsparse-complex-types.h
  include/rocsparse.h
  ${PROJECT_BINARY_DIR}/include/rocsparse-version.h
)

source_group("Header Files\\Public" FILES ${rocsparse_headers_public})

include(GNUInstallDirs)

set(BIN_INSTALL_DIR ${CMAKE_INSTALL_BINDIR})
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR})
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR})

# Include sources
include(src/CMakeLists.txt)

# Set Fortran module output directory
set(CMAKE_Fortran_MODULE_DIRECTORY ${PROJECT_BINARY_DIR}/include)

# Create rocSPARSE Fortran module
add_library(rocsparse_fortran OBJECT ${rocsparse_fortran_source})

# Target compile options
target_compile_options(rocsparse_fortran PRIVATE -std=f2003 -ffree-form -cpp)

# Create rocSPARSE library
add_library(rocsparse ${rocsparse_source} ${rocsparse_headers_public})
add_library(roc::rocsparse ALIAS rocsparse)

# Target compile options
target_compile_options(rocsparse PRIVATE -Wno-unused-command-line-argument -Wall)

foreach(target ${AMDGPU_TARGETS})
  target_compile_options(rocsparse PRIVATE --amdgpu-target=${target})
endforeach()

# Target include directories
target_include_directories(rocsparse
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
                                   $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/library/include>
                           PUBLIC  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
                                   $<INSTALL_INTERFACE:include>
)

# Target link libraries
target_link_libraries(rocsparse PRIVATE roc::rocprim)

# Target properties
rocm_set_soversion(rocsparse ${rocsparse_SOVERSION})
set_target_properties(rocsparse PROPERTIES CXX_VISIBILITY_PRESET "hidden" VISIBILITY_INLINES_HIDDEN ON)
set_target_properties(rocsparse PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/staging")
set_target_properties(rocsparse PROPERTIES DEBUG_POSTFIX "-d")

# Generate export header
include(GenerateExportHeader)
generate_export_header(rocsparse EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/rocsparse-export.h)

# Force installation of .f module file
install(FILES "${CMAKE_SOURCE_DIR}/library/src/rocsparse.f"
              "${CMAKE_SOURCE_DIR}/library/src/rocsparse_enums.f"
        DESTINATION "rocsparse/include"
)

# Install targets
rocm_install_targets(TARGETS rocsparse
                     INCLUDE
                       ${CMAKE_SOURCE_DIR}/library/include
                       ${CMAKE_BINARY_DIR}/include
                     PREFIX rocsparse
)

# Export targets
rocm_export_targets(TARGETS roc::rocsparse
                    PREFIX rocsparse
                    DEPENDS PACKAGE hip
                    NAMESPACE roc::
)

# Symbolic links
rocm_install_symlink_subdir(rocsparse)

# Package specific CPACK vars
set(CPACK_RPM_UNSPECIFIED_PACKAGE_REQUIRES "hip-rocclr >= 3.5.0")
set(CPACK_DEBIAN_UNSPECIFIED_PACKAGE_DEPENDS "hip-rocclr (>= 3.5.0)")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.md")

if(NOT CPACK_PACKAGING_INSTALL_PREFIX)
  set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
endif()

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "\${CPACK_PACKAGING_INSTALL_PREFIX}" "\${CPACK_PACKAGING_INSTALL_PREFIX}/include")

# Package name
set(package_name rocsparse)

set(ROCSPARSE_CONFIG_DIR "\${CPACK_PACKAGING_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" CACHE PATH "Path placed into ldconfig file")

rocm_create_package(
  NAME ${package_name}
  DESCRIPTION "Radeon Open Compute SPARSE library"
  MAINTAINER "rocSPARSE Maintainer <rocsparse-maintainer@amd.com>"
  COMPONENTS clients
  LDCONFIG
  LDCONFIG_DIR ${ROCSPARSE_CONFIG_DIR}
)
