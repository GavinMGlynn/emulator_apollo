/* EXABYTE EXB-8200, the DS5500's SCSI target.
 *
 * `src/core/device/ap_exb8200.c` against `[EXB]` 510006-007 and `[EXBPS]`
 * 510005-006, both walked whole. Every test states a rule from one of them. */

#include "unity.h"

#include <string.h>

#include "device/ap_exb8200.h"

void setUp(void) {}
void tearDown(void) {}

/* Host memory for the data phases, addressed from zero. */
static uint8_t host[4096];

static uint8_t host_read(void *context, uint32_t address) {
  (void)context;
  return host[address % sizeof host];
}

static void host_write(void *context, uint32_t address, uint8_t value) {
  (void)context;
  host[address % sizeof host] = value;
}

static const ap_scsi_memory_t MEMORY = {
    .context = nullptr, .read = host_read, .write = host_write};

/* A cartridge: 64 records and 64 KB of data. */
static ap_exb8200_record_t records[64];
static uint8_t tape[65536];

static ap_exb8200_media_t cartridge(bool writable) {
  memset(records, 0, sizeof records);
  memset(tape, 0, sizeof tape);
  return (ap_exb8200_media_t){.record = records,
                              .records = 0u,
                              .capacity = 64u,
                              .data = tape,
                              .data_bytes = sizeof tape,
                              .data_used = 0u,
                              .writable = writable,
                              .medium_type = 0x85u, /* P6-120, §9.2 */
                              .blocks_to_leot = 0x22FC20u};
}

/* A drive with a cartridge in, past the power-on Unit Attention. */
static void loaded(ap_exb8200_t *drive, bool writable) {
  memset(host, 0, sizeof host);
  ap_exb8200_power_on(drive);
  const ap_exb8200_media_t media = cartridge(writable);
  TEST_ASSERT_TRUE(ap_exb8200_insert(drive, &media));
  /* §23.3's Unit Attention is consumed by the first non-exempt command. */
  ap_scsi_result_t result = {0};
  const uint8_t tur[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0};
  const ap_scsi_target_t target = ap_exb8200_target(drive);
  (void)target.execute(drive, 0u, tur, 6u, nullptr, 0u, 0u, &result);
}

/* Run one CDB and give back its status. */
static uint8_t run(ap_exb8200_t *drive, const uint8_t *cdb,
                   ap_scsi_result_t *result) {
  const ap_scsi_target_t target = ap_exb8200_target(drive);
  memset(result, 0, sizeof *result);
  result->status = AP_SCSI_STATUS_GOOD;
  TEST_ASSERT_TRUE(
      target.execute(drive, 0u, cdb, 6u, &MEMORY, 0u, sizeof host, result));
  return result->status;
}

/* The sense key the last Check Condition left. */
static uint8_t key(const ap_exb8200_t *drive) {
  return drive->sense[2] & AP_EXB8200_SENSE_KEY_MASK;
}

/* ---- The command set ------------------------------------------------------ */

/* Eighteen commands, `[EXB]` Table 4-1, and their opcodes. */
static void test_the_command_set_is_the_tables_own(void) {
  TEST_ASSERT_EQUAL_UINT(18u, AP_EXB8200_COMMANDS);
  TEST_ASSERT_EQUAL_HEX8(0x00u, AP_EXB8200_CMD_TEST_UNIT_READY);
  TEST_ASSERT_EQUAL_HEX8(0x01u, AP_EXB8200_CMD_REWIND);
  TEST_ASSERT_EQUAL_HEX8(0x03u, AP_EXB8200_CMD_REQUEST_SENSE);
  TEST_ASSERT_EQUAL_HEX8(0x05u, AP_EXB8200_CMD_READ_BLOCK_LIMITS);
  TEST_ASSERT_EQUAL_HEX8(0x08u, AP_EXB8200_CMD_READ);
  TEST_ASSERT_EQUAL_HEX8(0x0Au, AP_EXB8200_CMD_WRITE);
  TEST_ASSERT_EQUAL_HEX8(0x10u, AP_EXB8200_CMD_WRITE_FILEMARKS);
  TEST_ASSERT_EQUAL_HEX8(0x11u, AP_EXB8200_CMD_SPACE);
  TEST_ASSERT_EQUAL_HEX8(0x12u, AP_EXB8200_CMD_INQUIRY);
  TEST_ASSERT_EQUAL_HEX8(0x15u, AP_EXB8200_CMD_MODE_SELECT);
  TEST_ASSERT_EQUAL_HEX8(0x16u, AP_EXB8200_CMD_RESERVE_UNIT);
  TEST_ASSERT_EQUAL_HEX8(0x17u, AP_EXB8200_CMD_RELEASE_UNIT);
  TEST_ASSERT_EQUAL_HEX8(0x19u, AP_EXB8200_CMD_ERASE);
  TEST_ASSERT_EQUAL_HEX8(0x1Au, AP_EXB8200_CMD_MODE_SENSE);
  TEST_ASSERT_EQUAL_HEX8(0x1Bu, AP_EXB8200_CMD_LOAD_UNLOAD);
  TEST_ASSERT_EQUAL_HEX8(0x1Cu, AP_EXB8200_CMD_RECEIVE_DIAGNOSTIC);
  TEST_ASSERT_EQUAL_HEX8(0x1Du, AP_EXB8200_CMD_SEND_DIAGNOSTIC);
  TEST_ASSERT_EQUAL_HEX8(0x1Eu, AP_EXB8200_CMD_PREVENT_ALLOW);
}

