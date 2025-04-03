#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_OUTLINE_H
#include FT_SYSTEM_H

// HarfBuzz Headers
#include <hb.h>
#include <hb-ft.h> // HarfBuzz FreeType integration

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
static hb_buffer_t *hb_buffer; // Global HarfBuzz buffer

// Predefined ligature sequences
static const char* ligature_strings[] = {
  "<=", ">=", "==", "!=", "->", "&&", "||", ">>", "<<", "//", "/*", "*/", "++", "--"
  // Add more ligatures here if needed
};
static const int num_ligatures = sizeof(ligature_strings) / sizeof(ligature_strings[0]);


// draw_rect_surface is used as a 1x1 surface to simplify ren_draw_rect with blending
static SDL_Surface *draw_rect_surface;

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

/************************* Fonts *************************/

typedef struct {
  unsigned int x0, x1, y0, y1, loaded;
  int bitmap_left, bitmap_top;
  float xadvance;
} GlyphMetric;

typedef struct {
  SDL_Surface* surface;
  GlyphMetric metrics[GLYPHSET_SIZE];
} GlyphSet;

typedef struct RenFont {
  FT_Face face;
  hb_font_t *hb_font; // HarfBuzz font object
  FT_StreamRec stream;
  GlyphSet* sets[SUBPIXEL_BITMAPS_CACHED][MAX_LOADABLE_GLYPHSETS];
  float size, space_advance, tab_advance;
  unsigned short max_height, baseline, height;
  ERenFontAntialiasing antialiasing;
  ERenFontHinting hinting;
  unsigned char style;
  unsigned short underline_thickness;
  char path[];
} RenFont;

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

static RenFont* font_group_get_glyph(GlyphSet** set, GlyphMetric** metric, RenFont** fonts, unsigned int codepoint, int bitmap_index) {
  if (!metric) {
    return NULL;
  }
  if (bitmap_index < 0)
    bitmap_index += SUBPIXEL_BITMAPS_CACHED;
  for (int i = 0; i < FONT_FALLBACK_MAX && fonts[i]; ++i) {
    *set = font_get_glyphset(fonts[i], codepoint, bitmap_index);
    *metric = &(*set)->metrics[codepoint % GLYPHSET_SIZE];
    if ((*metric)->loaded || codepoint < 0xFF)
      return fonts[i];
  }
  if (*metric && !(*metric)->loaded && codepoint > 0xFF && codepoint != 0x25A1)
    return font_group_get_glyph(set, metric, fonts, 0x25A1, bitmap_index);
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
  // Note: HarfBuzz might hold a reference to the FT_Face, which holds the stream.
  // Ensure hb_font_destroy is called before FT_Done_Face, which might trigger this close.
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

  // Create HarfBuzz font
  font->hb_font = hb_ft_font_create_referenced(face);
  if (!font->hb_font) {
    fprintf(stderr, "Warning: Could not create HarfBuzz font for %s\n", path);
    // Continue without HarfBuzz support for this font? Or fail harder?
    // Let's allow it to continue for now, ligatures just won't work.
  }


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
  if (font && font->hb_font) {
    hb_font_destroy(font->hb_font); // Destroys hb_font, decreases ft_face ref count
    font->hb_font = NULL;
  }
  if (face)
    FT_Done_Face(face); // May free face if ref count is 0
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
  if (!font) return;
  font_clear_glyph_cache(font);
  if (font->hb_font) {
    hb_font_destroy(font->hb_font); // Decrements ft_face ref count
    font->hb_font = NULL;
  }
  FT_Done_Face(font->face); // Decrements ft_face ref count again
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
    fonts[i]->size = size;
    fonts[i]->height = (short)((face->height / (float)face->units_per_EM) * size);
    fonts[i]->baseline = (short)((face->ascender / (float)face->units_per_EM) * size);
    FT_Load_Char(face, ' ', font_set_load_options(fonts[i]));
    fonts[i]->space_advance = face->glyph->advance.x / 64.0f;
    fonts[i]->tab_advance = fonts[i]->space_advance * 2;
    // Notify HarfBuzz that font parameters might have changed
    if (fonts[i]->hb_font) {
      hb_ft_font_changed(fonts[i]->hb_font);
    }
  }
}

