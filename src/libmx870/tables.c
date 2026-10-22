/*
 * Paper, media and print-mode tables for the MX870.
 *
 * Paper sizes are the exact 1/600 inch values found in the Windows driver's
 * ESC (p templates (the firmware matches them to pick a tray); size codes are
 * the ESC (P values shared by the Windows driver and Gutenprint.
 */

#include "mx870_private.h"

#include <stdlib.h>
#include <string.h>

const mx870_paper_t mx870_papers[] = {
  {"iso_a4_210x297mm",       "A4",                   0x03, 4961, 7016},
  {"na_letter_8.5x11in",     "Letter",               0x0d, 5100, 6600},
  {"na_legal_8.5x14in",      "Legal",                0x0f, 5100, 8400},
  {"iso_a5_148x210mm",       "A5",                   0x01, 3497, 4961},
  {"jis_b5_182x257mm",       "B5",                   0x08, 4300, 6071},
  {"na_index-4x6_4x6in",     "4 x 6 in",             0x34, 2400, 3600},
  {"na_5x7_5x7in",           "5 x 7 in",             0x37, 3000, 4200},
  {"na_govt-letter_8x10in",  "8 x 10 in",            0x49, 4800, 6000},
  {"na_index-4x8_4x8in",     "4 x 8 in",             0x46, 2400, 4800},
  {"om_wide_101.6x180.6mm",  "Wide 101.6 x 180.6 mm", 0x52, 2400, 4267},
  {"oe_photo-l_3.5x5in",     "L 89 x 127 mm",        0x32, 2103, 3000},
  {"om_2l_127x178mm",        "2L 127 x 178 mm",      0x33, 3000, 4205},
  {"om_card_55x91mm",        "Card 55 x 91 mm",      0x36, 1300, 2150},
  {"jpn_hagaki_100x148mm",   "Hagaki",               0x14, 2363, 3497},
  {"om_oufuku-landscape_200x148mm", "Oufuku Hagaki",       0x39, 4725, 3497},
  {"na_number-10_4.125x9.5in", "Envelope #10",       0x2e, 2475, 5700},
  {"iso_dl_110x220mm",       "Envelope DL",          0x2f, 2599, 5197},
  {"jpn_you4_105x235mm",     "Envelope You 4",       0x30, 2481, 5552},
  {"om_you6_98x190mm",       "Envelope You 6",       0x31, 2315, 4489},
  {"jpn_chou3_120x235mm",    "Envelope Chou 3",      0x3a, 2835, 5552},
  {"jpn_chou4_90x205mm",     "Envelope Chou 4",      0x3b, 2126, 4843},
  {NULL, NULL, 0, 0, 0}
};

/* name, label, ESC (c code, ESC (l code, ESC (P code, photo */
const mx870_media_t mx870_media[] = {
  {"stationery",               "Plain Paper",                  0x00, 0x00, 0x00, 0},
  {"photographic-glossy",      "Photo Paper Plus Glossy II",   0x1d, 0x23, 0x32, 1},
  {"photographic-high-gloss",  "Photo Paper Pro II",           0x1f, 0x25, 0x34, 1},
  {"photographic-glossy-pro-platinum", "Photo Paper Pro Platinum", 0x1e, 0x24, 0x33, 1},
  {"photographic-semi-gloss",  "Photo Paper Plus Semi-gloss",  0x1a, 0x1f, 0x2a, 1},
  {"photographic-glossy-std",  "Glossy Photo Paper",           0x05, 0x05, 0x16, 1},
  {"photographic-matte",       "Matte Photo Paper",            0x0a, 0x10, 0x1c, 1},
  {"stationery-coated",        "High Resolution Paper",        0x07, 0x07, 0x10, 1},
  {"photographic",             "Other Photo Paper",            0x0f, 0x14, 0x24, 1},
  {"envelope",                 "Envelope",                     0x08, 0x08, 0x08, 0},
  {"jp-hagaki-inkjet",         "Ink Jet Hagaki",               0x0d, 0x09, 0x1b, 1},
  {"jp-hagaki",                "Hagaki",                       0x08, 0x09, 0x07, 0},
  {"transfer",                 "T-Shirt Transfers",            0x03, 0x03, 0x12, 1},
  {NULL, NULL, 0, 0, 0, 0}
};

/*
 * modes.inc and papers.inc are generated at build time from the user's own
 * copy of Canon's Windows driver (tools/extract_assets.sh), into build/generated.
 */
const mx870_mode_t mx870_modes[] = {
#include "modes.inc"
};
const size_t mx870_num_modes = sizeof(mx870_modes) / sizeof(mx870_modes[0]);

static const mx870_geometry_t geometries[] = {
#include "papers.inc"
};

const mx870_geometry_t *mx870_find_geometry(const mx870_paper_t *paper)
{
  for (size_t i = 0; i < sizeof(geometries) / sizeof(geometries[0]); i++)
    if (geometries[i].width == paper->width && geometries[i].length == paper->length)
      return geometries + i;
  return NULL;
}

const mx870_paper_t *mx870_find_paper(const char *name)
{
  for (const mx870_paper_t *p = mx870_papers; p->name; p++)
    if (!strcmp(p->name, name) || !strcmp(p->label, name))
      return p;
  return NULL;
}

/* Closest standard size within 1 mm (~24 units). */
const mx870_paper_t *mx870_find_paper_by_size(int width, int length)
{
  for (const mx870_paper_t *p = mx870_papers; p->name; p++)
    if (abs(p->width - width) <= 24 && abs(p->length - length) <= 24)
      return p;
  return NULL;
}

const mx870_media_t *mx870_find_media(const char *name)
{
  for (const mx870_media_t *m = mx870_media; m->name; m++)
    if (!strcmp(m->name, name) || !strcmp(m->label, name))
      return m;
  return NULL;
}

/*
 * Pick the Windows mode record for a media/quality pair. Falls back to the
 * nearest available quality for that media, then to plain paper.
 */
const mx870_mode_t *mx870_find_mode(uint8_t media_c, int quality, int gray, int duplex)
{
  uint8_t model = gray ? 0x31 : 0x30;
  const mx870_mode_t *best = NULL;
  int best_dist = 99;

  for (int pass = 0; pass < 2 && !best; pass++) {
    for (size_t i = 0; i < mx870_num_modes; i++) {
      const mx870_mode_t *m = mx870_modes + i;
      int dist;

      if (m->media != media_c || (pass == 0 && m->model != model) ||
          (pass == 1 && m->model != 0x30))
        continue;
      dist = abs(m->quality - quality) * 2 + (m->duplex != (duplex != 0));
      if (dist < best_dist) {
        best = m;
        best_dist = dist;
      }
    }
  }
  if (!best && media_c != 0x00)
    return mx870_find_mode(0x00, quality, gray, duplex);
  return best;
}