/* §4.4's Illegal Operation Code: an opcode outside the eighteen. */
static void test_an_unsupported_opcode_is_illegal_request(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  const uint8_t cdb[6] = {0x04u, 0, 0, 0, 0, 0}; /* FORMAT UNIT, not ours */
  ap_scsi_result_t result;
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
}

/* §4.4: "if the Link or Flag bits are not 0, the command operation is
 * terminated" with Illegal Request. */
static void test_link_or_flag_set_is_illegal_request(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t link[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0x01u};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, link, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
  const uint8_t flag[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0x02u};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, flag, &result));
}

/* ---- INQUIRY, ch. 6 ------------------------------------------------------- */

/* 56 bytes: `01` sequential access, removable, ANSI 1, additional length `33h`,
 * and the vendor and product strings the guest's own table matches. */
static void test_inquiry_is_the_string_rmt_scsi_matches(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  const uint8_t cdb[6] = {AP_EXB8200_CMD_INQUIRY, 0, 0, 0,
                          AP_EXB8200_INQUIRY_BYTES, 0};
  ap_scsi_result_t result;
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_INQUIRY_BYTES, result.transferred);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_DEVICE_TYPE_SEQUENTIAL, host[0]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_INQUIRY_RMB, host[1]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_INQUIRY_ANSI_1, host[2]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_INQUIRY_ADDITIONAL, host[4]);
  TEST_ASSERT_EQUAL_MEMORY("EXABYTE ", &host[8], 8u);
  TEST_ASSERT_EQUAL_MEMORY("EXB-8200        ", &host[16], 16u);
  /* This is the 24-byte string `rmt_scsi` compares against. */
  TEST_ASSERT_EQUAL_MEMORY("EXABYTE EXB-8200        ", &host[8], 24u);
}

/* §6.2: "if the LUN in the CDB is not 0, the value returned is `7Fh`" -- so
 * INQUIRY answers a bad LUN rather than refusing it, which is how a host
 * discovers the LUN is invalid. */
static void test_inquiry_answers_a_bad_lun_with_device_type_7f(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  const uint8_t cdb[6] = {AP_EXB8200_CMD_INQUIRY, (uint8_t)(1u << 5), 0, 0,
                          AP_EXB8200_INQUIRY_BYTES, 0};
  ap_scsi_result_t result;
  const ap_scsi_target_t target = ap_exb8200_target(&drive);
  memset(&result, 0, sizeof result);
  TEST_ASSERT_TRUE(
      target.execute(&drive, 1u, cdb, 6u, &MEMORY, 0u, sizeof host, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, result.status);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_DEVICE_TYPE_BAD_LUN, host[0]);
}

/* §6.1: "a value of 0 indicates that no Inquiry data is to be transferred.
 * This is not an error." */
static void test_an_allocation_of_zero_transfers_nothing_and_is_not_an_error(
    void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  const uint8_t cdb[6] = {AP_EXB8200_CMD_INQUIRY, 0, 0, 0, 0, 0};
  ap_scsi_result_t result;
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_UINT(0u, result.transferred);
}

/* ---- REQUEST SENSE, ch. 15 ------------------------------------------------ */

/* 26 bytes of Error Class 7, and an allocation of 0 transfers **four**. */
static void test_request_sense_is_twenty_six_bytes_and_zero_means_four(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t full[6] = {AP_EXB8200_CMD_REQUEST_SENSE, 0, 0, 0,
                           AP_EXB8200_SENSE_BYTES, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, full, &result));
  TEST_ASSERT_EQUAL_UINT(26u, result.transferred);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_SENSE_CLASS_7, host[0] & 0x70u);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_ADDITIONAL_SENSE_LENGTH, host[7]);

  const uint8_t none[6] = {AP_EXB8200_CMD_REQUEST_SENSE, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, none, &result));
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_SENSE_MINIMUM, result.transferred);
  TEST_ASSERT_EQUAL_UINT(4u, AP_EXB8200_SENSE_MINIMUM);
}

