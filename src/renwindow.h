#include <stdbool.h>

#include <SDL3/SDL.h>
#include "renderer.h"
#include "rensurface.h"

struct RenWindow {
  SDL_Window *window;
  SDL_Renderer *renderer;
  int w_pixels, h_pixels;
  float scale; // scale factors between pixels and windows coordinates.
  bool get_windows_coordinates; // On macOS SDL_GetWindowSize and mouve events gets points coordinates but
                               // not on Windows or Linux, where we gets pixels coordinates.
};
typedef struct RenWindow RenWindow;

void renwin_get_size(RenWindow *ren, int *w, int *h);
void renwin_init_renderer(RenWindow *ren);
void renwin_resize_window(RenWindow *ren);
void renwin_present(RenWindow *ren);
void renwin_render_surface(RenWindow *ren, RenSurface *rs, float x, float y);
void renwin_set_clip_rect(RenWindow *ren, const SDL_Rect *r);
void renwin_free(RenWindow *ren);
void renwin_render_fill_rect(RenWindow *ren, SDL_Rect *rect, SDL_Color color);

static inline float renwin_events_scale_factor(RenWindow *ren) {
  return (ren->get_windows_coordinates ? 1.0f : ren->scale);
}
