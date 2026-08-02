/* MC68882 §4.3.2's exponential family, measured against the published bound.
 *
 * The vectors below are `(argument, expected)` pairs computed to a hundred and
 * twenty decimal digits and rounded once to extended precision. They are not
 * this core's own output recorded back: an expectation generated from the
 * implementation would pass forever and prove nothing.
 *
 * One trap in generating them is worth recording, because it produced a
 * confident four-thousand-unit error that looked exactly like an implementation
 * fault. The expectation must be computed from the argument **after** it is
 * rounded to extended precision, not from the decimal literal it was written
 * as. A value like `7973.123456789012` does not fit in 64 bits; rounding it
 * moves the argument by half a unit in the last place, and for an exponential
 * that is a *relative* change of the same size in the answer -- some four
 * thousand units in the last place. Compute the expectation from the decimal
 * and the test is asking about a number the model is never given.
 *
 * The bound is `ap_m68882_accuracy.h`'s, which is §4.3.2's. Everything here is
 * asserted against the **typical** figure of 64 units in the last place rather
 * than the 4096-unit worst case, because the implementation is comfortably
 * inside it and a test pinned to the loose bound would not notice a regression
 * that mattered. */

#include "cpu/m68882/ap_m68882_transcendental.h"

#include "cpu/m68882/ap_m68882_accuracy.h"
#include "cpu/m68882/ap_m68882_format.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* One named type, so the four tables can share a sweep function -- each
 * anonymous struct would otherwise be its own incompatible type. */
typedef struct {
  ap_m68882_extended_t x, expected;
} vector_t;

static const vector_t etox_vectors[] = {
    {{false, 0x3FFE, 0x8000000000000000ULL}, {false, 0x3FFF, 0xD3094C70F034DE4CULL}},
    {{true, 0x3FFE, 0x8000000000000000ULL}, {false, 0x3FFE, 0x9B4597E37CB04FF4ULL}},
    {{false, 0x3FFF, 0x8000000000000000ULL}, {false, 0x4000, 0xADF85458A2BB4A9BULL}},
    {{true, 0x3FFF, 0x8000000000000000ULL}, {false, 0x3FFD, 0xBC5AB1B16779BE35ULL}},
    {{false, 0x4000, 0x8000000000000000ULL}, {false, 0x4001, 0xEC7325C6A6ED6E62ULL}},
    {{false, 0x4002, 0xA000000000000000ULL}, {false, 0x400D, 0xAC14EE7CA82AFCF8ULL}},
    {{true, 0x4002, 0xA000000000000000ULL}, {false, 0x3FF0, 0xBE6BCDAB23E4D4E3ULL}},
    {{false, 0x4005, 0xC800000000000000ULL}, {false, 0x408F, 0x9A4A54D8B8DFA566ULL}},
    {{true, 0x4005, 0xC800000000000000ULL}, {false, 0x3F6E, 0xD460F8A7157AE57AULL}},
    {{false, 0x4008, 0xAF00000000000000ULL}, {false, 0x43F0, 0xECA2EFA7C7647118ULL}},
    {{true, 0x4008, 0xAF00000000000000ULL}, {false, 0x3C0D, 0x8A79587DC983F856ULL}},
    {{false, 0x400C, 0xABE0000000000000ULL}, {false, 0x7DFC, 0xC838961A26B2ADDAULL}},
    {{true, 0x400C, 0xABE0000000000000ULL}, {false, 0x0201, 0xA3A8BC537A427C0FULL}},
    {{false, 0x3FF1, 0xD1B71758E219652CULL}, {false, 0x3FFF, 0x800346E71A426E71ULL}},
    {{true, 0x3FF1, 0xD1B71758E219652CULL}, {false, 0x3FFE, 0xFFF9725CBE98E819ULL}},
    {{false, 0x400B, 0xF92F57EFCC91A09FULL}, {false, 0x6CEE, 0xF430DC313BC593ACULL}},
    {{true, 0x4009, 0xE7D79E5975547299ULL}, {false, 0x358B, 0x90DD90EF5585BF4AULL}},
    {{true, 0x400C, 0xA21847FA198E88ADULL}, {false, 0x0588, 0xA6996DBBA9AC7E90ULL}},
    {{true, 0x400A, 0x96E32492DE33C539ULL}, {false, 0x3264, 0x8496001075A6CD10ULL}},
    {{true, 0x400A, 0xF62400D036F82E61ULL}, {false, 0x29CD, 0x9E3D63DEBD798822ULL}},
    {{true, 0x400B, 0xD89FCE2AD209CF41ULL}, {false, 0x18EE, 0x9AAC604021C609FCULL}},
    {{true, 0x4006, 0x8BAF90F62A282156ULL}, {false, 0x3F35, 0xB207D0C5AC1FFCE0ULL}},
    {{false, 0x400B, 0xD7A68125D1C42238ULL}, {false, 0x66E2, 0xDA170FBAB5533D81ULL}},
    {{true, 0x400B, 0xEB8CCB6D3BBB63DAULL}, {false, 0x1584, 0xBA793AAA4EB7492DULL}},
    {{false, 0x4009, 0xCAED47CB94C88293ULL}, {false, 0x4925, 0x8880CC5E1401A5FAULL}},
    {{false, 0x400A, 0xEC642D6EE2E8FFDAULL}, {false, 0x554F, 0xC8C24E1F9BEFDD70ULL}},
    {{true, 0x400C, 0x82A8C9DF6F44AED5ULL}, {false, 0x10DE, 0xEECC8D4359B73E83ULL}},
    {{true, 0x400C, 0x804BA1A82C6455D5ULL}, {false, 0x11B9, 0x8F6752D0277B7A1CULL}},
    {{true, 0x400B, 0xF6133911DB8DDFDFULL}, {false, 0x139E, 0xC731D4EC5E979DDFULL}},
    {{true, 0x400A, 0xE0450B7B4AC01CB7ULL}, {false, 0x2BC6, 0x8E8C5D717755583BULL}},
    {{true, 0x400B, 0xE1E261E5B0E8AE45ULL}, {false, 0x1742, 0xDA5DD4573BC98789ULL}},
    {{true, 0x400B, 0xB45028CF07F00037ULL}, {false, 0x1F7A, 0xC4D697292607668CULL}},
    {{false, 0x400B, 0x81A0DA8C64B9D452ULL}, {false, 0x575F, 0xAF3716CC85A36A0AULL}},
    {{false, 0x400A, 0xE77D3188E5F023BAULL}, {false, 0x54DE, 0xB3B0DCC779D024DCULL}},
    {{true, 0x400B, 0xB71381D92E6946FCULL}, {false, 0x1EFB, 0x856ECAB581679748ULL}},
    {{true, 0x4009, 0x95AF3D4284A6F974ULL}, {false, 0x393F, 0xA9B7762D9DFD4032ULL}},
    {{false, 0x400C, 0x8DDA7BA519107EC0ULL}, {false, 0x7328, 0xCD3C0B440E5C3688ULL}},
    {{true, 0x400A, 0xE0DE8E82AE0DE7DFULL}, {false, 0x2BB8, 0x9F0E284CD6CD2738ULL}},
    {{true, 0x400A, 0xF8EB412630D2E729ULL}, {false, 0x298D, 0x905CAD988D0E2435ULL}},
    {{true, 0x400B, 0xE41727C077AF8ED6ULL}, {false, 0x16DC, 0xF265BE3FF4C498DDULL}},
};

