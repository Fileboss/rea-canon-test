/*
 * Unit tests for libmx870, plus generation of sample streams that
 * tools/bjdump.py validates independently (see `make check`).
 */

#include "../src/libmx870/mx870.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static size_t unpackbits(uint8_t *dst, const uint8_t *src, size_t len)
{
  size_t i = 0, out = 0;

  while (i < len) {
    int h = (int8_t)src[i++];

    if (h >= 0) {
      memcpy(dst + out, src + i, (size_t)h + 1);
      out += (size_t)h + 1;
      i += (size_t)h + 1;
    } else if (h != -128) {
      memset(dst + out, src[i++], (size_t)(1 - h));
      out += (size_t)(1 - h);
    }
  }
  return out;
}

static void test_packbits(void)
{
  uint8_t src[2000], comp[2100], back[2000];
  size_t n;

  srand(42);
  for (int round = 0; round < 500; round++) {
    size_t len = (size_t)(rand() % 2000) + 1;

    for (size_t i = 0; i < len; i++)
      src[i] = (uint8_t)((rand() % 4 == 0) ? rand() : (i ? src[i - 1] : 0)); /* mix runs/literals */
    n = mx870_packbits(comp, src, len);
    CHECK(n <= len + len / 128 + 1);
    for (size_t i = 0; i < n;) { /* never emits the 0x80 line terminator */
      int h = (int8_t)comp[i];

      CHECK(h != -128);
      i += h >= 0 ? (size_t)h + 2 : 2;
    }
    CHECK(unpackbits(back, comp, n) == len);
    CHECK(!memcmp(src, back, len));
  }

  memset(src, 7, 300);
  n = mx870_packbits(comp, src, 300);
  CHECK(n == 6); /* 128 + 128 + 44 */
  CHECK(comp[0] == 0x81 && comp[1] == 7 && comp[4] == (uint8_t)(1 - 44));
}

static void test_pack5(void)
{
  const uint8_t a[5] = {2, 2, 2, 2, 2}, b[5] = {1, 0, 0, 0, 0}, c[7] = {0, 0, 0, 0, 1, 2, 1};
  uint8_t out[2];

  CHECK(mx870_pack5(out, a, 5) == 1 && out[0] == 242);
  CHECK(mx870_pack5(out, b, 5) == 1 && out[0] == 81);
  CHECK(mx870_pack5(out, c, 7) == 2 && out[0] == 1 && out[1] == 2 * 81 + 1 * 27);
}

static int write_file(void *ctx, const void *buf, size_t len)
{
  return fwrite(buf, 1, len, (FILE *)ctx) == len ? 0 : -1;
}

