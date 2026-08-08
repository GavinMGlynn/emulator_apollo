/* Apollo cascaded interrupt controllers.
 *
 * The figures here are measured, not transcribed: the 8259A's initialization
 * words cannot be read back, so `tools/mame-oracle/writetrace.lua` watched the
 * boot PROM write them. `FINDINGS.md` C11 has the transcript. */

#include "unity.h"

#include "board/ap_intr.h"

void setUp(void) {}
void tearDown(void) {}

/* Exactly the sequence the Apollo boot PROM was observed to write, in order.
 * Reproduced rather than paraphrased, so that a change to this core's idea of
 * the machine has to disagree with a recorded measurement to pass. */
static void program_as_firmware_does(ap_intr_t *intr) {
  ap_intr_reset(intr);

  ap_intr_write(intr, AP_INTR_MASTER_ADDR + 0u, 0x11); /* ICW1 */
  ap_intr_write(intr, AP_INTR_MASTER_ADDR + 1u, 0xA0); /* ICW2: vector base */
  ap_intr_write(intr, AP_INTR_MASTER_ADDR + 1u, 0x08); /* ICW3: slave on IR3 */
  ap_intr_write(intr, AP_INTR_MASTER_ADDR + 1u, 0x01); /* ICW4: 8086 mode */
  ap_intr_write(intr, AP_INTR_MASTER_ADDR + 1u, 0xFF); /* OCW1: all masked */

  ap_intr_write(intr, AP_INTR_SLAVE_ADDR + 0u, 0x11);
  ap_intr_write(intr, AP_INTR_SLAVE_ADDR + 1u, 0xA8); /* ICW2 */
  ap_intr_write(intr, AP_INTR_SLAVE_ADDR + 1u, 0x03); /* ICW3: slave ID 3 */
  ap_intr_write(intr, AP_INTR_SLAVE_ADDR + 1u, 0x01); /* ICW4 */
  ap_intr_write(intr, AP_INTR_SLAVE_ADDR + 1u, 0xFF); /* OCW1 */
}

/* Firmware masks everything at the end of initialization; a test that wants an
 * interrupt has to unmask first, exactly as a driver would. */
static void unmask_all(ap_intr_t *intr) {
  ap_intr_write(intr, AP_INTR_MASTER_ADDR + 1u, 0x00);
  ap_intr_write(intr, AP_INTR_SLAVE_ADDR + 1u, 0x00);
}

static void raise(ap_intr_t *intr, unsigned irq) {
  ap_intr_set_request(intr, irq, false);
  ap_intr_set_request(intr, irq, true);
}

static void test_a_board_out_of_reset_has_neither_controller_programmed(void) {
  ap_intr_t intr;
  ap_intr_reset(&intr);

  /* The measured ICW values belong to the firmware, not to this core. A board
   * that came up pre-programmed would boot a machine whose PROM had not run. */
  raise(&intr, 6);
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));
}

static void test_the_firmware_sequence_leaves_everything_masked(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);

  /* The last thing the PROM writes to each part is OCW1 = FF. */
  TEST_ASSERT_EQUAL_HEX8(0xFF, ap_intr_read(&intr, AP_INTR_MASTER_ADDR + 1u));
  TEST_ASSERT_EQUAL_HEX8(0xFF, ap_intr_read(&intr, AP_INTR_SLAVE_ADDR + 1u));

  raise(&intr, 6);
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));
}

static void test_a_master_line_vectors_from_the_measured_base(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* ICW2 = A0, so master IR0 is vector A0 and the low three bits are the
   * level. `008778-03` §3.2's `T7 T6 T5 T4 T3` plus the level, and `[8259]`'s
   * 8086 vectoring, are the same byte. */
  raise(&intr, 5);
  TEST_ASSERT_TRUE(ap_intr_pending(&intr));
  TEST_ASSERT_EQUAL_HEX8(0xA5, ap_intr_acknowledge(&intr));
}

static void test_a_slave_line_vectors_from_the_slaves_own_base(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* ICW2 = A8 on the slave, so IRQ8 is A8 -- and the sixteen levels together
   * occupy A0-AF with no gap, which is what makes the two bases look chosen
   * rather than arbitrary. */
  raise(&intr, 8);
  TEST_ASSERT_TRUE(ap_intr_pending(&intr));
  TEST_ASSERT_EQUAL_HEX8(0xA8, ap_intr_acknowledge(&intr));

  /* And the last slave line. */
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x20); /* EOI master */
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 0u, 0x20);  /* EOI slave */
  raise(&intr, 15);
  TEST_ASSERT_EQUAL_HEX8(0xAF, ap_intr_acknowledge(&intr));
}

