/*
 * mx870-app - IPP Everywhere / AirPrint printer application for the Canon
 * PIXMA MX870, built on PAPPL.
 *
 * macOS (and iOS) see the MX870 as a driverless AirPrint printer; this
 * application renders jobs, encodes them with libmx870 and sends them to the
 * printer over BJNP (bjnp://host) or USB (usb://, provided by PAPPL).
 *
 *   mx870-app server            run the printer application (default port 8000)
 *   mx870-app devices           list printers PAPPL can see, including bjnp://
 *   mx870-app add -d NAME -v bjnp://192.168.1.20 -m canon_mx870
 *   see `mx870-app --help` for the standard PAPPL sub-commands.
 */

#include "../libbjnp/bjnp.h"
#include "../libmx870/mx870.h"

#include <pappl/pappl.h>
#include <stdlib.h>
#include <string.h>

#define DRIVER_NAME "canon_mx870"

/* ---------------------------------------------------------------------------
 * BJNP device scheme
 * ------------------------------------------------------------------------ */

typedef struct {
  char host[256];
  char title[256];
  bjnp_job_t *job;      /* opened lazily on first write: status queries open the device too */
} bjnp_dev_t;

static bool bjnp_list(pappl_device_cb_t cb, void *data, pappl_deverror_cb_t err_cb, void *err_data)
{
  bjnp_printer_t printers[16];
  int n = bjnp_discover(printers, 16, 1500);

  (void)err_cb;
  (void)err_data;
  for (int i = 0; i < n; i++) {
    char uri[300], info[400];

    snprintf(uri, sizeof(uri), "bjnp://%s", printers[i].host);
    snprintf(info, sizeof(info), "Canon (BJNP %s)", printers[i].host);
    if ((*cb)(info, uri, printers[i].device_id, data))
      return true;
  }
  return false;
}

static bool bjnp_open(pappl_device_t *device, const char *uri, const char *name)
{
  bjnp_dev_t *d = calloc(1, sizeof(*d));
  const char *host = uri + strlen("bjnp://");
  size_t len;

  if (!d || strncmp(uri, "bjnp://", 7)) {
    free(d);
    papplDeviceError(device, "Bad BJNP URI \"%s\".", uri);
    return false;
  }
  len = strcspn(host, "/?");
  snprintf(d->host, sizeof(d->host), "%.*s", (int)len, host);
  snprintf(d->title, sizeof(d->title), "%s", name ? name : "PAPPL job");
  papplDeviceSetData(device, d);
  return true;
}

static void bjnp_close(pappl_device_t *device)
{
  bjnp_dev_t *d = papplDeviceGetData(device);

  if (d) {
    if (d->job && bjnp_job_close(d->job))
      papplDeviceError(device, "BJNP close was not acknowledged by %s.", d->host);
    free(d);
  }
}

static ssize_t bjnp_read(pappl_device_t *device, void *buffer, size_t bytes)
{
  (void)device;
  (void)buffer;
  (void)bytes;
  return 0;
}

static ssize_t bjnp_write(pappl_device_t *device, const void *buffer, size_t bytes)
{
  bjnp_dev_t *d = papplDeviceGetData(device);
  char err[256];

  if (!d->job && !(d->job = bjnp_job_open(d->host, "mx870-app", d->title, err, sizeof(err)))) {
    papplDeviceError(device, "%s", err);
    return -1;
  }
  if (bjnp_job_write(d->job, buffer, bytes)) {
    papplDeviceError(device, "%s", bjnp_job_error(d->job));
    return -1;
  }
  return (ssize_t)bytes;
}

static pappl_preason_t bjnp_status(pappl_device_t *device)
{
  (void)device;
  return PAPPL_PREASON_NONE;
}

static char *bjnp_id(pappl_device_t *device, char *buffer, size_t bufsize)
{
  bjnp_dev_t *d = papplDeviceGetData(device);

  return bjnp_get_device_id(d->host, buffer, bufsize, 2000) ? NULL : buffer;
}

/* ---------------------------------------------------------------------------
 * Raster driver
 * ------------------------------------------------------------------------ */

typedef struct {
  mx870_job_t *enc;
  mx870_page_settings_t settings;
  uint8_t **rows;
  unsigned nrows;
  bjnp_dev_t *bjnp;     /* set when printing to a bjnp:// device */
} job_data_t;

/*
 * PAPPL keeps devices open between jobs, so each print job gets its own
 * BJNP session: opened on the job's first write (with its title), closed at
 * the end of the job. Only bjnp:// devices carry a bjnp_dev_t.
 */
static bjnp_dev_t *job_bjnp(pappl_job_t *job, pappl_device_t *device)
{
  const char *uri = papplPrinterGetDeviceURI(papplJobGetPrinter(job));

  return uri && !strncmp(uri, "bjnp://", 7) ? papplDeviceGetData(device) : NULL;
}

static int device_write(void *ctx, const void *buf, size_t len)
{
  return papplDeviceWrite((pappl_device_t *)ctx, buf, len) == (ssize_t)len ? 0 : -1;
}

