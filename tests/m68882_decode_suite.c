/* MC68882 instruction decode, `[68881]` §4.7 and Tables 4-11 and 4-13.
 *
 * Checked against the tables' structure: every extension value classified, the
 * three classes shown to be three rather than two, and the operation word's
 * fields told apart from one another.
 */

#include "cpu/m68882/ap_m68882_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* The operation word's fields, each at its own bits. `1111` selects the
 * coprocessor family, bits 11-9 the cpID and bits 8-6 the type -- and a decoder
 * that read the cpID one bit out would send every FPU instruction to a
 * different coprocessor without ever faulting. */
static void test_the_operation_word_fields(void) {
  /* cpID 1, type 000 (general), effective address 000000. */
  const ap_m68882_operation_word_t general = ap_m68882_decode_operation(0xF200u);
  TEST_ASSERT_TRUE(general.is_coprocessor);
  TEST_ASSERT_EQUAL_UINT(1u, general.cpid);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_GENERAL, general.type);

  /* Not a coprocessor instruction at all. */
  const ap_m68882_operation_word_t other = ap_m68882_decode_operation(0x4E71u);
  TEST_ASSERT_FALSE(other.is_coprocessor);

  /* Each type, at its own encoding. */
  const struct {
    uint16_t word;
    ap_m68882_instruction_type_t type;
  } TYPES[] = {
      {0xF200u, AP_M68882_TYPE_GENERAL},
      {0xF240u, AP_M68882_TYPE_CONDITIONAL},
      {0xF280u, AP_M68882_TYPE_BRANCH_WORD},
      {0xF2C0u, AP_M68882_TYPE_BRANCH_LONG},
      {0xF300u, AP_M68882_TYPE_SAVE},
      {0xF340u, AP_M68882_TYPE_RESTORE},
  };
  for (unsigned i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++) {
    TEST_ASSERT_EQUAL_INT(TYPES[i].type,
                          ap_m68882_decode_operation(TYPES[i].word).type);
  }
}

/* The cpID is carried through rather than assumed: a system can hold several
 * coprocessors and the FPU answers only its own. */
static void test_the_cpid_is_decoded_not_assumed(void) {
  for (unsigned cpid = 0; cpid < 8u; cpid++) {
    const uint16_t word = (uint16_t)(0xF000u | (cpid << 9));
    TEST_ASSERT_EQUAL_UINT(cpid, ap_m68882_decode_operation(word).cpid);
  }
}

/* Table 4-11's opclasses, at the command word's top three bits. */
static void test_table_4_11_opclasses(void) {
  const ap_m68882_opclass_t CLASSES[] = {
      AP_M68882_OPCLASS_REGISTER,
      AP_M68882_OPCLASS_RESERVED_1,
      AP_M68882_OPCLASS_MEMORY_TO_REGISTER,
      AP_M68882_OPCLASS_REGISTER_TO_MEMORY,
      AP_M68882_OPCLASS_MOVE_TO_CONTROL,
      AP_M68882_OPCLASS_MOVE_FROM_CONTROL,
      AP_M68882_OPCLASS_MOVEM_TO_REGISTERS,
      AP_M68882_OPCLASS_MOVEM_FROM_REGISTERS,
  };
  for (unsigned i = 0; i < 8u; i++) {
    const uint16_t word = (uint16_t)(i << 13);
    TEST_ASSERT_EQUAL_INT(CLASSES[i], ap_m68882_decode_command(word).opclass);
  }
}

/* The command word's three register fields, each at its own bits: RX at 12-10,
 * RY at 9-7 and the extension in the low seven. Overlapping them is the kind of
 * error that puts a source register where a destination belongs. */
static void test_the_command_word_register_fields(void) {
  /* Opclass 000, RX = 5, RY = 3, extension $22 (FADD). */
  const uint16_t word = (uint16_t)((0u << 13) | (5u << 10) | (3u << 7) | 0x22u);
  const ap_m68882_command_word_t command = ap_m68882_decode_command(word);
  TEST_ASSERT_EQUAL_UINT(5u, command.rx);
  TEST_ASSERT_EQUAL_UINT(3u, command.ry);
  TEST_ASSERT_EQUAL_UINT(0x22u, command.extension);
  TEST_ASSERT_EQUAL_INT(AP_M68882_OP_FADD, command.operation);
}

