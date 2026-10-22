/*
 * MX870 job/page/band encoder.
 *
 * Stream layout (see docs/protocole-mx870.md):
 *
 *   IVEC StartJob + ModeShift
 *   page: [BJL SetSilent, ESC [K reset] ESC (b (q (d (t (c (p (P (l [(r] [(u (s] (I (J
 *         { [ESC (e skip] [ESC (L planes] ESC (F band-per-plane ... }
 *         FF ESC (b 0
 *   ESC @ + IVEC EndJob
 */

#include "mx870_private.h"

#include <stdlib.h>
#include <string.h>

/* IVEC job envelope, as sent by Gutenprint (https://gimp-print.sourceforge.io) for this model. */
static const char xml_start[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\" ?><cmd xmlns:ivec=\"http://www.canon.com/ns/cmd/2008/07/common/\">"
  "<ivec:contents><ivec:operation>StartJob</ivec:operation><ivec:param_set servicetype=\"print\">"
  "<ivec:jobID>00000001</ivec:jobID><ivec:bidi>0</ivec:bidi></ivec:param_set></ivec:contents></cmd>"
  "<?xml version=\"1.0\" encoding=\"utf-8\" ?><cmd xmlns:ivec=\"http://www.canon.com/ns/cmd/2008/07/common/\" "
  "xmlns:vcn=\"http://www.canon.com/ns/cmd/2008/07/canon/\"><ivec:contents><ivec:operation>VendorCmd</ivec:operation>"
  "<ivec:param_set servicetype=\"print\"><vcn:ijoperation>ModeShift</vcn:ijoperation><vcn:ijmode>1</vcn:ijmode>"
  "<ivec:jobID>00000001</ivec:jobID></ivec:param_set></ivec:contents></cmd>";

static const char xml_end[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\" ?><cmd xmlns:ivec=\"http://www.canon.com/ns/cmd/2008/07/common/\">"
  "<ivec:contents><ivec:operation>EndJob</ivec:operation><ivec:param_set servicetype=\"print\">"
  "<ivec:jobID>00000001</ivec:jobID></ivec:param_set></ivec:contents></cmd>";

/* ESC (t slot -> ESC (L letter. Slots 4/5 (light inks) are unused by the MX870. */
static const char slot_letters[MX870_MAX_PLANES] = {'C', 'M', 'Y', 'K', 'c', 'm', 'k'};

/* ESC (l tray codes (low nibble), shared by Windows and Gutenprint. */
static const uint8_t tray_codes[] = {
  [MX870_SOURCE_AUTO] = 0x0e,
  [MX870_SOURCE_REAR] = 0x04,
  [MX870_SOURCE_CASSETTE] = 0x08,
};

/* ESC (L band states, mirroring the Windows output module. */
enum { STATE_EMPTY = 0, STATE_K = 1, STATE_KDYE = 2, STATE_KK = 3, STATE_ALL = 4 };

int mx870_buf_put(mx870_buf_t *b, const void *data, size_t len)
{
  if (b->len + len > b->cap) {
    size_t cap = b->cap ? b->cap : 4096;
    uint8_t *p;

    while (cap < b->len + len)
      cap *= 2;
    if (!(p = realloc(b->data, cap)))
      return -1;
    b->data = p;
    b->cap = cap;
  }
  memcpy(b->data + b->len, data, len);
  b->len += len;
  return 0;
}

int mx870_buf_putc(mx870_buf_t *b, uint8_t c)
{
  return mx870_buf_put(b, &c, 1);
}

void mx870_buf_free(mx870_buf_t *b)
{
  free(b->data);
  memset(b, 0, sizeof(*b));
}

static void put_be16(uint8_t *p, int v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, int v)
{
  put_be16(p, (int)((uint32_t)v >> 16));
  put_be16(p + 2, v & 0xffff);
}

static int emit(mx870_job_t *job, const void *data, size_t len)
{
  return len ? job->write(job->ctx, data, len) : 0;
}

/* ESC ( <letter> <len LE16> <payload> */
static int cmd(mx870_job_t *job, char letter, const uint8_t *payload, size_t len)
{
  uint8_t hdr[5] = {0x1b, '(', (uint8_t)letter, (uint8_t)len, (uint8_t)(len >> 8)};

  if (emit(job, hdr, sizeof(hdr)))
    return -1;
  return emit(job, payload, len);
}

static int cmd1(mx870_job_t *job, char letter, uint8_t value)
{
  return cmd(job, letter, &value, 1);
}

/* ESC [K 02 00 00 1F, then a BJL text block. */
static int bjl(mx870_job_t *job, const char *command)
{
  static const uint8_t hdr[] = {0x1b, '[', 'K', 0x02, 0x00, 0x00, 0x1f};
  static const char start[] = "BJLSTART\nControlMode=Common\n";
  static const char end[] = "\nBJLEND\n";

  if (emit(job, hdr, sizeof(hdr)) || emit(job, start, sizeof(start) - 1) ||
      emit(job, command, strlen(command)))
    return -1;
  return emit(job, end, sizeof(end) - 1);
}

mx870_job_t *mx870_job_new(mx870_write_fn write, void *ctx)
{
  mx870_job_t *job = calloc(1, sizeof(*job));

  if (!job)
    return NULL;
  job->write = write;
  job->ctx = ctx;
  job->send_esc_r = 1;
  return job;
}

int mx870_job_start(mx870_job_t *job)
{
  job->started = 1;
  return emit(job, xml_start, sizeof(xml_start) - 1);
}

int mx870_job_end(mx870_job_t *job)
{
  static const uint8_t esc_at[] = {0x1b, '@'};

  if (job->page_open && mx870_page_end(job))
    return -1;
  if (!job->started)
    return 0;
  job->started = 0;
  if (emit(job, esc_at, sizeof(esc_at)))
    return -1;
  return emit(job, xml_end, sizeof(xml_end) - 1);
}

static void free_planes(mx870_job_t *job)
{
  for (int i = 0; i < job->nplanes; i++) {
    mx870_buf_free(&job->planes[i].band);
    free(job->planes[i].err[0]);
    free(job->planes[i].err[1]);
  }
  job->nplanes = 0;
  free(job->levels);
  free(job->packed);
  free(job->comp);
  free(job->ink);
  job->levels = job->packed = job->comp = NULL;
  job->ink = NULL;
}

void mx870_job_free(mx870_job_t *job)
{
  if (!job)
    return;
  free_planes(job);
  free(job);
}

/*
 * Printable area in 1/600 inch, exactly as in the Windows ESC (p templates.
 * Returns -1 when the paper size has no template or no borderless variant.
 */
static int page_area(const mx870_page_settings_t *s, int *x0, int *y0, int *w, int *l)
{
  const mx870_geometry_t *g = mx870_find_geometry(s->paper);

  if (!g)
    return -1;
  if (s->borderless) {
    if (!g->borderless_w)
      return -1;
    *x0 = -70;
    *y0 = -118;
    *w = g->borderless_w;
    *l = g->borderless_l;
    return 0;
  }
  *x0 = g->x0;
  *w = g->area_w;
  /* Windows has a 70 and a 118 top-margin variant; assume 118 is for duplex. */
  if (s->duplex != MX870_DUPLEX_NONE) {
    *y0 = 118;
    *l = g->area_l118;
  } else {
    *y0 = 70;
    *l = g->area_l70;
  }
  return 0;
}

static int send_page_header(mx870_job_t *job)
{
  const mx870_page_settings_t *s = &job->settings;
  const mx870_mode_t *mode = job->mode;
  uint8_t buf[64];
  int ax, ay, aw, al;

  if (job->pages == 0) {
    static const uint8_t reset[] = {0x1b, '[', 'K', 0x02, 0x00, 0x00, 0x0f};

    if (bjl(job, "SetSilent=OFF") || emit(job, reset, sizeof(reset)))
      return -1;
    if (s->duplex != MX870_DUPLEX_NONE) {
      static const uint8_t duplex[9] = {0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};

      if (cmd(job, '$', duplex, sizeof(duplex)))
        return -1;
    }
  }

  if (cmd1(job, 'b', 0x01) || cmd1(job, 'q', 0x01))
    return -1;

  put_be16(buf, mode->dpi);
  put_be16(buf + 2, mode->dpi);
  if (cmd(job, 'd', buf, 4))
    return -1;

  memcpy(buf, mode->t_header, 3);
  for (int i = 0; i < MX870_SLOTS; i++) {
    buf[3 + i * 3] = mode->inks[i].info;
    buf[4 + i * 3] = 0;
    buf[5 + i * 3] = mode->inks[i].levels;
  }
  if (cmd(job, 't', buf, 3 + MX870_SLOTS * 3))
    return -1;

  buf[0] = mode->model;
  buf[1] = s->media->code_c;
  buf[2] = mode->quality;
  if (cmd(job, 'c', buf, 3))
    return -1;

  if (page_area(s, &ax, &ay, &aw, &al))
    return -1;
  memset(buf, 0, 46);
  if (!s->borderless) {
    put_be16(buf, (al + 5) / 10);
    put_be16(buf + 4, (aw + 5) / 10);
  }
  put_be16(buf + 12, 600);
  put_be32(buf + 14, ax);
  put_be32(buf + 18, ay);
  put_be32(buf + 22, aw);
  put_be32(buf + 26, al);
  put_be32(buf + 38, s->paper->width);
  put_be32(buf + 42, s->paper->length);
  if (cmd(job, 'p', buf, 46))
    return -1;

  {
    const uint8_t esc_P[8] = {0x00, s->paper->code, 0x00, s->media->code_p, 0x01, 0x00, 0x01, 0x00};
    const uint8_t esc_l[3] = {(uint8_t)(0x30 | tray_codes[s->source]), s->media->code_l, 0x00};

    if (cmd(job, 'P', esc_P, sizeof(esc_P)) || cmd(job, 'l', esc_l, sizeof(esc_l)))
      return -1;
  }

  if (job->send_esc_r && cmd1(job, 'r', 0x64))
    return -1;

  if (s->duplex != MX870_DUPLEX_NONE) {
    /* Back sides of long-edge duplex are printed rotated. */
    if ((job->pages & 1) && s->duplex == MX870_DUPLEX_LONG_EDGE && cmd1(job, 'u', 0x01))
      return -1;
    if (cmd1(job, 's', 0x00))
      return -1;
  }

  if (cmd1(job, 'I', 0x01) || cmd1(job, 'J', (uint8_t)job->lines_per_block))
    return -1;
  return 0;
}

int mx870_page_start(mx870_job_t *job, const mx870_page_settings_t *settings)
{
  int ax, ay, aw, al;

  if (!settings->paper || !settings->media)
    return -1;
  if (job->page_open && mx870_page_end(job))
    return -1;
  if (!job->started && mx870_job_start(job))
    return -1;

  job->settings = *settings;
  job->mode = mx870_find_mode(settings->media->code_c, settings->quality,
                              settings->color == MX870_COLOR_GRAY,
                              settings->duplex != MX870_DUPLEX_NONE);
  if (!job->mode)
    return -1;

  job->dpi = job->mode->dpi;
  job->lines_per_block = job->dpi >= 600 ? 16 : 8;
  if (page_area(settings, &ax, &ay, &aw, &al))
    return -1;
  job->x0 = ax;
  job->y0 = ay;
  job->width = aw * job->dpi / 600;
  job->height = al * job->dpi / 600;
  job->line = job->band_lines = job->pending_blocks = 0;
  job->last_state = -1;

  free_planes(job);
  for (int slot = 0; slot < MX870_MAX_PLANES; slot++) {
    const mx870_ink_t *ink = &job->mode->inks[slot];
    mx870_plane_t *p;

    if (!ink->levels)
      continue;
    p = &job->planes[job->nplanes++];
    memset(p, 0, sizeof(*p));
    p->slot = slot;
    p->letter = slot_letters[slot];
    p->bits = ink->info & 0x1f;
    p->levels = ink->levels;
    p->pack5 = (ink->info & MX870_INFO_PACK5) != 0;
    p->line_bytes = p->pack5 ? (job->width + 4) / 5 : (job->width * p->bits + 7) / 8;
    p->err[0] = calloc((size_t)job->width + 2, sizeof(int16_t));
    p->err[1] = calloc((size_t)job->width + 2, sizeof(int16_t));
    if (!p->err[0] || !p->err[1])
      return -1;
  }
  job->levels = malloc((size_t)job->width);
  job->packed = malloc((size_t)job->width + 8);
  job->comp = malloc((size_t)job->width * 2 + 64);
  job->ink = malloc((size_t)job->width * MX870_MAX_PLANES * sizeof(int16_t));
  if (!job->levels || !job->packed || !job->comp || !job->ink)
    return -1;

  job->page_open = 1;
  return send_page_header(job);
}

void mx870_page_geometry(const mx870_job_t *job, int *width, int *height, int *dpi,
                         int *x0, int *y0)
{
  if (width)
    *width = job->width;
  if (height)
    *height = job->height;
  if (dpi)
    *dpi = job->dpi;
  if (x0)
    *x0 = job->x0 * job->dpi / 600;
  if (y0)
    *y0 = job->y0 * job->dpi / 600;
}

/* Pack one line of output levels into the plane's wire format. */
static void pack_line(const mx870_plane_t *p, const uint8_t *levels, int width, uint8_t *out)
{
  if (p->pack5) {
    mx870_pack5(out, levels, (size_t)width);
    return;
  }
  memset(out, 0, (size_t)p->line_bytes);
  for (int x = 0; x < width; x++) {
    int bit = x * p->bits;

    out[bit >> 3] |= (uint8_t)(levels[x] << (8 - p->bits - (bit & 7)));
  }
}

/* Encode one packed line into the plane's band: zero-run prefix, PackBits, 0x80. */
static int append_line(mx870_job_t *job, mx870_plane_t *p, const uint8_t *packed)
{
  int first = 0, last = p->line_bytes - 1;
  size_t n = 0;

  while (first <= last && !packed[first])
    first++;
  if (first > last) {
    p->empty_lines++;
    return mx870_buf_putc(&p->band, 0x80);
  }
  while (!packed[last])
    last--;

  for (int skip = first; skip > 0;) {
    int run = skip > 128 ? 128 : skip;

    job->comp[n++] = (uint8_t)(1 - run);
    job->comp[n++] = 0x00;
    skip -= run;
  }
  n += mx870_packbits(job->comp + n, packed + first, (size_t)(last - first + 1));
  job->comp[n++] = 0x80;
  return mx870_buf_put(&p->band, job->comp, n);
}

static int flush_band(mx870_job_t *job)
{
  static const char *const state_letters[] = {"", "K", "k", "Kk"};
  int state = STATE_EMPTY;

  for (int i = 0; i < job->nplanes; i++) {
    mx870_plane_t *p = &job->planes[i];

    if (p->empty_lines >= job->lines_per_block) {
      p->band.len = 0;
      continue;
    }
    if (p->slot == MX870_SLOT_K)
      state |= STATE_K;
    else if (p->slot == MX870_SLOT_KDYE)
      state |= STATE_KDYE;
    else
      state = STATE_ALL;
  }
  if (state & STATE_ALL)
    state = STATE_ALL;

  if (state == STATE_EMPTY) {
    job->pending_blocks++;
  } else {
    while (job->pending_blocks > 0) {
      int n = job->pending_blocks > 0x7fff ? 0x7fff : job->pending_blocks;
      uint8_t arg[2];

      put_be16(arg, n);
      if (cmd(job, 'e', arg, 2))
        return -1;
      job->pending_blocks -= n;
    }

    if (state != job->last_state) {
      uint8_t letters[MX870_MAX_PLANES];
      size_t n = 0;

      if (state == STATE_ALL) {
        for (int i = 0; i < job->nplanes; i++)
          letters[n++] = (uint8_t)job->planes[i].letter;
      } else {
        n = strlen(state_letters[state]);
        memcpy(letters, state_letters[state], n);
      }
      if (cmd(job, 'L', letters, n))
        return -1;
      job->last_state = state;
    }

    for (int i = 0; i < job->nplanes; i++) {
      mx870_plane_t *p = &job->planes[i];
      int in_set = state == STATE_ALL ||
                   (p->slot == MX870_SLOT_K && (state & STATE_K)) ||
                   (p->slot == MX870_SLOT_KDYE && (state & STATE_KDYE));

      if (in_set && cmd(job, 'F', p->band.data, p->band.len))
        return -1;
    }
  }

  for (int i = 0; i < job->nplanes; i++) {
    job->planes[i].band.len = 0;
    job->planes[i].empty_lines = 0;
  }
  job->band_lines = 0;
  return 0;
}

int mx870_page_write_line(mx870_job_t *job, const uint8_t *line)
{
  if (!job->page_open || job->line >= job->height)
    return -1;

  mx870_separate(job, line, job->ink);
  for (int i = 0; i < job->nplanes; i++) {
    mx870_plane_t *p = &job->planes[i];

    mx870_dither_line(p, job->ink + (size_t)i * job->width, 1, job->width, job->line & 1, job->levels);
    pack_line(p, job->levels, job->width, job->packed);
    if (append_line(job, p, job->packed))
      return -1;
  }

  job->line++;
  if (++job->band_lines == job->lines_per_block)
    return flush_band(job);
  return 0;
}

int mx870_page_end(mx870_job_t *job)
{
  static const uint8_t ff = 0x0c;

  if (!job->page_open)
    return 0;

  /* Feed blank lines for anything the caller did not send, then pad the band. */
  if (job->band_lines) {
    while (job->band_lines < job->lines_per_block) {
      for (int i = 0; i < job->nplanes; i++) {
        mx870_buf_putc(&job->planes[i].band, 0x80);
        job->planes[i].empty_lines++;
      }
      job->band_lines++;
    }
    if (flush_band(job))
      return -1;
  }

  job->page_open = 0;
  job->pages++;
  if (emit(job, &ff, 1))
    return -1;
  return cmd1(job, 'b', 0x00);
}
