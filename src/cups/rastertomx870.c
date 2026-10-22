/*
 * rastertomx870 - CUPS filter: CUPS raster -> Canon MX870 BJ raster stream.
 *
 * Usage (as invoked by CUPS): rastertomx870 job user title copies options [file]
 *
 * Settings come from the page header, set by the PPD's setpagedevice code:
 *   MediaType        libmx870 media keyword (e.g. "stationery")
 *   cupsInteger0     quality: 1 draft, 2 standard, 3 high
 *   MediaPosition    0 auto, 1 rear tray, 2 cassette
 *   Duplex, Tumble   two-sided printing
 *   cupsInteger1     1 = borderless (also detected from a ".Fullbleed" PageSize name)
 *   cupsColorSpace   CUPS_CSPACE_RGB / SRGB or CUPS_CSPACE_SW (8 bits)
 */

#include "../libmx870/mx870.h"

#include <cups/cups.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t canceled;

static void cancel_job(int sig)
{
  (void)sig;
  canceled = 1;
}

static int write_stdout(void *ctx, const void *buf, size_t len)
{
  (void)ctx;
  return fwrite(buf, 1, len, stdout) == len ? 0 : -1;
}

static const mx870_paper_t *header_paper(const cups_page_header2_t *h)
{
  /* PageSize is in points; papers are in 1/600 inch. */
  const mx870_paper_t *p = mx870_find_paper_by_size((int)h->PageSize[0] * 600 / 72,
                                                    (int)h->PageSize[1] * 600 / 72);

  if (!p)
    fprintf(stderr, "ERROR: Unsupported page size %ux%u points\n", h->PageSize[0], h->PageSize[1]);
  return p;
}

static int setup_page(const cups_page_header2_t *h, mx870_page_settings_t *s)
{
  memset(s, 0, sizeof(*s));
  if (!(s->paper = header_paper(h)))
    return -1;
  s->media = mx870_find_media(h->MediaType[0] ? h->MediaType : "stationery");
  if (!s->media) {
    fprintf(stderr, "WARNING: Unknown media type \"%s\", using plain paper\n", h->MediaType);
    s->media = mx870_find_media("stationery");
  }

  switch (h->cupsInteger[0]) {
  case 1: s->quality = MX870_QUALITY_DRAFT; break;
  case 3: s->quality = MX870_QUALITY_HIGH; break;
  default: s->quality = h->HWResolution[0] < 600 ? MX870_QUALITY_DRAFT : MX870_QUALITY_STANDARD;
  }

  s->color = h->cupsColorSpace == CUPS_CSPACE_SW || h->cupsColorSpace == CUPS_CSPACE_W ||
             h->cupsColorSpace == CUPS_CSPACE_K
             ? MX870_COLOR_GRAY : MX870_COLOR_RGB;
  s->source = h->MediaPosition == 1 ? MX870_SOURCE_REAR
            : h->MediaPosition == 2 ? MX870_SOURCE_CASSETTE : MX870_SOURCE_AUTO;
  s->duplex = !h->Duplex ? MX870_DUPLEX_NONE
            : h->Tumble ? MX870_DUPLEX_SHORT_EDGE : MX870_DUPLEX_LONG_EDGE;
  s->borderless = h->cupsInteger[1] == 1 || strstr(h->cupsPageSizeName, ".Fullbleed") != NULL;
  return 0;
}

/*
 * Where the raster's top-left pixel sits on the sheet, in raster pixels.
 * macOS cgpdftoraster renders exactly the PPD ImageableArea, other RIPs the
 * whole sheet; ImagingBoundingBox is only whole points, so prefer exact
 * matches of the encoder area or the sheet before falling back to it.
 */
static void raster_origin(const cups_page_header2_t *h, int width, int height, int dpi,
                          int x0, int y0, int *rx, int *ry)
{
  int resx = (int)h->HWResolution[0], resy = (int)h->HWResolution[1];
  int sheet_w = (int)h->PageSize[0] * resx / 72, sheet_h = (int)h->PageSize[1] * resy / 72;

  if (abs((int)h->cupsWidth - width * resx / dpi) <= 2 &&
      abs((int)h->cupsHeight - height * resy / dpi) <= 2) {
    *rx = x0 * resx / dpi;
    *ry = y0 * resy / dpi;
  } else if (abs((int)h->cupsWidth - sheet_w) <= 2 && abs((int)h->cupsHeight - sheet_h) <= 2) {
    *rx = *ry = 0;
  } else {
    *rx = (int)h->ImagingBoundingBox[0] * resx / 72;
    *ry = ((int)h->PageSize[1] - (int)h->ImagingBoundingBox[3]) * resy / 72;
  }
}

