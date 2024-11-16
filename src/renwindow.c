#include <assert.h>
#include <stdio.h>
#include "renwindow.h"

/* Query surface size and returns the scale factor. */
static int get_window_pixels_size(RenWindow *ren, int *w_pixels, int *h_pixels) {
  int w_points, h_points;
  SDL_GL_GetDrawableSize(ren->window, w_pixels, h_pixels);
  SDL_GetWindowSize(ren->window, &w_points, &h_points);
  /* We consider that the ratio pixel/point will always be an integer and
     it is the same along the x and the y axis. */
  assert(*w_pixels % w_points == 0 && *h_pixels % h_points == 0 && *w_pixels / w_points == *h_pixels / h_points);
  return *w_pixels / w_points;
}


void renwin_get_size(RenWindow *ren, int *w, int *h) {
  SDL_GetWindowSize(ren->window, w, h);
}


void renwin_init_renderer(RenWindow *ren) {
  /* We assume here "ren" is zero-initialized */
  ren->renderer = SDL_CreateRenderer(ren->window, -1, SDL_RENDERER_PRESENTVSYNC);
  ren->scale = get_window_pixels_size(ren, &ren->w_pixels, &ren->h_pixels);
}

void renwin_resize_window(RenWindow *ren) {
  ren->scale = get_window_pixels_size(ren, &ren->w_pixels, &ren->h_pixels);
}

void renwin_render_surface(RenWindow *ren, RenSurface *rs, int x, int y) {
  /* Width and height of the surface, in pixels. */
  int w, h;
  rensurf_get_size(rs, &w, &h);
  const SDL_Rect dst = { x * rs->scale, y * rs->scale, w * rs->scale, h * rs->scale };
  SDL_RenderCopy(ren->renderer, rs->texture, NULL, &dst);
}

void renwin_present(RenWindow *ren) {
  static bool initial_frame = true;
  if (initial_frame) {
    SDL_ShowWindow(ren->window);
    initial_frame = false;
  }
  SDL_RenderPresent(ren->renderer);
}

void renwin_set_clip_rect(RenWindow *ren, const SDL_Rect *r) {
  const int scale = ren->scale;
  if (r) {
    SDL_Rect r_scaled = {r->x * scale, r->y * scale, r->w * scale, r->h * scale};
    SDL_RenderSetClipRect(ren->renderer, &r_scaled);
  } else {
    SDL_RenderSetClipRect(ren->renderer, NULL);
  }
}

void renwin_free(RenWindow *ren) {
  SDL_DestroyWindow(ren->window);
  ren->window = NULL;
  SDL_DestroyRenderer(ren->renderer);
}

void renwin_render_fill_rect(RenWindow *ren, SDL_Rect *r, SDL_Color color) {
  const int scale = ren->scale;
  SDL_Rect r_scaled = {r->x * scale, r->y * scale, r->w * scale, r->h * scale};
  SDL_SetRenderDrawColor(ren->renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(ren->renderer, &r_scaled);
}
