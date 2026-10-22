/*
 * Line codecs: PackBits run-length compression and the 5-pixels-per-byte
 * base-3 packing used by three-level inks.
 */

#include "mx870_private.h"

/*
 * PackBits (TIFF/Apple): header n in 0..127 -> n+1 literal bytes follow;
 * header 1-n (n in 2..128, as a signed byte) -> next byte repeated n times.
 * dst must hold len + len/128 + 1 bytes.
 */
size_t mx870_packbits(uint8_t *dst, const uint8_t *src, size_t len)
{
  size_t out = 0, i = 0;

  while (i < len) {
    size_t run = 1;

    while (i + run < len && run < 128 && src[i + run] == src[i])
      run++;
    if (run >= 2) {
      dst[out++] = (uint8_t)(1 - (int)run);
      dst[out++] = src[i];
      i += run;
      continue;
    }

    /* Literal: extend until a run of 2 starts or 128 bytes are collected. */
    size_t start = i, count = 0;

    while (i < len && count < 128 && !(i + 1 < len && src[i] == src[i + 1])) {
      i++;
      count++;
    }
    if (!count) { /* a run starts here; handled next iteration */
      continue;
    }
    dst[out++] = (uint8_t)(count - 1);
    for (size_t k = 0; k < count; k++)
      dst[out++] = src[start + k];
  }
  return out;
}

/*
 * Pack three-level pixels (values 0..2), five per byte, most significant
 * first: byte = p0*81 + p1*27 + p2*9 + p3*3 + p4. Matches the 1024-entry
 * table of the Windows output module for all 243 valid inputs.
 */
size_t mx870_pack5(uint8_t *dst, const uint8_t *levels, size_t count)
{
  size_t out = 0;

  for (size_t i = 0; i < count; i += 5) {
    unsigned v = 0;

    for (size_t k = 0; k < 5; k++)
      v = v * 3 + (i + k < count ? levels[i + k] : 0);
    dst[out++] = (uint8_t)v;
  }
  return out;
}
