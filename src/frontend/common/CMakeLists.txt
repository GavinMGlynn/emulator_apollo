# Shared by every frontend. Depends on the core; no frontend depends on another.
add_library(apollo_frontend_common STATIC ap_frontend.c)
target_include_directories(apollo_frontend_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apollo_frontend_common PUBLIC apollo_core)
apollo_set_warnings(apollo_frontend_common)
