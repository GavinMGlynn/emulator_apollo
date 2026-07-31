# One executable and one CTest entry per suite, so a crash in one suite does not
# take down the rest.

# Unity is vendored in ext/ and never gets our warning set.
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
