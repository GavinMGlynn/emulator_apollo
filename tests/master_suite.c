/* How an I/O adapter reaches the arbiter: `008778-03` §2.4.7.
 *
 * Every test here is a clause of one paragraph, quoted in `board/ap_master.h`.
 * The arbitration that follows is `arbiter_suite`'s; what is checked here is the
 * route in, and the two things that make it a route rather than a request line:
 * the channel's mode, and MASTER.L.
 */

#include "unity.h"

#include "board/ap_arbiter.h"
#include "board/ap_master.h"
#include "device/ap_i8237.h"

void setUp(void) {}
void tearDown(void) {}

/* The adapter's channel and the arbiter line it appears on. Both arbitrary --
 * this board's DMA cascade wiring has not been measured and no module here
 * claims it -- so they are deliberately *not* the AT's conventional pairing. */
#define CHANNEL 2u
#define DRQ 3u

typedef struct {
  ap_i8237_t dma;
  ap_arbiter_t arbiter;
  ap_master_t port;
} rig_t;

/* Program a channel's mode. `[8237]` Figure 6: register 11 write, bits 1-0
 * selecting the channel and bits 7-6 the mode. */
static void set_mode(rig_t *r, unsigned channel, unsigned select) {
  ap_i8237_write(&r->dma, AP_I8237_REG_MODE,
                 (uint8_t)((select << 6) | channel));
}

/* Unmask a channel: register 10, bit 2 clear to enable. */
static void unmask(rig_t *r, unsigned channel) {
  ap_i8237_write(&r->dma, AP_I8237_REG_MASK_SINGLE, (uint8_t)channel);
}

static void build(rig_t *r, unsigned mode_select) {
  ap_i8237_reset(&r->dma);
  ap_arbiter_reset(&r->arbiter);
  ap_master_init(&r->port, 0u, CHANNEL, DRQ);
  set_mode(r, CHANNEL, mode_select);
  unmask(r, CHANNEL);
}

/* One bus clock of the whole rig. The port does not tick the arbiter -- the
 * board owns that clock -- so this is what the board would do. */
static void tick(rig_t *r, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    ap_master_tick(&r->port, &r->dma, &r->arbiter);
    ap_arbiter_tick(&r->arbiter);
  }
}

/* The whole sequence, in order: DRQ, then DACK, then MASTER.L, then the bus. */
static void test_an_adapter_takes_the_bus_through_cascade_then_master_l(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);

  TEST_ASSERT_EQUAL_INT(AP_MASTER_IDLE, ap_master_state(&r.port));

  /* "asserting its DMA Request signal (DRQx) to a DMA channel". */
  ap_master_set_request(&r.port, true);
  tick(&r, 1);
  TEST_ASSERT_EQUAL_INT(AP_MASTER_REQUESTING, ap_master_state(&r.port));
  TEST_ASSERT_FALSE(ap_master_acknowledged(&r.port));

  /* "the system board asserts DACKx.L" -- which it can only do once the
   * arbitration has been won, so this takes clocks rather than none. */
  tick(&r, 16);
  TEST_ASSERT_EQUAL_INT(AP_MASTER_ACKNOWLEDGED, ap_master_state(&r.port));
  TEST_ASSERT_TRUE(ap_master_acknowledged(&r.port));

  /* Acknowledged is not owned: the adapter has been offered the bus and has
   * not yet taken it. */
  TEST_ASSERT_FALSE(ap_master_owns_bus(&r.port));

  /* "and then asserting the MASTER.L signal after its DMA Acknowledge is
   * received". */
  ap_master_set_master_l(&r.port, true);
  tick(&r, 1);
  TEST_ASSERT_EQUAL_INT(AP_MASTER_OWNS, ap_master_state(&r.port));
  TEST_ASSERT_TRUE(ap_master_owns_bus(&r.port));

  /* "At this point, the system processor relinquishes ownership of the bus." */
  TEST_ASSERT_FALSE(ap_arbiter_processor_may_run(&r.arbiter));
  TEST_ASSERT_EQUAL_INT((int)DRQ, ap_arbiter_master(&r.arbiter));
}

/* "a DMA channel that has been programmed in cascade mode". The same DRQ on a
 * channel in any other mode wins the same arbitration and does not give the
 * adapter the bus: it is an ordinary DMA request, and what happens next is the
 * controller's transfer rather than the card's ownership. */
static void test_a_channel_not_in_cascade_mode_never_yields_the_bus(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_SINGLE);

  ap_master_set_request(&r.port, true);
  ap_master_set_master_l(&r.port, true);
  tick(&r, 64);

  /* The request still reached the arbiter -- the channel is asking for
   * service -- so this is not a request that went nowhere. */
  TEST_ASSERT_TRUE(ap_master_acknowledged(&r.port));
  TEST_ASSERT_EQUAL_INT(AP_MASTER_ACKNOWLEDGED, ap_master_state(&r.port));
  TEST_ASSERT_FALSE(ap_master_owns_bus(&r.port));
}

/* "Programming a DMA channel into cascade mode prevents the DMA controllers
 * from driving the address and control bus, making the bus available to the bus
 * Master." The converse is the whole reason the mode matters. */
