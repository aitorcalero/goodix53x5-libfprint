/*
 * Goodix 53x5 driver for libfprint — FDT policy helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/* The 5381 OTP fallback reports delta_fdt == 0. Requiring two open-time
 * baseline reads to be bit-for-bit identical makes device opening depend on
 * normal sensor noise. Three is the smallest tolerance produced by the
 * non-fallback OTP formula (diff == 1), so use it only for open-time
 * stabilization. Finger-down/up detection keeps the original OTP value. */
#define GOODIX5381_OPEN_FDT_MIN_DELTA 3u
#define GOODIX5381_OPEN_FDT_MAX_RETRIES 10u
#define GOODIX5381_OPEN_FDT_RETRY_DELAY_MS 100u

static inline guint16
goodix_open_fdt_tolerance (gboolean experimental_5381,
                           guint16  otp_delta)
{
  if (experimental_5381 && otp_delta < GOODIX5381_OPEN_FDT_MIN_DELTA)
    return GOODIX5381_OPEN_FDT_MIN_DELTA;

  return otp_delta;
}

static inline guint16
goodix_fdt_max_delta (const guint8 *data1,
                      const guint8 *data2,
                      gsize         len)
{
  guint16 max_delta = 0;

  for (gsize i = 0; i < len; i += 2)
    {
      guint16 val1 = data1[i] | ((guint16) data1[i + 1] << 8);
      guint16 val2 = data2[i] | ((guint16) data2[i + 1] << 8);
      guint16 shifted1 = val1 >> 1;
      guint16 shifted2 = val2 >> 1;
      guint16 delta = shifted1 > shifted2 ? shifted1 - shifted2 :
                                            shifted2 - shifted1;

      max_delta = MAX (max_delta, delta);
    }

  return max_delta;
}
