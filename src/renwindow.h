#include <SDL3/SDL.h>
#include "renderer.h"
#include "rensurface.h"

struct RenWindow {
  SDL_Window *window;
  SDL_Renderer *renderer;
  int w_pixels, h_pixels;
  int hard_scale;
  float soft_scale;
};
typedef struct RenWindow RenWindow;

void renwin_get_size_pixels(RenWindow *ren, int *w, int *h);
void renwin_get_size_window_coord(RenWindow *ren, float *w, float *h);
void renwin_init_renderer(RenWindow *ren);
void renwin_resize_window(RenWindow *ren);
void renwin_present(RenWindow *ren);
void renwin_render_surface(RenWindow *ren, RenSurface *rs, int x, int y);
void renwin_set_clip_rect(RenWindow *ren, const SDL_Rect *r);
void renwin_free(RenWindow *ren);
void renwin_render_fill_rect(RenWindow *ren, SDL_FRect *rect, SDL_Color color);

static inline float renwin_events_scale_factor(RenWindow *ren) {
  return ren->soft_scale;
}

static inline float renwin_global_scale(RenWindow *ren) {
  return ren->soft_scale * ren->hard_scale;
}