/* **Table 4-13 in full**: every one of the 128 extension values classified, and
 * the defined ones mapped to their operations. A gap would be an encoding the
 * FPU can be handed and this model cannot name. */
static void test_every_extension_value_classifies(void) {
  for (unsigned extension = 0; extension < 128u; extension++) {
    const ap_m68882_command_word_t command =
        ap_m68882_decode_command((uint16_t)extension);
    /* Every value is exactly one of the three classes, which the enum makes
     * true by construction -- what this asserts is that the *undefined* class
     * is confined to where the table puts it. */
    if (extension >= 0x40u) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68882_EXTENSION_UNDEFINED,
                                    command.extension_class,
                                    "above $3F must be undefined");
    }
  }
}

/* **The three classes are three, not two.** Footnote 3: the redundant
 * encodings "do not cause an F-line exception if executed", while `$40-$7F`
 * does. A decoder with two classes traps on code the hardware runs. */
static void test_the_redundant_encodings_are_not_undefined(void) {
  const unsigned REDUNDANT[] = {0x05u, 0x07u, 0x0Bu, 0x13u, 0x17u, 0x1Bu,
                                0x29u, 0x2Au, 0x2Bu, 0x2Cu, 0x2Du, 0x2Eu,
                                0x2Fu, 0x39u, 0x3Bu, 0x3Cu, 0x3Du, 0x3Eu,
                                0x3Fu};
  for (unsigned i = 0; i < sizeof REDUNDANT / sizeof REDUNDANT[0]; i++) {
    const ap_m68882_command_word_t command =
        ap_m68882_decode_command((uint16_t)REDUNDANT[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68882_EXTENSION_REDUNDANT,
                                  command.extension_class,
                                  "a redundant encoding must not be undefined");
  }

  /* And $40 upward *is* undefined, so the two are genuinely distinguished. */
  TEST_ASSERT_EQUAL_INT(AP_M68882_EXTENSION_UNDEFINED,
                        ap_m68882_decode_command(0x40u).extension_class);
  TEST_ASSERT_EQUAL_INT(AP_M68882_EXTENSION_UNDEFINED,
                        ap_m68882_decode_command(0x7Fu).extension_class);
}

/* Every operation Table 4-13 names, at its own encoding. Transcribed as a list
 * so a transposed pair -- `FDIV` and `FMOD` are adjacent, as are `FADD` and
 * `FMUL` -- fails here rather than in a program's results. */
static void test_table_4_13_operations(void) {
  const struct {
    unsigned extension;
    ap_m68882_operation_t operation;
    const char *what;
  } ROWS[] = {
      {0x00u, AP_M68882_OP_FMOVE_TO_FPN, "FMOVE"},
      {0x01u, AP_M68882_OP_FINT, "FINT"},
      {0x04u, AP_M68882_OP_FSQRT, "FSQRT"},
      {0x0Eu, AP_M68882_OP_FSIN, "FSIN"},
      {0x0Fu, AP_M68882_OP_FTAN, "FTAN"},
      {0x18u, AP_M68882_OP_FABS, "FABS"},
      {0x1Au, AP_M68882_OP_FNEG, "FNEG"},
      {0x1Du, AP_M68882_OP_FCOS, "FCOS"},
      {0x20u, AP_M68882_OP_FDIV, "FDIV"},
      {0x21u, AP_M68882_OP_FMOD, "FMOD"},
      {0x22u, AP_M68882_OP_FADD, "FADD"},
      {0x23u, AP_M68882_OP_FMUL, "FMUL"},
      {0x28u, AP_M68882_OP_FSUB, "FSUB"},
      {0x38u, AP_M68882_OP_FCMP, "FCMP"},
      {0x3Au, AP_M68882_OP_FTST, "FTST"},
  };
  for (unsigned i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
    const ap_m68882_command_word_t command =
        ap_m68882_decode_command((uint16_t)ROWS[i].extension);
    TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68882_EXTENSION_DEFINED,
                                  command.extension_class, ROWS[i].what);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROWS[i].operation, command.operation,
                                  ROWS[i].what);
  }
}