/* Table 15-1's own key set: `1h`, `Ah`, `Ch` and `Eh` are unused, `9h` is
 * EXABYTE's vendor-unique key and `Dh` is Volume Overflow. */
static void test_the_sense_keys_are_table_fifteen_ones(void) {
  TEST_ASSERT_EQUAL_HEX8(0x0u, AP_EXB8200_KEY_NO_SENSE);
  TEST_ASSERT_EQUAL_HEX8(0x2u, AP_EXB8200_KEY_NOT_READY);
  TEST_ASSERT_EQUAL_HEX8(0x3u, AP_EXB8200_KEY_MEDIUM_ERROR);
  TEST_ASSERT_EQUAL_HEX8(0x4u, AP_EXB8200_KEY_HARDWARE_ERROR);
  TEST_ASSERT_EQUAL_HEX8(0x5u, AP_EXB8200_KEY_ILLEGAL_REQUEST);
  TEST_ASSERT_EQUAL_HEX8(0x6u, AP_EXB8200_KEY_UNIT_ATTENTION);
  TEST_ASSERT_EQUAL_HEX8(0x7u, AP_EXB8200_KEY_DATA_PROTECT);
  TEST_ASSERT_EQUAL_HEX8(0x8u, AP_EXB8200_KEY_BLANK_CHECK);
  TEST_ASSERT_EQUAL_HEX8(0x9u, AP_EXB8200_KEY_EXABYTE);
  TEST_ASSERT_EQUAL_HEX8(0xBu, AP_EXB8200_KEY_ABORTED_COMMAND);
  TEST_ASSERT_EQUAL_HEX8(0xDu, AP_EXB8200_KEY_VOLUME_OVERFLOW);
}

/* §15: the sense survives a REQUEST SENSE and an INQUIRY and is cleared by the
 * next command that is neither. */
static void test_sense_survives_request_sense_and_inquiry_only(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  /* Make some sense: an unsupported opcode. */
  const uint8_t bad[6] = {0x04u, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, bad, &result));
  TEST_ASSERT_TRUE(drive.sense_valid);

  const uint8_t inq[6] = {AP_EXB8200_CMD_INQUIRY, 0, 0, 0, 5u, 0};
  (void)run(&drive, inq, &result);
  TEST_ASSERT_TRUE(drive.sense_valid);
  const uint8_t rs[6] = {AP_EXB8200_CMD_REQUEST_SENSE, 0, 0, 0, 26u, 0};
  (void)run(&drive, rs, &result);
  TEST_ASSERT_TRUE(drive.sense_valid);

  const uint8_t tur[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, tur, &result));
  TEST_ASSERT_FALSE(drive.sense_valid);
}

/* ---- TEST UNIT READY and the Unit Attention, ch. 20 and §23.3 ------------- */

/* §23.3: the first command after a power-on reset other than REQUEST SENSE or
 * INQUIRY is Unit Attention, and the command is **not performed**. */
static void test_the_first_command_after_power_on_is_unit_attention(void) {
  ap_exb8200_t drive;
  ap_exb8200_power_on(&drive);
  const ap_exb8200_media_t media = cartridge(true);
  TEST_ASSERT_TRUE(ap_exb8200_insert(&drive, &media));

  ap_scsi_result_t result;
  const uint8_t tur[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, tur, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_UNIT_ATTENTION, key(&drive));
  /* And the next one runs. */
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, tur, &result));
}

/* §20.2: no cartridge is Not Ready with TNP and ASC `04`/ASCQ `00`. */
static void test_no_cartridge_is_not_ready_with_tnp(void) {
  ap_exb8200_t drive;
  ap_exb8200_power_on(&drive);
  ap_scsi_result_t result;
  const uint8_t tur[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0};
  /* Consume the power-on Unit Attention first. */
  (void)run(&drive, tur, &result);
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, tur, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_NOT_READY, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_U19_TNP,
                         drive.sense[19] & AP_EXB8200_U19_TNP);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_ASC_NOT_READY, drive.sense[12]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_ASCQ_NOT_MOUNTED, drive.sense[13]);
}

/* §20.2's distinction, which only the part's own manual carries: an unload done
 * with the **front-panel button** leaves Unit Attention with TNP, where the
 * UNLOAD *command* leaves Not Ready. */
