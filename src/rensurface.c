#include "rensurface.h"

void rensurf_init(RenSurface *rs, SDL_Renderer *renderer, int x, int y, int w, int h, int scale) {
  /* Note that w and h here should always be in pixels and obtained from
     a call to SDL_GL_GetDrawableSize(). */
  rs->surface = NULL;
  rs->texture = NULL;
  rencache_init(&rs->rencache, x, y);

  if (w > 0 && h > 0) {
    const int w_scaled = w * scale, h_scaled = h * scale;
    rs->surface = SDL_CreateRGBSurfaceWithFormat(0, w_scaled, h_scaled, 32, SDL_PIXELFORMAT_BGRA32);
    rs->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, w_scaled, h_scaled);
    if (!rs->surface || !rs->texture) {
      fprintf(stderr, "Error creating surface or texture: %s", SDL_GetError());
      exit(1);
    }
  }
  rs->scale = scale;
}


void rensurf_update_rects(RenSurface *rs, RenRect *rects, int count) {
  if (!rs->surface) return;
  const int scale = rs->scale;
  for (int i = 0; i < count; i++) {
    const RenRect *r = &rects[i];
    const int x = scale * r->x, y = scale * r->y;
    const int w = scale * r->width, h = scale * r->height;
    const SDL_Rect sr = {.x = x, .y = y, .w = w, .h = h};
    int32_t *pixels = ((int32_t *) rs->surface->pixels) + x + rs->surface->w * y;
    SDL_UpdateTexture(rs->texture, &sr, pixels, rs->surface->w * 4);
  }
}

void rensurf_free(RenSurface *rs) {
  if (rs->surface) {
    SDL_DestroyTexture(rs->texture);
    SDL_FreeSurface(rs->surface);
  }
}

void rensurf_get_rect(RenSurface *rs, int *x, int *y, int *w, int *h) {
  *x = rs->rencache.x_origin;
  *y = rs->rencache.y_origin;
  *w = (rs->surface ? rs->surface->w : 0) / rs->scale;
  *h = (rs->surface ? rs->surface->h : 0) / rs->scale;
}

void rensurf_get_size(RenSurface *rs, int *w, int *h) {
  *w = (rs->surface ? rs->surface->w : 0) / rs->scale;
  *h = (rs->surface ? rs->surface->h : 0) / rs->scale;
}