int ren_font_group_get_height(RenFont **fonts) {
  return fonts[0]->height;
}


// Helper to check for and process a ligature at the current text position
static const char* process_ligature(RenFont **fonts, const char *text, const char *end, double *advance_out, int *glyph_count_out, hb_glyph_info_t **glyph_info_out, hb_glyph_position_t **glyph_pos_out) {
    if (!hb_buffer || !fonts[0]->hb_font) {
        return NULL; // HarfBuzz not available or not initialized for this font
    }

    const char* best_match_end = NULL;
    const char* best_ligature_str = NULL;

    // Find the longest matching ligature string at the current position
    for (int i = 0; i < num_ligatures; ++i) {
        const char* ligature = ligature_strings[i];
        size_t lig_len = strlen(ligature);
        if ((size_t)(end - text) >= lig_len && strncmp(text, ligature, lig_len) == 0) {
            if (!best_match_end || lig_len > strlen(best_ligature_str)) {
                 best_match_end = text + lig_len;
                 best_ligature_str = ligature;
            }
        }
    }

    if (best_match_end) {
        // Found a ligature, shape it with HarfBuzz
        hb_buffer_reset(hb_buffer);
        hb_buffer_add_utf8(hb_buffer, best_ligature_str, -1, 0, -1);
        hb_buffer_set_direction(hb_buffer, HB_DIRECTION_LTR);
        hb_buffer_set_script(hb_buffer, HB_SCRIPT_LATIN); // Assuming Latin script for common ligatures
        hb_buffer_set_language(hb_buffer, hb_language_from_string("en", -1));

        hb_shape(fonts[0]->hb_font, hb_buffer, NULL, 0); // Use primary font for shaping

        unsigned int hb_glyph_count;
        *glyph_info_out = hb_buffer_get_glyph_infos(hb_buffer, &hb_glyph_count);
        *glyph_pos_out = hb_buffer_get_glyph_positions(hb_buffer, &hb_glyph_count);
        *glyph_count_out = (int)hb_glyph_count;

        // Calculate total advance width from HarfBuzz positions
        *advance_out = 0;
        for (unsigned int i = 0; i < hb_glyph_count; ++i) {
            *advance_out += (*glyph_pos_out)[i].x_advance / 64.0;
        }
        return best_match_end; // Return pointer to the end of the processed ligature
    }

    return NULL; // No ligature found at this position
}


double ren_font_group_get_width(RenFont **fonts, const char *text, size_t len, int *x_offset) {
  double width = 0;
  const char* end = text + len;
  const char* current_text = text;
  GlyphMetric* metric = NULL; GlyphSet* set = NULL;
  bool set_x_offset = x_offset == NULL;
  int surface_scale = -1;

  while (current_text < end) {
    double ligature_advance = 0;
    int ligature_glyph_count = 0;
    hb_glyph_info_t *ligature_info = NULL;
    hb_glyph_position_t *ligature_pos = NULL;

    // Try to process a ligature
    const char* next_text = process_ligature(fonts, current_text, end, &ligature_advance, &ligature_glyph_count, &ligature_info, &ligature_pos);

    if (next_text) {
      // Ligature processed by HarfBuzz
      if (surface_scale < 0 && fonts[0]) {
          surface_scale = ren_font_get_scale(fonts[0]);
      }
      width += ligature_advance;
      current_text = next_text;

      // Set x_offset based on the first glyph of the ligature (if needed)
      if (!set_x_offset && ligature_glyph_count > 0) {
          set_x_offset = true;
          // Need to load the glyph to get bitmap_left, similar to non-ligature path
          RenFont* font = fonts[0]; // Assume primary font for metrics
          FT_UInt glyph_index = ligature_info[0].codepoint;
          if (FT_Load_Glyph(font->face, glyph_index, font_set_load_options(font) | FT_LOAD_BITMAP_METRICS_ONLY) == 0) {
              *x_offset = font->face->glyph->bitmap_left; // TODO: Scale?
          } else {
              *x_offset = 0; // Fallback
          }
      }

    } else {
      // Process a single character using FreeType
      unsigned int codepoint;
      next_text = utf8_to_codepoint(current_text, &codepoint);
      RenFont* font = font_group_get_glyph(&set, &metric, fonts, codepoint, 0);
      assert(font != NULL); // Should always return a font

      if (surface_scale < 0) {
        surface_scale = ren_font_get_scale(font);
      }
      if (!metric) { // Should not happen if font_group_get_glyph works correctly
        current_text = next_text;
        continue;
      }

      width += metric->xadvance ? metric->xadvance : fonts[0]->space_advance;

      if (!set_x_offset) {
        set_x_offset = true;
        *x_offset = metric->bitmap_left; // TODO: should this be scaled by the surface scale?
      }
      current_text = next_text;
    }
  }

  if (x_offset && !set_x_offset) { // Handle empty string case
    *x_offset = 0;
  }

  return width / surface_scale;
}