static void test_the_button_leaves_unit_attention_and_the_command_not_ready(
    void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_exb8200_unload_button(&drive);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_UNIT_ATTENTION, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_U19_TNP,
                         drive.sense[19] & AP_EXB8200_U19_TNP);

  ap_exb8200_t other;
  loaded(&other, true);
  ap_scsi_result_t result;
  const uint8_t unload[6] = {AP_EXB8200_CMD_LOAD_UNLOAD, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&other, unload, &result));
  const uint8_t tur[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&other, tur, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_NOT_READY, key(&other));
}

/* ---- READ BLOCK LIMITS, ch. 12 -------------------------------------------- */

/* 240 KB and one byte, and 160 KB when No Disconnect is set. */
static void test_the_block_limits_are_the_manuals(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t cdb[6] = {AP_EXB8200_CMD_READ_BLOCK_LIMITS, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_UINT(6u, result.transferred);
  TEST_ASSERT_EQUAL_HEX8(0x03u, host[1]);
  TEST_ASSERT_EQUAL_HEX8(0xC0u, host[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, host[3]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, host[4]);
  TEST_ASSERT_EQUAL_HEX8(0x01u, host[5]);

  drive.vendor_flags |= AP_EXB8200_VU_ND;
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_HEX8(0x02u, host[1]);
  TEST_ASSERT_EQUAL_HEX8(0x80u, host[2]);
}

/* ---- WRITE and READ, ch. 21 and 11 ---------------------------------------- */

/* Write two fixed blocks and read them back. */
static void test_a_fixed_write_and_read_round_trip(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  for (unsigned i = 0; i < 2048u; i++) {
    host[i] = (uint8_t)(i & 0xFFu);
  }
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 2u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, write, &result));
  TEST_ASSERT_EQUAL_UINT(2u, drive.media.records);
  TEST_ASSERT_EQUAL_UINT(1024u, drive.media.record[0].length);

  const uint8_t rewind[6] = {AP_EXB8200_CMD_REWIND, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, rewind, &result));
  memset(host, 0, sizeof host);
  const uint8_t read[6] = {AP_EXB8200_CMD_READ, 0x01u, 0, 0, 2u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, read, &result));
  TEST_ASSERT_EQUAL_UINT(2048u, result.transferred);
  TEST_ASSERT_EQUAL_HEX8(0x00u, host[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, host[255]);
}

/* §21.5: a write-protected cartridge is Data Protect with the WP bit. */
static void test_a_write_protected_cartridge_is_data_protect(void) {
  ap_exb8200_t drive;
  loaded(&drive, false);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, write, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_DATA_PROTECT, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_U20_WP,
                         drive.sense[20] & AP_EXB8200_U20_WP);
}

/* §8.5's cross-command rule: Fixed set with a block length of 0, or Fixed clear
 * with a block length other than 0, is an error. */
static void test_the_modes_must_match(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  /* Default block length is 1024, so a *variable* write is the mismatch. */
  const uint8_t variable[6] = {AP_EXB8200_CMD_WRITE, 0x00u, 0, 0x04u, 0x00u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, variable, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
}

/* §11.1 and §21.1: a transfer length of 0 moves nothing, changes nothing and
 * "is not an error". */
static void test_a_zero_transfer_length_is_not_an_error(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, write, &result));
  TEST_ASSERT_EQUAL_UINT(0u, drive.media.records);
  const uint8_t read[6] = {AP_EXB8200_CMD_READ, 0x01u, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, read, &result));
}

/* §11.3: reading a blank tape ends with Blank Check, and at LBOT §24.2 adds
 * EOM. */
static void test_reading_blank_tape_is_blank_check(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t read[6] = {AP_EXB8200_CMD_READ, 0x01u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, read, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_BLANK_CHECK, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_SENSE_EOM,
                         drive.sense[2] & AP_EXB8200_SENSE_EOM);
}

/* §11.3: a filemark ends a read with FMK and No Sense, positioned on its EOT
 * side, and setting SILI does not suppress it. */
static void test_a_filemark_ends_a_read_with_fmk(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 1u, 0};
  (void)run(&drive, write, &result);
  const uint8_t mark[6] = {AP_EXB8200_CMD_WRITE_FILEMARKS, 0, 0, 0, 1u, 0};
  (void)run(&drive, mark, &result);
  const uint8_t rewind[6] = {AP_EXB8200_CMD_REWIND, 0, 0, 0, 0, 0};
  (void)run(&drive, rewind, &result);

  const uint8_t read[6] = {AP_EXB8200_CMD_READ, 0x01u, 0, 0, 4u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, read, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_NO_SENSE, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_SENSE_FMK,
                         drive.sense[2] & AP_EXB8200_SENSE_FMK);
  /* One block was read before the mark, so three were not found. */
  TEST_ASSERT_EQUAL_HEX8(3u, drive.sense[6]);
  TEST_ASSERT_EQUAL_UINT(1024u, result.transferred);
}