static void test_the_sixteen_levels_occupy_one_contiguous_vector_range(void) {
  /* Every line in turn, which is cheap here and is the check that no level
   * aliases another -- a cascade wired to the wrong line would collide two
   * vectors and this is what would catch it. */
  for (unsigned irq = 0; irq < AP_INTR_LINES; irq++) {
    if (irq == AP_INTR_CASCADE_LINE) {
      continue; /* carries the slave, not a device */
    }
    ap_intr_t intr;
    program_as_firmware_does(&intr);
    unmask_all(&intr);
    raise(&intr, irq);

    uint8_t expected = (uint8_t)(0xA0u + irq);
    TEST_ASSERT_EQUAL_HEX8(expected, ap_intr_acknowledge(&intr));
  }
}

static void test_the_slave_is_cascaded_on_line_three(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* The measurement this whole module turns on: master ICW3 = 08 and slave
   * ICW3 = 03, agreeing on IR3. Not IR2, which is the AT convention and is
   * wrong here.
   *
   * Observable rather than asserted from the constant: a slave request must
   * appear on the master's line 3, so it outranks IR4 and is outranked by
   * IR2. */
  raise(&intr, 9);  /* slave */
  raise(&intr, 4);  /* master, lower priority than the cascade on IR3 */
  TEST_ASSERT_EQUAL_HEX8(0xA9, ap_intr_acknowledge(&intr));
}

static void test_a_master_line_above_the_cascade_outranks_the_slave(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* The other side of the same fact. IR2 is above the cascade on IR3, so it
   * wins -- and this is exactly the case that distinguishes a cascade on IR3
   * from one on IR2, where IR2 *is* the cascade and could not compete with it. */
  raise(&intr, 9);
  raise(&intr, 2);
  TEST_ASSERT_EQUAL_HEX8(0xA2, ap_intr_acknowledge(&intr));
}

static void test_the_priority_order_is_table_two_threes(void) {
  ap_intr_t intr;

  /* `008778-03` Table 2-3, which with the cascade on IR3 is plain fixed
   * priority and carries no anomaly: IR0, IR1, IR2, the slave group, then IR4
   * through IR7. Checked as an ordering rather than line by line -- each
   * neighbouring pair raised together, the higher must win. */
  static const unsigned order[] = {0, 1, 2, 8, 9, 10, 11, 12, 13, 14, 15,
                                   4, 5, 6, 7};
  for (unsigned i = 0; i + 1 < sizeof order / sizeof order[0]; i++) {
    program_as_firmware_does(&intr);
    unmask_all(&intr);
    raise(&intr, order[i + 1]);
    raise(&intr, order[i]);

    uint8_t expected = (uint8_t)(0xA0u + order[i]);
    TEST_ASSERT_EQUAL_HEX8(expected, ap_intr_acknowledge(&intr));
  }
}

static void test_a_device_cannot_drive_the_cascade_line(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* IR3 carries the slave's output. Accepting a request on it would let a
   * caller forge an interrupt that appears to come from the second controller
   * and then acknowledge into a slave with nothing pending -- which would
   * produce the slave's spurious level 7 and look like a real device. */
  ap_intr_set_request(&intr, AP_INTR_CASCADE_LINE, true);
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));
}

static void test_the_cascade_follows_the_slave_dropping_its_request(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* Nothing in the 8259A drives the master's cascade input; the board does.
   * After the slave's only request is acknowledged its output falls, and if
   * the master's line stayed high the next acknowledge would go to a slave
   * with nothing to give. */
  raise(&intr, 12);
  TEST_ASSERT_EQUAL_HEX8(0xAC, ap_intr_acknowledge(&intr));

  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x20);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 0u, 0x20);
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));
}

/* ## Unmasking a slave line must reach the master on its own
 *
 * The board owns the cascade: the slave's INT output is an ordinary IR input on
 * the master and nothing in either part keeps them in step. So *every* path that
 * can change what the slave is asking for has to refresh it -- and the register
 * write path did not.
 *
 * A line masked in the slave, raised, then unmasked through `OCW1` changes the
 * slave's output at the moment of the unmask. Without a refresh there the
 * master's cascade input stayed low and the interrupt was invisible until some
 * unrelated device happened to toggle a line.
 *
 * It never showed on a booting machine because `ap_board_sample_interrupts`
 * re-drives every device's line on every instruction, dragging the cascade up
 * to date before anything could observe the gap. It surfaced from the other
 * end: skipping that redundant re-drive for speed changed the boot state hash,
 * and bisecting the divergence found this. The test raises the line and touches
 * nothing else, which is what the board's re-drive was accidentally providing. */
