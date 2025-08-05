#ifndef RENSURFACE_H
#define RENSURFACE_H

#include "rencache.h"

#include <SDL3/SDL.h>

struct RenSurface {
  SDL_Surface *surface;
  SDL_Texture *texture;
  RenCache rencache;
};

extern void rensurf_init(RenSurface *rs, SDL_Renderer *renderer, int x, int y, int w, int h);
extern void rensurf_update_rect(RenSurface *rs, const RenRect *rect);
extern void rensurf_free(RenSurface *rs);
extern void rensurf_get_rect(RenSurface *rs, int *x, int *y, int *w, int *h);
extern void rensurf_get_size(RenSurface *rs, int *w, int *h);

#endif