static bool mx_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  job_data_t *jd = calloc(1, sizeof(*jd));

  (void)options;
  if (!jd || !(jd->enc = mx870_job_new(device_write, device))) {
    free(jd);
    return false;
  }
  if ((jd->bjnp = job_bjnp(job, device)) != NULL) {
    const char *name = papplJobGetName(job);

    snprintf(jd->bjnp->title, sizeof(jd->bjnp->title), "%s", name ? name : "PAPPL job");
  }
  papplJobSetData(job, jd);
  return mx870_job_start(jd->enc) == 0;
}

static const mx870_paper_t *media_paper(const pappl_media_col_t *m)
{
  const mx870_paper_t *p = mx870_find_paper(m->size_name);

  /* hundredths of mm -> 1/600 inch */
  return p ? p : mx870_find_paper_by_size(m->size_width * 600 / 2540, m->size_length * 600 / 2540);
}

static bool mx_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned page)
{
  job_data_t *jd = papplJobGetData(job);
  mx870_page_settings_t *s = &jd->settings;
  const pappl_media_col_t *m = &options->media;

  (void)device;
  memset(s, 0, sizeof(*s));
  if (!(s->paper = media_paper(m))) {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unsupported media size \"%s\".", m->size_name);
    return false;
  }
  if (!(s->media = mx870_find_media(m->type)))
    s->media = mx870_find_media("stationery");
  s->quality = options->print_quality == IPP_QUALITY_DRAFT ? MX870_QUALITY_DRAFT
             : options->print_quality == IPP_QUALITY_HIGH ? MX870_QUALITY_HIGH
             : MX870_QUALITY_STANDARD;
  s->color = options->header.cupsNumColors == 1 ? MX870_COLOR_GRAY : MX870_COLOR_RGB;
  s->source = !strcmp(m->source, "rear") ? MX870_SOURCE_REAR
            : !strcmp(m->source, "main") ? MX870_SOURCE_CASSETTE : MX870_SOURCE_AUTO;
  s->duplex = options->sides == PAPPL_SIDES_TWO_SIDED_LONG_EDGE ? MX870_DUPLEX_LONG_EDGE
            : options->sides == PAPPL_SIDES_TWO_SIDED_SHORT_EDGE ? MX870_DUPLEX_SHORT_EDGE
            : MX870_DUPLEX_NONE;
  s->borderless = !m->left_margin && !m->right_margin && !m->top_margin && !m->bottom_margin;

  if (mx870_page_start(jd->enc, s)) {
    if (!s->borderless) {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Cannot start page %u.", page);
      return false;
    }
    /* No borderless template for this size: print with a border instead. */
    papplLogJob(job, PAPPL_LOGLEVEL_WARN, "Borderless not available for %s, printing with margins.",
                s->paper->label);
    s->borderless = 0;
    if (mx870_page_start(jd->enc, s))
      return false;
  }

  jd->nrows = options->header.cupsHeight;
  if (!(jd->rows = calloc(jd->nrows, sizeof(uint8_t *))))
    return false;
  for (unsigned y = 0; y < jd->nrows; y++)
    if (!(jd->rows[y] = calloc(1, options->header.cupsBytesPerLine)))
      return false;
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Page %u: %s, %s, quality %d, %s%s.", page + 1,
              s->paper->label, s->media->label, s->quality,
              s->color == MX870_COLOR_GRAY ? "gray" : "color", s->borderless ? ", borderless" : "");
  return true;
}

static bool mx_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned y, const unsigned char *line)
{
  job_data_t *jd = papplJobGetData(job);

  (void)device;
  if (y < jd->nrows)
    memcpy(jd->rows[y], line, options->header.cupsBytesPerLine);
  return true;
}

static void free_rows(job_data_t *jd)
{
  for (unsigned y = 0; jd->rows && y < jd->nrows; y++)
    free(jd->rows[y]);
  free(jd->rows);
  jd->rows = NULL;
  jd->nrows = 0;
}

static bool mx_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                        unsigned page)
{
  job_data_t *jd = papplJobGetData(job);
  const cups_page_header2_t *h = &options->header;
  mx870_raster_t r;
  int rc;

  (void)device;
  (void)page;
  /* PWG raster always covers the whole sheet. */
  r.width = r.sheet_w = (int)h->cupsWidth;
  r.height = r.sheet_h = (int)h->cupsHeight;
  r.res_x = (int)h->HWResolution[0];
  r.res_y = (int)h->HWResolution[1];
  r.origin_x = r.origin_y = 0;
  r.ncomp = (int)h->cupsNumColors;

  rc = mx870_page_write_raster(jd->enc, &r, (const uint8_t *const *)jd->rows);
  free_rows(jd);
  if (!rc)
    rc = mx870_page_end(jd->enc);
  papplDeviceFlush(device);
  return rc == 0;
}

