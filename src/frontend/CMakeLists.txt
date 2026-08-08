add_subdirectory(common)
add_subdirectory(headless)

# The interactive frontend, built where SDL3 is present. It was deliberately
# absent rather than stubbed until it could be implemented properly; it now is,
# and the guard is about the dependency rather than about the work.
find_package(SDL3 QUIET)
if(SDL3_FOUND)
  add_subdirectory(sdl)
  message(STATUS "apollo: interactive SDL3 frontend enabled")
else()
  message(STATUS "apollo: no SDL3 found; the interactive frontend is skipped")
endif()