/* §11.3: SILI in fixed mode is Illegal Request -- it is valid only in variable
 * read mode. */
static void test_sili_in_fixed_mode_is_illegal_request(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t read[6] = {AP_EXB8200_CMD_READ, 0x03u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, read, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
}

/* §24.11, the one fact the command chapters leave implicit: after a write, a
 * READ is **Illegal Request**, not Blank Check -- the head is in the gap track
 * rather than at end of data. */
static void test_a_read_after_a_write_is_illegal_request(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, write, &result));
  const uint8_t read[6] = {AP_EXB8200_CMD_READ, 0x01u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, read, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
}

/* §21.5's Illegal Tape Position: a write is legal only at LBOT, at blank tape
 * or at the BOT side of a **long** filemark. */
static void test_a_write_in_the_middle_of_data_is_illegal(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 2u, 0};
  (void)run(&drive, write, &result);
  const uint8_t rewind[6] = {AP_EXB8200_CMD_REWIND, 0, 0, 0, 0, 0};
  (void)run(&drive, rewind, &result);
  /* Space forward one block: now between two data records. */
  const uint8_t space[6] = {AP_EXB8200_CMD_SPACE, 0x00u, 0, 0, 1u, 0};
  (void)run(&drive, space, &result);
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, write, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
}

/* ---- WRITE FILEMARKS, ch. 22 ---------------------------------------------- */

/* Byte 05 bit 7 picks the kind, and §22 sizes them: 270 tracks long, 60 short.
 * A count of 0 writes none and "is not an error". */
static void test_both_filemark_kinds_and_a_count_of_zero(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t longmark[6] = {AP_EXB8200_CMD_WRITE_FILEMARKS, 0, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, longmark, &result));
  TEST_ASSERT_EQUAL_UINT(1u, drive.media.records);
  TEST_ASSERT_FALSE(drive.media.record[0].short_mark);

  const uint8_t shortmark[6] = {AP_EXB8200_CMD_WRITE_FILEMARKS, 0, 0, 0, 1u,
                                0x80u};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, shortmark, &result));
  TEST_ASSERT_EQUAL_UINT(2u, drive.media.records);
  TEST_ASSERT_TRUE(drive.media.record[1].short_mark);

  const uint8_t none[6] = {AP_EXB8200_CMD_WRITE_FILEMARKS, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, none, &result));
  TEST_ASSERT_EQUAL_UINT(2u, drive.media.records);

  /* And the capacity they cost: `[EXBPS]` §4.3.5's 270 and 60 tracks of eight
   * 1,024-byte blocks each, on top of LBOT's own `500h`. */
  const uint32_t blocks = ap_exb8200_physical_blocks(&drive);
  TEST_ASSERT_EQUAL_UINT32(AP_EXB8200_LBOT_BLOCKS +
                               (270u + 60u) * AP_EXB8200_TRACK_BLOCKS,
                           blocks);
}

/* ---- SPACE, ch. 19 --------------------------------------------------------- */

/* Code `00` blocks and `01` filemarks; `10` and `11` are not supported. */
static void test_space_codes_two_and_three_are_refused(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t two[6] = {AP_EXB8200_CMD_SPACE, 0x02u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, two, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
  const uint8_t three[6] = {AP_EXB8200_CMD_SPACE, 0x03u, 0, 0, 1u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, three, &result));
}

/* §19.1: the count is signed, and a backward space past LBOT ends there with
 * EOM and the LBOT bit and No Sense. */
static void test_a_backward_space_past_lbot_stops_there(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 2u, 0};
  (void)run(&drive, write, &result);
  /* -5 in two's complement over three bytes. */
  const uint8_t back[6] = {AP_EXB8200_CMD_SPACE, 0x00u, 0xFFu, 0xFFu, 0xFBu, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, back, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_NO_SENSE, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_SENSE_EOM,
                         drive.sense[2] & AP_EXB8200_SENSE_EOM);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_U19_LBOT,
                         drive.sense[19] & AP_EXB8200_U19_LBOT);
}

/* §19.4: a filemark met while spacing over **blocks** ends the space with FMK
 * and No Sense. */
