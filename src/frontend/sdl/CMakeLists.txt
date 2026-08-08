# The interactive frontend. Built only where SDL3 is present: CI runs on three
# platforms and a hard dependency would trade a window for a red tree, which is
# the same argument `common/CMakeLists.txt` makes about libpng.
set(VCPKG_APPLOCAL_DEPS ON)
add_executable(apollo-sdl main.c)
set(VCPKG_APPLOCAL_DEPS OFF)
target_link_libraries(apollo-sdl PRIVATE apollo_frontend_common SDL3::SDL3)
apollo_set_warnings(apollo-sdl)

# The bounded mode is what makes an interactive program testable: `--frames N`
# runs exactly N frames and exits, and the dummy video driver means it needs no
# display. Without this the frontend could only ever be smoke-tested by hand.
add_test(NAME sdl_frames
         COMMAND apollo-sdl --boot-prom
                 ${CMAKE_SOURCE_DIR}/tests/data/sdl_prom.bin --frames 3)
set_tests_properties(sdl_frames PROPERTIES
                     ENVIRONMENT "SDL_VIDEODRIVER=dummy")
