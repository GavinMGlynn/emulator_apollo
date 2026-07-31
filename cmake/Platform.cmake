# Supported platforms and toolchain.
#
# The emulator targets exactly three platforms, all 64-bit:
#
#   Linux x86-64   -- both Red Hat and Debian derived distributions
#   Windows x86-64 -- clang in an MSVC environment
#   macOS arm64    -- Apple silicon
#
# Clang is the default and the supported compiler on all three; every preset in
# CMakePresets.json sets it. Using one toolchain everywhere is what makes
# "identical results on every platform" a claim about the emulator rather than a
# claim about three different compilers' shared luck.

# --- 64-bit only -------------------------------------------------------------
#
# Emulated cycle counts and state hashes must be bit-identical everywhere. A
# 32-bit build is not merely unsupported, it is a silent-wrong-answer risk: the
# time base is a uint64_t counter in AP_TIME_BASE_HZ units, and pointer-width
# and long-width assumptions differ. Fail at configure time, not in a golden
# diff nobody can explain.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "apollo requires a 64-bit target; this toolchain produces "
    "${CMAKE_SIZEOF_VOID_P}-byte pointers.\n"
    "Supported: Linux x86-64, Windows x86-64, macOS arm64. Emulated results "
    "must be bit-identical across all three, which a 32-bit build cannot be "
    "relied on to reproduce.")
endif()

# --- Known platforms ---------------------------------------------------------
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux"
        OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"
        OR CMAKE_SYSTEM_NAME STREQUAL "Windows"))
  message(WARNING
    "Unsupported platform '${CMAKE_SYSTEM_NAME}'. The build may work, but it is "
    "not in the CI matrix, so nothing proves its results match the others.")
endif()

# --- Clang is the default ----------------------------------------------------
#
# A warning rather than an error: another compiler is a useful portability
# check, and CI's -O0/-O3 identity job would catch a real divergence. But it is
# off the supported path, so say so once rather than letting it pass silently.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(WARNING
    "Building with ${CMAKE_C_COMPILER_ID}, not Clang. Clang is the default and "
    "the only compiler in the CI matrix -- use a preset from CMakePresets.json "
    "to get it. Emulated results are still expected to be identical; if they "
    "are not, that is a bug worth reporting rather than a tolerated difference.")
endif()

message(STATUS
  "apollo: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}, "
  "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}, 64-bit")