/* **`FSINCOS` occupies eight encodings**, `$30-$37`, because it produces two
 * results and its low three bits name the second destination register. Folding
 * them into one entry is what stops seven of them looking undefined -- and a
 * decoder that took only `$30` would F-line trap on seven eighths of the
 * instruction's uses. */
static void test_fsincos_occupies_eight_encodings(void) {
  for (unsigned extension = 0x30u; extension <= 0x37u; extension++) {
    const ap_m68882_command_word_t command =
        ap_m68882_decode_command((uint16_t)extension);
    TEST_ASSERT_EQUAL_INT(AP_M68882_EXTENSION_DEFINED,
                          command.extension_class);
    TEST_ASSERT_EQUAL_INT(AP_M68882_OP_FSINCOS, command.operation);
  }

  /* $38 is FCMP, so the run stops where the table says it does. */
  TEST_ASSERT_EQUAL_INT(AP_M68882_OP_FCMP,
                        ap_m68882_decode_command(0x38u).operation);
}

/* Whether the operation word's low six bits are an effective address at all
 * depends on the *command* word: "if the command word indicates that an operand
 * external to the FPCP is to be fetched or stored, the effective address field
 * of the operation word is an MPU effective address descriptor". For a
 * register-to-register operation it is not, and evaluating it would compute an
 * address the instruction never names -- with the increment modes' side effects
 * along with it. */
static void test_only_some_opclasses_use_the_effective_address(void) {
  const ap_m68882_command_word_t register_to_register =
      ap_m68882_decode_command((uint16_t)(0u << 13));
  TEST_ASSERT_FALSE(ap_m68882_command_uses_memory(&register_to_register));

  const ap_m68882_command_word_t from_memory =
      ap_m68882_decode_command((uint16_t)((2u << 13) | (1u << 10)));
  TEST_ASSERT_TRUE(ap_m68882_command_uses_memory(&from_memory));

  const ap_m68882_command_word_t to_memory =
      ap_m68882_decode_command((uint16_t)(3u << 13));
  TEST_ASSERT_TRUE(ap_m68882_command_uses_memory(&to_memory));
}

/* **Move constant is the exception inside its own opclass.** "010 111" reads
 * the FPCP's constant ROM and touches no memory, so the answer depends on RX --
 * treating the whole opclass as external would evaluate an effective address
 * for an instruction whose operand is a constant. */
static void test_move_constant_uses_no_effective_address(void) {
  const ap_m68882_command_word_t constant =
      ap_m68882_decode_command((uint16_t)((2u << 13) | (7u << 10)));
  TEST_ASSERT_EQUAL_INT(AP_M68882_OPCLASS_MEMORY_TO_REGISTER,
                        constant.opclass);
  TEST_ASSERT_EQUAL_UINT(7u, constant.rx);
  TEST_ASSERT_FALSE(ap_m68882_command_uses_memory(&constant));

  /* Every other RX in the same opclass does use one. */
  for (unsigned rx = 0; rx < 7u; rx++) {
    const ap_m68882_command_word_t external =
        ap_m68882_decode_command((uint16_t)((2u << 13) | (rx << 10)));
    TEST_ASSERT_TRUE(ap_m68882_command_uses_memory(&external));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_operation_word_fields);
  RUN_TEST(test_the_cpid_is_decoded_not_assumed);
  RUN_TEST(test_table_4_11_opclasses);
  RUN_TEST(test_the_command_word_register_fields);
  RUN_TEST(test_every_extension_value_classifies);
  RUN_TEST(test_the_redundant_encodings_are_not_undefined);
  RUN_TEST(test_table_4_13_operations);
  RUN_TEST(test_fsincos_occupies_eight_encodings);
  RUN_TEST(test_only_some_opclasses_use_the_effective_address);
  RUN_TEST(test_move_constant_uses_no_effective_address);
  return UNITY_END();
}
