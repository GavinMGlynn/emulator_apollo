# The emulator core: a static library that knows nothing about any frontend.
# Frontends depend on this; it never depends on them.

add_library(apollo_core STATIC
  cpu/m68030/ap_m68030_bounds.c
  cpu/m68030/ap_m68030_branch.c
  cpu/m68030/ap_m68030_bus.c
  cpu/m68030/ap_m68030_cache.c
  cpu/m68030/ap_m68030_category.c
  cpu/m68030/ap_m68030_cond.c
  cpu/m68030/ap_m68030_access.c
  board/ap_atmap.c
  device/ap_i8259.c
  device/ap_mc6840.c
  board/ap_boardreg.c
  board/ap_intr.c
  board/ap_timer.c
  cpu/m68030/ap_m68030_addr.c
  cpu/m68030/ap_m68030_alu.c
  cpu/m68030/ap_m68030_arb.c
  cpu/m68030/ap_m68030_arith.c
  cpu/m68030/ap_m68030_atc.c
  cpu/m68030/ap_m68030_control.c
  cpu/m68030/ap_m68030_coproc.c
  cpu/m68030/ap_m68030_decode.c
  cpu/m68030/ap_m68030_desc.c
  cpu/m68030/ap_m68030_ea.c
  cpu/m68030/ap_m68030_ea_timing.c
  cpu/m68030/ap_m68030_exception.c
  cpu/m68030/ap_m68030_fetch.c
  cpu/m68030/ap_m68030_immediate.c
  cpu/m68030/ap_m68030_misc.c
  cpu/m68030/ap_m68030_mmusr.c
  cpu/m68030/ap_m68030_move.c
  cpu/m68030/ap_m68030_opcode.c
  cpu/m68030/ap_m68030_operand.c
  cpu/m68030/ap_m68030_overlap.c
  cpu/m68030/ap_m68030_pipe.c
  cpu/m68030/ap_m68030_quick.c
  cpu/m68030/ap_m68030_regs.c
  cpu/m68030/ap_m68030_shift.c
  cpu/m68030/ap_m68030_single.c
  cpu/m68030/ap_m68030_state.c
  cpu/m68030/ap_m68030_step.c
  cpu/m68030/ap_m68030_tc.c
  cpu/m68030/ap_m68030_timing_table.c
  cpu/m68030/ap_m68030_tt.c
  cpu/m68030/ap_m68030_walk.c
  model/ap_model.c
  machine/ap_machine.c
  probe/ap_probe.c
  state/ap_hash.c
  time/ap_time.c
)

target_include_directories(apollo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

apollo_set_warnings(apollo_core)
