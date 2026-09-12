/* Host-side tests for open-time FDT tolerance selection. */

#include "goodix53x5-fdt.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
      if (!(cond))                                                             \
        {                                                                      \
          printf ("  FAIL: %s\n", msg);                                       \
          failures++;                                                          \
        }                                                                      \
      else                                                                     \
        printf ("  ok:   %s\n", msg);                                         \
  } while (0)

int
main (void)
{
  guint8 baseline[4] = { 0x00, 0x02, 0x00, 0x04 };
  guint8 noisy[4] = { 0x06, 0x02, 0x0a, 0x04 };

  CHECK (goodix_open_fdt_tolerance (TRUE, 0) ==
         GOODIX5381_OPEN_FDT_MIN_DELTA,
         "5381 fallback gets a nonzero open-time tolerance");
  CHECK (goodix_open_fdt_tolerance (TRUE, 2) ==
         GOODIX5381_OPEN_FDT_MIN_DELTA,
         "5381 values below the minimum are raised");
  CHECK (goodix_open_fdt_tolerance (TRUE, 7) == 7,
         "5381 values above the minimum remain unchanged");
  CHECK (goodix_open_fdt_tolerance (FALSE, 0) == 0,
         "other devices keep their OTP tolerance");
  CHECK (goodix_fdt_max_delta (baseline, noisy, sizeof (baseline)) == 5,
         "maximum FDT delta is reported in shifted sensor units");

  return failures != 0;
}
