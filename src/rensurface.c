#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "rensurface.h"

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

  if (r_scaled.w > 0 && r_scaled.h > 0) {
    rs->surface = SDL_CreateSurface(r_scaled.w, r_scaled.h, SDL_PIXELFORMAT_BGRA32);
    rs->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, r_scaled.w, r_scaled.h);
    if (!rs->surface || !rs->texture) {
      fprintf(stderr, "Error creating surface or texture: %s", SDL_GetError());
      exit(1);
    }
  }
  rs->scale = scale;
}


void rensurf_update_rect(RenSurface *rs, const SDL_FRect *r) {
  if (!rs->surface) return;
  const float scale = rs->scale;
  SDL_Rect r_scaled = ren_scaled_rect(r, scale);
  int32_t *pixels = ((int32_t *) rs->surface->pixels) + r_scaled.x + rs->surface->w * r_scaled.y;
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

