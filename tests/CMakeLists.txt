# One executable and one CTest entry per suite, so a crash in one suite does not
# take down the rest.

# Unity is vendored in ext/ and never gets our warning set.
#
# It is the *only* submodule the build requires: sdl, zlib and libpng are linked
# but not yet used, and mame and musashi are reference-only and never linked at
# all (see ext/README.md). An uninitialised submodule is an empty directory, and
# a missing source file here otherwise reports as a bare "cannot find
# .../unity.c" from the generator, which does not tell a first-time cloner what
# to do about it.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ext/unity/src/unity.c")
  message(FATAL_ERROR
    "ext/unity is not initialised, so there is no test framework to build.\n"
    "  git submodule update --init --depth 1 ext/unity\n"
    "That one submodule is all the build needs; do not init the rest unless you "
    "are working on them. ext/mame in particular is hundreds of megabytes and is "
    "only ever built out-of-tree as the oracle.")
endif()

add_library(unity STATIC ${CMAKE_SOURCE_DIR}/ext/unity/src/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_SOURCE_DIR}/ext/unity/src)

# apollo_add_suite(<name> [extra libraries...])
function(apollo_add_suite name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE apollo_core unity ${ARGN})
  apollo_set_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

apollo_add_suite(bus_suite)
apollo_add_suite(overlap_suite)
apollo_add_suite(pipe_suite)
apollo_add_suite(atc_suite)
apollo_add_suite(desc_suite)
apollo_add_suite(tc_suite)
apollo_add_suite(timing_table_suite)
apollo_add_suite(tt_suite)
apollo_add_suite(walk_suite)
apollo_add_suite(mmusr_suite)
apollo_add_suite(bounds_suite)
apollo_add_suite(cache_suite)
apollo_add_suite(arb_suite)
apollo_add_suite(arbiter_suite)
apollo_add_suite(atmap_suite)
apollo_add_suite(boardreg_suite)
apollo_add_suite(i8237_suite)
apollo_add_suite(i8259_suite)
apollo_add_suite(intr_suite)
apollo_add_suite(mc6840_suite)
apollo_add_suite(timer_suite)
apollo_add_suite(mc146818_suite)
apollo_add_suite(mc68681_suite)
apollo_add_suite(omti_suite)
apollo_add_suite(sc499_suite)
apollo_add_suite(calendar_suite)
apollo_add_suite(disk_suite)
apollo_add_suite(dma_suite)
apollo_add_suite(nodeid_suite)
apollo_add_suite(sio_suite)
apollo_add_suite(tape_suite)
apollo_add_suite(category_suite)
apollo_add_suite(exception_suite)
apollo_add_suite(regs_suite)
apollo_add_suite(ea_suite)
apollo_add_suite(ea_timing_suite)
apollo_add_suite(cond_suite)
apollo_add_suite(opcode_suite)
apollo_add_suite(branch_suite)
apollo_add_suite(quick_suite)
apollo_add_suite(control_suite)
apollo_add_suite(misc_suite)
apollo_add_suite(single_suite)
apollo_add_suite(move_suite)
apollo_add_suite(immediate_suite)
apollo_add_suite(arith_suite)
apollo_add_suite(shift_suite)
apollo_add_suite(coproc_suite)
apollo_add_suite(decode_suite)
apollo_add_suite(addr_suite)
apollo_add_suite(access_suite)
apollo_add_suite(fetch_suite)
apollo_add_suite(probe_suite)
apollo_add_suite(state_suite)
apollo_add_suite(step_suite)
apollo_add_suite(operand_suite)
apollo_add_suite(alu_suite)
apollo_add_suite(time_suite)
apollo_add_suite(machine_suite)
apollo_add_suite(model_suite)
apollo_add_suite(hash_suite)
apollo_add_suite(frontend_common_suite apollo_frontend_common)

# Golden result blocks. These supersede a bare "does it run" smoke test:
# regress.py already fails on a non-zero exit, so the golden checks that the
# frontend runs *and* that it printed the expected bytes.
#
# One block today. Probe results, register dumps and boot state hashes join this
# list as the subsystems that produce them land, and each arrives with its own
# golden rather than a promise to add one later.
apollo_add_golden(golden_model_table model_table.txt apollo-headless --list-models)
apollo_add_golden(golden_probes probes.txt apollo-headless --run-probes)
apollo_add_golden(golden_timing timing.txt apollo-headless --time-instructions)

# The oracle driver's logic, against a stub MAME: dump extraction from noisy
# output, the two-run determinism check, and the failure paths. Whether the
# oracle's numbers are right needs a real emulator and is what FINDINGS.md
# campaigns settle; this is the ordinary program logic around them.
#
# Not registered on Windows, and that is a scope statement rather than a
# workaround. The test drives stub oracles as executables, which means it
# depends on how a platform launches a script -- and the thing under test only
# ever runs where the oracle itself is built, which is the Linux development
# side. Windows is a supported *emulator* platform, not an oracle platform: it
# builds and tests the core and checks the goldens, which is what its
# portability claim actually is. The stub launcher still handles Windows so the
# test can be run there by hand.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND NOT WIN32)
  add_test(NAME oracle_driver
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mame-oracle/test_oracle.py)
  set_tests_properties(oracle_driver PROPERTIES LABELS tools)
endif()

# The oracle harness's dump format, checked against a mock machine so it needs
# no MAME -- which is what lets it run in CI, where the oracle is never built.
#
# Best-effort, unlike the goldens above, and the difference is deliberate. A
# golden asserts the emulator's own cross-platform identity claim, so missing
# Python is a fatal configure. This pins the format of a development tool that
# only ever runs on a developer's machine, so a missing Lua names what is not
# being checked and moves on rather than failing a build of the core.
find_program(APOLLO_LUA NAMES lua lua5.4 lua5.3)
if(APOLLO_LUA)
  add_test(NAME oracle_dump_format
           COMMAND ${APOLLO_LUA} ${CMAKE_SOURCE_DIR}/tools/mame-oracle/test_dump.lua)
  set_tests_properties(oracle_dump_format PROPERTIES LABELS tools)
else()
  message(STATUS
    "apollo: no lua found, so the oracle dump format is NOT checked "
    "(tools/mame-oracle/test_dump.lua). Install lua to enable it.")
endif()