static void test_cascade_mode_is_what_stands_the_controllers_down(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);
  TEST_ASSERT_FALSE(ap_master_controllers_may_drive(&r.port, &r.dma));

  build(&r, AP_I8237_MODE_BLOCK);
  TEST_ASSERT_TRUE(ap_master_controllers_may_drive(&r.port, &r.dma));
}

/* MASTER.L alone is not a route to the bus. A card that asserted it without
 * having been acknowledged would be driving a bus the processor still owns. */
static void test_master_l_without_an_acknowledgement_takes_nothing(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);

  ap_master_set_master_l(&r.port, true);
  tick(&r, 64);

  TEST_ASSERT_EQUAL_INT(AP_MASTER_IDLE, ap_master_state(&r.port));
  TEST_ASSERT_FALSE(ap_master_owns_bus(&r.port));
  TEST_ASSERT_TRUE(ap_arbiter_processor_may_run(&r.arbiter));
}

/* The manual states an order -- request, acknowledge, MASTER.L -- and says
 * nothing about what the hardware does with the other one. So ownership needs
 * both signals and does not care which arrived first, which is the weaker claim
 * and the only one §2.4.7 supports. */
static void test_master_l_asserted_early_takes_the_bus_when_dack_arrives(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);

  ap_master_set_request(&r.port, true);
  ap_master_set_master_l(&r.port, true);
  tick(&r, 64);

  TEST_ASSERT_EQUAL_INT(AP_MASTER_OWNS, ap_master_state(&r.port));
}

/* "until it releases the DRQx and MASTER.L signals" -- both, which is the
 * clause that stops the bus being handed back underneath a card still driving
 * it. Dropping DRQ alone is what a transfer's end looks like, and it must not
 * end the mastership. */
static void test_releasing_drq_alone_does_not_give_the_bus_back(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);
  ap_master_set_request(&r.port, true);
  tick(&r, 16);
  ap_master_set_master_l(&r.port, true);
  tick(&r, 1);
  TEST_ASSERT_TRUE(ap_master_owns_bus(&r.port));

  ap_master_set_request(&r.port, false);
  tick(&r, 64);
  TEST_ASSERT_TRUE(ap_master_owns_bus(&r.port));
  TEST_ASSERT_FALSE(ap_arbiter_processor_may_run(&r.arbiter));
  TEST_ASSERT_EQUAL_INT((int)DRQ, ap_arbiter_master(&r.arbiter));

  /* And releasing the second signal does. */
  ap_master_set_master_l(&r.port, false);
  tick(&r, 64);
  TEST_ASSERT_EQUAL_INT(AP_MASTER_IDLE, ap_master_state(&r.port));
  TEST_ASSERT_EQUAL_INT(AP_ARBITER_PROCESSOR, ap_arbiter_master(&r.arbiter));
  TEST_ASSERT_TRUE(ap_arbiter_processor_may_run(&r.arbiter));
}

/* "The MASTER.L signal prevents assertion of the AEN signal, allowing the bus
 * Master to comunicate with the I/O devices." AEN asserted tells an I/O card
 * that the address belongs to a DMA controller; a bus master that could not
 * suppress it could not address a card at all. */
static void test_master_l_inhibits_aen(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);
  TEST_ASSERT_FALSE(ap_master_aen_inhibited(&r.port));

  ap_master_set_master_l(&r.port, true);
  TEST_ASSERT_TRUE(ap_master_aen_inhibited(&r.port));

  ap_master_set_master_l(&r.port, false);
  TEST_ASSERT_FALSE(ap_master_aen_inhibited(&r.port));
}

/* The route is through the *part*, not past it: a masked channel is not asking
 * for service, so an adapter pulling its DRQ line reaches no arbiter at all.
 * Software has closed the route by masking, which is a fact about the 8237's
 * priority encoder rather than anything this module decides. */
static void test_a_masked_channel_closes_the_route(void) {
  rig_t r;
  build(&r, AP_I8237_MODE_CASCADE);
  /* Register 10, bit 2 set: mask this channel. */
  ap_i8237_write(&r.dma, AP_I8237_REG_MASK_SINGLE, (uint8_t)(0x04u | CHANNEL));

  ap_master_set_request(&r.port, true);
  ap_master_set_master_l(&r.port, true);
  tick(&r, 64);

  TEST_ASSERT_EQUAL_INT(AP_MASTER_REQUESTING, ap_master_state(&r.port));
  TEST_ASSERT_FALSE(ap_master_acknowledged(&r.port));
  TEST_ASSERT_TRUE(ap_arbiter_processor_may_run(&r.arbiter));

  /* Unmasking it opens the route with nothing else changing. */
  unmask(&r, CHANNEL);
  tick(&r, 64);
  TEST_ASSERT_EQUAL_INT(AP_MASTER_OWNS, ap_master_state(&r.port));
}

