/*
 * libmx870 - Canon PIXMA MX870 raster stream encoder.
 *
 * Turns 8-bit RGB (or gray) scanlines into the Canon BJ raster command
 * stream understood by the MX870. Shared by the CUPS filter and the
 * PAPPL printer application. See docs/protocole-mx870.md for the protocol.
 */

#ifndef MX870_H
#define MX870_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sink for the generated stream; returns 0 on success. */
typedef int (*mx870_write_fn)(void *ctx, const void *buf, size_t len);

typedef enum {
  MX870_QUALITY_DRAFT = 1,
  MX870_QUALITY_STANDARD = 2,
  MX870_QUALITY_HIGH = 3
} mx870_quality_t;

typedef enum {
  MX870_COLOR_RGB,
  MX870_COLOR_GRAY
} mx870_color_t;

typedef enum {
  MX870_SOURCE_AUTO,     /* firmware picks rear tray or cassette from paper size */
  MX870_SOURCE_REAR,     /* rear tray */
  MX870_SOURCE_CASSETTE  /* front cassette */
} mx870_source_t;

typedef enum {
  MX870_DUPLEX_NONE,
  MX870_DUPLEX_LONG_EDGE,
  MX870_DUPLEX_SHORT_EDGE
} mx870_duplex_t;

typedef struct {
  const char *name;      /* PWG-ish keyword, e.g. "iso_a4_210x297mm" */
  const char *label;
  uint8_t code;          /* ESC (P paper size code */
  int width, length;     /* paper size in 1/600 inch, as sent in ESC (p */
} mx870_paper_t;

typedef struct {
  const char *name;      /* keyword, e.g. "stationery" */
  const char *label;
  uint8_t code_c;        /* ESC (c media code */
  uint8_t code_l;        /* ESC (l media code */
  uint8_t code_p;        /* ESC (P media code */
  int photo;             /* nonzero: no pigment black, dye black only */
} mx870_media_t;

typedef struct {
  const mx870_paper_t *paper;
  const mx870_media_t *media;
  mx870_quality_t quality;
  mx870_color_t color;
  mx870_source_t source;
  mx870_duplex_t duplex;
  int borderless;
} mx870_page_settings_t;

typedef struct mx870_job_s mx870_job_t;

/* Lookup tables (NULL-terminated by name). */
extern const mx870_paper_t mx870_papers[];
extern const mx870_media_t mx870_media[];

const mx870_paper_t *mx870_find_paper(const char *name);
const mx870_paper_t *mx870_find_paper_by_size(int width_600, int length_600);
const mx870_media_t *mx870_find_media(const char *name);

/* Job lifecycle. */
mx870_job_t *mx870_job_new(mx870_write_fn write, void *ctx);
int mx870_job_start(mx870_job_t *job);
int mx870_job_end(mx870_job_t *job);
void mx870_job_free(mx870_job_t *job);

/*
 * Page lifecycle. After mx870_page_start, query the raster geometry the
 * encoder expects, then feed exactly `height` lines of `width` pixels.
 * Lines are 8-bit RGB (3 bytes/pixel) or gray (1 byte/pixel), white = 255,
 * covering the printable area only. (x0, y0) is the area origin relative to
 * the paper's top-left corner in pixels at `dpi`; negative when borderless.
 */
int mx870_page_start(mx870_job_t *job, const mx870_page_settings_t *settings);
void mx870_page_geometry(const mx870_job_t *job, int *width, int *height, int *dpi,
                         int *x0, int *y0);
int mx870_page_write_line(mx870_job_t *job, const uint8_t *line);
int mx870_page_end(mx870_job_t *job);

/*
 * Convenience: encode a whole page from an 8-bit raster of the sheet (or of
 * part of it), after mx870_page_start. Bordered pages are cropped to the
 * printable area; borderless pages stretch the sheet over the overspray area,
 * as the Windows driver does. Resolutions may differ (nearest neighbour).
 */
typedef struct {
  int width, height;      /* raster size in pixels */
  int res_x, res_y;       /* raster resolution, dpi */
  int sheet_w, sheet_h;   /* sheet size in raster pixels */
  int origin_x, origin_y; /* raster top-left corner on the sheet, raster pixels */
  int ncomp;              /* 3 = RGB, 1 = gray; must match the page color setting */
} mx870_raster_t;

int mx870_page_write_raster(mx870_job_t *job, const mx870_raster_t *raster,
                            const uint8_t *const *rows);

/* Exposed for tests. */
size_t mx870_packbits(uint8_t *dst, const uint8_t *src, size_t len);
size_t mx870_pack5(uint8_t *dst, const uint8_t *levels, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* MX870_H */
