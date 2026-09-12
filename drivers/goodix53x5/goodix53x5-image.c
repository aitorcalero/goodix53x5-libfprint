/*
 * Goodix 53x5 driver for libfprint — Image decoding and preprocessing
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "goodix53x5-private.h"
#include "goodix53x5-image.h"

#include <stdlib.h>
#include <string.h>

/**
 * goodix_device_decode_image:
 *
 * Decode 12-bit packed image data to 16-bit pixel array.
 * 6 bytes → 4 pixels, matching tool.py decode_image().
 *
 * Returns newly allocated array of GOODIX_SENSOR_PIXELS guint16 values, or
 * NULL if the decrypted payload is too short for a full raw12 frame.
 */
guint16 *
goodix_device_decode_image (const guint8 *data,
                             gsize         data_len)
{
  guint16 *image;
  gsize pixel_idx = 0;

  if (data_len < GOODIX_SENSOR_RAW12_BYTES)
    {
      fp_warn ("Truncated raw12 image payload: %zu < %d",
               data_len, GOODIX_SENSOR_RAW12_BYTES);
      return NULL;
    }

  image = g_new0 (guint16, GOODIX_SENSOR_PIXELS);

  for (gsize i = 0; i + 5 < data_len && pixel_idx + 3 < GOODIX_SENSOR_PIXELS; i += 6)
    {
      /* 6 bytes → 4 pixels of 12 bits each.
       * Byte order matches tool.py decode_image() — NOT standard LE packed. */
      image[pixel_idx++] = ((data[i + 0] & 0x0F) << 8) | data[i + 1];
      image[pixel_idx++] = ((guint16) data[i + 3] << 4) | (data[i + 0] >> 4);
      image[pixel_idx++] = ((data[i + 5] & 0x0F) << 8) | data[i + 2];
      image[pixel_idx++] = ((guint16) data[i + 4] << 4) | (data[i + 5] >> 4);
    }

  return image;
}

/**
 * goodix_device_deinterleave_milan_f_5381:
 *
 * Milan F emits each four-sample unit as a 2x2 sensor block.  The packed
 * values are ordered TL, BL, BR, TR; the generic 53x5 decoder historically
 * treated all four as consecutive horizontal pixels.  Put the samples back
 * into their spatial positions in-place.
 */
void
goodix_device_deinterleave_milan_f_5381 (guint16 *image)
{
  g_autofree guint16 *linear = g_memdup2 (image,
                                           GOODIX_SENSOR_PIXELS * sizeof (*image));
  const gsize blocks_per_row = GOODIX_SENSOR_WIDTH / 2;

  for (gsize block = 0; block < GOODIX_SENSOR_PIXELS / 4; block++)
    {
      gsize block_y = block / blocks_per_row;
      gsize block_x = block % blocks_per_row;
      gsize top = (block_y * 2) * GOODIX_SENSOR_WIDTH + block_x * 2;
      gsize bottom = top + GOODIX_SENSOR_WIDTH;

      image[top] = linear[block * 4];
      image[top + 1] = linear[block * 4 + 3];
      image[bottom] = linear[block * 4 + 1];
      image[bottom + 1] = linear[block * 4 + 2];
    }
}

/**
 * goodix_device_canonicalize_milan_f_5381:
 *
 * A Milan F frame contains two adjacent 54x88 readings of the same sensing
 * area.  Leaving both copies in the image gives SIFT duplicate descriptors;
 * mutual matching can then pair equivalent points with opposite copies and
 * reject the otherwise consistent geometry.  Average the copies and expand
 * the canonical 54x88 reading horizontally to the driver's 108x88 image.
 */
void
goodix_device_canonicalize_milan_f_5381 (guint8 *image)
{
  guint8 row[GOODIX_SENSOR_WIDTH / 2];

  for (gsize y = 0; y < GOODIX_SENSOR_HEIGHT; y++)
    {
      guint8 *src = image + y * GOODIX_SENSOR_WIDTH;

      for (gsize x = 0; x < GOODIX_SENSOR_WIDTH / 2; x++)
        row[x] = ((guint16) src[x] + src[x + GOODIX_SENSOR_WIDTH / 2]) / 2;

      for (gsize x = 0; x < GOODIX_SENSOR_WIDTH / 2; x++)
        {
          src[x * 2] = row[x];
          src[x * 2 + 1] = row[x];
        }
    }
}

static int
compare_double (const void *a,
                const void *b)
{
  double left = *(const double *) a;
  double right = *(const double *) b;

  return (left > right) - (left < right);
}