static const vector_t etoxm1_vectors[] = {
    {{false, 0x3FB9, 0x8000000000000000ULL}, {false, 0x3FB9, 0x8000000000000000ULL}},
    {{false, 0x3FC3, 0x8000000000000000ULL}, {false, 0x3FC3, 0x8000000000000004ULL}},
    {{false, 0x3FCD, 0x8000000000000000ULL}, {false, 0x3FCD, 0x8000000000001000ULL}},
    {{false, 0x3FD7, 0x8000000000000000ULL}, {false, 0x3FD7, 0x8000000000400000ULL}},
    {{false, 0x3FE1, 0x8000000000000000ULL}, {false, 0x3FE1, 0x8000000100000001ULL}},
    {{false, 0x3FEB, 0x8000000000000000ULL}, {false, 0x3FEB, 0x8000040000155556ULL}},
    {{false, 0x3FF5, 0x8000000000000000ULL}, {false, 0x3FF5, 0x801001556AABBBC7ULL}},
    {{false, 0x3FFB, 0x8000000000000000ULL}, {false, 0x3FFB, 0x8415ABBE9A76BEAEULL}},
    {{false, 0x3FFE, 0x8000000000000000ULL}, {false, 0x3FFE, 0xA61298E1E069BC97ULL}},
    {{true, 0x3FB9, 0x8000000000000000ULL}, {true, 0x3FB9, 0x8000000000000000ULL}},
    {{true, 0x3FCD, 0x8000000000000000ULL}, {true, 0x3FCC, 0xFFFFFFFFFFFFE000ULL}},
    {{true, 0x3FE1, 0x8000000000000000ULL}, {true, 0x3FE0, 0xFFFFFFFE00000003ULL}},
    {{true, 0x3FF5, 0x8000000000000000ULL}, {true, 0x3FF4, 0xFFE002AA8002220BULL}},
    {{true, 0x3FFD, 0x8000000000000000ULL}, {true, 0x3FFC, 0xE2820C2A6FBEA2F3ULL}},
    {{false, 0x3FFF, 0x8000000000000000ULL}, {false, 0x3FFF, 0xDBF0A8B145769535ULL}},
    {{true, 0x3FFF, 0x8000000000000000ULL}, {true, 0x3FFE, 0xA1D2A7274C4320E5ULL}},
    {{false, 0x4001, 0xA000000000000000ULL}, {false, 0x4006, 0x9369C4CB819C78FBULL}},
    {{true, 0x4001, 0xA000000000000000ULL}, {true, 0x3FFE, 0xFE466C01FF2AC89EULL}},
    {{false, 0x4003, 0xA000000000000000ULL}, {false, 0x401B, 0xE758445347402011ULL}},
    {{true, 0x4003, 0xA000000000000000ULL}, {true, 0x3FFE, 0xFFFFFFF725BCD506ULL}},
    {{true, 0x4002, 0x83041060CA73CA91ULL}, {true, 0x3FFE, 0xFFEDCABE8B4A106BULL}},
    {{true, 0x4001, 0xE71D96EE8FCABC29ULL}, {true, 0x3FFE, 0xFFD0275BBC8E3AA5ULL}},
    {{true, 0x4001, 0xA8C16A2F46033775ULL}, {true, 0x3FFE, 0xFEB01F845109FBA8ULL}},
    {{true, 0x4002, 0x8F57AB822CE26FE8ULL}, {true, 0x3FFE, 0xFFF792AA76F4E441ULL}},
    {{true, 0x4002, 0x9A5DEE5159590749ULL}, {true, 0x3FFE, 0xFFFBC4E1936093F0ULL}},
    {{true, 0x4003, 0x84E5DFBCC822DF42ULL}, {true, 0x3FFE, 0xFFFFFEF9F7CEF943ULL}},
    {{false, 0x4001, 0xA21E7F7FF3ACFC5AULL}, {false, 0x4006, 0x9D9301B7850CA3A5ULL}},
    {{true, 0x3FFE, 0xC1824653450BDE4EULL}, {true, 0x3FFE, 0x87C8E9849E84B391ULL}},
    {{true, 0x4000, 0xBDEB1120801DAAD8ULL}, {true, 0x3FFE, 0xF2D546E8B51E84FEULL}},
    {{false, 0x4002, 0xF79C1657FEDD9A2CULL}, {false, 0x4015, 0xA08454C3B2CB1F7CULL}},
    {{false, 0x4002, 0xB2DA58A07D416D4EULL}, {false, 0x400F, 0x8BC40F41CBD40DB8ULL}},
    {{true, 0x3FFF, 0x8B4E8252B860F03DULL}, {true, 0x3FFE, 0xA9C8F68B514C2F3DULL}},
    {{true, 0x4003, 0x852E7703381B9C70ULL}, {true, 0x3FFE, 0xFFFFFF0317CEAFCFULL}},
    {{false, 0x4001, 0xB208C541371963BCULL}, {false, 0x4007, 0x81E048DB7708F51CULL}},
    {{false, 0x4003, 0xA44FC7CA150E27CFULL}, {false, 0x401C, 0xC64990B47945AFABULL}},
};