static bool mx_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  job_data_t *jd = papplJobGetData(job);
  int rc;

  (void)options;
  rc = mx870_job_end(jd->enc);
  papplDeviceFlush(device);
  if (jd->bjnp && jd->bjnp->job) {
    if (bjnp_job_close(jd->bjnp->job))
      papplLogJob(job, PAPPL_LOGLEVEL_WARN, "BJNP close was not acknowledged.");
    jd->bjnp->job = NULL;
  }
  mx870_job_free(jd->enc);
  free_rows(jd);
  free(jd);
  papplJobSetData(job, NULL);
  return rc == 0;
}

static bool driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri,
                      const char *device_id, pappl_pr_driver_data_t *dd, ipp_t **driver_attrs,
                      void *data)
{
  int i;

  (void)system;
  (void)device_uri;
  (void)device_id;
  (void)driver_attrs;
  (void)data;
  if (strcmp(driver_name, DRIVER_NAME))
    return false;

  dd->rstartjob_cb = mx_rstartjob;
  dd->rstartpage_cb = mx_rstartpage;
  dd->rwriteline_cb = mx_rwriteline;
  dd->rendpage_cb = mx_rendpage;
  dd->rendjob_cb = mx_rendjob;

  snprintf(dd->make_and_model, sizeof(dd->make_and_model), "Canon MX870 series");
  dd->ppm = 9;
  dd->ppm_color = 8;
  dd->kind = PAPPL_KIND_DOCUMENT | PAPPL_KIND_PHOTO | PAPPL_KIND_ENVELOPE;
  dd->color_supported = PAPPL_COLOR_MODE_AUTO | PAPPL_COLOR_MODE_COLOR | PAPPL_COLOR_MODE_MONOCHROME;
  dd->color_default = PAPPL_COLOR_MODE_AUTO;
  dd->raster_types = PAPPL_PWG_RASTER_TYPE_SRGB_8 | PAPPL_PWG_RASTER_TYPE_SGRAY_8;
  dd->quality_default = IPP_QUALITY_NORMAL;
  dd->duplex = PAPPL_DUPLEX_NORMAL; /* back sides are rotated by the printer (ESC (u) */
  dd->sides_supported = PAPPL_SIDES_ONE_SIDED | PAPPL_SIDES_TWO_SIDED_LONG_EDGE |
                        PAPPL_SIDES_TWO_SIDED_SHORT_EDGE;
  dd->sides_default = PAPPL_SIDES_ONE_SIDED;

  dd->num_resolution = 2;
  dd->x_resolution[0] = dd->y_resolution[0] = 300;
  dd->x_resolution[1] = dd->y_resolution[1] = 600;
  dd->x_default = dd->y_default = 600;

  /* Smallest Windows margins: 80/600 in left, 70 top, 118 bottom (hundredths of mm). */
  dd->borderless = true;
  dd->left_right = 339;
  dd->bottom_top = 500;

  for (i = 0; mx870_papers[i].name && i < PAPPL_MAX_MEDIA; i++)
    dd->media[i] = mx870_papers[i].name;
  dd->num_media = i;
  for (i = 0; mx870_media[i].name && i < PAPPL_MAX_TYPE; i++)
    dd->type[i] = mx870_media[i].name;
  dd->num_type = i;

  dd->num_source = 3;
  dd->source[0] = "auto";
  dd->source[1] = "rear";
  dd->source[2] = "main";

  for (i = 0; i < dd->num_source; i++) {
    pappl_media_col_t *m = &dd->media_ready[i];

    snprintf(m->size_name, sizeof(m->size_name), "%s", i == 1 ? "na_index-4x6_4x6in" : "iso_a4_210x297mm");
    snprintf(m->source, sizeof(m->source), "%s", dd->source[i]);
    snprintf(m->type, sizeof(m->type), "%s", i == 1 ? "photographic-glossy" : "stationery");
    m->size_width = i == 1 ? 10160 : 21000;
    m->size_length = i == 1 ? 15240 : 29700;
    m->left_margin = m->right_margin = dd->left_right;
    m->top_margin = m->bottom_margin = dd->bottom_top;
  }
  dd->media_default = dd->media_ready[0];
  return true;
}

static const char *autoadd_cb(const char *device_info, const char *device_uri, const char *device_id,
                              void *data)
{
  (void)device_info;
  (void)device_uri;
  (void)data;
  return device_id && strstr(device_id, "MDL:MX870") ? DRIVER_NAME : NULL;
}

int main(int argc, char *argv[])
{
  static pappl_pr_driver_t drivers[] = {
    {DRIVER_NAME, "Canon MX870 series", "MFG:Canon;MDL:MX870 series;", NULL}
  };

  papplDeviceAddScheme("bjnp", PAPPL_DEVTYPE_CUSTOM_NETWORK, bjnp_list, bjnp_open, bjnp_close,
                       bjnp_read, bjnp_write, bjnp_status, bjnp_id);
  return papplMainloop(argc, argv, "0.1.0", NULL, 1, drivers, autoadd_cb, driver_cb,
                       NULL, NULL, NULL, NULL, NULL);
}
