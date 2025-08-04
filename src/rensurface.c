#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "rensurface.h"

// #define DEBUG_TEXTURE_PAINT 1
#define DEBUG_TEXTURE_SIZE 1

void rensurf_init(RenSurface *rs, SDL_Renderer *renderer, const SDL_FRect *rect, float scale) {
  /* Note that w and h here should always be in pixels and obtained from
     a call to SDL_GL_GetDrawableSize(). */
  rs->surface = NULL;
  rs->texture = NULL;
  rs->w = rect->w;
  rs->h = rect->h;
  rencache_init(&rs->rencache, rect->x, rect->y);

  /* These below are the coordinates at which the SDL_Surface/Texture is going
   * to be effectively rendered. We want to be pixel exact about that.
   * The rounding in the function renwin_render_surface must be done in the
   * same way. */
  /* Now we are creating an SDL_Surface and texture whose size must be an *integer* number
   * of pixels. There we get the rounding so that the equality:
   * rs->w * rs->scale == rs->surface->w
   * May no longer hold exactly. For example:
   * scale = 1.25, rs->w = 10.0, surface->w = 13 */
  const SDL_Rect r_scaled = ren_scaled_rect(rect, scale);

  fprintf(stderr, "DEBUG: rensurf_init (%g,%g,%g,%g) rounded (%d,%d,%d,%d)\n", rect->x, rect->y, rect->w, rect->h, r_scaled.x, r_scaled.y, r_scaled.w, r_scaled.h);

  if (r_scaled.w > 0 && r_scaled.h > 0) {
    rs->surface = SDL_CreateSurface(r_scaled.w, r_scaled.h, SDL_PIXELFORMAT_BGRA32);
    rs->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, r_scaled.w, r_scaled.h);

#ifdef DEBUG_TEXTURE_PAINT
    SDL_SetRenderTarget(renderer, rs->texture);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, NULL);
#endif
    if (!rs->surface || !rs->texture) {
      fprintf(stderr, "Error creating surface or texture: %s", SDL_GetError());
      exit(1);
    }
  }
  rs->scale = scale;
}


void rensurf_update_rect(RenSurface *rs, const SDL_FRect *r) {
  if (!rs->surface) return;

// #ifdef DEBUG_TEXTURE_SIZE
//   Sint64 tex_w, tex_h; // Use Sint64 as returned by SDL_GetNumberProperty
//   SDL_PropertiesID props = SDL_GetTextureProperties(rs->texture);
//   tex_w = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0);
//   tex_h = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0);
// #endif

#if 0
  SDL_FRect placed_rect = { x_origin, y_origin, r->w, r->h };
  SDL_Rect r_scaled = ren_scaled_rect(&placed_rect, rs->scale);
  r_scaled.x -= lroundf(x_origin * rs->scale);
  r_scaled.y -= lroundf(y_origin * rs->scale);
#else
  SDL_Rect r_scaled = ren_scaled_rect(r, rs->scale);
#endif

#ifdef DEBUG_TEXTURE_SIZE
  if (true || r_scaled.w != rs->surface->w || r_scaled.h != rs->surface->h) {
    fprintf(stderr, 
            "DEBUG: rensurf_update_rect | rect: (%.1f,%.1f,%.1f,%.1f) scaled: (%d,%d,%d,%d) | surface: %dx%d\n",
            r->x, r->y, r->w, r->h, 
            r_scaled.x, r_scaled.y, r_scaled.w, r_scaled.h,
            rs->surface->w, rs->surface->h);
  }
#endif

  int32_t *pixels = ((int32_t *) rs->surface->pixels) + r_scaled.x + rs->surface->w * r_scaled.y;

  // The call to SDL_UpdateTexture remains the same
  SDL_UpdateTexture(rs->texture, &r_scaled, pixels, rs->surface->pitch);
}

void rensurf_free(RenSurface *rs) {
  if (rs->surface) {
    SDL_DestroyTexture(rs->texture);
    SDL_DestroySurface(rs->surface);
  }
}

SDL_FRect rensurf_get_rect(RenSurface *rs) {
  return (SDL_FRect){
    rs->rencache.x_origin,
    rs->rencache.y_origin,
    (rs->surface ? rs->w : 0),
    (rs->surface ? rs->h : 0)
  };
}

void rensurf_get_size(RenSurface *rs, float *w, float *h) {
  *w = (rs->surface ? rs->w : 0);
  *h = (rs->surface ? rs->h : 0);
}