static const vector_t twotox_vectors[] = {
    {{false, 0x3FFF, 0x8000000000000000ULL}, {false, 0x4000, 0x8000000000000000ULL}},
    {{false, 0x4000, 0x8000000000000000ULL}, {false, 0x4001, 0x8000000000000000ULL}},
    {{false, 0x4000, 0xC000000000000000ULL}, {false, 0x4002, 0x8000000000000000ULL}},
    {{false, 0x4002, 0xA000000000000000ULL}, {false, 0x4009, 0x8000000000000000ULL}},
    {{true, 0x4002, 0xA000000000000000ULL}, {false, 0x3FF5, 0x8000000000000000ULL}},
    {{false, 0x3FFE, 0x8000000000000000ULL}, {false, 0x3FFF, 0xB504F333F9DE6484ULL}},
    {{true, 0x3FFE, 0x8000000000000000ULL}, {false, 0x3FFE, 0xB504F333F9DE6484ULL}},
    {{false, 0x400C, 0xFA00000000000000ULL}, {false, 0x7E7F, 0x8000000000000000ULL}},
    {{true, 0x400C, 0xFA00000000000000ULL}, {false, 0x017F, 0x8000000000000000ULL}},
    {{false, 0x3FF1, 0xD1B71758E219652CULL}, {false, 0x3FFF, 0x8002457961EDD281ULL}},
    {{true, 0x4008, 0x9E776FBD0FA27132ULL}, {false, 0x3D85, 0x8C70838039AEA48AULL}},
    {{false, 0x400C, 0xD59E9978484F4FAAULL}, {false, 0x7566, 0xC8D5F94E4DEE2EAFULL}},
    {{false, 0x400C, 0xC320A949967FC1C4ULL}, {false, 0x70C7, 0x8F8A839DBD18E20BULL}},
    {{false, 0x400B, 0xE79120BE1FD01BE3ULL}, {false, 0x5CF1, 0x8D23F412D2278B68ULL}},
    {{false, 0x400C, 0xA68710BC746ED100ULL}, {false, 0x69A0, 0xD9B8DA146DD2D006ULL}},
    {{false, 0x400B, 0xA906DA4D5C005F83ULL}, {false, 0x551F, 0xE7C6E1B90C3209E3ULL}},
    {{true, 0x4009, 0x88D256140ECA0220ULL}, {false, 0x3BB8, 0xAC1636A627E0564EULL}},
    {{true, 0x400C, 0x8A2185F2DFB8DE61ULL}, {false, 0x1D76, 0xC49C155B992A8B8BULL}},
    {{true, 0x400C, 0x8C8D2B9182B48034ULL}, {false, 0x1CDB, 0xD10378B01FC57C23ULL}},
    {{false, 0x400A, 0xF5B88BC1A0B9FD01ULL}, {false, 0x4F5A, 0xB959FDCB36C01F42ULL}},
    {{true, 0x400A, 0xAD6C1A48E503F7C8ULL}, {false, 0x3528, 0x978AFE133850C188ULL}},
    {{false, 0x400C, 0xBAB4D7F5A969CE29ULL}, {false, 0x6EAC, 0x9425F5A731A82FE8ULL}},
    {{true, 0x400C, 0x8DDDE9208913BF30ULL}, {false, 0x1C87, 0xB7D80434CDD542DFULL}},
    {{true, 0x400C, 0x8046BD32EF224FC0ULL}, {false, 0x1FED, 0x9F426CE66B5005EEULL}},
    {{false, 0x400B, 0xDA1FDD18E05E9E52ULL}, {false, 0x5B42, 0xFCFE629AC4397CBBULL}},
    {{false, 0x4008, 0xEA38DD55290BE706ULL}, {false, 0x43A7, 0xECF62713003B20F3ULL}},
    {{true, 0x400C, 0xD3B82DB05D606F60ULL}, {false, 0x0B10, 0xF834320ED3353926ULL}},
    {{true, 0x400B, 0xC2004DBC7F7A4BE5ULL}, {false, 0x27BE, 0xF95A3CEE9DFA2C3FULL}},
    {{false, 0x400C, 0x924712B27561DF0DULL}, {false, 0x6490, 0xDA02E26EBF04DEE9ULL}},
    {{false, 0x4009, 0x8C6619527C6D0FD0ULL}, {false, 0x4462, 0x9213DD25BA045AC5ULL}},
    {{true, 0x400C, 0xCB174136B15D49F1ULL}, {false, 0x0D39, 0x91A52ED73C68D17FULL}},
    {{true, 0x400C, 0xFA4C4C4602E4E7F1ULL}, {false, 0x016B, 0xF31E435891B0E7CBULL}},
    {{true, 0x400C, 0xF2BAB03E4103976DULL}, {false, 0x0350, 0xA0A98BBA53794F47ULL}},
    {{false, 0x4009, 0xE078A9CED36073D1ULL}, {false, 0x4702, 0xDA6280F60C6D2892ULL}},
    {{false, 0x400A, 0xC59F7C268D4BBB5FULL}, {false, 0x4C58, 0xFA59EF8E6D60EE3CULL}},
};

