add_executable(apollo-headless main.c)
target_link_libraries(apollo-headless PRIVATE apollo_core)
apollo_set_warnings(apollo-headless)
