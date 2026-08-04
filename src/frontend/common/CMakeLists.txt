# Shared by every frontend. Depends on the core; no frontend depends on another.
add_library(apollo_frontend_common STATIC ap_frontend.c ap_png.c)
target_include_directories(apollo_frontend_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apollo_frontend_common PUBLIC apollo_core)

# Screenshots, and only if a libpng is already there. CI builds on three
# platforms and does not have one everywhere unprompted, so a hard dependency
# would trade a screenshot for a red tree. `ap_png.c` compiles either way and
# says which build it is, which is a different answer from a failed write.
find_package(PNG QUIET)
if(PNG_FOUND)
  target_compile_definitions(apollo_frontend_common PRIVATE APOLLO_HAVE_PNG)
  target_link_libraries(apollo_frontend_common PRIVATE PNG::PNG)
  message(STATUS "apollo: screenshots enabled (libpng ${PNG_VERSION_STRING})")
else()
  message(STATUS "apollo: no libpng found; --screenshot will report so")
endif()

apollo_set_warnings(apollo_frontend_common)