static void test_a_filemark_ends_a_block_space(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 1u, 0};
  (void)run(&drive, write, &result);
  const uint8_t mark[6] = {AP_EXB8200_CMD_WRITE_FILEMARKS, 0, 0, 0, 1u, 0};
  (void)run(&drive, mark, &result);
  const uint8_t rewind[6] = {AP_EXB8200_CMD_REWIND, 0, 0, 0, 0, 0};
  (void)run(&drive, rewind, &result);

  const uint8_t space[6] = {AP_EXB8200_CMD_SPACE, 0x00u, 0, 0, 4u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, space, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_SENSE_FMK,
                         drive.sense[2] & AP_EXB8200_SENSE_FMK);
}

/* ---- MODE SELECT and MODE SENSE, ch. 8 and 9 ------------------------------ */

/* §9.1: seventeen bytes, and §8.4's defaults come back in them. */
static void test_mode_sense_returns_the_defaults(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t cdb[6] = {AP_EXB8200_CMD_MODE_SENSE, 0, 0, 0,
                          AP_EXB8200_MODE_BYTES, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_UINT(17u, result.transferred);
  TEST_ASSERT_EQUAL_HEX8(16u, host[0]); /* length excluding itself */
  TEST_ASSERT_EQUAL_HEX8(0x85u, host[1]); /* medium type */
  /* Buffered is the power-on default, and the cartridge is writable. */
  TEST_ASSERT_EQUAL_HEX8(0x10u, host[2]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_MODE_DESCRIPTOR, host[3]);
  /* Block length 1,024 = `400h`. */
  TEST_ASSERT_EQUAL_HEX8(0x04u, host[10]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, host[11]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_MOTION_THRESHOLD_DEFAULT, host[14]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_RECONNECT_THRESHOLD_DEFAULT, host[15]);
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_GAP_THRESHOLD_DEFAULT, host[16]);
}

/* Table 8-1: only 0, 4-9 and `0Ch`-`11h` are legal parameter list lengths. */
static void test_only_table_eight_ones_lengths_are_legal(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  for (unsigned length = 0; length <= 0x14u; length++) {
    const bool legal = length == 0u || (length >= 4u && length <= 9u) ||
                       (length >= 0x0Cu && length <= 0x11u);
    memset(host, 0, sizeof host);
    host[2] = 0x10u; /* buffered, speed 0 */
    const bool descriptor = length >= 0x0Cu;
    host[3] = descriptor ? AP_EXB8200_MODE_DESCRIPTOR : 0u;
    host[10] = 0x04u; /* block length 1,024 */
    /* The vendor bytes follow whatever is there: the header alone at 4, or the
     * header and the descriptor at 12. Table 8-1's lengths 05-09 are the first
     * case and 0D-11 the second, which is the whole point of the table. */
    const unsigned vendor = descriptor ? 12u : 4u;
    host[vendor + 2u] = AP_EXB8200_MOTION_THRESHOLD_DEFAULT;
    host[vendor + 3u] = AP_EXB8200_RECONNECT_THRESHOLD_DEFAULT;
    const uint8_t cdb[6] = {AP_EXB8200_CMD_MODE_SELECT, 0, 0, 0,
                            (uint8_t)length, 0};
    const uint8_t status = run(&drive, cdb, &result);
    if (legal) {
      TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, status);
    } else {
      TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION, status);
      TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
    }
  }
}

/* §8.2: buffered mode is `000` or `001` and the speed field must be 0. */
static void test_an_invalid_buffered_mode_or_speed_is_refused(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  memset(host, 0, sizeof host);
  host[2] = 0x20u; /* buffered mode 010 */
  const uint8_t cdb[6] = {AP_EXB8200_CMD_MODE_SELECT, 0, 0, 0, 4u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, cdb, &result));
  memset(host, 0, sizeof host);
  host[2] = 0x11u; /* buffered 001, speed 1 */
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, cdb, &result));
}

/* §8.4: "any value greater than `07h` is treated as `07h`" for the gap
 * threshold, and both other thresholds are bounded `20h`-`D0h`. */
static void test_the_gap_threshold_saturates_and_the_others_are_bounded(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  memset(host, 0, sizeof host);
  host[2] = 0x10u;
  host[3] = AP_EXB8200_MODE_DESCRIPTOR;
  host[10] = 0x04u;
  host[12] = 0u;
  host[13] = 0u;
  host[14] = 0x40u;
  host[15] = 0x40u;
  host[16] = 0xFFu;
  const uint8_t cdb[6] = {AP_EXB8200_CMD_MODE_SELECT, 0, 0, 0, 0x11u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_GAP_THRESHOLD_CAP, drive.gap_threshold);

  host[14] = 0x10u; /* below `20h` */
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
}

