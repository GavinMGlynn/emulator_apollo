/* The SCSI bus between the WD7000-ASC and its targets.
 *
 * `src/core/device/ap_scsi.c`: addressing, selection and its timeout, the
 * reset silence, the message set, and the one transaction the ASC's firmware
 * reduces a bus phase sequence to. The targets themselves are separate parts,
 * the same split `ap_sc499` and `ap_qic` have. */

#include "unity.h"

#include <string.h>

#include "device/ap_scsi.h"

void setUp(void) {}
void tearDown(void) {}

/* A target that records what it was asked and answers what it is told to. */
typedef struct {
  bool present;
  unsigned executes;
  unsigned resets;
  uint8_t last_lun;
  uint8_t last_cdb[AP_SCSI_CDB_MAX];
  unsigned last_cdb_length;
  /* What to answer with. */
  uint8_t status;
  ap_scsi_dir_t direction;
  unsigned transferred;
  bool refuse; /* return false from execute */
} probe_t;

static bool probe_execute(void *device, uint8_t lun, const uint8_t *cdb,
                          unsigned cdb_length, uint8_t *data, unsigned capacity,
                          ap_scsi_result_t *result) {
  probe_t *probe = (probe_t *)device;
  probe->executes++;
  probe->last_lun = lun;
  probe->last_cdb_length = cdb_length;
  memcpy(probe->last_cdb, cdb, cdb_length);
  if (probe->refuse) {
    return false;
  }
  result->status = probe->status;
  result->direction = probe->direction;
  result->transferred = probe->transferred;
  if (probe->direction == AP_SCSI_DATA_IN && data != nullptr) {
    for (unsigned i = 0; i < probe->transferred && i < capacity; i++) {
      data[i] = (uint8_t)(0xA0u + i);
    }
  }
  return true;
}

static void probe_reset(void *device) { ((probe_t *)device)->resets++; }

static bool probe_present(const void *device) {
  return ((const probe_t *)device)->present;
}

static void probe_init(probe_t *probe) {
  memset(probe, 0, sizeof *probe);
  probe->present = true;
  probe->status = AP_SCSI_STATUS_GOOD;
}

static ap_scsi_target_t probe_target(probe_t *probe) {
  return (ap_scsi_target_t){.device = probe,
                            .execute = probe_execute,
                            .reset = probe_reset,
                            .present = probe_present};
}

/* A bus with the ASC at 7 -- `[WD7000]` §6.1.2's three-bit host ID, and the
 * value `scsi14.drvr` initialises with -- and one target at 0. */
static void fitted(ap_scsi_bus_t *bus, probe_t *probe) {
  probe_init(probe);
  ap_scsi_bus_init(bus, 7u);
  const ap_scsi_target_t target = probe_target(probe);
  TEST_ASSERT_TRUE(ap_scsi_attach(bus, 0u, &target));
}

/* ---- Addressing ---------------------------------------------------------- */

/* Eight IDs and eight LUNs, because `[WD7000]` SCB byte 01 carries a three-bit
 * target in 7-5 and a three-bit LUN in 2-0. */
static void test_the_bus_carries_eight_ids_and_eight_luns(void) {
  TEST_ASSERT_EQUAL_UINT(8u, AP_SCSI_IDS);
  TEST_ASSERT_EQUAL_UINT(8u, AP_SCSI_LUNS);

  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  const ap_scsi_target_t target = probe_target(&probe);
  for (uint8_t id = 0; id < AP_SCSI_IDS; id++) {
    /* Every address but the initiator's own takes a target. */
    TEST_ASSERT_EQUAL(id != 7u, ap_scsi_attach(&bus, id, &target));
  }
  TEST_ASSERT_FALSE(ap_scsi_attach(&bus, AP_SCSI_IDS, &target));
}

/* The initiator's own slot is a placeholder, `[WD7000]` Table 6-5, so a target
 * cannot be put there and cannot answer its own initiator. */
