#include <assert.h>
#include <stdio.h>
#include "renwindow.h"

/* Query surface size and returns the scale factor. */
static float get_window_soft_scale(RenWindow *ren) {
#if defined(__APPLE__)
  return 1.0f;
#else
  return SDL_GetWindowDisplayScale(ren->window);
#endif
}

static int get_window_hard_scale(RenWindow *ren, int *w_pixels, int *h_pixels) {
#if defined(__APPLE__)
  int w_points, h_points;
  SDL_GetWindowSizeInPixels(ren->window, w_pixels, h_pixels);
  SDL_GetWindowSize(ren->window, &w_points, &h_points);
  /* On macOS the ratio pixel/point will always be an integer and it is the same
   * along the x and the y axis. */
  assert(*w_pixels % w_points == 0 && *h_pixels % h_points == 0 && *w_pixels / w_points == *h_pixels / h_points);
  return *w_pixels / w_points;
#else
  return 1;
#endif
}

void renwin_get_size_pixels(RenWindow *ren, int *w, int *h) {
  SDL_GetWindowSizeInPixels(ren->window, w, h);
}

void renwin_get_size_window_coord(RenWindow *ren, float *w, float *h) {
  int w_pixels, h_pixels;
  SDL_GetWindowSizeInPixels(ren->window, &w_pixels, &h_pixels);
  if (ren->soft_scale != 1.0f) {
    *w = w_pixels / ren->soft_scale;
    *h = h_pixels / ren->soft_scale;
  }
}


void renwin_init_renderer(RenWindow *ren) {
  /* We assume here "ren" is zero-initialized */
  ren->renderer = SDL_CreateRenderer(ren->window, NULL);
  ren->hard_scale = get_window_hard_scale(ren, &ren->w_pixels, &ren->h_pixels);
  ren->soft_scale = get_window_soft_scale(ren);
}

void renwin_resize_window(RenWindow *ren) {
  ren->hard_scale = get_window_hard_scale(ren, &ren->w_pixels, &ren->h_pixels);
  ren->soft_scale = get_window_soft_scale(ren);
}

void renwin_render_surface(RenWindow *ren, RenSurface *rs, int x, int y) {
  /* Width and height of the surface, in pixels. */
  int w, h;
  rensurf_get_size_pixels(rs, &w, &h);
  const SDL_FRect dst = { x, y, w, h };
  SDL_RenderTexture(ren->renderer, rs->texture, NULL, &dst);
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
  // Note that r may be NULL below
  SDL_SetRenderClipRect(ren->renderer, r);
}

void renwin_free(RenWindow *ren) {
  SDL_DestroyWindow(ren->window);
  ren->window = NULL;
  SDL_DestroyRenderer(ren->renderer);
}

void renwin_render_fill_rect(RenWindow *ren, SDL_Rect *r, SDL_Color color) {
  SDL_SetRenderDrawColor(ren->renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(ren->renderer, r);
}

