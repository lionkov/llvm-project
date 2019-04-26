##===----------------------------------------------------------------------===##
# 
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# 
##===----------------------------------------------------------------------===##
#
# Build a plugin for a WebAssembly.
#
##===----------------------------------------------------------------------===##

if (NOT LIBOMPTARGET_DEP_LIBELF_FOUND)
  libomptarget_say("Not building Wasm32 offloading plugin: libelf dependency not found.")
  return()
endif()

libomptarget_say("Building Wasm32 offloading plugin.")

# Define the suffix for the runtime messaging dumps.
add_definitions(-DTARGET_NAME=WASM32)

include_directories(${LIBOMPTARGET_DEP_LIBELF_INCLUDE_DIRS})

add_library(omptarget.rtl.wasm32 SHARED src/rtl.cpp)

# Install plugin under the lib destination folder.
install(TARGETS omptarget.rtl.wasm32 LIBRARY DESTINATION "${OPENMP_INSTALL_LIBDIR}")

target_link_libraries(omptarget.rtl.wasm32
  ${LIBOMPTARGET_DEP_LIBELF_LIBRARIES}
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/../exports")

# Report to the parent scope that we are building a plugin for Wasm.
set(LIBOMPTARGET_SYSTEM_TARGETS "${LIBOMPTARGET_SYSTEM_TARGETS} wasm32" PARENT_SCOPE)
