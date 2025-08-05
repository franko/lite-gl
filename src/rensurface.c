#include <stdio.h>
#include <stdlib.h>
#include "rensurface.h"

void rensurf_init(RenSurface *rs, SDL_Renderer *renderer, int x, int y, int w, int h) {
  /* Note that w and h here should always be in pixels and obtained from
     a call to SDL_GL_GetDrawableSize(). */
  rs->surface = NULL;
  rs->texture = NULL;
  rencache_init(&rs->rencache, x, y);

  if (w > 0 && h > 0) {
    rs->surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_BGRA32);
    rs->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!rs->surface || !rs->texture) {
      fprintf(stderr, "Error creating surface or texture: %s", SDL_GetError());
      exit(1);
    }
  }
}


void rensurf_update_rect(RenSurface *rs, const RenRect *r) {
  if (!rs->surface) return;
  int32_t *pixels = ((int32_t *) rs->surface->pixels) + r->x + rs->surface->w * r->y;
  SDL_UpdateTexture(rs->texture, &(SDL_Rect){ r->x, r->y, r->width, r->height }, pixels, rs->surface->w * 4);
}

void rensurf_free(RenSurface *rs) {
  if (rs->surface) {
    SDL_DestroyTexture(rs->texture);
    SDL_DestroySurface(rs->surface);
  }
}

void rensurf_get_rect(RenSurface *rs, int *x, int *y, int *w, int *h) {
  *x = rs->rencache.x_origin;
  *y = rs->rencache.y_origin;
  *w = (rs->surface ? rs->surface->w : 0);
  *h = (rs->surface ? rs->surface->h : 0);
}

void rensurf_get_size(RenSurface *rs, int *w, int *h) {
  *w = (rs->surface ? rs->surface->w : 0);
  *h = (rs->surface ? rs->surface->h : 0);
}