static const vector_t tentox_vectors[] = {
    {{false, 0x3FFF, 0x8000000000000000ULL}, {false, 0x4002, 0xA000000000000000ULL}},
    {{false, 0x4000, 0x8000000000000000ULL}, {false, 0x4005, 0xC800000000000000ULL}},
    {{false, 0x4000, 0xC000000000000000ULL}, {false, 0x4008, 0xFA00000000000000ULL}},
    {{true, 0x3FFF, 0x8000000000000000ULL}, {false, 0x3FFB, 0xCCCCCCCCCCCCCCCDULL}},
    {{true, 0x4000, 0x8000000000000000ULL}, {false, 0x3FF8, 0xA3D70A3D70A3D70AULL}},
    {{false, 0x3FFE, 0x8000000000000000ULL}, {false, 0x4000, 0xCA62C1D6D2DA9490ULL}},
    {{false, 0x400B, 0x9920000000000000ULL}, {false, 0x7F94, 0xAE9204275937A4C1ULL}},
    {{true, 0x400B, 0x9920000000000000ULL}, {false, 0x0069, 0xBBB4DF56BAF62972ULL}},
    {{false, 0x3FF1, 0xD1B71758E219652CULL}, {false, 0x3FFF, 0x80078BC5510CBAE8ULL}},
    {{false, 0x4008, 0xBD84A3B18776CD79ULL}, {false, 0x49D5, 0x9985C07CA2EDB86DULL}},
    {{false, 0x400A, 0xB39E468706E10660ULL}, {false, 0x6549, 0xE8DB7C33B9EDCF85ULL}},
    {{true, 0x400A, 0x979971D6822D87B5ULL}, {false, 0x2085, 0xA4AC4DF7B77B064CULL}},
    {{false, 0x400A, 0xC1E66BF1851176D1ULL}, {false, 0x6840, 0xF800EDA0A52410DEULL}},
    {{true, 0x4006, 0x84F3F505EE817D6EULL}, {false, 0x3E45, 0xA1FF92C49DBC9B41ULL}},
    {{true, 0x400A, 0xAD30F95608CFFA2AULL}, {false, 0x1C09, 0xD50C3A7F0EADCFD7ULL}},
    {{true, 0x4008, 0xBD56873E397935BAULL}, {false, 0x362B, 0x8C2D1F96F11DB00DULL}},
    {{true, 0x400A, 0x9CA204EA00D67887ULL}, {false, 0x1F79, 0xE361883359B03A8CULL}},
    {{false, 0x400A, 0xB9667C9F0A9DA0C1ULL}, {false, 0x667D, 0x9190B65D53F880F1ULL}},
    {{false, 0x400A, 0xCA6BFB6A656FD0F7ULL}, {false, 0x6A05, 0xED5ABD51B5198C7EULL}},
    {{false, 0x400A, 0xB98BC6E934D48FD5ULL}, {false, 0x6684, 0xF37DFDE2C81FFF4BULL}},
    {{false, 0x400B, 0x94A2023C2A04139FULL}, {false, 0x7DB6, 0xF2E26E3C3613F933ULL}},
    {{false, 0x4004, 0xC2434D2FAEB80123ULL}, {false, 0x40A0, 0xA11A7AA4F0803931ULL}},
    {{true, 0x4009, 0x9441FA1CBCBCE142ULL}, {false, 0x309A, 0xFDE33DF850E2887FULL}},
    {{true, 0x400A, 0xEE08D323E8137CBBULL}, {false, 0x0E93, 0x99DFEBC8B1982052ULL}},
    {{true, 0x4006, 0x98185F1BE3432D68ULL}, {false, 0x3E05, 0xD75EE33B58FCDD51ULL}},
    {{false, 0x400B, 0x8BB24346AEDDBF2AULL}, {false, 0x7A00, 0xF8B0023DA2019047ULL}},
    {{true, 0x400A, 0xA9E6D7D432A37161ULL}, {false, 0x1CB8, 0xBF2A3879FD6043B4ULL}},
    {{true, 0x4009, 0x9A5DAC886A173DA0ULL}, {false, 0x2FF8, 0xCA49D65D6725578DULL}},
    {{true, 0x4009, 0xF91FF9CCCD2F0009ULL}, {false, 0x2622, 0xA8E045AC87FE5985ULL}},
    {{false, 0x4007, 0x9D704D29210AD1D8ULL}, {false, 0x4414, 0xFFFCE00C22785662ULL}},
    {{false, 0x4009, 0xED01596C1C0DA790ULL}, {false, 0x5899, 0xB7014B4B0AF5E7BCULL}},
    {{false, 0x400A, 0xDDD98D12D2FAD97FULL}, {false, 0x6E0E, 0xB5BFD5A9B25C4C70ULL}},
    {{true, 0x400B, 0x91585D049060EC92ULL}, {false, 0x03A4, 0xBCE8DDF0E9D394ABULL}},
    {{false, 0x400A, 0xFBB7DBED4B61991DULL}, {false, 0x7442, 0x833F5CF3E55D44ECULL}},
};

