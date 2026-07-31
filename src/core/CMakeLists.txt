# The emulator core: a static library that knows nothing about any frontend.
# Frontends depend on this; it never depends on them.

add_library(apollo_core STATIC
  cpu/m68030/ap_m68030_branch.c
  cpu/m68030/ap_m68030_bus.c
  cpu/m68030/ap_m68030_cache.c
  cpu/m68030/ap_m68030_cond.c
  cpu/m68030/ap_m68030_atc.c
  cpu/m68030/ap_m68030_control.c
  cpu/m68030/ap_m68030_desc.c
  cpu/m68030/ap_m68030_ea.c
  cpu/m68030/ap_m68030_exception.c
  cpu/m68030/ap_m68030_misc.c
  cpu/m68030/ap_m68030_mmusr.c
  cpu/m68030/ap_m68030_opcode.c
  cpu/m68030/ap_m68030_pipe.c
  cpu/m68030/ap_m68030_quick.c
  cpu/m68030/ap_m68030_regs.c
  cpu/m68030/ap_m68030_tc.c
  cpu/m68030/ap_m68030_tt.c
  cpu/m68030/ap_m68030_walk.c
  model/ap_model.c
  state/ap_hash.c
  time/ap_time.c
)

target_include_directories(apollo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

apollo_set_warnings(apollo_core)