// Helper to draw a single glyph bitmap (used by both standard and HarfBuzz paths)
static void draw_glyph_bitmap(RenSurface *rs, RenFont* font, FT_Bitmap *bitmap, int x, int y, RenColor color) {
    SDL_Surface *surface = rs->surface;
    if (!surface || color.a == 0 || bitmap->width == 0 || bitmap->rows == 0) return;

    SDL_Rect clip;
    SDL_GetClipRect(surface, &clip);

    int surface_scale = rs->scale; // Assumes font scale matches surface scale
    int bytes_per_pixel = surface->format->BytesPerPixel;
    uint8_t* destination_pixels = surface->pixels;
    int clip_end_x = clip.x + clip.w, clip_end_y = clip.y + clip.h;

    unsigned int src_bytes_per_pixel = 1;
    if (font->antialiasing == FONT_ANTIALIASING_SUBPIXEL) {
        src_bytes_per_pixel = 3;
    } else if (font->antialiasing == FONT_ANTIALIASING_NONE) {
        // Mono bitmaps are handled differently below
    }

    for (unsigned int line = 0; line < bitmap->rows; ++line) {
        int target_y = y + line;
        if (target_y < clip.y || target_y >= clip_end_y) continue;

        int start_x = x;
        int end_x = x + (font->antialiasing == FONT_ANTIALIASING_NONE ? bitmap->width * 8 : bitmap->width / src_bytes_per_pixel); // Adjust width for mono/subpixel
        int glyph_start_col = 0; // Column index within the source bitmap

        if (end_x <= clip.x || start_x >= clip_end_x) continue;

        // Manual clipping for the row
        if (start_x < clip.x) {
            glyph_start_col += (clip.x - start_x);
            start_x = clip.x;
        }
        if (end_x > clip_end_x) {
            end_x = clip_end_x;
        }

        uint32_t* destination_pixel = (uint32_t*)&(destination_pixels[surface->pitch * target_y + start_x * bytes_per_pixel]);
        uint8_t* source_pixel_row = &bitmap->buffer[line * bitmap->pitch];

        for (int current_x = start_x; current_x < end_x; ++current_x, ++glyph_start_col) {
            uint32_t destination_color = *destination_pixel;
            SDL_Color dst = { (destination_color & surface->format->Rmask) >> surface->format->Rshift, (destination_color & surface->format->Gmask) >> surface->format->Gshift, (destination_color & surface->format->Bmask) >> surface->format->Bshift, (destination_color & surface->format->Amask) >> surface->format->Ashift };
            SDL_Color src = {0, 0, 0, 0xFF}; // Default alpha
            unsigned int r, g, b;

            if (font->antialiasing == FONT_ANTIALIASING_NONE) {
                int source_byte_idx = glyph_start_col / 8;
                int source_bit_idx = 7 - (glyph_start_col % 8);
                uint8_t source_val = (source_pixel_row[source_byte_idx] >> source_bit_idx) & 0x1;
                src.r = src.g = src.b = source_val * 0xFF;
            } else if (font->antialiasing == FONT_ANTIALIASING_SUBPIXEL) {
                uint8_t* p = &source_pixel_row[glyph_start_col * src_bytes_per_pixel];
                src.r = p[0];
                src.g = p[1];
                src.b = p[2];
            } else { // Grayscale
                src.r = src.g = src.b = source_pixel_row[glyph_start_col * src_bytes_per_pixel];
            }

            // Alpha Blending (Premultiplied Alpha)
            r = (color.r * src.r * color.a + dst.r * (65025 - src.r * color.a) + 32767) / 65025;
            g = (color.g * src.g * color.a + dst.g * (65025 - src.g * color.a) + 32767) / 65025;
            b = (color.b * src.b * color.a + dst.b * (65025 - src.b * color.a) + 32767) / 65025;

            *destination_pixel++ = dst.a << surface->format->Ashift | r << surface->format->Rshift | g << surface->format->Gshift | b << surface->format->Bshift;
        }
    }
}


