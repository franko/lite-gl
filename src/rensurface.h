#ifndef RENSURFACE_H
#define RENSURFACE_H

#include <SDL.h>

typedef struct {
  SDL_Surface *surface;
  SDL_Texture *texture;
  int scale;
} RenSurface;

typedef struct { int x, y, width, height; } RenRect;

extern void rensurf_setup(RenSurface *rs, SDL_Renderer *renderer, int w, int h, int scale);
extern void rensurf_update_rects(RenSurface *rs, RenRect *rects, int count);
extern void rensurf_free(RenSurface *rs);

#endif

