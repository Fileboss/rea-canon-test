/*
 * libmx870 internals.
 */

#ifndef MX870_PRIVATE_H
#define MX870_PRIVATE_H

#include "mx870.h"

#define MX870_SLOTS 13       /* ESC (t ink slots */
#define MX870_MAX_PLANES 7   /* C M Y K - - k actually used by the MX870 */
#define MX870_SLOT_K 3       /* pigment black */
#define MX870_SLOT_KDYE 6    /* dye black */
#define MX870_INFO_PACK5 0x20 /* ESC (t flag: 5 three-level pixels per byte */

typedef struct {
  uint8_t info;    /* bits per pixel | MX870_INFO_PACK5 */
  uint8_t levels;  /* number of output levels, 0 = slot unused */
} mx870_ink_t;

/* One ESC (c / (d / (t record of the Windows driver. */
typedef struct {
  uint8_t model;          /* ESC (c byte 0: 0x30 color, 0x31 grayscale */
  uint8_t media;          /* ESC (c media code */
  uint8_t quality;        /* ESC (c quality 1..3 */
  uint8_t duplex;         /* variant used when printing both sides */
  int dpi;
  uint8_t t_header[3];
  mx870_ink_t inks[MX870_SLOTS];
} mx870_mode_t;

extern const mx870_mode_t mx870_modes[];
extern const size_t mx870_num_modes;

const mx870_mode_t *mx870_find_mode(uint8_t media_c, int quality, int gray, int duplex);

/* Exact ESC (p geometry of the Windows templates, 1/600 inch. */
typedef struct {
  int width, length;
  int x0, area_w;
  int area_l70, area_l118;    /* bordered, top margin 70 or 118 */
  int borderless_w, borderless_l; /* 0 = borderless not offered */
} mx870_geometry_t;

const mx870_geometry_t *mx870_find_geometry(const mx870_paper_t *paper);

/* Growable byte buffer. */
typedef struct {
  uint8_t *data;
  size_t len, cap;
} mx870_buf_t;

int mx870_buf_put(mx870_buf_t *b, const void *data, size_t len);
int mx870_buf_putc(mx870_buf_t *b, uint8_t c);
void mx870_buf_free(mx870_buf_t *b);

typedef struct {
  int slot;               /* ESC (t slot */
  char letter;            /* ESC (L letter */
  int bits, levels, pack5;
  int line_bytes;         /* packed bytes per raster line */
  int empty_lines;        /* lines in the current band without ink */
  mx870_buf_t band;       /* ESC (F payload for the current band */
  int16_t *err[2];        /* error diffusion rows, current/next */
} mx870_plane_t;

struct mx870_job_s {
  mx870_write_fn write;
  void *ctx;
  int started, page_open, pages;
  int send_esc_r;         /* Gutenprint sends ESC (r 0x64; not seen in Windows templates */

  mx870_page_settings_t settings;
  const mx870_mode_t *mode;
  int dpi, lines_per_block;
  int width, height;      /* printable area at dpi */
  int x0, y0;             /* printable area origin, 1/600 inch, may be < 0 */
  int line;               /* lines received so far */
  int band_lines;         /* lines in the current band */
  int pending_blocks;     /* empty blocks not yet skipped with ESC (e */
  int last_state;         /* last ESC (L state sent, -1 = none */

  int nplanes;
  mx870_plane_t planes[MX870_MAX_PLANES];
  uint8_t *levels;        /* one dithered line, reused per plane */
  uint8_t *packed, *comp; /* scratch buffers */
  int16_t *ink;           /* per-plane ink amount 0..255 for one line */
};

/* Color separation of one RGB/gray line into ink amounts (0..255). */
void mx870_separate(const mx870_job_t *job, const uint8_t *line, int16_t *out);

/* Error-diffusion of one plane line to plane->levels output levels. */
void mx870_dither_line(mx870_plane_t *plane, const int16_t *ink, int stride, int width,
                       int serpentine_left, uint8_t *levels);

#endif /* MX870_PRIVATE_H */