double ren_draw_text(RenSurface *rs, RenFont **fonts, const char *text, size_t len, float x, int y, RenColor color) {
  if (!rs->surface || color.a == 0) { return x; } // Nothing to draw or fully transparent

  const int surface_scale = rs->scale;
  double pen_x = x * surface_scale; // Pen position in surface pixels
  double pen_y = 0; // For vertical positioning if needed by HarfBuzz (usually 0 for LTR)
  int base_y = y * surface_scale; // Baseline y in surface pixels

  const char* current_text = text;
  const char* end = text + len;

  RenFont* last_font_for_line = NULL; // Track font for underline/strikethrough segments
  double segment_start_pen_x = pen_x; // Track start x for underline/strikethrough

  bool underline = fonts[0]->style & FONT_STYLE_UNDERLINE;
  bool strikethrough = fonts[0]->style & FONT_STYLE_STRIKETHROUGH;

  while (current_text < end) {
    double ligature_advance = 0;
    int ligature_glyph_count = 0;
    hb_glyph_info_t *ligature_info = NULL;
    hb_glyph_position_t *ligature_pos = NULL;
    RenFont* current_font = fonts[0]; // Use primary font by default

    // Try to process a ligature
    const char* next_text = process_ligature(fonts, current_text, end, &ligature_advance, &ligature_glyph_count, &ligature_info, &ligature_pos);

    if (next_text && ligature_glyph_count > 0) {
      // --- Render Ligature using HarfBuzz results ---
      current_font = fonts[0]; // Assume primary font shaped the ligature

      // Apply underline/strikethrough for the previous segment if font changes
      if (last_font_for_line && current_font != last_font_for_line) {
          if (underline) ren_draw_rect(rs, (RenRect){segment_start_pen_x / surface_scale, y + last_font_for_line->height - 1, (pen_x - segment_start_pen_x) / surface_scale, last_font_for_line->underline_thickness}, color);
          if (strikethrough) ren_draw_rect(rs, (RenRect){segment_start_pen_x / surface_scale, y + last_font_for_line->height / 2, (pen_x - segment_start_pen_x) / surface_scale, last_font_for_line->underline_thickness}, color);
          segment_start_pen_x = pen_x;
      }
      last_font_for_line = current_font;


      for (int i = 0; i < ligature_glyph_count; ++i) {
        FT_UInt glyph_index = ligature_info[i].codepoint;
        hb_glyph_position_t *pos = &ligature_pos[i];

        // Load glyph data first (without rendering)
        FT_Error ft_error = FT_Load_Glyph(current_font->face, glyph_index, font_set_load_options(current_font));
        if (ft_error) {
           fprintf(stderr, "Warning: Could not load glyph index %u for ligature\n", glyph_index);
           // Advance pen position even if glyph fails
           pen_x += pos->x_advance / 64.0;
           pen_y += pos->y_advance / 64.0;
           continue;
        }

        // Apply style transformations (e.g., bold, italic) to the outline
        // We need to determine the correct subpixel shift based on the *current* pen position,
        // similar to how font_load_glyphset does it, although HarfBuzz handles positioning.
        // For simplicity here, let's use 0 shift, as HarfBuzz provides the precise offsets.
        // If subpixel positioning artifacts appear, this might need refinement.
        font_set_style(&current_font->face->glyph->outline, 0, current_font->style);

        // Now render the styled glyph
        ft_error = FT_Render_Glyph(current_font->face->glyph, font_set_render_options(current_font));
        if (ft_error) {
          fprintf(stderr, "Warning: Could not render glyph index %u for ligature\n", glyph_index);
          // Advance pen position even if glyph fails
          pen_x += pos->x_advance / 64.0;
          pen_y += pos->y_advance / 64.0;
          continue;
        }

        FT_GlyphSlot slot = current_font->face->glyph;

        // Calculate draw position using HarfBuzz offsets and FreeType bearings
        // pen_x/y = current pen position (bottom-left for FT?)
        // draw_x = pen_x + hb_x_offset + ft_bitmap_left
        // draw_y = baseline_y - ft_bitmap_top + hb_y_offset + pen_y
        int draw_x = round(pen_x + (pos->x_offset / 64.0) + slot->bitmap_left);
        int draw_y = round(base_y + current_font->baseline * surface_scale - slot->bitmap_top + (pos->y_offset / 64.0) + pen_y);

        // Draw the bitmap using the helper function
        draw_glyph_bitmap(rs, current_font, &slot->bitmap, draw_x, draw_y, color);

        // Advance the pen position using HarfBuzz's advances
        pen_x += pos->x_advance / 64.0;
        pen_y += pos->y_advance / 64.0;
      }
      current_text = next_text; // Move past the processed ligature

    } else {
      // --- Render Single Character using existing FreeType path ---
      unsigned int codepoint;
      next_text = utf8_to_codepoint(current_text, &codepoint);
      GlyphSet* set = NULL; GlyphMetric* metric = NULL;

      // Determine subpixel index based on fractional pen position
      int subpixel_idx = (int)(fmod(pen_x, 1.0) * SUBPIXEL_BITMAPS_CACHED);
      current_font = font_group_get_glyph(&set, &metric, fonts, codepoint, subpixel_idx);

      if (!metric) { // Should not happen
          current_text = next_text;
          continue;
      }

      // Apply underline/strikethrough for the previous segment if font changes
      if (last_font_for_line && current_font != last_font_for_line) {
          if (underline) ren_draw_rect(rs, (RenRect){segment_start_pen_x / surface_scale, y + last_font_for_line->height - 1, (pen_x - segment_start_pen_x) / surface_scale, last_font_for_line->underline_thickness}, color);
          if (strikethrough) ren_draw_rect(rs, (RenRect){segment_start_pen_x / surface_scale, y + last_font_for_line->height / 2, (pen_x - segment_start_pen_x) / surface_scale, last_font_for_line->underline_thickness}, color);
          segment_start_pen_x = pen_x;
      }
      last_font_for_line = current_font;


      // Calculate draw position (relative to baseline)
      int draw_x = floor(pen_x) + metric->bitmap_left;
      int draw_y = base_y + current_font->baseline * surface_scale - metric->bitmap_top;

      // Draw placeholder for missing glyphs > 255
      if (!metric->loaded && codepoint > 0xFF) {
          ren_draw_rect(rs, (RenRect){(float)draw_x / surface_scale + 1, (float)y, (current_font->space_advance - 1) / surface_scale, (float)ren_font_group_get_height(fonts)}, color);
      }

      // Render the glyph from the pre-rendered GlyphSet surface
      if (set && set->surface && metric->loaded && color.a > 0) {
          SDL_Rect src_rect = { metric->x0, metric->y0, metric->x1 - metric->x0, metric->y1 - metric->y0 };
          SDL_Rect dst_rect = { draw_x, draw_y, src_rect.w, src_rect.h };

          // Manual Blitting with Alpha Blend (similar logic to draw_glyph_bitmap but uses GlyphSet)
          // This part needs careful review and integration with the draw_glyph_bitmap logic
          // For simplicity, let's reuse the core blending logic if possible, but source is different.
          // *** This section needs careful implementation matching draw_glyph_bitmap's blending ***
          // Simplified placeholder: SDL_BlitSurface(set->surface, &src_rect, rs->surface, &dst_rect); // Needs alpha blending!

          // Replicating blending logic here (needs testing and refinement):
          SDL_Surface *source_surface = set->surface;
          SDL_Surface *dest_surface = rs->surface;
          SDL_Rect clip; SDL_GetClipRect(dest_surface, &clip);
          if (SDL_IntersectRect(&dst_rect, &clip, &dst_rect)) {
              uint8_t* source_pixels = source_surface->pixels;
              uint8_t* dest_pixels = dest_surface->pixels;
              int src_bpp = source_surface->format->BytesPerPixel; // Should be 1 (gray) or 3 (subpixel)
              int dst_bpp = dest_surface->format->BytesPerPixel;

              for (int row = 0; row < dst_rect.h; ++row) {
                  uint8_t* src_p = source_pixels + (src_rect.y + row) * source_surface->pitch + src_rect.x * src_bpp;
                  uint32_t* dst_p = (uint32_t*)(dest_pixels + (dst_rect.y + row) * dest_surface->pitch + dst_rect.x * dst_bpp);

                  for (int col = 0; col < dst_rect.w; ++col) {
                      uint32_t destination_color = *dst_p;
                      SDL_Color dst = { (destination_color & dest_surface->format->Rmask) >> dest_surface->format->Rshift, (destination_color & dest_surface->format->Gmask) >> dest_surface->format->Gshift, (destination_color & dest_surface->format->Bmask) >> dest_surface->format->Bshift, (destination_color & dest_surface->format->Amask) >> dest_surface->format->Ashift };
                      SDL_Color src = {0, 0, 0, 0xFF};
                      unsigned int r, g, b;

                      if (current_font->antialiasing == FONT_ANTIALIASING_SUBPIXEL) {
                          src.r = *(src_p++); src.g = *(src_p++); src.b = *(src_p++);
                      } else { // Grayscale or Mono (GlyphSet stores mono as grayscale 0/255)
                          src.r = src.g = src.b = *(src_p++);
                      }

                      r = (color.r * src.r * color.a + dst.r * (65025 - src.r * color.a) + 32767) / 65025;
                      g = (color.g * src.g * color.a + dst.g * (65025 - src.g * color.a) + 32767) / 65025;
                      b = (color.b * src.b * color.a + dst.b * (65025 - src.b * color.a) + 32767) / 65025;
                      *dst_p++ = dst.a << dest_surface->format->Ashift | r << dest_surface->format->Rshift | g << dest_surface->format->Gshift | b << dest_surface->format->Bshift;
                  }
              }
          }
      }

      // Advance pen position using FreeType metrics
      pen_x += metric->xadvance ? metric->xadvance : current_font->space_advance;
      current_text = next_text;
    }
  }

  // Apply underline/strikethrough for the final segment
  if (last_font_for_line) {
      if (underline) ren_draw_rect(rs, (RenRect){segment_start_pen_x / surface_scale, y + last_font_for_line->height - 1, (pen_x - segment_start_pen_x) / surface_scale, last_font_for_line->underline_thickness}, color);
      if (strikethrough) ren_draw_rect(rs, (RenRect){segment_start_pen_x / surface_scale, y + last_font_for_line->height / 2, (pen_x - segment_start_pen_x) / surface_scale, last_font_for_line->underline_thickness}, color);
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
  if (hb_buffer) {
    hb_buffer_destroy(hb_buffer);
    hb_buffer = NULL;
  }
  FT_Done_FreeType(library); // Free FreeType library
}

// TODO remove global and return RenWindow*
void ren_init(SDL_Window *win) {
  assert(win);
  int error = FT_Init_FreeType(&library);
  if (error) {
    fprintf(stderr, "Fatal: Could not initialize FreeType library\n");
    exit(EXIT_FAILURE); // Cannot proceed without FreeType
  }

  // Initialize HarfBuzz buffer
  hb_buffer = hb_buffer_create();
   if (!hb_buffer) {
    fprintf(stderr, "Fatal: Could not create HarfBuzz buffer\n");
    FT_Done_FreeType(library);
    exit(EXIT_FAILURE); // Cannot proceed without HarfBuzz buffer
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

