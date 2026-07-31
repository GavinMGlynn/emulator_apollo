# Golden result-block regression, wired into CTest.
#
# A "result block" is any deterministic report the emulator prints: the model
# table today, and later probe results, state hashes and register dumps. Each is
# pinned by a committed golden under tests/goldens/ and checked by
# tools/regress.py, whose docstring explains why a committed golden beats
# diffing two builds against each other -- two builds can drift together, and
# only a golden catches one platform quietly disagreeing with the other three.
#
# Because these are ordinary CTest entries they run under every build preset, so
# the -O0-vs-O3 and four-platform identity claims are asserted by the same
# mechanism rather than by a bespoke CI step.

option(APOLLO_GOLDENS "Register golden result-block regression tests" ON)

set(APOLLO_REGRESS "${CMAKE_CURRENT_SOURCE_DIR}/tools/regress.py")
set(APOLLO_GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens")

if(APOLLO_GOLDENS)
  find_package(Python3 COMPONENTS Interpreter)
  if(NOT Python3_Interpreter_FOUND)
    # Deliberately fatal rather than a silent skip. A ctest run reporting all
    # green while pinning nothing is worse than a failed configure: the whole
    # point of this project's portability claim is that the goldens ran.
    message(FATAL_ERROR
      "No Python 3 interpreter, so the golden regression tests cannot be "
      "registered.\n"
      "  Install python3, or configure with -DAPOLLO_GOLDENS=OFF.\n"
      "Turning them off is a real loss, not a formality: without them nothing "
      "checks that this build's output matches the committed goldens, which is "
      "the cross-platform, cross-build-type claim in docs/PROJECT_STATUS.md.")
  endif()

  # Regenerating goldens is a build target rather than a documented shell
  # incantation, so a golden is never hand-edited into agreement with a change.
  # It is an aggregate: each golden contributes its own update target, because
  # add_custom_command(TARGET) can only attach to a target created in the same
  # directory and goldens are registered from tests/.
  add_custom_target(goldens-update
    COMMENT "Rewriting every golden from the current build")
endif()

# apollo_add_golden(<test name> <golden file name> <target> [arguments...])
#
# Registers a CTest entry comparing the target's stdout against
# tests/goldens/<golden file name>, and adds the same command to the
# `goldens-update` target in update mode.
function(apollo_add_golden name golden target)
  if(NOT APOLLO_GOLDENS)
    return()
  endif()

  add_test(NAME ${name}
    COMMAND ${Python3_EXECUTABLE} ${APOLLO_REGRESS}
            check ${APOLLO_GOLDEN_DIR}/${golden}
            -- $<TARGET_FILE:${target}> ${ARGN})
  # `ctest -L golden` runs just these, which is what a portability spot-check
  # wants; the unit suites are not platform-sensitive in the same way.
  set_tests_properties(${name} PROPERTIES LABELS golden)

  add_custom_target(${name}-update
    COMMAND ${Python3_EXECUTABLE} ${APOLLO_REGRESS}
            update ${APOLLO_GOLDEN_DIR}/${golden}
            -- $<TARGET_FILE:${target}> ${ARGN}
    COMMENT "Rewriting ${golden}"
    VERBATIM)
  add_dependencies(${name}-update ${target})
  add_dependencies(goldens-update ${name}-update)
endfunction()
