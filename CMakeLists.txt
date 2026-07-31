cmake_minimum_required(VERSION 3.21)

project(apollo
  VERSION 0.1.0
  DESCRIPTION "Cycle-accurate Apollo Domain workstation emulator with an emulated Apollo Token Ring"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(GplBoundary)

# ext/mame is GPL-2.0-or-later and this core is MIT: assert the boundary rather
# than trusting review to catch an accidental include. See ext/README.md.
apollo_check_no_reference_includes()

# Emulated cycle counts are not wall-clock measurements: results must be
# bit-identical on every build type and platform. The regression suite runs on
# both -O0 and release builds to assert exactly that.
enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/frontend)
add_subdirectory(tests)

# Targets exist only now, so the linkage half of the boundary check runs last.
apollo_check_no_reference_linkage()