static void test_a_target_cannot_be_fitted_at_the_initiators_own_id(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  probe_init(&probe);
  ap_scsi_bus_init(&bus, 3u);
  const ap_scsi_target_t target = probe_target(&probe);
  TEST_ASSERT_FALSE(ap_scsi_attach(&bus, 3u, &target));
  TEST_ASSERT_FALSE(ap_scsi_selectable(&bus, 3u));
  TEST_ASSERT_TRUE(ap_scsi_attach(&bus, 2u, &target));
}

/* An initiator ID wider than three bits would alias onto a real address. */
static void test_the_initiator_id_is_three_bits(void) {
  ap_scsi_bus_t bus;
  ap_scsi_bus_init(&bus, 0xFFu);
  TEST_ASSERT_EQUAL_UINT8(7u, bus.initiator_id);
}

/* ---- Selection ----------------------------------------------------------- */

/* An empty address is not an error: it is a command that takes the timeout and
 * comes back with no transaction, which is what vue `4D` names. */
static void test_an_empty_address_times_out_rather_than_failing(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);

  const uint8_t cdb[6] = {0x00u};
  ap_scsi_result_t result;
  TEST_ASSERT_FALSE(ap_scsi_selectable(&bus, 4u));
  TEST_ASSERT_FALSE(
      ap_scsi_command(&bus, 4u, 0u, cdb, sizeof cdb, nullptr, 0u, &result));
  TEST_ASSERT_EQUAL_UINT32(1u, bus.selections);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.selection_timeouts);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.commands);

  /* The timeout is `[WD7000]` §6.2.11.4's default: 250 ms. */
  TEST_ASSERT_EQUAL_UINT64((ap_time_t)AP_TIME_BASE_HZ / 4u,
                           AP_SCSI_T_SELECTION_TIMEOUT);
}

/* A drive whose power is off is not Busy -- it does not answer selection. */
static void test_an_absent_target_is_a_timeout_not_a_busy_status(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  probe.present = false;

  const uint8_t cdb[6] = {0x00u};
  ap_scsi_result_t result;
  TEST_ASSERT_FALSE(
      ap_scsi_command(&bus, 0u, 0u, cdb, sizeof cdb, nullptr, 0u, &result));
  TEST_ASSERT_EQUAL_UINT(0u, probe.executes);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.selection_timeouts);
}

/* ---- The reset silence --------------------------------------------------- */

/* `[EXB]` §23.2 and `[EXBPS]` §10.1.2: 300 ms of silence after any reset, and
 * during it a selection times out exactly as an empty address does. */
static void test_a_reset_makes_the_bus_quiet_for_three_hundred_milliseconds(
    void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);

  ap_scsi_reset(&bus);
  TEST_ASSERT_EQUAL_UINT(1u, probe.resets);
  TEST_ASSERT_FALSE(ap_scsi_selectable(&bus, 0u));

  ap_scsi_advance(&bus, AP_SCSI_T_RESET_SILENCE - 1u);
  TEST_ASSERT_FALSE(ap_scsi_selectable(&bus, 0u));
  ap_scsi_advance(&bus, AP_SCSI_T_RESET_SILENCE);
  TEST_ASSERT_TRUE(ap_scsi_selectable(&bus, 0u));

  TEST_ASSERT_EQUAL_UINT64((ap_time_t)AP_TIME_BASE_HZ * 3u / 10u,
                           AP_SCSI_T_RESET_SILENCE);
}

/* A second reset inside the window restarts the silence rather than shortening
 * it: the drive re-runs its power-on sequence from the new edge. */
static void test_a_second_reset_restarts_the_silence(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);

  ap_scsi_reset(&bus);
  ap_scsi_advance(&bus, AP_SCSI_T_RESET_SILENCE / 2u);
  ap_scsi_reset(&bus);
  TEST_ASSERT_EQUAL_UINT(2u, probe.resets);

  ap_scsi_advance(&bus, AP_SCSI_T_RESET_SILENCE);
  TEST_ASSERT_FALSE(ap_scsi_selectable(&bus, 0u));
  ap_scsi_advance(&bus, AP_SCSI_T_RESET_SILENCE / 2u + AP_SCSI_T_RESET_SILENCE);
  TEST_ASSERT_TRUE(ap_scsi_selectable(&bus, 0u));
}