static void test_unmasking_a_slave_line_raises_the_cascade_by_itself(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);

  /* Mask everything on the slave, so raising the line asks for nothing yet. */
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0xFFu);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00u);
  raise(&intr, 12);
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));

  /* Now unmask it, and touch nothing else at all. */
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x00u);
  TEST_ASSERT_TRUE(ap_intr_pending(&intr));
  TEST_ASSERT_EQUAL_HEX8(0xAC, ap_intr_acknowledge(&intr));
}

static void test_a_cascaded_interrupt_owes_an_end_of_interrupt_to_each(void) {
  ap_intr_t intr;
  program_as_firmware_does(&intr);
  unmask_all(&intr);

  /* `[8259]`: "An EOI command must be issued twice if in the Cascade mode,
   * once for the master and once for the corresponding slave." So one EOI is
   * not enough, and the master's in-service bit on IR3 keeps blocking IR4. */
  raise(&intr, 10);
  (void)ap_intr_acknowledge(&intr);

  raise(&intr, 4);
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));

  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 0u, 0x20); /* slave only */
  TEST_ASSERT_FALSE(ap_intr_pending(&intr));

  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x20); /* and the master */
  TEST_ASSERT_TRUE(ap_intr_pending(&intr));
}

static void test_both_controllers_decode_at_their_documented_addresses(void) {
  bool is_slave;
  bool a0;

  TEST_ASSERT_TRUE(ap_intr_decode(0x011000u, &is_slave, &a0));
  TEST_ASSERT_FALSE(is_slave);
  TEST_ASSERT_FALSE(a0);

  /* ICW2 onward were observed one byte up, so A0 is address bit 0. */
  TEST_ASSERT_TRUE(ap_intr_decode(0x011001u, &is_slave, &a0));
  TEST_ASSERT_FALSE(is_slave);
  TEST_ASSERT_TRUE(a0);

  TEST_ASSERT_TRUE(ap_intr_decode(0x011100u, &is_slave, &a0));
  TEST_ASSERT_TRUE(is_slave);
  TEST_ASSERT_FALSE(a0);

  TEST_ASSERT_FALSE(ap_intr_decode(0x011200u, &is_slave, &a0)); /* node ID PROM */
}

static void test_the_controllers_drive_interrupt_level_six(void) {
  /* Measured, not transcribed: neither manual states it. A single write of the
   * CPU's mask, with the interval timer armed on IRQ0, is taken at mask 5 and
   * blocked at mask 6 -- so only level 6 fits, since mask 6 permits level 7
   * alone. `FINDINGS.md` C12.
   *
   * Asserted as a bare constant because that is all it is until something
   * consumes it; the value is the measurement, and a change to it has to
   * disagree with a recorded experiment. */
  TEST_ASSERT_EQUAL_UINT(6u, AP_INTR_CPU_LEVEL);

  /* And it is below the level 7 that `008778-03` §3.2 reserves for the parity
   * non-maskable interrupt -- "It generates a Level 7 interrupt to the CPU."
   * A controller at level 7 could not be masked apart from parity, which is
   * the sanity check on the measurement rather than a second source for it. */
  TEST_ASSERT_TRUE(AP_INTR_CPU_LEVEL < 7u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_controllers_drive_interrupt_level_six);
  RUN_TEST(test_a_board_out_of_reset_has_neither_controller_programmed);
  RUN_TEST(test_the_firmware_sequence_leaves_everything_masked);
  RUN_TEST(test_a_master_line_vectors_from_the_measured_base);
  RUN_TEST(test_a_slave_line_vectors_from_the_slaves_own_base);
  RUN_TEST(test_the_sixteen_levels_occupy_one_contiguous_vector_range);
  RUN_TEST(test_the_slave_is_cascaded_on_line_three);
  RUN_TEST(test_a_master_line_above_the_cascade_outranks_the_slave);
  RUN_TEST(test_the_priority_order_is_table_two_threes);
  RUN_TEST(test_a_device_cannot_drive_the_cascade_line);
  RUN_TEST(test_the_cascade_follows_the_slave_dropping_its_request);
  RUN_TEST(test_unmasking_a_slave_line_raises_the_cascade_by_itself);
  RUN_TEST(test_a_cascaded_interrupt_owes_an_end_of_interrupt_to_each);
  RUN_TEST(test_both_controllers_decode_at_their_documented_addresses);
  return UNITY_END();
}