/* Distance between two extended values in units of the last place of the
 * expected one. Both are finite and of the same sign in every vector, so the
 * mantissas can be compared directly once the exponents are aligned. */
static uint64_t ulp_distance(ap_m68882_extended_t got,
                             ap_m68882_extended_t expected) {
  if (got.sign != expected.sign) {
    return UINT64_MAX;
  }
  const int de = (int)got.exponent - (int)expected.exponent;
  if (de > 1 || de < -1) {
    return UINT64_MAX;
  }
  /* Bring both to the expected value's exponent. A one-step difference is the
   * most a correct result can be away, and shifting the larger down loses only
   * bits that are already beyond the comparison. */
  uint64_t a = got.mantissa, b = expected.mantissa;
  if (de == 1) {
    a = (a >> 1) | (a & 1u);
    if (a < b) return b - a;
    return a - b;
  }
  if (de == -1) {
    b = (b >> 1) | (b & 1u);
    if (a < b) return b - a;
    return a - b;
  }
  return a > b ? a - b : b - a;
}

typedef ap_m68882_op_t (*unary_t)(const ap_m68882_extended_t *,
                                  ap_m68882_rounding_t,
                                  ap_m68882_precision_t);

static uint64_t sweep(unary_t f, const vector_t *v, size_t n) {
  uint64_t worst = 0;
  for (size_t i = 0; i < n; i++) {
    const ap_m68882_op_t got =
        f(&v[i].x, AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
    const uint64_t d = ulp_distance(got.value, v[i].expected);
    if (d > worst) worst = d;
  }
  return worst;
}

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void test_the_exponential_is_inside_the_typical_error_bound(void) {
  /* `e^x` over the whole representable range, from `2^-14` to the overflow
   * boundary near 11356. */
  const uint64_t worst = sweep(ap_m68882_etox, etox_vectors, COUNT(etox_vectors));
  TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
      AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED, worst,
      "FETOX left the typical error bound");
}

