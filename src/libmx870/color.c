/*
 * Color separation and halftoning.
 *
 * This is our own pipeline, not a reimplementation of Canon's: a simple
 * RGB -> CMYK separation with black generation, followed by serpentine
 * Floyd-Steinberg error diffusion to the number of levels each ink accepts.
 */

#include "mx870_private.h"

#include <string.h>

static int plane_index(const mx870_job_t *job, int slot)
{
  for (int i = 0; i < job->nplanes; i++)
    if (job->planes[i].slot == slot)
      return i;
  return -1;
}

void mx870_separate(const mx870_job_t *job, const uint8_t *line, int16_t *out)
{
  int w = job->width;
  int gray_in = job->settings.color == MX870_COLOR_GRAY;
  int gray_out = job->mode->model == 0x31;
  int ci = plane_index(job, 0), mi = plane_index(job, 1), yi = plane_index(job, 2);
  int ki = plane_index(job, MX870_SLOT_K), kdi = plane_index(job, MX870_SLOT_KDYE);
  /* Pigment black on plain paper; dye black on photo media. */
  int black = (ki >= 0 && !job->settings.media->photo) ? ki : kdi;

  memset(out, 0, (size_t)w * (size_t)job->nplanes * sizeof(int16_t));

  for (int x = 0; x < w; x++) {
    int r, g, b, c, m, y, k;

    if (gray_in) {
      r = g = b = line[x];
    } else {
      r = line[3 * x];
      g = line[3 * x + 1];
      b = line[3 * x + 2];
    }

    if (gray_out) {
      /* Grayscale jobs use black ink only. */
      int lum = (r * 77 + g * 150 + b * 29) >> 8;

      if (black >= 0)
        out[black * w + x] = (int16_t)(255 - lum);
      continue;
    }

    c = 255 - r;
    m = 255 - g;
    y = 255 - b;
    {
      int mn = c < m ? (c < y ? c : y) : (m < y ? m : y);
      int mx = c > m ? (c > y ? c : y) : (m > y ? m : y);
      int chroma = mx - mn;
      /*
       * Black generation must be continuous or gradients crossing gray show
       * seams: full GCR for neutrals (crisp black text), easing to a gentle
       * k = mn^2/255 that keeps colors saturated once chroma exceeds 48.
       */
      int neutral = chroma >= 48 ? 0 : 255 - chroma * 255 / 48;

      k = (mn * neutral + (mn * mn / 255) * (255 - neutral)) / 255;
    }
    c -= k;
    m -= k;
    y -= k;

    if (ci >= 0)
      out[ci * w + x] = (int16_t)c;
    if (mi >= 0)
      out[mi * w + x] = (int16_t)m;
    if (yi >= 0)
      out[yi * w + x] = (int16_t)y;
    if (black >= 0)
      out[black * w + x] = (int16_t)k;
  }
}

void mx870_dither_line(mx870_plane_t *plane, const int16_t *ink, int stride, int width,
                       int right_to_left, uint8_t *levels)
{
  int16_t *cur = plane->err[0], *next = plane->err[1];
  int top = plane->levels - 1;
  int dir = right_to_left ? -1 : 1;
  int x = right_to_left ? width - 1 : 0;

  memset(next, 0, ((size_t)width + 2) * sizeof(int16_t));

  for (int n = 0; n < width; n++, x += dir) {
    int v = ink[x * stride] + cur[x + 1];
    int q, e;

    if (v < -128)
      v = -128;
    else if (v > 383)
      v = 383;
    q = (v * top + 127) / 255;
    if (q < 0)
      q = 0;
    else if (q > top)
      q = top;
    levels[x] = (uint8_t)q;

    e = v - q * 255 / top;
    cur[x + 1 + dir] += (int16_t)(e * 7 / 16);
    next[x + 1 - dir] += (int16_t)(e * 3 / 16);
    next[x + 1] += (int16_t)(e * 5 / 16);
    next[x + 1 + dir] += (int16_t)(e / 16);
  }

  plane->err[0] = next;
  plane->err[1] = cur;
}
