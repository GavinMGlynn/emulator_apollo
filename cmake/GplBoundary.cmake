# Enforce the licence boundary described in ext/README.md and LICENSE.
#
# This core is MIT. ext/mame is GPL-2.0-or-later and ext/musashi is a second
# 68000 implementation we read but never derive from. Both are reference-only:
# MAME is run as a separate program and its output compared with ours, which is
# an arms-length relationship. Linking either into anything we build, or copying
# code across, would be a licence violation in the MAME case and a defeat of the
# project's premise in both.
#
# The rule is easy to state and easy to break by accident -- one #include, one
# add_subdirectory, one target_link_libraries during a debugging session that
# nobody remembers to revert. So it is asserted at configure time rather than
# left to discipline and review.

set(APOLLO_REFERENCE_ONLY mame musashi)

# 1. No first-party source may include a header from a reference-only submodule.
#    Matches an #include whose path mentions the submodule, e.g.
#    #include "ext/musashi/m68k.h" or <mame/emu.h>.
function(apollo_check_no_reference_includes)
  file(GLOB_RECURSE _sources
       "${CMAKE_SOURCE_DIR}/src/*.c"   "${CMAKE_SOURCE_DIR}/src/*.h"
       "${CMAKE_SOURCE_DIR}/tests/*.c" "${CMAKE_SOURCE_DIR}/tests/*.h")
  foreach(_f IN LISTS _sources)
    file(STRINGS "${_f}" _hits REGEX "^[ \t]*#[ \t]*include.*(mame|musashi)")
    if(_hits)
      file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_f}")
      message(FATAL_ERROR
        "Licence boundary violated: ${_rel} includes a reference-only "
        "submodule.\n  ${_hits}\n"
        "ext/mame is GPL-2.0-or-later and this core is MIT; ext/musashi is read "
        "for cross-checking, never derived from. Both are run or read "
        "out-of-tree only -- see ext/README.md.")
    endif()
  endforeach()
endfunction()

# 2. No target may link a reference-only submodule, and it must never be added
#    to the build at all. Checked after all targets are defined.
function(apollo_check_no_reference_linkage)
  foreach(_name IN LISTS APOLLO_REFERENCE_ONLY)
    if(TARGET ${_name})
      message(FATAL_ERROR
        "Licence boundary violated: a CMake target named '${_name}' exists, so "
        "a reference-only submodule has been added to the build. It may be run "
        "or read out-of-tree, never built or linked -- see ext/README.md.")
    endif()
  endforeach()

  get_property(_targets DIRECTORY "${CMAKE_SOURCE_DIR}"
               PROPERTY BUILDSYSTEM_TARGETS)
  apollo_collect_targets("${CMAKE_SOURCE_DIR}" _all)
  foreach(_t IN LISTS _all)
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()
    foreach(_prop LINK_LIBRARIES INCLUDE_DIRECTORIES)
      get_target_property(_val ${_t} ${_prop})
      if(_val)
        foreach(_name IN LISTS APOLLO_REFERENCE_ONLY)
          if(_val MATCHES "(^|[/;])ext/${_name}([/;]|$)" OR _val MATCHES "(^|;)${_name}(;|$)")
            message(FATAL_ERROR
              "Licence boundary violated: target '${_t}' has ext/${_name} in "
              "its ${_prop}.\n  ${_val}\n"
              "Reference-only submodules are never linked -- see ext/README.md.")
          endif()
        endforeach()
      endif()
    endforeach()
  endforeach()
endfunction()

# Walk the directory tree collecting every target, since BUILDSYSTEM_TARGETS is
# per-directory and our targets live in subdirectories.
function(apollo_collect_targets dir out)
  get_property(_here DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  set(_acc ${_here})
  foreach(_s IN LISTS _subs)
    apollo_collect_targets("${_s}" _child)
    list(APPEND _acc ${_child})
  endforeach()
  set(${out} ${_acc} PARENT_SCOPE)
endfunction()