static int print_page(mx870_job_t *job, cups_raster_t *ras, const cups_page_header2_t *h,
                      int page)
{
  mx870_page_settings_t s;
  mx870_raster_t r;
  int width, height, dpi, x0, y0, rc = 0;
  uint8_t **rows;

  if (h->cupsBitsPerColor != 8 ||
      !(h->cupsColorSpace == CUPS_CSPACE_RGB || h->cupsColorSpace == CUPS_CSPACE_SRGB ||
        h->cupsColorSpace == CUPS_CSPACE_SW || h->cupsColorSpace == CUPS_CSPACE_W)) {
    fprintf(stderr, "ERROR: Unsupported raster format (%u bits, color space %u)\n",
            h->cupsBitsPerColor, h->cupsColorSpace);
    return -1;
  }
  if (setup_page(h, &s))
    return -1;
  if (mx870_page_start(job, &s)) {
    fprintf(stderr, "ERROR: Cannot start page %d (%s%s on %s)\n", page, s.paper->label,
            s.borderless ? " borderless" : "", s.media->label);
    return -1;
  }
  mx870_page_geometry(job, &width, &height, &dpi, &x0, &y0);

  r.width = (int)h->cupsWidth;
  r.height = (int)h->cupsHeight;
  r.res_x = (int)h->HWResolution[0];
  r.res_y = (int)h->HWResolution[1];
  r.sheet_w = (int)h->PageSize[0] * r.res_x / 72;
  r.sheet_h = (int)h->PageSize[1] * r.res_y / 72;
  r.ncomp = s.color == MX870_COLOR_GRAY ? 1 : 3;
  raster_origin(h, width, height, dpi, x0, y0, &r.origin_x, &r.origin_y);

  fprintf(stderr, "INFO: Printing page %d\n", page);
  fprintf(stderr, "DEBUG: page %d: %s, %s, %d dpi, area %dx%d at (%d,%d), raster %ux%u @%u at (%d,%d)\n",
          page, s.paper->label, s.media->label, dpi, width, height, x0, y0,
          h->cupsWidth, h->cupsHeight, h->HWResolution[0], r.origin_x, r.origin_y);

  /*
   * Read the whole page first: borderless stretching and resolution changes
   * make the row mapping non-sequential. 8-bit RGB at 600 dpi A4 is ~100 MB;
   * a streaming window would be leaner but this keeps the mapping simple.
   */
  if (!(rows = calloc(h->cupsHeight, sizeof(uint8_t *))))
    return -1;
  for (unsigned y = 0; y < h->cupsHeight && !rc; y++) {
    if (!(rows[y] = malloc(h->cupsBytesPerLine)) ||
        cupsRasterReadPixels(ras, rows[y], h->cupsBytesPerLine) != h->cupsBytesPerLine)
      rc = -1;
  }
  if (!rc && !canceled)
    rc = mx870_page_write_raster(job, &r, (const uint8_t *const *)rows);

  for (unsigned y = 0; y < h->cupsHeight; y++)
    free(rows[y]);
  free(rows);
  if (!rc)
    rc = mx870_page_end(job);
  return rc;
}

int main(int argc, char *argv[])
{
  int fd = 0, page = 0, rc = 0;
  cups_raster_t *ras;
  cups_page_header2_t header;
  mx870_job_t *job;

  if (argc < 6 || argc > 7) {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
    return 1;
  }
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: Unable to open raster file");
    return 1;
  }
  signal(SIGTERM, cancel_job);

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  job = mx870_job_new(write_stdout, NULL);
  if (!ras || !job) {
    fputs("ERROR: Out of memory\n", stderr);
    return 1;
  }

  while (!rc && !canceled && cupsRasterReadHeader2(ras, &header))
    rc = print_page(job, ras, &header, ++page);

  if (page == 0)
    fputs("ERROR: No pages found\n", stderr);
  if (mx870_job_end(job))
    rc = -1;
  fflush(stdout);

  mx870_job_free(job);
  cupsRasterClose(ras);
  if (fd)
    close(fd);
  return (rc || page == 0) ? 1 : 0;
}
