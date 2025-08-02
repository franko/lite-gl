#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "renwindow.h"

/* Query surface size and returns the scale factor. */
static float get_window_display_scale(RenWindow *ren, int *w_pixels, int *h_pixels) {
#if defined(__APPLE__)
  int w_points, h_points;
  SDL_GetWindowSizeInPixels(ren->window, w_pixels, h_pixels);
  SDL_GetWindowSize(ren->window, &w_points, &h_points);
  /* On macOS the ratio pixel/point will always be an integer and it is the same
   * along the x and the y axis. */
  assert(*w_pixels % w_points == 0 && *h_pixels % h_points == 0 && *w_pixels / w_points == *h_pixels / h_points);
  return *w_pixels / w_points;
#else
  return SDL_GetWindowDisplayScale(ren->window);
#endif
}


void renwin_get_size(RenWindow *ren, int *w, int *h) {
  SDL_GetWindowSize(ren->window, w, h);
}


void renwin_init_renderer(RenWindow *ren) {
  /* We assume here "ren" is zero-initialized */
  ren->renderer = SDL_CreateRenderer(ren->window, NULL);
  ren->scale = get_window_display_scale(ren, &ren->w_pixels, &ren->h_pixels);
#if defined(__APPLE__)
  /* On macOS the mouse events and call to SDL_GetWindowSize() will return window coordinates
   * so we don't need to apply the scale factor on them. On Windows and Linux instead they
   * returns pixel coordinates and we need to scale them back. */
  ren->get_windows_coordinates = true;
#else
  ren->get_windows_coordinates = false;
#endif
}

void renwin_resize_window(RenWindow *ren) {
  ren->scale = get_window_display_scale(ren, &ren->w_pixels, &ren->h_pixels);
}

void renwin_render_surface(RenWindow *ren, RenSurface *rs, float x, float y) {
  const float scale = rs->scale;
  int w, h;
  rensurf_get_pixels_size(rs, &w, &h);
  const SDL_FRect dst = { lroundf(x * scale), lroundf(y * scale), w, h };
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
  if (r) {
    const float scale = ren->scale;
    SDL_Rect r_scaled = { r->x * scale, r->y * scale, r->w * scale, r->h * scale };
    SDL_SetRenderClipRect(ren->renderer, &r_scaled);
  } else {
    SDL_SetRenderClipRect(ren->renderer, NULL);
  }
}

void renwin_free(RenWindow *ren) {
  SDL_DestroyWindow(ren->window);
  ren->window = NULL;
  SDL_DestroyRenderer(ren->renderer);
}

void renwin_render_fill_rect(RenWindow *ren, SDL_Rect *r, SDL_Color color) {
  const float scale = ren->scale;
  SDL_FRect r_scaled = { r->x * scale, r->y * scale, r->w * scale, r->h * scale };
  SDL_SetRenderDrawColor(ren->renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(ren->renderer, &r_scaled);
}