/* Two adapters requesting through the *same* controller are ordered by the
 * controller's channel priority, and the arbiter never sees the loser at all.
 *
 * This is the shape of the hardware and it is not the shape it first looks. A
 * controller has one request output, so its own priority encoder resolves its
 * four channels before anything downstream is asked -- which means an adapter
 * on a low-numbered arbiter line loses to one on a low-numbered *channel*, and
 * the AT bus's "DRQO having the highest priority" order is what the cascaded
 * pair of controllers implements rather than a separate encoder above them.
 *
 * Written the other way round first, expecting the arbiter's line order to
 * decide, and the model disagreed: `ap_i8237_service_pending` had already
 * picked, and one of the two adapters was pulling a DRQ line nothing was
 * listening to. The model was right. */
static void test_two_adapters_on_one_controller_are_ordered_by_channel(void) {
  ap_i8237_t dma;
  ap_arbiter_t arbiter;
  ap_i8237_reset(&dma);
  ap_arbiter_reset(&arbiter);

  /* Two cascade channels on one controller, on two arbiter lines chosen so the
   * two orders disagree: the winning channel is on the *lower*-priority line. */
  ap_i8237_write(&dma, AP_I8237_REG_MODE, (uint8_t)((3u << 6) | 1u));
  ap_i8237_write(&dma, AP_I8237_REG_MODE, (uint8_t)((3u << 6) | 3u));
  ap_i8237_write(&dma, AP_I8237_REG_MASK_SINGLE, 1u);
  ap_i8237_write(&dma, AP_I8237_REG_MASK_SINGLE, 3u);

  ap_master_t early; /* channel 1, on DRQ 5 */
  ap_master_t late;  /* channel 3, on DRQ 1 */
  ap_master_init(&early, 0u, 1u, 5u);
  ap_master_init(&late, 0u, 3u, 1u);

  ap_master_set_request(&early, true);
  ap_master_set_request(&late, true);
  for (unsigned i = 0; i < 64u; i++) {
    ap_master_tick(&early, &dma, &arbiter);
    ap_master_tick(&late, &dma, &arbiter);
    ap_arbiter_tick(&arbiter);
  }

  /* Channel 1 beats channel 3 inside the controller, so the bus went to that
   * adapter's line -- 5 -- even though 1 outranks it at the arbiter. */
  TEST_ASSERT_EQUAL_INT(5, ap_arbiter_master(&arbiter));
  TEST_ASSERT_TRUE(ap_master_acknowledged(&early));
  TEST_ASSERT_FALSE(ap_master_acknowledged(&late));
  TEST_ASSERT_EQUAL_INT(AP_MASTER_REQUESTING, ap_master_state(&late));
}

/* Two adapters on *different* controllers do reach the arbiter together, and
 * there the line order decides: "DRQO having the highest priority and DRQ7
 * having the lowest". The two encoders are in series, which is what a cascade
 * is; this is the second of them. */
static void test_two_adapters_on_two_controllers_are_ordered_by_line(void) {
  ap_i8237_t first;
  ap_i8237_t second;
  ap_arbiter_t arbiter;
  ap_i8237_reset(&first);
  ap_i8237_reset(&second);
  ap_arbiter_reset(&arbiter);

  ap_i8237_write(&first, AP_I8237_REG_MODE, (uint8_t)((3u << 6) | 1u));
  ap_i8237_write(&first, AP_I8237_REG_MASK_SINGLE, 1u);
  ap_i8237_write(&second, AP_I8237_REG_MODE, (uint8_t)((3u << 6) | 1u));
  ap_i8237_write(&second, AP_I8237_REG_MASK_SINGLE, 1u);

  ap_master_t low;  /* controller 0, channel 1, on DRQ 6 */
  ap_master_t high; /* controller 1, channel 1, on DRQ 2 */
  ap_master_init(&low, 0u, 1u, 6u);
  ap_master_init(&high, 1u, 1u, 2u);

  ap_master_set_request(&low, true);
  ap_master_set_request(&high, true);
  for (unsigned i = 0; i < 64u; i++) {
    ap_master_tick(&low, &first, &arbiter);
    ap_master_tick(&high, &second, &arbiter);
    ap_arbiter_tick(&arbiter);
  }

  TEST_ASSERT_EQUAL_INT(2, ap_arbiter_master(&arbiter));
  TEST_ASSERT_TRUE(ap_master_acknowledged(&high));
  TEST_ASSERT_FALSE(ap_master_acknowledged(&low));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_adapter_takes_the_bus_through_cascade_then_master_l);
  RUN_TEST(test_a_channel_not_in_cascade_mode_never_yields_the_bus);
  RUN_TEST(test_cascade_mode_is_what_stands_the_controllers_down);
  RUN_TEST(test_master_l_without_an_acknowledgement_takes_nothing);
  RUN_TEST(test_master_l_asserted_early_takes_the_bus_when_dack_arrives);
  RUN_TEST(test_releasing_drq_alone_does_not_give_the_bus_back);
  RUN_TEST(test_master_l_inhibits_aen);
  RUN_TEST(test_a_masked_channel_closes_the_route);
  RUN_TEST(test_two_adapters_on_one_controller_are_ordered_by_channel);
  RUN_TEST(test_two_adapters_on_two_controllers_are_ordered_by_line);
  return UNITY_END();
}