/**
 * goodix_device_image_to_8bit:
 *
 * Convert 12-bit sensor image to 8-bit grayscale.
 *
 * Subtracts the TX-off no-finger reference frame, then normalizes using the
 * interior 3%..97% percentile range. SIGFM applies CLAHE before SIFT feature
 * extraction.
 *
 * Raw pixels at GOODIX_RAW12_CLIP are non-contact areas: the ADC clip
 * destroys the fixed sensor pattern there, so the reference subtraction
 * would leave the inverted reference grid behind. Clipped pixels carry no
 * finger signal, so they are excluded from the normalization sample and
 * filled with the unclipped interior's 99th-percentile residual, which maps
 * above the p97 ceiling and renders as flat white with no SIFT keypoints.
 * A frame with no unclipped interior pixels carries no finger signal at all
 * and is returned as a flat white image rather than normalizing the bare
 * reference grid.
 *
 * Returns newly allocated array of GOODIX_SENSOR_PIXELS guint8 values.
 */
guint8 *
goodix_device_image_to_8bit (const guint16 *img12,
                             const guint16 *calib_img)
{
  const int W = GOODIX_SENSOR_WIDTH;
  const int H = GOODIX_SENSOR_HEIGHT;
  const int N = GOODIX_SENSOR_PIXELS;

  guint8 *img8 = g_malloc (N);
  double *corrected = g_malloc (N * sizeof (double));
  double *sample = g_malloc ((W - 2) * (H - 2) * sizeof (double));
  int sample_count = 0;
  double corr_min = G_MAXDOUBLE;
  double corr_max = -G_MAXDOUBLE;

  for (int i = 0; i < N; i++)
    corrected[i] = calib_img != NULL ? img12[i] - calib_img[i] : img12[i];

  for (int r = 1; r < H - 1; r++)
    for (int c = 1; c < W - 1; c++)
      if (img12[r * W + c] < GOODIX_RAW12_CLIP)
        sample[sample_count++] = corrected[r * W + c];

  if (sample_count == 0)
    {
      memset (img8, 255, N);
      g_free (corrected);
      g_free (sample);
      return img8;
    }

  qsort (sample, sample_count, sizeof (double), compare_double);
  corr_min = sample[(int) (0.03 * (sample_count - 1))];
  corr_max = sample[(int) (0.97 * (sample_count - 1))];

  double white = sample[(int) (0.99 * (sample_count - 1))];

  for (int i = 0; i < N; i++)
    if (img12[i] >= GOODIX_RAW12_CLIP)
      corrected[i] = white;

  double range = corr_max - corr_min;
  if (range < 1.0)
    range = 1.0;

  for (int i = 0; i < N; i++)
    img8[i] = (guint8) CLAMP ((int) (((corrected[i] - corr_min) * 255.0) / range), 0, 255);

  g_free (corrected);
  g_free (sample);
  return img8;
}

/**
 * goodix_device_image_clipped_fraction:
 *
 * Fraction of raw12 pixels at ADC full scale, i.e. the non-contact area of
 * the frame. Used as an exact contact-coverage metric for enrollment gating.
 */
double
goodix_device_image_clipped_fraction (const guint16 *img12)
{
  int clipped = 0;

  for (int i = 0; i < GOODIX_SENSOR_PIXELS; i++)
    if (img12[i] >= GOODIX_RAW12_CLIP)
      clipped++;

  return (double) clipped / GOODIX_SENSOR_PIXELS;
}
/*
 * Goodix 53x5 driver for libfprint — Image decoding and preprocessing
 * Copyright (C) 2024 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "goodix53x5"

#include "drivers_api.h"
#include "goodix53x5-private.h"
#include "goodix53x5-image.h"

#include <stdlib.h>
#include <string.h>

/**
 * goodix_device_decode_image:
 *
 * Decode 12-bit packed image data to 16-bit pixel array.
 * 6 bytes → 4 pixels, matching tool.py decode_image().
 *
 * Returns newly allocated array of GOODIX_SENSOR_PIXELS guint16 values, or
 * NULL if the decrypted payload is too short for a full raw12 frame.
 */