static void test_the_other_three_exponentials_share_the_bound(void) {
  TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
      AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED,
      sweep(ap_m68882_etoxm1, etoxm1_vectors, COUNT(etoxm1_vectors)),
      "FETOXM1 left the typical error bound");
  TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
      AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED,
      sweep(ap_m68882_twotox, twotox_vectors, COUNT(twotox_vectors)),
      "FTWOTOX left the typical error bound");
  TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
      AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED,
      sweep(ap_m68882_tentox, tentox_vectors, COUNT(tentox_vectors)),
      "FTENTOX left the typical error bound");
}

static void test_the_family_is_far_inside_the_worst_case_bound(void) {
  /* The margin, stated rather than implied. Every vector lands within a couple
   * of units in the last place, which is three orders of magnitude inside
   * §4.3.2's worst case and comfortably inside its typical figure -- so the
   * accuracy claim does not depend on the vectors happening to be kind. */
  uint64_t worst = sweep(ap_m68882_etox, etox_vectors, COUNT(etox_vectors));
  uint64_t d;
  if ((d = sweep(ap_m68882_etoxm1, etoxm1_vectors, COUNT(etoxm1_vectors))) > worst) worst = d;
  if ((d = sweep(ap_m68882_twotox, twotox_vectors, COUNT(twotox_vectors))) > worst) worst = d;
  if ((d = sweep(ap_m68882_tentox, tentox_vectors, COUNT(tentox_vectors))) > worst) worst = d;
  TEST_ASSERT_LESS_OR_EQUAL_UINT64(
      AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_EXTENDED, worst);
  TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
      4u, worst, "the family should be within a few units in the last place");
}

static void test_etoxm1_keeps_the_bits_that_a_subtraction_would_lose(void) {
  /* The instruction's whole reason for existing. For `x = 2^-70`, `e^x - 1` is
   * about `2^-70` -- but computing it as `(1 + 2^-70) - 1` gives *zero*,
   * because `1 + 2^-70` rounds back to 1 in extended precision. A model that
   * implemented `FETOXM1` as `FETOX` minus one would return zero here and be
   * wrong by the entire result.
   *
   * The relative accuracy is what is checked, not the absolute: an answer of
   * zero is within `2^-70` of the truth in absolute terms and useless. */
  ap_m68882_extended_t tiny = {false, AP_M68882_BIAS_EXTENDED - 70,
                               0x8000000000000000ULL};
  const ap_m68882_op_t got =
      ap_m68882_etoxm1(&tiny, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0u, got.value.mantissa,
                                       "FETOXM1 collapsed a small argument");
  /* `e^x - 1` is `x + x^2/2 + ...`, so for `x = 2^-70` the answer rounds to
   * exactly `x`: same exponent, same mantissa. */
  TEST_ASSERT_EQUAL_UINT(tiny.exponent, got.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(tiny.mantissa, got.value.mantissa);
  TEST_ASSERT_FALSE(got.value.sign);

  /* And the plain exponential does round to one there, which is what makes the
   * separate instruction necessary rather than merely convenient. */
  const ap_m68882_op_t plain =
      ap_m68882_etox(&tiny, AP_M68882_ROUND_NEAREST,
                     AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED, plain.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0x8000000000000000ULL, plain.value.mantissa);
}

static void test_a_zero_argument_is_the_one_exact_case(void) {
  /* "the exponential functions check for a zero input value" -- §4.3.2 names
   * this as the single special case the part does test for, and it is the only
   * argument for which any of the four returns an exact result. */
  const ap_m68882_extended_t zero = {false, 0u, 0u};
  const ap_m68882_extended_t minus_zero = {true, 0u, 0u};
  const unary_t to_one[] = {ap_m68882_etox, ap_m68882_twotox,
                            ap_m68882_tentox};
  for (unsigned i = 0; i < 3; i++) {
    for (unsigned s = 0; s < 2; s++) {
      const ap_m68882_op_t got =
          to_one[i](s ? &minus_zero : &zero, AP_M68882_ROUND_NEAREST,
                    AP_M68882_PRECISION_EXTENDED);
      TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED, got.value.exponent);
      TEST_ASSERT_EQUAL_UINT64(0x8000000000000000ULL, got.value.mantissa);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, got.exceptions,
                                     "an exact result raised an exception");
    }
  }
  /* `e^0 - 1` is zero, and keeps the argument's sign: `-0` in, `-0` out. */
  const ap_m68882_op_t m1 =
      ap_m68882_etoxm1(&minus_zero, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO, ap_m68882_classify(&m1.value));
  TEST_ASSERT_TRUE(m1.value.sign);
}

