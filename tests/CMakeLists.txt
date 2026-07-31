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

apollo_add_suite(time_suite)
apollo_add_suite(model_suite)
apollo_add_suite(frontend_common_suite apollo_frontend_common)

# The deterministic frontend must at least run everywhere the core does.
add_test(NAME headless_lists_models
         COMMAND apollo-headless --list-models)
