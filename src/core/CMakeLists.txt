# The emulator core: a static library that knows nothing about any frontend.
# Frontends depend on this; it never depends on them.

add_library(apollo_core STATIC
  model/ap_model.c
  state/ap_hash.c
  time/ap_time.c
)

target_include_directories(apollo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

apollo_set_warnings(apollo_core)