static void test_the_infinities_have_values_rather_than_being_errors(void) {
  /* Table 6-2 lists no operand error for any of these, so both infinities have
   * defined results: `e^+inf` is an infinity and `e^-inf` is zero. Trapping
   * either would fault where the hardware computes. */
  const ap_m68882_extended_t plus = {false, 0x7FFFu, 0u};
  const ap_m68882_extended_t minus = {true, 0x7FFFu, 0u};
  const unary_t all[] = {ap_m68882_etox, ap_m68882_twotox, ap_m68882_tentox};
  for (unsigned i = 0; i < 3; i++) {
    ap_m68882_op_t up = all[i](&plus, AP_M68882_ROUND_NEAREST,
                               AP_M68882_PRECISION_EXTENDED);
    TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                          ap_m68882_classify(&up.value));
    TEST_ASSERT_FALSE(up.value.sign);
    ap_m68882_op_t down = all[i](&minus, AP_M68882_ROUND_NEAREST,
                                 AP_M68882_PRECISION_EXTENDED);
    TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO,
                          ap_m68882_classify(&down.value));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        0u, down.exceptions & (1u << AP_M68882_EXC_OPERR),
        "a defined infinity case raised an operand error");
  }
  /* `e^-inf - 1` is `-1`, which is the one place the family's minus-infinity
   * results differ from each other. */
  const ap_m68882_op_t m1 =
      ap_m68882_etoxm1(&minus, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_TRUE(m1.value.sign);
  TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED, m1.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0x8000000000000000ULL, m1.value.mantissa);
}

static void test_a_nan_propagates_and_a_signalling_one_is_reported(void) {
  ap_m68882_extended_t quiet = {false, 0x7FFFu, 0xC000000000000000ULL};
  ap_m68882_extended_t signalling = {false, 0x7FFFu, 0x2000000000000000ULL};
  const unary_t all[] = {ap_m68882_etox, ap_m68882_etoxm1, ap_m68882_twotox,
                         ap_m68882_tentox};
  for (unsigned i = 0; i < 4; i++) {
    ap_m68882_op_t q = all[i](&quiet, AP_M68882_ROUND_NEAREST,
                              AP_M68882_PRECISION_EXTENDED);
    TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&q.value));
    TEST_ASSERT_EQUAL_UINT(0u, q.exceptions & (1u << AP_M68882_EXC_SNAN));

    ap_m68882_op_t s = all[i](&signalling, AP_M68882_ROUND_NEAREST,
                              AP_M68882_PRECISION_EXTENDED);
    TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_NAN, ap_m68882_classify(&s.value));
    TEST_ASSERT_NOT_EQUAL_UINT(0u, s.exceptions & (1u << AP_M68882_EXC_SNAN));
    /* "a signalling NAN raises once and comes out quiet", so the result must
     * not itself be signalling -- otherwise every later operation on it raises
     * the exception again. */
    TEST_ASSERT_FALSE(ap_m68882_is_signalling_nan(&s.value));
  }
}