/* Test chart: CMY/RGB bars, a gray ramp, black text-like strokes, blank gap. */
static void chart_pixel(int x, int y, int w, int h, uint8_t *rgb)
{
  int band = y * 8 / h;

  rgb[0] = rgb[1] = rgb[2] = 255;
  if (band == 0) {
    static const uint8_t bars[6][3] = {{0, 255, 255}, {255, 0, 255}, {255, 255, 0},
                                       {255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    memcpy(rgb, bars[x * 6 / w], 3);
  } else if (band == 1) {
    rgb[0] = rgb[1] = rgb[2] = (uint8_t)(255 - x * 255 / (w - 1));
  } else if (band == 2) {
    rgb[0] = (uint8_t)(x * 255 / (w - 1));
    rgb[1] = (uint8_t)(255 - x * 255 / (w - 1));
    rgb[2] = 128;
  } else if (band == 3 && (x / 12) % 4 == 0) {
    rgb[0] = rgb[1] = rgb[2] = 0;
  } else if (band >= 5 && band <= 6) {
    int dx = x - w / 2, dy = y - h * 6 / 8;

    if (dx * dx + dy * dy < (h / 10) * (h / 10)) {
      rgb[0] = (uint8_t)(200 - (x * 100 / w));
      rgb[1] = 120;
      rgb[2] = (uint8_t)(60 + (y * 100 / h));
    }
  }
}

static int make_sample(const char *path, const char *paper, const char *media,
                       mx870_quality_t quality, mx870_color_t color, int borderless, int pages)
{
  FILE *f = fopen(path, "wb");
  mx870_job_t *job;
  mx870_page_settings_t s = {0};
  int w, h, dpi, rc = 0;
  uint8_t *line;

  if (!f)
    return -1;
  job = mx870_job_new(write_file, f);
  s.paper = mx870_find_paper(paper);
  s.media = mx870_find_media(media);
  s.quality = quality;
  s.color = color;
  s.borderless = borderless;

  for (int p = 0; p < pages && !rc; p++) {
    if (mx870_page_start(job, &s)) {
      rc = -1;
      break;
    }
    mx870_page_geometry(job, &w, &h, &dpi, NULL, NULL);
    line = malloc((size_t)w * 3);
    for (int y = 0; y < h && !rc; y++) {
      if (color == MX870_COLOR_GRAY) {
        uint8_t rgb[3];

        for (int x = 0; x < w; x++) {
          chart_pixel(x, y, w, h, rgb);
          line[x] = (uint8_t)((rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8);
        }
      } else {
        for (int x = 0; x < w; x++)
          chart_pixel(x, y, w, h, line + 3 * x);
      }
      rc = mx870_page_write_line(job, line);
    }
    free(line);
    if (!rc)
      rc = mx870_page_end(job);
  }
  if (!rc)
    rc = mx870_job_end(job);
  mx870_job_free(job);
  fclose(f);
  printf("%s: %s (%s, %d dpi)\n", rc ? "FAILED" : "wrote", path, paper, dpi);
  return rc;
}

int main(int argc, char **argv)
{
  const char *outdir = argc > 1 ? argv[1] : ".";
  char path[1024];

  test_packbits();
  test_pack5();

  CHECK(mx870_find_paper("iso_a4_210x297mm") != NULL);
  CHECK(mx870_find_paper_by_size(5100, 6600) == mx870_find_paper("Letter"));
  CHECK(mx870_find_media("stationery") != NULL);

  snprintf(path, sizeof(path), "%s/sample_a4_plain_std.bin", outdir);
  CHECK(!make_sample(path, "iso_a4_210x297mm", "stationery", MX870_QUALITY_STANDARD, MX870_COLOR_RGB, 0, 2));
  snprintf(path, sizeof(path), "%s/sample_letter_plain_draft_gray.bin", outdir);
  CHECK(!make_sample(path, "na_letter_8.5x11in", "stationery", MX870_QUALITY_DRAFT, MX870_COLOR_GRAY, 0, 1));
  snprintf(path, sizeof(path), "%s/sample_4x6_photo_high_borderless.bin", outdir);
  CHECK(!make_sample(path, "na_index-4x6_4x6in", "photographic-glossy", MX870_QUALITY_HIGH, MX870_COLOR_RGB, 1, 1));
  snprintf(path, sizeof(path), "%s/sample_a4_plain_high.bin", outdir);
  CHECK(!make_sample(path, "iso_a4_210x297mm", "stationery", MX870_QUALITY_HIGH, MX870_COLOR_RGB, 0, 1));

  /* Borderless is not offered for A5 in the Windows templates. */
  {
    mx870_job_t *job = mx870_job_new(write_file, stdout);
    mx870_page_settings_t s = {mx870_find_paper("iso_a5_148x210mm"), mx870_find_media("stationery"),
                               MX870_QUALITY_STANDARD, MX870_COLOR_RGB, MX870_SOURCE_AUTO,
                               MX870_DUPLEX_NONE, 1};
    FILE *null = fopen("/dev/null", "wb");

    mx870_job_free(job);
    job = mx870_job_new(write_file, null);
    CHECK(mx870_page_start(job, &s) != 0);
    mx870_job_free(job);
    fclose(null);
  }

  if (failures)
    fprintf(stderr, "%d check(s) failed\n", failures);
  else
    printf("all unit checks passed\n");
  return failures != 0;
}
