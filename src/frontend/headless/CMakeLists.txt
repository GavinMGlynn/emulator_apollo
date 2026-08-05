# The only executable in this directory, so it is the only one that can copy
# vcpkg's DLLs beside itself -- see the top-level `CMakeLists.txt` on why that
# is switched off everywhere else.
set(VCPKG_APPLOCAL_DEPS ON)
add_executable(apollo-headless main.c)
set(VCPKG_APPLOCAL_DEPS OFF)
target_link_libraries(apollo-headless PRIVATE apollo_frontend_common)
apollo_set_warnings(apollo-headless)