static void test_ten_to_the_one_is_exactly_ten_and_the_part_disagrees(void) {
  /* The recorded divergence, asserted so it cannot drift unnoticed.
   *
   * §4.3.2: "the exponential functions ... do not check for exact integer
   * values. Thus, raising a number to an exact integer value may not produce an
   * exact result (e.g., the instruction FTENTOX #1,FP0 does not produce an
   * extended precision value of exactly 10.0)."
   *
   * This model *does* produce exactly 10.0. That is more correct than the part
   * and therefore a difference from it -- the one place where conforming to
   * §4.3.2's bound is visible in a value a program reads back rather than
   * hidden in the last bits. It is not closable without the algorithm Motorola
   * did not publish, and it is asserted here rather than merely written down so
   * that the divergence is a fact of the test suite and not a note in a
   * document. */
  const ap_m68882_extended_t one = {false, AP_M68882_BIAS_EXTENDED,
                                    0x8000000000000000ULL};
  const ap_m68882_op_t got =
      ap_m68882_tentox(&one, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED);
  /* 10.0 is 1.25 * 2^3. */
  TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED + 3, got.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0xA000000000000000ULL, got.value.mantissa);
  TEST_ASSERT_FALSE(got.value.sign);

  /* The same holds for the other two at their own bases, which the manual does
   * not name but which follow from the same argument reduction. */
  const ap_m68882_op_t two =
      ap_m68882_twotox(&one, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_UINT(AP_M68882_BIAS_EXTENDED + 1, two.value.exponent);
  TEST_ASSERT_EQUAL_UINT64(0x8000000000000000ULL, two.value.mantissa);
}

static void test_an_overflowing_argument_reports_rather_than_wraps(void) {
  /* `e^20000` is beyond the extended exponent range. The result must be an
   * infinity with overflow raised, not a wrapped exponent -- which is what an
   * unguarded scaling by `2^n` would produce, and which would be a plausible
   * finite number rather than a visible failure. */
  const ap_m68882_extended_t big = {false, AP_M68882_BIAS_EXTENDED + 14,
                                    0x9C40000000000000ULL};
  const ap_m68882_op_t over =
      ap_m68882_etox(&big, AP_M68882_ROUND_NEAREST,
                     AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_INFINITY,
                        ap_m68882_classify(&over.value));
  TEST_ASSERT_NOT_EQUAL_UINT(0u, over.exceptions & (1u << AP_M68882_EXC_OVFL));

  ap_m68882_extended_t small = big;
  small.sign = true;
  const ap_m68882_op_t under =
      ap_m68882_etox(&small, AP_M68882_ROUND_NEAREST,
                     AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_INT(AP_M68882_TYPE_ZERO, ap_m68882_classify(&under.value));
  TEST_ASSERT_NOT_EQUAL_UINT(0u,
                             under.exceptions & (1u << AP_M68882_EXC_UNFL));
}

static void test_the_result_precision_is_the_callers_and_the_steps_are_not(void) {
  /* The FPCR's precision applies to the *result*. Rounding every intermediate
   * to single precision as well would be double rounding on a grand scale and
   * would make a single-precision `FETOX` materially worse than an extended
   * one -- so a single-precision result must be the correctly rounded form of
   * the extended one, differing from it only in the bits single precision
   * cannot hold. */
  const ap_m68882_extended_t x = {false, AP_M68882_BIAS_EXTENDED + 1,
                                  0xC000000000000000ULL}; /* 3.0 */
  const ap_m68882_op_t wide =
      ap_m68882_etox(&x, AP_M68882_ROUND_NEAREST,
                     AP_M68882_PRECISION_EXTENDED);
  const ap_m68882_op_t narrow =
      ap_m68882_etox(&x, AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_SINGLE);
  TEST_ASSERT_EQUAL_UINT(wide.value.exponent, narrow.value.exponent);
  /* Single precision keeps 24 mantissa bits; the rest must be clear. */
  TEST_ASSERT_EQUAL_UINT64(0u, narrow.value.mantissa & 0x000000FFFFFFFFFFULL);
  TEST_ASSERT_LESS_OR_EQUAL_UINT64(1ULL << 40,
                                   ulp_distance(narrow.value, wide.value));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_exponential_is_inside_the_typical_error_bound);
  RUN_TEST(test_the_other_three_exponentials_share_the_bound);
  RUN_TEST(test_the_family_is_far_inside_the_worst_case_bound);
  RUN_TEST(test_etoxm1_keeps_the_bits_that_a_subtraction_would_lose);
  RUN_TEST(test_a_zero_argument_is_the_one_exact_case);
  RUN_TEST(test_the_infinities_have_values_rather_than_being_errors);
  RUN_TEST(test_a_nan_propagates_and_a_signalling_one_is_reported);
  RUN_TEST(test_ten_to_the_one_is_exactly_ten_and_the_part_disagrees);
  RUN_TEST(test_an_overflowing_argument_reports_rather_than_wraps);
  RUN_TEST(test_the_result_precision_is_the_callers_and_the_steps_are_not);
  return UNITY_END();
}