/* ---- ERASE, LOAD/UNLOAD, PREVENT, diagnostics ---------------------------- */

/* §5.1: "the EXB-8200 supports only the long erase operation ... if the Long
 * bit is not set, the EXB-8200 accepts the ERASE command but no operation is
 * performed." */
static void test_erase_without_the_long_bit_does_nothing_and_succeeds(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x01u, 0, 0, 2u, 0};
  (void)run(&drive, write, &result);
  const uint8_t rewind[6] = {AP_EXB8200_CMD_REWIND, 0, 0, 0, 0, 0};
  (void)run(&drive, rewind, &result);

  const uint8_t shortly[6] = {AP_EXB8200_CMD_ERASE, 0x00u, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, shortly, &result));
  TEST_ASSERT_EQUAL_UINT(2u, drive.media.records);

  const uint8_t longly[6] = {AP_EXB8200_CMD_ERASE, 0x01u, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, longly, &result));
  TEST_ASSERT_EQUAL_UINT(0u, drive.media.records);
  /* §5: "when the erase operation is successfully completed, a rewind
   * automatically occurs." */
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_AT_LBOT, drive.where);
}

/* §7: UNLOAD ejects, unless PREVENT MEDIUM REMOVAL is in force -- in which
 * case the tape is unloaded but the cartridge stays. */
static void test_prevent_medium_removal_keeps_the_cartridge_in(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t prevent[6] = {AP_EXB8200_CMD_PREVENT_ALLOW, 0, 0, 0, 0x01u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, prevent, &result));
  const uint8_t unload[6] = {AP_EXB8200_CMD_LOAD_UNLOAD, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, unload, &result));
  TEST_ASSERT_TRUE(drive.present);
  TEST_ASSERT_FALSE(drive.loaded);

  /* And the front-panel button does nothing while it is in force, §23.1. */
  ap_exb8200_unload_button(&drive);
  TEST_ASSERT_TRUE(drive.present);

  const uint8_t allow[6] = {AP_EXB8200_CMD_PREVENT_ALLOW, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, allow, &result));
  ap_exb8200_unload_button(&drive);
  TEST_ASSERT_FALSE(drive.present);
}

/* §7.3: loading with no cartridge is Not Ready with TNP. */
static void test_loading_with_no_cartridge_is_not_ready(void) {
  ap_exb8200_t drive;
  ap_exb8200_power_on(&drive);
  ap_scsi_result_t result;
  const uint8_t tur[6] = {AP_EXB8200_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0};
  (void)run(&drive, tur, &result); /* consume the Unit Attention */
  const uint8_t load[6] = {AP_EXB8200_CMD_LOAD_UNLOAD, 0, 0, 0, 0x01u, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION,
                         run(&drive, load, &result));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_NOT_READY, key(&drive));
  TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_U19_TNP,
                         drive.sense[19] & AP_EXB8200_U19_TNP);
}

/* §18.3's Table 18-1: exactly five legal test combinations. */
static void test_only_five_diagnostic_tests_are_legal(void) {
  ap_exb8200_t drive;
  ap_scsi_result_t result;
  for (unsigned test = 0; test < 8u; test++) {
    const bool legal = test == AP_EXB8200_TEST_COUNTERS ||
                       test == AP_EXB8200_TEST_POWER_ON ||
                       test == AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_NO_TAPE ||
                       test == AP_EXB8200_TEST_POWER_ON_TAPE ||
                       test == AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_TAPE;
    loaded(&drive, true);
    const uint8_t cdb[6] = {AP_EXB8200_CMD_SEND_DIAGNOSTIC, (uint8_t)test, 0, 0,
                            0, 0};
    const uint8_t status = run(&drive, cdb, &result);
    if (!legal) {
      TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_CHECK_CONDITION, status);
      TEST_ASSERT_EQUAL_HEX8(AP_EXB8200_KEY_ILLEGAL_REQUEST, key(&drive));
    }
  }
  /* The two with-tape tests run on a loaded drive and leave it at LBOT. */
  loaded(&drive, true);
  const uint8_t with_tape[6] = {AP_EXB8200_CMD_SEND_DIAGNOSTIC,
                                AP_EXB8200_TEST_POWER_ON_TAPE, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, with_tape, &result));
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_AT_LBOT, drive.where);
  TEST_ASSERT_TRUE(drive.diagnostic_ready);
}

/* §13.2: six bytes with an additional length of `0004h` and the three
 * counters. */