guint16 *
goodix_device_decode_image (const guint8 *data,
                             gsize         data_len)
{
  guint16 *image;
  gsize pixel_idx = 0;

  if (data_len < GOODIX_SENSOR_RAW12_BYTES)
    {
      fp_warn ("Truncated raw12 image payload: %zu < %d",
               data_len, GOODIX_SENSOR_RAW12_BYTES);
      return NULL;
    }

  image = g_new0 (guint16, GOODIX_SENSOR_PIXELS);

  for (gsize i = 0; i + 5 < data_len && pixel_idx + 3 < GOODIX_SENSOR_PIXELS; i += 6)
    {
      /* 6 bytes → 4 pixels of 12 bits each.
       * Byte order matches tool.py decode_image() — NOT standard LE packed. */
      image[pixel_idx++] = ((data[i + 0] & 0x0F) << 8) | data[i + 1];
      image[pixel_idx++] = ((guint16) data[i + 3] << 4) | (data[i + 0] >> 4);
      image[pixel_idx++] = ((data[i + 5] & 0x0F) << 8) | data[i + 2];
      image[pixel_idx++] = ((guint16) data[i + 4] << 4) | (data[i + 5] >> 4);
    }

  return image;
}

static int
compare_double (const void *a,
                const void *b)
{
  double left = *(const double *) a;
  double right = *(const double *) b;

  return (left > right) - (left < right);
}

/**
 * goodix_device_image_to_8bit:
 *
 * Convert 12-bit sensor image to 8-bit grayscale.
 *
 * Subtracts the TX-off no-finger reference frame, then normalizes using the
 * interior 3%..97% percentile range. SIGFM applies CLAHE before SIFT feature
 * extraction.
 *
 * Raw pixels at GOODIX_RAW12_CLIP are non-contact areas: the ADC clip
 * destroys the fixed sensor pattern there, so the reference subtraction
 * would leave the inverted reference grid behind. Clipped pixels carry no
 * finger signal, so they are excluded from the normalization sample and
 * filled with the unclipped interior's 99th-percentile residual, which maps
 * above the p97 ceiling and renders as flat white with no SIFT keypoints.
 * A frame with no unclipped interior pixels carries no finger signal at all
 * and is returned as a flat white image rather than normalizing the bare
 * reference grid.
 *
 * Returns newly allocated array of GOODIX_SENSOR_PIXELS guint8 values.
 */
guint8 *
goodix_device_image_to_8bit (const guint16 *img12,
                             const guint16 *calib_img)
{
  const int W = GOODIX_SENSOR_WIDTH;
  const int H = GOODIX_SENSOR_HEIGHT;
  const int N = GOODIX_SENSOR_PIXELS;

  guint8 *img8 = g_malloc (N);
  double *corrected = g_malloc (N * sizeof (double));
  double *sample = g_malloc ((W - 2) * (H - 2) * sizeof (double));
  int sample_count = 0;
  double corr_min = G_MAXDOUBLE;
  double corr_max = -G_MAXDOUBLE;

  for (int i = 0; i < N; i++)
    corrected[i] = calib_img != NULL ? img12[i] - calib_img[i] : img12[i];

  for (int r = 1; r < H - 1; r++)
    for (int c = 1; c < W - 1; c++)
      if (img12[r * W + c] < GOODIX_RAW12_CLIP)
        sample[sample_count++] = corrected[r * W + c];

  if (sample_count == 0)
    {
      memset (img8, 255, N);
      g_free (corrected);
      g_free (sample);
      return img8;
    }

  qsort (sample, sample_count, sizeof (double), compare_double);
  corr_min = sample[(int) (0.03 * (sample_count - 1))];
  corr_max = sample[(int) (0.97 * (sample_count - 1))];

  double white = sample[(int) (0.99 * (sample_count - 1))];

  for (int i = 0; i < N; i++)
    if (img12[i] >= GOODIX_RAW12_CLIP)
      corrected[i] = white;

  double range = corr_max - corr_min;
  if (range < 1.0)
    range = 1.0;

  for (int i = 0; i < N; i++)
    img8[i] = (guint8) CLAMP ((int) (((corrected[i] - corr_min) * 255.0) / range), 0, 255);

  g_free (corrected);
  g_free (sample);
  return img8;
}

/**
 * goodix_device_image_clipped_fraction:
 *
 * Fraction of raw12 pixels at ADC full scale, i.e. the non-contact area of
 * the frame. Used as an exact contact-coverage metric for enrollment gating.
 */
double
goodix_device_image_clipped_fraction (const guint16 *img12)
{
  int clipped = 0;

  for (int i = 0; i < GOODIX_SENSOR_PIXELS; i++)
    if (img12[i] >= GOODIX_RAW12_CLIP)
      clipped++;

  return (double) clipped / GOODIX_SENSOR_PIXELS;
}
