#include <assert.h>
#include <stdio.h>
#include "renwindow.h"

static int query_surface_scale(RenWindow *ren) {
  int w_pixels, h_pixels;
  int w_points, h_points;
  SDL_GL_GetDrawableSize(ren->window, &w_pixels, &h_pixels);
  SDL_GetWindowSize(ren->window, &w_points, &h_points);
  /* We consider that the ratio pixel/point will always be an integer and
     it is the same along the x and the y axis. */
  assert(w_pixels % w_points == 0 && h_pixels % h_points == 0 && w_pixels / w_points == h_pixels / h_points);
  return w_pixels / w_points;
}

static void setup_renderer(RenWindow *ren) {
  if (!ren->renderer) {
    ren->renderer = SDL_CreateRenderer(ren->window, -1, 0);
  }
}


void renwin_init_surface(RenWindow *ren) {
  int w, h;
  setup_renderer(ren);
  SDL_GL_GetDrawableSize(ren->window, &w, &h);
  rensurf_setup(&ren->rensurface, ren->renderer, w, h, query_surface_scale(ren));
}


void renwin_clip_to_surface(RenWindow *ren) {
  SDL_SetClipRect(renwin_get_surface(ren).surface, NULL);
}


RenSurface renwin_get_surface(RenWindow *ren) {
  return ren->rensurface;
}

void renwin_resize_surface(UNUSED RenWindow *ren) {
  int new_w, new_h;
  SDL_GL_GetDrawableSize(ren->window, &new_w, &new_h);
  /* Note that (w, h) may differ from (new_w, new_h) on retina displays. */
  if (new_w != ren->rensurface.surface->w || new_h != ren->rensurface.surface->h) {
    rensurf_setup(&ren->rensurface, ren->renderer, new_w, new_h, query_surface_scale(ren));
    renwin_clip_to_surface(ren);
  }
}

void renwin_show_window(RenWindow *ren) {
  SDL_ShowWindow(ren->window);
}

void renwin_render_surface(RenWindow *ren) {
  SDL_RenderCopy(ren->renderer, ren->rensurface.texture, NULL, NULL);
  SDL_RenderPresent(ren->renderer);
}

void renwin_free(RenWindow *ren) {
  SDL_DestroyWindow(ren->window);
  ren->window = NULL;
  SDL_DestroyRenderer(ren->renderer);
  rensurf_free(&ren->rensurface);
}