/* ---- Messages, `[EXB]` Table 3-1 ----------------------------------------- */

/* Eleven messages and no others. The eight the LCPU handles transparently are
 * accepted and change nothing; that is what transparent means from here. */
static void test_the_eight_transparent_messages_are_accepted(void) {
  static const uint8_t TRANSPARENT[] = {
      AP_SCSI_MSG_COMMAND_COMPLETE, AP_SCSI_MSG_SAVE_DATA_POINTER,
      AP_SCSI_MSG_RESTORE_POINTERS, AP_SCSI_MSG_DISCONNECT,
      AP_SCSI_MSG_INITIATOR_DETECTED_ERROR, AP_SCSI_MSG_REJECT,
      AP_SCSI_MSG_NO_OPERATION, AP_SCSI_MSG_PARITY_ERROR};
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  for (unsigned i = 0; i < sizeof TRANSPARENT / sizeof TRANSPARENT[0]; i++) {
    TEST_ASSERT_TRUE(ap_scsi_message(&bus, 0u, 0u, TRANSPARENT[i]));
  }
  TEST_ASSERT_EQUAL_UINT(0u, probe.resets);
}

/* A message outside Table 3-1 is rejected rather than ignored -- the extended
 * format is not supported and the note under the table says so. */
static void test_a_message_outside_the_table_is_rejected(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  TEST_ASSERT_FALSE(ap_scsi_message(&bus, 0u, 0u, 0x01u)); /* extended */
  TEST_ASSERT_FALSE(ap_scsi_message(&bus, 0u, 0u, 0x0Au));
  TEST_ASSERT_FALSE(ap_scsi_message(&bus, 0u, 0u, 0x23u));
}

/* Identify is a field: bit 7 identifies, bit 6 grants disconnect privilege,
 * bits 2-0 are the LUN, so `80h` and `C0h` are the same message. */
static void test_identify_carries_the_lun_and_the_disconnect_privilege(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  TEST_ASSERT_TRUE(ap_scsi_message(&bus, 0u, 0u, AP_SCSI_MSG_IDENTIFY));
  TEST_ASSERT_TRUE(
      ap_scsi_message(&bus, 0u, 0u, AP_SCSI_MSG_IDENTIFY_DISCPRIV));
  TEST_ASSERT_TRUE(ap_scsi_message(&bus, 0u, 5u,
                                   (uint8_t)(AP_SCSI_MSG_IDENTIFY | 5u)));
  /* An Identify whose LUN is not the one being addressed identifies nothing. */
  TEST_ASSERT_FALSE(ap_scsi_message(&bus, 0u, 5u,
                                    (uint8_t)(AP_SCSI_MSG_IDENTIFY | 4u)));
  TEST_ASSERT_EQUAL_UINT8(0xC0u,
                          AP_SCSI_MSG_IDENTIFY | 0x40u);
}

/* A Bus Device Reset resets one target and starts its silence; a bus RST
 * resets every target. That is the whole difference between them here. */
static void test_bus_device_reset_reaches_one_target_and_rst_reaches_all(void) {
  ap_scsi_bus_t bus;
  probe_t first, second;
  fitted(&bus, &first);
  probe_init(&second);
  const ap_scsi_target_t target = probe_target(&second);
  TEST_ASSERT_TRUE(ap_scsi_attach(&bus, 1u, &target));

  TEST_ASSERT_TRUE(
      ap_scsi_message(&bus, 0u, 0u, AP_SCSI_MSG_BUS_DEVICE_RESET));
  TEST_ASSERT_EQUAL_UINT(1u, first.resets);
  TEST_ASSERT_EQUAL_UINT(0u, second.resets);

  ap_scsi_advance(&bus, AP_SCSI_T_RESET_SILENCE);
  ap_scsi_reset(&bus);
  TEST_ASSERT_EQUAL_UINT(2u, first.resets);
  TEST_ASSERT_EQUAL_UINT(1u, second.resets);
}

