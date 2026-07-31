add_executable(apollo-headless main.c)
target_link_libraries(apollo-headless PRIVATE apollo_frontend_common)
apollo_set_warnings(apollo-headless)
