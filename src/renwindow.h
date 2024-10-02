#include <SDL.h>
#include "renderer.h"
#include "rensurface.h"

struct RenWindow {
  SDL_Window *window;
  SDL_Renderer *renderer;
  int w_pixels, h_pixels;
  int scale;
};
typedef struct RenWindow RenWindow;

void renwin_get_size(RenWindow *ren, int *w, int *h);
void renwin_init_renderer(RenWindow *ren);
void renwin_resize_window(RenWindow *ren);
void renwin_present(RenWindow *ren);
void renwin_render_surface(RenWindow *ren, RenSurface *rs, int x, int y);
void renwin_set_clip_rect(RenWindow *ren, const SDL_Rect *r);
void renwin_free(RenWindow *ren);
void renwin_render_fill_rect(RenWindow *ren, SDL_Rect *rect, SDL_Color color);

