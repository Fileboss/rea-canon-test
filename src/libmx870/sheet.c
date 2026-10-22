/*
 * Map a sheet raster (CUPS or PWG) onto the encoder's printable area.
 */

#include "mx870_private.h"

#include <stdlib.h>
#include <string.h>

/* Encoder coordinate e -> raster coordinate e * num / den + off. */
typedef struct {
  long num, den, off;
} axis_t;

/*
 * Bordered: the area sits at `origin` (encoder pixels) on the sheet.
 * Borderless: the area is larger than the sheet, so the sheet is stretched
 * over it instead of replicating edges into the overspray.
 */
static void make_axis(axis_t *a, int borderless, int res, int dpi, int area_px, int origin,
                      int sheet_px, int raster_origin)
{
  if (borderless) {
    a->num = sheet_px;
    a->den = area_px;
    a->off = -raster_origin;
  } else {
    a->num = res;
    a->den = dpi;
    a->off = (long)origin * res / dpi - raster_origin;
  }
}

static int map(const axis_t *a, int e, int limit)
{
  long v = (long)e * a->num / a->den + a->off;

  return v < 0 ? 0 : v >= limit ? limit - 1 : (int)v;
}

int mx870_page_write_raster(mx870_job_t *job, const mx870_raster_t *r, const uint8_t *const *rows)
{
  int ncomp = job->settings.color == MX870_COLOR_GRAY ? 1 : 3;
  int x0 = job->x0 * job->dpi / 600, y0 = job->y0 * job->dpi / 600;
  axis_t ax, ay;
  uint8_t *line;
  int rc = 0;

  if (!job->page_open || r->ncomp != ncomp || r->width <= 0 || r->height <= 0)
    return -1;
  make_axis(&ax, job->settings.borderless, r->res_x, job->dpi, job->width, x0, r->sheet_w, r->origin_x);
  make_axis(&ay, job->settings.borderless, r->res_y, job->dpi, job->height, y0, r->sheet_h, r->origin_y);

  if (!(line = malloc((size_t)job->width * (size_t)ncomp)))
    return -1;
  for (int y = 0; y < job->height && !rc; y++) {
    const uint8_t *src = rows[map(&ay, y, r->height)];

    for (int x = 0; x < job->width; x++)
      memcpy(line + x * ncomp, src + map(&ax, x, r->width) * ncomp, (size_t)ncomp);
    rc = mx870_page_write_line(job, line);
  }
  free(line);
  return rc;
}
