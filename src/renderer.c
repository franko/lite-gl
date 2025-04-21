#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_OUTLINE_H
#include FT_SYSTEM_H

#include <hb.h>
#include <hb-ft.h>

#ifdef _WIN32
#include <windows.h>
#include "utfconv.h"
#endif

#include "renderer.h"
#include "rensurface.h"
#include "renwindow.h"

#define MAX_UNICODE 0x100000
#define GLYPHSET_SIZE 256
#define MAX_LOADABLE_GLYPHSETS (MAX_UNICODE / GLYPHSET_SIZE)
#define SUBPIXEL_BITMAPS_CACHED 3

RenWindow window_renderer = {0};
static FT_Library library;

// draw_rect_surface is used as a 1x1 surface to simplify ren_draw_rect with blending
static SDL_Surface *draw_rect_surface;
/* Re-usable HarfBuzz buffer for glyph shaping in ren_draw_text()
   (allocated on first use, freed in ren_free_window_resources). */
static hb_buffer_t *hb_draw_buf = NULL;
/* Buffer used by width-measurement path (freed in ren_free_window_resources) */
static hb_buffer_t *hb_measure_buf = NULL;

static void* check_alloc(void *ptr) {
  if (!ptr) {
    fprintf(stderr, "Fatal error: memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  return ptr;
}

static RenRect scaled_rect(const RenRect rect, const int scale) {
  return (RenRect) {rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale};
}

/* ------------------------------------------------------------------------
 * blit_glyph: unico punto dove avviene il blending pixel‑per‑pixel dei
 * bitmap dei glifi sul surface di destinazione.  È usato sia dal percorso
 * “fast‑path” ASCII sia da quello HarfBuzz, evitando duplicazione di ~150
 * righe di codice.  Modifiche future al blending vanno fatte solo qui.
 * --------------------------------------------------------------------- */
static void blit_glyph(SDL_Surface *surface,
                       const uint8_t *src_pixels, int src_pitch,
                       int glyph_w, int glyph_h,
                       int dst_x, int dst_y,
                       const SDL_Rect *clip, RenColor color,
                       bool is_subpixel)
{
  const int clip_end_x = clip->x + clip->w;
  const int clip_end_y = clip->y + clip->h;

  const int bpp = surface->format->BytesPerPixel;
  uint8_t *dst_base = (uint8_t *) surface->pixels;

  for (int row = 0; row < glyph_h; ++row) {
    int y = dst_y + row;
    if (y < clip->y || y >= clip_end_y) continue;

    const uint8_t *src = src_pixels + row * src_pitch;
    uint32_t *dst = (uint32_t *) &dst_base[surface->pitch * y + dst_x * bpp];

    int x_col = dst_x;
    for (int col = 0; col < glyph_w; ++col, ++x_col, ++dst) {
      if (x_col < clip->x) { src += is_subpixel ? 3 : 1; continue; }
      if (x_col >= clip_end_x) break;

      uint8_t src_r, src_g, src_b;
      if (is_subpixel) {
        src_r = src[0]; src_g = src[1]; src_b = src[2];
        src += 3;
      } else {
        src_r = src_g = src_b = *src;
        ++src;
      }
      uint8_t src_a = src_r;
      if (src_a == 0) continue;

      uint32_t dst_pix = *dst;
      SDL_Color d = {
        (dst_pix & surface->format->Rmask) >> surface->format->Rshift,
        (dst_pix & surface->format->Gmask) >> surface->format->Gshift,
        (dst_pix & surface->format->Bmask) >> surface->format->Bshift,
        (dst_pix & surface->format->Amask) >> surface->format->Ashift
      };

      uint32_t r = (color.r * src_r * color.a + d.r * (65025 - src_r * color.a) + 32767) / 65025;
      uint32_t g = (color.g * src_g * color.a + d.g * (65025 - src_g * color.a) + 32767) / 65025;
      uint32_t b = (color.b * src_b * color.a + d.b * (65025 - src_b * color.a) + 32767) / 65025;

      *dst = d.a << surface->format->Ashift |
             r   << surface->format->Rshift |
             g   << surface->format->Gshift |
             b   << surface->format->Bshift;
    }
  }
}

/* ------------------------------------------------------------------------
 * Fast-path helpers: UTF-8 decoding, ligature detection and legacy
 * rendering routines (avoid HarfBuzz when not needed).
 * --------------------------------------------------------------------- */

typedef struct {
  unsigned int x0, x1, y0, y1, loaded;
  int bitmap_left, bitmap_top;
  float xadvance;
} GlyphMetric;

typedef struct {
  SDL_Surface* surface;
  GlyphMetric metrics[GLYPHSET_SIZE];
  /* map entry i → glyph id returned by FreeType for quick lookup from
     HarfBuzz glyph indexes. */
  uint32_t gid_map[GLYPHSET_SIZE];
} GlyphSet;

/************************* Fonts *************************/

typedef struct RenFont {
  FT_Face face;
  FT_StreamRec stream;
  GlyphSet* sets[SUBPIXEL_BITMAPS_CACHED][MAX_LOADABLE_GLYPHSETS];
  float size, space_advance, tab_advance;
  unsigned short max_height, baseline, height;
  ERenFontAntialiasing antialiasing;
  ERenFontHinting hinting;
  unsigned char style;
  unsigned short underline_thickness;
  hb_font_t *hb_font;
  hb_face_t *hb_face;
  char path[];
} RenFont;

static RenFont* font_group_get_glyph(GlyphSet **set, GlyphMetric **metric,
                                     RenFont **fonts, uint32_t id,
                                     bool is_gid, int bitmap_index);

static double ren_draw_text_hb(RenSurface *rs, RenFont **fonts, const char *text,
                               size_t len, float x, int y, RenColor color);

static double hb_measure(RenFont **fonts, const char *text, size_t len,
                         bool want_x_offset, int *out_x_offset);

/* Decode one UTF-8 sequence, return pointer to next byte. */
static const char* utf8_to_codepoint(const char *p, unsigned *dst) {
  const unsigned char *up = (unsigned char*)p;
  unsigned res, n;
  switch (*p & 0xf0) {
    case 0xf0 :  res = *up & 0x07;  n = 3;  break;
    case 0xe0 :  res = *up & 0x0f;  n = 2;  break;
    case 0xd0 :
    case 0xc0 :  res = *up & 0x1f;  n = 1;  break;
    default   :  res = *up;         n = 0;  break;
  }
  while (n--) {
    res = (res << 6) | (*(++up) & 0x3f);
  }
  *dst = res;
  return (const char*)up + 1;
}

/* Conservative heuristic telling whether HarfBuzz shaping is required. */
static inline bool needs_harfbuzz(const char *s, size_t len) {
  const unsigned char *p = (const unsigned char *)s, *end = p + len;

  /* Any non-ASCII byte ⇒ must shape. */
  while (p < end)
    if (*p++ & 0x80)
      return true;

  /* Potential ASCII ligatures (==, ->, &&, fi …). */
  static const char suspect[] = "-=<>!/*&|+:.?^";
  p = (const unsigned char *)s;
  while (p + 1 < end) {
    if (strchr(suspect, p[0]) && strchr(suspect, p[1]))
      return true;
    if (p[0] == 'f' && (p[1] == 'i' || p[1] == 'l'))
      return true;
    ++p;
  }
  return false;
}

static int font_set_load_options(RenFont* font) {
  int load_target = font->antialiasing == FONT_ANTIALIASING_NONE ? FT_LOAD_TARGET_MONO
    : (font->hinting == FONT_HINTING_SLIGHT ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_NORMAL);
  int hinting = font->hinting == FONT_HINTING_NONE ? FT_LOAD_NO_HINTING : FT_LOAD_FORCE_AUTOHINT;
  return load_target | hinting;
}

static int font_set_render_options(RenFont* font) {
  if (font->antialiasing == FONT_ANTIALIASING_NONE)
    return FT_RENDER_MODE_MONO;
  if (font->antialiasing == FONT_ANTIALIASING_SUBPIXEL) {
    /**************************************************************************
     * Some conventional weights are:
     *  { 1./3., 2./9., 1./9. } => { 0x1d, 0x39, 0x54, 0x39, 0x1d };
     *  but previous versions of Lite XL, using the AGG library, was using:
     *  { 0.448, 0.184, 0.092 } => { 0x18, 0x2f, 0x72, 0x2f, 0x18 };
     *  where the values where manually adjusted on a LCD screen to look better.
     *
     *  Adam Harrison was using: { 0x10, 0x40, 0x70, 0x40, 0x10 } but they gave
     *  color fringes in my LCD screen, with my perception (Francesco).
     *
     *  The standard values of Freetype2 are:
     *  FT_LCD_FILTER_DEFAULT { 0x08 0x4D 0x56 0x4D 0x08 }
     *  FT_LCD_FILTER_LIGHT { 0x00 0x55 0x56 0x55 0x00 }
     */
    unsigned char weights[] = { 0x18, 0x2f, 0x72, 0x2f, 0x18 };
    switch (font->hinting) {
      case FONT_HINTING_NONE:   FT_Library_SetLcdFilter(library, FT_LCD_FILTER_NONE); break;
      case FONT_HINTING_SLIGHT:
      case FONT_HINTING_FULL: FT_Library_SetLcdFilterWeights(library, weights); break;
    }
    return FT_RENDER_MODE_LCD;
  } else {
    switch (font->hinting) {
      case FONT_HINTING_NONE:   return FT_RENDER_MODE_NORMAL; break;
      case FONT_HINTING_SLIGHT: return FT_RENDER_MODE_LIGHT; break;
      case FONT_HINTING_FULL:   return FT_RENDER_MODE_LIGHT; break;
    }
  }
  return 0;
}

static int font_set_style(FT_Outline* outline, int x_translation, unsigned char style) {
  FT_Outline_Translate(outline, x_translation, 0 );
  if (style & FONT_STYLE_SMOOTH)
    FT_Outline_Embolden(outline, 1 << 5);
  if (style & FONT_STYLE_BOLD)
    FT_Outline_EmboldenXY(outline, 1 << 5, 0);
  if (style & FONT_STYLE_ITALIC) {
    FT_Matrix matrix = { 1 << 16, 1 << 14, 0, 1 << 16 };
    FT_Outline_Transform(outline, &matrix);
  }
  return 0;
}

static void font_load_glyphset(RenFont* font, int idx) {
  unsigned int render_option = font_set_render_options(font), load_option = font_set_load_options(font);
  int bitmaps_cached = font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? SUBPIXEL_BITMAPS_CACHED : 1;
  unsigned int byte_width = font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? 3 : 1;
  for (int j = 0, pen_x = 0; j < bitmaps_cached; ++j) {
    GlyphSet* set = check_alloc(calloc(1, sizeof(GlyphSet)));
    font->sets[j][idx] = set;
    for (int i = 0; i < GLYPHSET_SIZE; ++i) {
      /* Use the real glyph id provided by FreeType for this Unicode
         code-point – glyph id ≠ code-point in most fonts. */
      int glyph_index = FT_Get_Char_Index(font->face, i + idx * GLYPHSET_SIZE);
      if (!glyph_index || FT_Load_Glyph(font->face, glyph_index, load_option | FT_LOAD_BITMAP_METRICS_ONLY)
        || font_set_style(&font->face->glyph->outline, j * (64 / SUBPIXEL_BITMAPS_CACHED), font->style) || FT_Render_Glyph(font->face->glyph, render_option)) {
        continue;
      }
      FT_GlyphSlot slot = font->face->glyph;
      unsigned int glyph_width = slot->bitmap.width / byte_width;
      if (font->antialiasing == FONT_ANTIALIASING_NONE)
        glyph_width *= 8;
      set->metrics[i] = (GlyphMetric){ pen_x, pen_x + glyph_width, 0, slot->bitmap.rows, true, slot->bitmap_left, slot->bitmap_top, (slot->advance.x + slot->lsb_delta - slot->rsb_delta) / 64.0f};
      /* Remember which glyph id lives in this slot so HarfBuzz glyph ids
         can be resolved back to cached metrics. */
      set->gid_map[i] = glyph_index;
      pen_x += glyph_width;
      font->max_height = slot->bitmap.rows > font->max_height ? slot->bitmap.rows : font->max_height;
      // In order to fix issues with monospacing; we need the unhinted xadvance; as FreeType doesn't correctly report the hinted advance for spaces on monospace fonts (like RobotoMono). See #843.
      if (!glyph_index || FT_Load_Glyph(font->face, glyph_index, (load_option | FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_HINTING) & ~FT_LOAD_FORCE_AUTOHINT)
        || font_set_style(&font->face->glyph->outline, j * (64 / SUBPIXEL_BITMAPS_CACHED), font->style) || FT_Render_Glyph(font->face->glyph, render_option)) {
        continue;
      }
      slot = font->face->glyph;
      set->metrics[i].xadvance = slot->advance.x / 64.0f;
    }
    if (pen_x == 0)
      continue;
    set->surface = check_alloc(SDL_CreateRGBSurface(0, pen_x, font->max_height, font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? 24 : 8, 0, 0, 0, 0));
    uint8_t* pixels = set->surface->pixels;
    for (int i = 0; i < GLYPHSET_SIZE; ++i) {
      int glyph_index = FT_Get_Char_Index(font->face, i + idx * GLYPHSET_SIZE);
      if (!glyph_index || FT_Load_Glyph(font->face, glyph_index, load_option))
        continue;
      FT_GlyphSlot slot = font->face->glyph;
      font_set_style(&slot->outline, (64 / bitmaps_cached) * j, font->style);
      if (FT_Render_Glyph(slot, render_option))
        continue;
      for (unsigned int line = 0; line < slot->bitmap.rows; ++line) {
        int target_offset = set->surface->pitch * line + set->metrics[i].x0 * byte_width;
        int source_offset = line * slot->bitmap.pitch;
        if (font->antialiasing == FONT_ANTIALIASING_NONE) {
          for (unsigned int column = 0; column < slot->bitmap.width; ++column) {
            int current_source_offset = source_offset + (column / 8);
            int source_pixel = slot->bitmap.buffer[current_source_offset];
            pixels[++target_offset] = ((source_pixel >> (7 - (column % 8))) & 0x1) * 0xFF;
          }
        } else
          memcpy(&pixels[target_offset], &slot->bitmap.buffer[source_offset], slot->bitmap.width);
      }
    }
  }
}

static GlyphSet* font_get_glyphset(RenFont* font, unsigned int codepoint, int subpixel_idx) {
  int idx = (codepoint / GLYPHSET_SIZE) % MAX_LOADABLE_GLYPHSETS;
  if (!font->sets[font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? subpixel_idx : 0][idx])
    font_load_glyphset(font, idx);
  return font->sets[font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? subpixel_idx : 0][idx];
}

/* ------------------------------------------------------------------------
 * Resolve a GlyphMetric entry starting from a glyph id (index in the font).
 * We use a simple linear search: GLYPHSET_SIZE is 256 so the cost is low.
 * --------------------------------------------------------------------- */
static GlyphMetric *glyph_metric_from_gid(GlyphSet *set, uint32_t gid) {
  /* linear scan over small fixed-size set (≤256) */
  for (int i = 0; i < GLYPHSET_SIZE; ++i)
    if (set->gid_map[i] == gid && set->metrics[i].loaded)
      return &set->metrics[i];
  return NULL;
}

/* ------------------------------------------------------------------------
 * Render a single glyph id into the destination surface when it is not
 * available in the cache (typical for GSUB ligatures).
 * Returns true when the glyph has been drawn.
 * --------------------------------------------------------------------- */
static bool draw_gid_direct(RenSurface *rs, RenFont *font, uint32_t gid,
                            double pen_x, int base_y,
                            const hb_glyph_position_t pos, RenColor color)
{
  SDL_Surface *surface = rs->surface;
  if (!surface) return false;

  /* Load & render the glyph directly by id */
  int load_opt   = font_set_load_options(font);
  int render_opt = font_set_render_options(font);
  if (FT_Load_Glyph(font->face, gid, load_opt | FT_LOAD_BITMAP_METRICS_ONLY))
    return false;
  FT_GlyphSlot slot = font->face->glyph;
  font_set_style(&slot->outline, 0, font->style);
  if (FT_Render_Glyph(slot, render_opt))
    return false;

  const int surface_scale = rs->scale;
  const int bpp  = surface->format->BytesPerPixel;
  uint8_t *dst_pixels = surface->pixels;

  int glyph_x = (int)floor(pen_x + pos.x_offset / 64.0) + slot->bitmap_left;
  int glyph_y = base_y + font->baseline * surface_scale
                - slot->bitmap_top   + pos.y_offset / 64;

  SDL_Rect clip;
  SDL_GetClipRect(surface, &clip);
  int clip_end_x = clip.x + clip.w, clip_end_y = clip.y + clip.h;

  int byte_width = font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? 3 : 1;
  const uint8_t *src_pixels = slot->bitmap.buffer;

  /* --------------------------------------------------------------------
   * Cache this glyph (gid) into the font atlas so the expensive
   * FT_Load_Glyph/Render sequence is executed only once.
   * ------------------------------------------------------------------ */
  int subpixel_idx = font->antialiasing == FONT_ANTIALIASING_SUBPIXEL
                       ? (int)(fmod(pen_x, 1.0) * SUBPIXEL_BITMAPS_CACHED)
                       : 0;
  GlyphSet *cache_set = font_get_glyphset(font, gid, subpixel_idx);
  GlyphMetric *cache_metric = &cache_set->metrics[gid % GLYPHSET_SIZE];

  if (!cache_metric->loaded) {
    unsigned int glyph_width = slot->bitmap.width / byte_width;
    if (font->antialiasing == FONT_ANTIALIASING_NONE)
      glyph_width *= 8;

    /* --- ensure surface exists and can host the new glyph ------------- */
    int old_width = cache_set->surface ? cache_set->surface->w : 0;
    int new_width = old_width + (int)glyph_width;

    if (!cache_set->surface || new_width > cache_set->surface->w) {
      SDL_Surface *old = cache_set->surface;
      cache_set->surface = check_alloc(SDL_CreateRGBSurface(
          0, new_width, font->max_height,
          font->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? 24 : 8,
          0, 0, 0, 0));
      if (old) {
        SDL_BlitSurface(old, NULL, cache_set->surface, NULL);
        SDL_FreeSurface(old);
      }
    }

    /* --- copy glyph bitmap into atlas surface ------------------------- */
    uint8_t *dst_base = cache_set->surface->pixels;
    for (unsigned int row = 0; row < slot->bitmap.rows; ++row) {
      int dst_off = cache_set->surface->pitch * row + old_width * byte_width;
      int src_off = row * slot->bitmap.pitch;
      if (font->antialiasing == FONT_ANTIALIASING_NONE) {
        for (unsigned int col = 0; col < slot->bitmap.width; ++col) {
          int src_byte = slot->bitmap.buffer[src_off + (col / 8)];
          dst_base[dst_off + col] =
              ((src_byte >> (7 - (col % 8))) & 0x1) * 0xFF;
        }
      } else {
        memcpy(&dst_base[dst_off],
               &slot->bitmap.buffer[src_off],
               slot->bitmap.width);
      }
    }

    /* --- record metric & mapping -------------------------------------- */
    *cache_metric = (GlyphMetric){
      old_width,
      old_width + (int)glyph_width,
      0,
      slot->bitmap.rows,
      true,
      slot->bitmap_left,
      slot->bitmap_top,
      (slot->advance.x) / 64.0f
    };
    cache_set->gid_map[gid % GLYPHSET_SIZE] = gid;
  }

  for (int row = 0; row < slot->bitmap.rows; ++row) {
    int dy = glyph_y + row;
    if (dy < clip.y || dy >= clip_end_y) continue;

    const uint8_t *src = &src_pixels[row * slot->bitmap.pitch];
    uint32_t *dst = (uint32_t *)&dst_pixels[surface->pitch * dy + glyph_x * bpp];

    for (int col = 0; col < (int)(slot->bitmap.width / byte_width); ++col) {
      int dx = glyph_x + col;
      if (dx < clip.x || dx >= clip_end_x) { ++dst; src += byte_width; continue; }

      uint8_t src_r, src_g, src_b;
      if (font->antialiasing == FONT_ANTIALIASING_SUBPIXEL) {
        src_r = src[0]; src_g = src[1]; src_b = src[2];
        src += 3;
      } else {
        src_r = src_g = src_b = *src;
        ++src;
      }
      uint8_t src_a = src_r;
      if (src_a == 0) { ++dst; continue; }

      uint32_t dst_pix = *dst;
      SDL_Color d = { (dst_pix & surface->format->Rmask)>>surface->format->Rshift,
                      (dst_pix & surface->format->Gmask)>>surface->format->Gshift,
                      (dst_pix & surface->format->Bmask)>>surface->format->Bshift,
                      (dst_pix & surface->format->Amask)>>surface->format->Ashift };

      uint32_t r = (color.r * src_r * color.a + d.r * (65025 - src_r * color.a) + 32767) / 65025;
      uint32_t g = (color.g * src_g * color.a + d.g * (65025 - src_g * color.a) + 32767) / 65025;
      uint32_t b = (color.b * src_b * color.a + d.b * (65025 - src_b * color.a) + 32767) / 65025;

      *dst++ = d.a << surface->format->Ashift |
               r   << surface->format->Rshift |
               g   << surface->format->Gshift |
               b   << surface->format->Bshift;
    }
  }
  return true;
}

/* ------------------------------------------------------------------------
 * Retrieve the glyph metric for either an Unicode code-point (legacy path)
 * or a HarfBuzz glyph id.  ‘is_gid’ tells which kind of identifier ‘id’ is.
 * --------------------------------------------------------------------- */
RenFont* font_group_get_glyph(GlyphSet **set, GlyphMetric **metric,
                              RenFont **fonts, uint32_t id,
                              bool is_gid, int bitmap_index) {
  if (!metric)
    return NULL;

  if (bitmap_index < 0)
    bitmap_index += SUBPIXEL_BITMAPS_CACHED;

  for (int i = 0; i < FONT_FALLBACK_MAX && fonts[i]; ++i) {
    if (is_gid) {
      /* We already have a glyph id produced by HarfBuzz.
         First try the glyph-set whose index is derived from the glyph
         id itself; if that fails, fall back to a full scan because the
         glyph could reside in the set that was created for a different
         Unicode code-point. */
      uint32_t gid = id;
      *set    = font_get_glyphset(fonts[i], gid, bitmap_index);
      *metric = glyph_metric_from_gid(*set, gid);

      if (!*metric) {
        /* Linear scan of all possible sets (256 max).  This is only done
           once per unseen glyph, so the cost is negligible in practice. */
        for (int s = 0; s < MAX_LOADABLE_GLYPHSETS && !*metric; ++s) {
          *set    = font_get_glyphset(fonts[i], s * GLYPHSET_SIZE, bitmap_index);
          *metric = glyph_metric_from_gid(*set, gid);
        }
      }

      if (*metric)
        return fonts[i];
    } else {
      /* Legacy path: translate code-point → glyph id via FreeType.      */
      uint32_t cp = id;
      *set    = font_get_glyphset(fonts[i], cp, bitmap_index);
      *metric = &(*set)->metrics[cp % GLYPHSET_SIZE];
      if ((*metric)->loaded || cp < 0xFF)
        return fonts[i];
    }
  }

  /* Legacy fallback to the white-square replacement character. */
  if (!is_gid && *metric && !(*metric)->loaded && id > 0xFF && id != 0x25A1)
    return font_group_get_glyph(set, metric, fonts, 0x25A1, false, bitmap_index);

  return fonts[0];
}

static void font_clear_glyph_cache(RenFont* font) {
  for (int i = 0; i < SUBPIXEL_BITMAPS_CACHED; ++i) {
    for (int j = 0; j < MAX_LOADABLE_GLYPHSETS; ++j) {
      if (font->sets[i][j]) {
        if (font->sets[i][j]->surface)
          SDL_FreeSurface(font->sets[i][j]->surface);
        free(font->sets[i][j]);
        font->sets[i][j] = NULL;
      }
    }
  }
}

// based on https://github.com/libsdl-org/SDL_ttf/blob/2a094959055fba09f7deed6e1ffeb986188982ae/SDL_ttf.c#L1735
static unsigned long font_file_read(FT_Stream stream, unsigned long offset, unsigned char *buffer, unsigned long count) {
  uint64_t amount;
  SDL_RWops *file = (SDL_RWops *) stream->descriptor.pointer;
  SDL_RWseek(file, (int) offset, RW_SEEK_SET);
  if (count == 0)
    return 0;
  amount = SDL_RWread(file, buffer, sizeof(char), count);
  if (amount <= 0)
    return 0;
  return (unsigned long) amount;
}

static void font_file_close(FT_Stream stream) {
  if (stream && stream->descriptor.pointer) {
    SDL_RWclose((SDL_RWops *) stream->descriptor.pointer);
    stream->descriptor.pointer = NULL;
  }
}

RenFont* ren_font_load(RenWindow *window_renderer, const char* path, float size, ERenFontAntialiasing antialiasing, ERenFontHinting hinting, unsigned char style) {
  RenFont *font = NULL;
  FT_Face face = NULL;

  SDL_RWops *file = SDL_RWFromFile(path, "rb");
  if (!file)
    goto rwops_failure;

  int len = strlen(path);
  font = check_alloc(calloc(1, sizeof(RenFont) + len + 1));
  font->stream.read = font_file_read;
  font->stream.close = font_file_close;
  font->stream.descriptor.pointer = file;
  font->stream.pos = 0;
  font->stream.size = (unsigned long) SDL_RWsize(file);

  if (FT_Open_Face(library, &(FT_Open_Args){ .flags = FT_OPEN_STREAM, .stream = &font->stream }, 0, &face))
    goto failure;

  const int surface_scale = window_renderer->scale;
  const float scaled_size = roundf(size * surface_scale);
  if (FT_Set_Pixel_Sizes(face, 0, scaled_size))
    goto failure;

  strcpy(font->path, path);
  font->face = face;
  font->size = scaled_size / surface_scale;
  font->height = (short)((face->height / (float)face->units_per_EM) * font->size);
  font->baseline = (short)((face->ascender / (float)face->units_per_EM) * font->size);
  font->antialiasing = antialiasing;
  font->hinting = hinting;
  font->style = style;
  font->hb_face = hb_ft_face_create_referenced(face);
  font->hb_font = hb_ft_font_create_referenced(face);

  if(FT_IS_SCALABLE(face))
    font->underline_thickness = (unsigned short)((face->underline_thickness / (float)face->units_per_EM) * font->size);
  if(!font->underline_thickness)
    font->underline_thickness = ceil((double) font->height / 14.0);

  if (FT_Load_Char(face, ' ', font_set_load_options(font)))
    goto failure;

  font->space_advance = face->glyph->advance.x / 64.0f;
  font->tab_advance = font->space_advance * 2;
  return font;

failure:
  if (face)
    FT_Done_Face(face);
  if (font)
    free(font);
  return NULL;

rwops_failure:
  if (file)
    SDL_RWclose(file);
  return NULL;
}

RenFont* ren_font_copy(RenWindow *window_renderer, RenFont* font, float size, ERenFontAntialiasing antialiasing, ERenFontHinting hinting, int style) {
  antialiasing = antialiasing == -1 ? font->antialiasing : antialiasing;
  hinting = hinting == -1 ? font->hinting : hinting;
  style = style == -1 ? font->style : style;

  return ren_font_load(window_renderer, font->path, size, antialiasing, hinting, style);
}

const char* ren_font_get_path(RenFont *font) {
  return font->path;
}

int ren_font_get_scale(RenFont *font) {
  /* Normally we may extract two scaling factor along x and y axis but,
     given the way we create fonts they should be always the same so we
     just look at the size along y. */
  int scaled_size_y = font->face->size->metrics.y_ppem;
  float surface_scale_y = (float)scaled_size_y / font->size;
  return (int) roundf(surface_scale_y);
}

void ren_font_free(RenFont* font) {
  font_clear_glyph_cache(font);
  if (font->hb_font)  hb_font_destroy(font->hb_font);
  if (font->hb_face)  hb_face_destroy(font->hb_face);
  FT_Done_Face(font->face);
  free(font);
}

void ren_font_group_set_tab_size(RenFont **fonts, int n) {
  unsigned int tab_index = '\t' % GLYPHSET_SIZE;
  for (int j = 0; j < FONT_FALLBACK_MAX && fonts[j]; ++j) {
    for (int i = 0; i < (fonts[j]->antialiasing == FONT_ANTIALIASING_SUBPIXEL ? SUBPIXEL_BITMAPS_CACHED : 1); ++i)
      font_get_glyphset(fonts[j], '\t', i)->metrics[tab_index].xadvance = fonts[j]->space_advance * n;
  }
}

int ren_font_group_get_tab_size(RenFont **fonts) {
  unsigned int tab_index = '\t' % GLYPHSET_SIZE;
  float advance = font_get_glyphset(fonts[0], '\t', 0)->metrics[tab_index].xadvance;
  if (fonts[0]->space_advance) {
    advance /= fonts[0]->space_advance;
  }
  return advance;
}

float ren_font_group_get_size(RenFont **fonts) {
  return fonts[0]->size;
}

void ren_font_group_set_size(RenWindow *window_renderer, RenFont **fonts, float size) {
  const int surface_scale = window_renderer->scale;
  for (int i = 0; i < FONT_FALLBACK_MAX && fonts[i]; ++i) {
    font_clear_glyph_cache(fonts[i]);
    FT_Face face = fonts[i]->face;
    FT_Set_Pixel_Sizes(face, 0, (int)(size*surface_scale));
    hb_ft_font_changed(fonts[i]->hb_font);
    fonts[i]->size = size;
    fonts[i]->height = (short)((face->height / (float)face->units_per_EM) * size);
    fonts[i]->baseline = (short)((face->ascender / (float)face->units_per_EM) * size);
    FT_Load_Char(face, ' ', font_set_load_options(fonts[i]));
    fonts[i]->space_advance = face->glyph->advance.x / 64.0f;
    fonts[i]->tab_advance = fonts[i]->space_advance * 2;
  }
}

int ren_font_group_get_height(RenFont **fonts) {
  return fonts[0]->height;
}

double fast_measure_width(RenFont **fonts, const char *text, size_t len, int *x_offset) {
  double width = 0;
  const char* end = text + len;
  GlyphMetric* metric = NULL; GlyphSet* set = NULL;
  bool set_x_offset = x_offset == NULL;
  int surface_scale = -1;
  while (text < end) {
    unsigned int codepoint;
    text = utf8_to_codepoint(text, &codepoint);
    RenFont* font = font_group_get_glyph(&set, &metric, fonts, codepoint, false, 0);
    /* we assume font is not NULL here because the previous function always return
       a non-null font except is a null metric pointer is provided. */
    assert(font != NULL);
    if (surface_scale < 0) {
      surface_scale = ren_font_get_scale(font);
    }
    if (!metric)
      break;
    width += metric->xadvance ? metric->xadvance : fonts[0]->space_advance;
    if (!set_x_offset) {
      set_x_offset = true;
      *x_offset = metric->bitmap_left; // TODO: should this be scaled by the surface scale?
    }
  }
  if (!set_x_offset) {
    *x_offset = 0;
  }
  return width / surface_scale;
}

double ren_draw_text(RenSurface *rs, RenFont **fonts, const char *text, size_t len, float x, int y, RenColor color) {
  if (needs_harfbuzz(text, len))
    return ren_draw_text_hb(rs, fonts, text, len, x, y, color);

  SDL_Surface *surface = rs->surface;
  /* Fast path: no ligatures, no HarfBuzz shaping required */
  SDL_Rect clip;
  SDL_GetClipRect(surface, &clip);

  const int surface_scale = rs->scale;
  double pen_x = x * surface_scale;
  y *= surface_scale;
  const char* end = text + len;
  int clip_end_x = clip.x + clip.w;

  RenFont* last = NULL;
  double last_pen_x = x;
  bool underline = fonts[0]->style & FONT_STYLE_UNDERLINE;
  bool strikethrough = fonts[0]->style & FONT_STYLE_STRIKETHROUGH;

  while (text < end) {
    unsigned int codepoint;
    text = utf8_to_codepoint(text, &codepoint);
    GlyphSet* set = NULL; GlyphMetric* metric = NULL;
    RenFont* font = font_group_get_glyph(&set, &metric, fonts, codepoint, false,
                                         (int)(fmod(pen_x, 1.0) * SUBPIXEL_BITMAPS_CACHED));
    if (!metric)
      break;
    int start_x = floor(pen_x) + metric->bitmap_left;
    int end_x = (metric->x1 - metric->x0) + start_x;
    if (!metric->loaded && codepoint > 0xFF)
      ren_draw_rect(rs, (RenRect){ start_x + 1, y, font->space_advance - 1, ren_font_group_get_height(fonts) }, color);
    if (set->surface && color.a > 0 && end_x >= clip.x && start_x < clip_end_x) {
      const bool subpx = font->antialiasing == FONT_ANTIALIASING_SUBPIXEL;

      const uint8_t *src =
          (const uint8_t *)set->surface->pixels +
          metric->y0 * set->surface->pitch +
          metric->x0 * (subpx ? 3 : 1);

      blit_glyph(rs->surface, src, set->surface->pitch,
                metric->x1 - metric->x0, metric->y1 - metric->y0,
                start_x, y - metric->bitmap_top + fonts[0]->baseline*surface_scale + metric->y0,
                &clip, color, subpx);

    }

    float adv = metric->xadvance ? metric->xadvance : font->space_advance;

    if(!last) last = font;
    else if(font != last || text == end) {
      double local_pen_x = text == end ? pen_x + adv : pen_x;
      if (underline)
        ren_draw_rect(rs, (RenRect){last_pen_x, y / surface_scale + last->height - 1, (local_pen_x - last_pen_x) / surface_scale, last->underline_thickness * surface_scale}, color);
      if (strikethrough)
        ren_draw_rect(rs, (RenRect){last_pen_x, y / surface_scale + last->height / 2, (local_pen_x - last_pen_x) / surface_scale, last->underline_thickness * surface_scale}, color);
      last = font;
      last_pen_x = pen_x;
    }

    pen_x += adv;
  }
  return pen_x / surface_scale;
}

/* ------------------------------------------------------------------------
 * Measure a UTF-8 text run with HarfBuzz and optionally return the bitmap
 * left bearing (x_offset) of the first rendered glyph.
 * Re-uses a static hb_buffer_t to minimise heap churn.
 * --------------------------------------------------------------------- */
double hb_measure(RenFont **fonts, const char *text, size_t len,
                         bool want_x_offset, int *out_x_offset)
{
  /* Static buffer reused across calls (harfbuzz is re-entrant). */
  hb_buffer_t *buf = hb_measure_buf;
  if (!buf) {
    buf = hb_buffer_create();
    hb_measure_buf = buf;
  }
  hb_buffer_reset(buf);

  hb_buffer_add_utf8(buf, text, len, 0, len);
  hb_buffer_guess_segment_properties(buf);
  hb_shape(fonts[0]->hb_font, buf, NULL, 0);

  unsigned n      = 0;
  const hb_glyph_info_t     *info = hb_buffer_get_glyph_infos(buf, &n);
  const hb_glyph_position_t *pos  = hb_buffer_get_glyph_positions(buf, &n);

  double w = 0.0;
  if (want_x_offset)
    *out_x_offset = 0;

  if (n) {
    /* first glyph → compute bitmap_left as x_offset */
    if (want_x_offset) {
      GlyphSet   *set    = NULL;
      GlyphMetric*metric = NULL;
      font_group_get_glyph(&set, &metric, fonts,
                           info[0].codepoint, true, 0);
      if (metric)
        *out_x_offset = metric->bitmap_left / ren_font_get_scale(fonts[0]);
    }
    for (unsigned i = 0; i < n; ++i)
      w += pos[i].x_advance / 64.0;
  }
  return w / ren_font_get_scale(fonts[0]);
}

double ren_font_group_get_width(RenFont **fonts, const char *text, size_t len, int *x_offset) {
  /* Fast ASCII path that bypasses HarfBuzz when no ligatures are possible */
  if (!needs_harfbuzz(text, len))
    return fast_measure_width(fonts, text, len, x_offset);

  int dummy;
  return hb_measure(fonts, text, len,
                    x_offset != NULL,
                    x_offset ? x_offset : &dummy);
}

double ren_draw_text_hb(RenSurface *rs, RenFont **fonts, const char *text,
                        size_t len, float x, int y, RenColor color)
{
  SDL_Surface *surface = rs->surface;
  if (!surface) return 0;

  const int surface_scale = rs->scale;
  SDL_Rect clip;
  SDL_GetClipRect(surface, &clip);
  int clip_end_x = clip.x + clip.w, clip_end_y = clip.y + clip.h;

  double pen_x = x * surface_scale;
  int base_y   = y * surface_scale;

  /* -------- HarfBuzz shaping -------- */
  if (!hb_draw_buf)
    hb_draw_buf = hb_buffer_create();
  hb_buffer_reset(hb_draw_buf);
  hb_buffer_add_utf8(hb_draw_buf, text, len, 0, len);
  hb_buffer_guess_segment_properties(hb_draw_buf);
  hb_shape(fonts[0]->hb_font, hb_draw_buf, NULL, 0);

  unsigned glyph_count;
  const hb_glyph_info_t     *info = hb_buffer_get_glyph_infos(hb_draw_buf, &glyph_count);
  const hb_glyph_position_t *pos  = hb_buffer_get_glyph_positions(hb_draw_buf, &glyph_count);

  for (unsigned i = 0; i < glyph_count; ++i) {
    unsigned gid = info[i].codepoint;
    int subpixel_idx = (int)(fmod(pen_x, 1.0) * SUBPIXEL_BITMAPS_CACHED);

    GlyphSet   *set    = NULL;
    GlyphMetric*metric = NULL;
    font_group_get_glyph(&set, &metric, fonts, gid, true, subpixel_idx);
    if (!metric || !set || !set->surface) {
      /* Try to draw the glyph directly – needed for ligatures not cached */
      draw_gid_direct(rs, fonts[0], gid, pen_x, base_y, pos[i], color);
      pen_x += pos[i].x_advance / 64.0;
      continue;
    }

    int glyph_x = (int)floor(pen_x + pos[i].x_offset / 64.0) + metric->bitmap_left;
    int glyph_y = base_y + fonts[0]->baseline * surface_scale
                  - metric->bitmap_top + pos[i].y_offset / 64;

    /* quick clip */
    if (glyph_x >= clip_end_x || glyph_y >= clip_end_y ||
        glyph_x + (metric->x1 - metric->x0) <= clip.x ||
        glyph_y + (metric->y1 - metric->y0) <= clip.y) {
      pen_x += pos[i].x_advance / 64.0;
      continue;
    }

    /* ---- actual blit ---- */
    const bool is_subpixel = fonts[0]->antialiasing == FONT_ANTIALIASING_SUBPIXEL;
    const int  byte_width  = is_subpixel ? 3 : 1;

    /* pointer to the first byte of the glyph bitmap we want       */
    const uint8_t *glyph_src =
      (const uint8_t *)set->surface->pixels + metric->y0 * set->surface->pitch +
      metric->x0 * byte_width;

    const int glyph_w = metric->x1 - metric->x0;
    const int glyph_h = metric->y1 - metric->y0;

    blit_glyph(surface, glyph_src, set->surface->pitch,
               glyph_w, glyph_h, glyph_x, glyph_y,
               &clip, color, is_subpixel);

    pen_x += pos[i].x_advance / 64.0;
  }

  return pen_x / surface_scale;
}

/******************* Rectangles **********************/
void ren_draw_rect(RenSurface *rs, RenRect rect, RenColor color) {
  SDL_Surface *surface = rs->surface;
  if (color.a == 0 || !surface) { return; }
  const int surface_scale = rs->scale;

  SDL_Rect dest_rect = { rect.x * surface_scale,
                         rect.y * surface_scale,
                         rect.width * surface_scale,
                         rect.height * surface_scale };

  if (color.a == 0xff) {
    uint32_t translated = SDL_MapRGB(surface->format, color.r, color.g, color.b);
    SDL_FillRect(surface, &dest_rect, translated);
  } else {
    // Seems like SDL doesn't handle clipping as we expect when using
    // scaled blitting, so we "clip" manually.
    SDL_Rect clip;
    SDL_GetClipRect(surface, &clip);
    if (!SDL_IntersectRect(&clip, &dest_rect, &dest_rect)) return;

    uint32_t *pixel = (uint32_t *)draw_rect_surface->pixels;
    *pixel = SDL_MapRGBA(draw_rect_surface->format, color.r, color.g, color.b, color.a);
    SDL_BlitScaled(draw_rect_surface, NULL, surface, &dest_rect);
  }
}

/*************** Window Management ****************/
void ren_free_window_resources(RenWindow *window_renderer) {
  renwin_free(window_renderer);
  SDL_FreeSurface(draw_rect_surface);
  if (hb_draw_buf) {
    hb_buffer_destroy(hb_draw_buf);
    hb_draw_buf = NULL;
  }
  if (hb_measure_buf) {
    hb_buffer_destroy(hb_measure_buf);
    hb_measure_buf = NULL;
  }
}

// TODO remove global and return RenWindow*
void ren_init(SDL_Window *win) {
  assert(win);
  int error = FT_Init_FreeType( &library );
  if ( error ) {
    fprintf(stderr, "internal font error when starting the application\n");
    return;
  }
  window_renderer.window = win;
  renwin_init_renderer(&window_renderer);
  // renwin_clip_to_surface(&window_renderer);
  draw_rect_surface = SDL_CreateRGBSurface(0, 1, 1, 32,
                       0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
}


void ren_resize_window(RenWindow *window_renderer) {
  renwin_resize_window(window_renderer);
}


void ren_set_clip_rect(RenSurface *rs, RenRect rect) {
  if (!rs->surface) return;
  RenRect sr = scaled_rect(rect, rs->scale);
  SDL_SetClipRect(rs->surface, &(SDL_Rect){.x = sr.x, .y = sr.y, .w = sr.width, .h = sr.height});
}