static void test_receive_diagnostic_returns_six_bytes(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  const uint8_t cdb[6] = {AP_EXB8200_CMD_RECEIVE_DIAGNOSTIC, 0, 0, 0,
                          AP_EXB8200_DIAGNOSTIC_BYTES, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, cdb, &result));
  TEST_ASSERT_EQUAL_UINT(6u, result.transferred);
  TEST_ASSERT_EQUAL_HEX8(0x00u, host[0]);
  TEST_ASSERT_EQUAL_HEX8(0x04u, host[1]);
}

/* ---- Capacity ------------------------------------------------------------- */

/* §23.5: a logical block that is not a multiple of the 1,024-byte physical
 * block wastes the remainder, so 1,536 bytes cost two physical blocks. */
static void test_a_short_logical_block_still_costs_a_whole_physical_one(void) {
  ap_exb8200_t drive;
  loaded(&drive, true);
  ap_scsi_result_t result;
  /* Variable mode: set the block length to 0 first. */
  memset(host, 0, sizeof host);
  host[2] = 0x10u;
  host[3] = AP_EXB8200_MODE_DESCRIPTOR;
  const uint8_t mode[6] = {AP_EXB8200_CMD_MODE_SELECT, 0, 0, 0, 0x0Cu, 0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, mode, &result));
  TEST_ASSERT_EQUAL_UINT32(0u, drive.block_length);

  const uint8_t write[6] = {AP_EXB8200_CMD_WRITE, 0x00u, 0x00u, 0x06u, 0x00u,
                            0};
  TEST_ASSERT_EQUAL_HEX8(AP_SCSI_STATUS_GOOD, run(&drive, write, &result));
  TEST_ASSERT_EQUAL_UINT32(1536u, drive.media.record[0].length);
  TEST_ASSERT_EQUAL_UINT32(AP_EXB8200_LBOT_BLOCKS + 2u,
                           ap_exb8200_physical_blocks(&drive));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_command_set_is_the_tables_own);
  RUN_TEST(test_an_unsupported_opcode_is_illegal_request);
  RUN_TEST(test_link_or_flag_set_is_illegal_request);
  RUN_TEST(test_inquiry_is_the_string_rmt_scsi_matches);
  RUN_TEST(test_inquiry_answers_a_bad_lun_with_device_type_7f);
  RUN_TEST(test_an_allocation_of_zero_transfers_nothing_and_is_not_an_error);
  RUN_TEST(test_request_sense_is_twenty_six_bytes_and_zero_means_four);
  RUN_TEST(test_the_sense_keys_are_table_fifteen_ones);
  RUN_TEST(test_sense_survives_request_sense_and_inquiry_only);
  RUN_TEST(test_the_first_command_after_power_on_is_unit_attention);
  RUN_TEST(test_no_cartridge_is_not_ready_with_tnp);
  RUN_TEST(test_the_button_leaves_unit_attention_and_the_command_not_ready);
  RUN_TEST(test_the_block_limits_are_the_manuals);
  RUN_TEST(test_a_fixed_write_and_read_round_trip);
  RUN_TEST(test_a_write_protected_cartridge_is_data_protect);
  RUN_TEST(test_the_modes_must_match);
  RUN_TEST(test_a_zero_transfer_length_is_not_an_error);
  RUN_TEST(test_reading_blank_tape_is_blank_check);
  RUN_TEST(test_a_filemark_ends_a_read_with_fmk);
  RUN_TEST(test_sili_in_fixed_mode_is_illegal_request);
  RUN_TEST(test_a_read_after_a_write_is_illegal_request);
  RUN_TEST(test_a_write_in_the_middle_of_data_is_illegal);
  RUN_TEST(test_both_filemark_kinds_and_a_count_of_zero);
  RUN_TEST(test_space_codes_two_and_three_are_refused);
  RUN_TEST(test_a_backward_space_past_lbot_stops_there);
  RUN_TEST(test_a_filemark_ends_a_block_space);
  RUN_TEST(test_mode_sense_returns_the_defaults);
  RUN_TEST(test_only_table_eight_ones_lengths_are_legal);
  RUN_TEST(test_an_invalid_buffered_mode_or_speed_is_refused);
  RUN_TEST(test_the_gap_threshold_saturates_and_the_others_are_bounded);
  RUN_TEST(test_erase_without_the_long_bit_does_nothing_and_succeeds);
  RUN_TEST(test_prevent_medium_removal_keeps_the_cartridge_in);
  RUN_TEST(test_loading_with_no_cartridge_is_not_ready);
  RUN_TEST(test_only_five_diagnostic_tests_are_legal);
  RUN_TEST(test_receive_diagnostic_returns_six_bytes);
  RUN_TEST(test_a_short_logical_block_still_costs_a_whole_physical_one);
  return UNITY_END();
}
