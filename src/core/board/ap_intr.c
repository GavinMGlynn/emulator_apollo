#include "board/ap_intr.h"

void ap_intr_reset(ap_intr_t *intr) {
  ap_i8259_init(&intr->master);
  ap_i8259_init(&intr->slave);
}

bool ap_intr_decode(uint32_t address, bool *is_slave, bool *a0) {
  uint32_t base = address & ~(AP_INTR_RANGE - 1u);
  if (base != AP_INTR_MASTER_ADDR && base != AP_INTR_SLAVE_ADDR) {
    return false;
  }
  *is_slave = base == AP_INTR_SLAVE_ADDR;
  /* A0 is address bit 0: the trace puts ICW1 on `011000` and every later word
   * on `011001`. */
  *a0 = (address & 1u) != 0u;
  return true;
}

uint8_t ap_intr_read(ap_intr_t *intr, uint32_t address) {
  bool is_slave;
  bool a0;
  if (!ap_intr_decode(address, &is_slave, &a0)) {
    return 0u;
  }
  return ap_i8259_read(is_slave ? &intr->slave : &intr->master, a0);
}

void ap_intr_write(ap_intr_t *intr, uint32_t address, uint8_t value) {
  bool is_slave;
  bool a0;
  if (!ap_intr_decode(address, &is_slave, &a0)) {
    return;
  }
  ap_i8259_write(is_slave ? &intr->slave : &intr->master, a0, value);
}

/* The slave's INT output is wired to the master's cascade line. Nothing in the
 * 8259A drives that for us: the master sees an ordinary IR input, and keeping
 * it in step is the board's job. Called after anything that could change the
 * slave's output. */
static void update_cascade(ap_intr_t *intr) {
  ap_i8259_set_request(&intr->master, AP_INTR_CASCADE_LINE,
                       ap_i8259_interrupt_pending(&intr->slave));
}

void ap_intr_set_request(ap_intr_t *intr, unsigned irq, bool asserted) {
  if (irq >= AP_INTR_LINES) {
    return;
  }
  if (irq < AP_I8259_LEVELS) {
    if (irq == AP_INTR_CASCADE_LINE) {
      /* The cascade line is not available to a device: it carries the slave's
       * output. Silently accepting a request here would let a caller forge
       * interrupts that appear to come from the second controller. */
      return;
    }
    ap_i8259_set_request(&intr->master, irq, asserted);
    return;
  }
  ap_i8259_set_request(&intr->slave, irq - AP_I8259_LEVELS, asserted);
  update_cascade(intr);
}

bool ap_intr_pending(const ap_intr_t *intr) {
  return ap_i8259_interrupt_pending(&intr->master);
}

uint8_t ap_intr_acknowledge(ap_intr_t *intr) {
  unsigned level = ap_i8259_acknowledge_first(&intr->master);

  if (level != AP_INTR_CASCADE_LINE) {
    return ap_i8259_acknowledge_second(&intr->master);
  }

  /* The master acknowledged the cascade, so the slave supplies the vector.
   * The master's own second cycle still happens -- it is what clears its
   * acknowledging state and would run its AEOI -- but its vector byte is
   * discarded in favour of the slave's, which is precisely what the CAS lines
   * do on the real part. */
  (void)ap_i8259_acknowledge_second(&intr->master);

  (void)ap_i8259_acknowledge_first(&intr->slave);
  uint8_t vector = ap_i8259_acknowledge_second(&intr->slave);

  /* The slave may have had only the one request, in which case its output has
   * just dropped and the master's cascade input must follow. */
  update_cascade(intr);
  return vector;
}
