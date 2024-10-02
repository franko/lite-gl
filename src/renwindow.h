#include <SDL.h>
#include "renderer.h"
#include "rensurface.h"

struct RenWindow {
  SDL_Window *window;
  SDL_Renderer *renderer;
  RenSurface rensurface;
  int scale;
};
typedef struct RenWindow RenWindow;

void renwin_init_surface(RenWindow *ren);
void renwin_clip_to_surface(RenWindow *ren);
void renwin_resize_surface(RenWindow *ren);
void renwin_show_window(RenWindow *ren);
void renwin_render_surface(RenWindow *ren);
void renwin_free(RenWindow *ren);
RenSurface renwin_get_surface(RenWindow *ren);