/* ---- The transaction ----------------------------------------------------- */

/* The CDB reaches the target byte for byte, with its LUN. */
static void test_a_command_reaches_the_target_with_its_lun(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);

  const uint8_t cdb[6] = {0x08u, 0x01u, 0x00u, 0x00u, 0x04u, 0x00u};
  ap_scsi_result_t result;
  TEST_ASSERT_TRUE(
      ap_scsi_command(&bus, 0u, 3u, cdb, sizeof cdb, nullptr, 0u, &result));
  TEST_ASSERT_EQUAL_UINT(1u, probe.executes);
  TEST_ASSERT_EQUAL_UINT8(3u, probe.last_lun);
  TEST_ASSERT_EQUAL_UINT(6u, probe.last_cdb_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cdb, probe.last_cdb, sizeof cdb);
  TEST_ASSERT_EQUAL_UINT8(AP_SCSI_STATUS_GOOD, result.status);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.commands);
}

/* Data in comes back in the initiator's buffer, and the count with it. */
static void test_a_data_in_phase_fills_the_initiators_buffer(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  probe.direction = AP_SCSI_DATA_IN;
  probe.transferred = 4u;

  uint8_t data[8] = {0};
  const uint8_t cdb[6] = {0x12u, 0u, 0u, 0u, 8u, 0u}; /* INQUIRY */
  ap_scsi_result_t result;
  TEST_ASSERT_TRUE(ap_scsi_command(&bus, 0u, 0u, cdb, sizeof cdb, data,
                                   sizeof data, &result));
  TEST_ASSERT_EQUAL_UINT(AP_SCSI_DATA_IN, result.direction);
  TEST_ASSERT_EQUAL_UINT(4u, result.transferred);
  TEST_ASSERT_EQUAL_UINT8(0xA0u, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xA3u, data[3]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, data[4]);
}

/* `[WD7000]` SCB bytes 16-18 are "the runaway-target guard", and this is where
 * it lives: a target may not claim more than the initiator offered. */
static void test_a_target_cannot_claim_more_than_the_allocation(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  probe.direction = AP_SCSI_DATA_IN;
  probe.transferred = 4096u;

  uint8_t data[8] = {0};
  const uint8_t cdb[6] = {0x08u, 0u, 0u, 0u, 1u, 0u};
  ap_scsi_result_t result;
  TEST_ASSERT_TRUE(ap_scsi_command(&bus, 0u, 0u, cdb, sizeof cdb, data,
                                   sizeof data, &result));
  TEST_ASSERT_EQUAL_UINT(sizeof data, result.transferred);
}

/* A target that answered selection and then could not run is Check Condition,
 * not a timeout: it has been selected, so the transaction exists. */
static void test_a_target_that_cannot_run_gives_check_condition(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);
  probe.refuse = true;

  const uint8_t cdb[6] = {0x00u};
  ap_scsi_result_t result;
  TEST_ASSERT_TRUE(
      ap_scsi_command(&bus, 0u, 0u, cdb, sizeof cdb, nullptr, 0u, &result));
  TEST_ASSERT_EQUAL_UINT8(AP_SCSI_STATUS_CHECK_CONDITION, result.status);
  TEST_ASSERT_EQUAL_UINT(AP_SCSI_DATA_NONE, result.direction);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.selection_timeouts);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.check_conditions);
}

/* The four status codes `[EXB]` Table 4-2 defines, and the shape that makes
 * them four: bit 0 always zero, bits 7-5 reserved. */
