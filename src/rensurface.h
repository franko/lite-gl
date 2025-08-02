#ifndef RENSURFACE_H
#define RENSURFACE_H

#include "rencache.h"

#include <SDL3/SDL.h>

struct RenSurface {
  SDL_Surface *surface;
  SDL_Texture *texture;
  RenCache rencache;
  float w, h; // logical width and height in points
  float scale;
};

extern void rensurf_init(RenSurface *rs, SDL_Renderer *renderer, const SDL_FRect *rect, float scale);
extern void rensurf_update_rect(RenSurface *rs, const SDL_FRect *rect);
extern void rensurf_free(RenSurface *rs);
extern SDL_FRect rensurf_get_rect(RenSurface *rs);
extern void rensurf_get_size(RenSurface *rs, float *w, float *h);

static inline void rensurf_get_pixels_size(RenSurface *rs, int *w, int *h) {
  *w = (rs->surface ? rs->surface->w : 0);
  *h = (rs->surface ? rs->surface->h : 0);
}

#endif