static void test_the_four_status_codes_are_the_tables_own(void) {
  TEST_ASSERT_EQUAL_UINT8(0x00u, AP_SCSI_STATUS_GOOD);
  TEST_ASSERT_EQUAL_UINT8(0x02u, AP_SCSI_STATUS_CHECK_CONDITION);
  TEST_ASSERT_EQUAL_UINT8(0x08u, AP_SCSI_STATUS_BUSY);
  TEST_ASSERT_EQUAL_UINT8(0x18u, AP_SCSI_STATUS_RESERVATION_CONFLICT);
  static const uint8_t ALL[] = {AP_SCSI_STATUS_GOOD,
                                AP_SCSI_STATUS_CHECK_CONDITION,
                                AP_SCSI_STATUS_BUSY,
                                AP_SCSI_STATUS_RESERVATION_CONFLICT};
  for (unsigned i = 0; i < sizeof ALL / sizeof ALL[0]; i++) {
    TEST_ASSERT_EQUAL_UINT8(0u, ALL[i] & 0x01u); /* bit 0 always zero */
    TEST_ASSERT_EQUAL_UINT8(0u, ALL[i] & 0xE0u); /* bits 7-5 reserved */
  }
}

/* A CDB longer than the SCB's twelve bytes, or of no length at all, is not a
 * command. */
static void test_a_cdb_must_fit_the_scbs_twelve_bytes(void) {
  ap_scsi_bus_t bus;
  probe_t probe;
  fitted(&bus, &probe);

  uint8_t cdb[AP_SCSI_CDB_MAX + 1u] = {0};
  ap_scsi_result_t result;
  TEST_ASSERT_FALSE(ap_scsi_command(&bus, 0u, 0u, cdb, 0u, nullptr, 0u,
                                    &result));
  TEST_ASSERT_FALSE(ap_scsi_command(&bus, 0u, 0u, cdb, sizeof cdb, nullptr, 0u,
                                    &result));
  TEST_ASSERT_TRUE(ap_scsi_command(&bus, 0u, 0u, cdb, AP_SCSI_CDB_MAX, nullptr,
                                   0u, &result));
  TEST_ASSERT_EQUAL_UINT(12u, AP_SCSI_CDB_MAX);
}

/* `[EXB]` §4.3: Group 0 only, six bytes; every other group code is an error
 * condition, and the bus refuses it by having no length for it. */
static void test_only_group_zero_has_a_length(void) {
  for (unsigned opcode = 0; opcode < 0x20u; opcode++) {
    TEST_ASSERT_EQUAL_UINT(6u, ap_scsi_cdb_length((uint8_t)opcode));
  }
  for (unsigned opcode = 0x20u; opcode < 0x100u; opcode++) {
    TEST_ASSERT_EQUAL_UINT(0u, ap_scsi_cdb_length((uint8_t)opcode));
  }
  TEST_ASSERT_EQUAL_UINT(6u, AP_SCSI_CDB_GROUP_0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_bus_carries_eight_ids_and_eight_luns);
  RUN_TEST(test_a_target_cannot_be_fitted_at_the_initiators_own_id);
  RUN_TEST(test_the_initiator_id_is_three_bits);
  RUN_TEST(test_an_empty_address_times_out_rather_than_failing);
  RUN_TEST(test_an_absent_target_is_a_timeout_not_a_busy_status);
  RUN_TEST(test_a_reset_makes_the_bus_quiet_for_three_hundred_milliseconds);
  RUN_TEST(test_a_second_reset_restarts_the_silence);
  RUN_TEST(test_the_eight_transparent_messages_are_accepted);
  RUN_TEST(test_a_message_outside_the_table_is_rejected);
  RUN_TEST(test_identify_carries_the_lun_and_the_disconnect_privilege);
  RUN_TEST(test_bus_device_reset_reaches_one_target_and_rst_reaches_all);
  RUN_TEST(test_a_command_reaches_the_target_with_its_lun);
  RUN_TEST(test_a_data_in_phase_fills_the_initiators_buffer);
  RUN_TEST(test_a_target_cannot_claim_more_than_the_allocation);
  RUN_TEST(test_a_target_that_cannot_run_gives_check_condition);
  RUN_TEST(test_the_four_status_codes_are_the_tables_own);
  RUN_TEST(test_a_cdb_must_fit_the_scbs_twelve_bytes);
  RUN_TEST(test_only_group_zero_has_a_length);
  return UNITY_END();
}
