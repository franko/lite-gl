renderer.show_debug(rensurface, active)
  take now a RenSurface as the first argument

renderer.get_size()
  Returns now the size of the top-level window and returns a
  third argument, the scale.
  This latter is currently not needed.

renderer.begin_frame(rensurface)
  Now it takes a RenSurface

renderer.end_frame(rensurface)
  Now it takes a RenSurface. It does no longer clear font refs.
  It does update the dirty rects in the surface's texture but do no
  longer copy or present the surface using the renderer.

renderer.clear_font_refs()
  NEW FUNCTION, clear the font references. No longer done after an end_frame.

renderer.set_clip_rect(rensurface, x, y, w, h)
  Now it takes a RenSurface.

renderer.draw_rect(rensurface, x, y, w, h)
  Now it takes a RenSurface.

renderer.draw_text(rensurface, font, text, x, y, color)
  Now it takes a RenSurface.

renderer.present_surface(rensurface, x, y)
  NEW FUNCTION, Present the surface on the window at th given (x, y) coordinates.




void  rencache_update_rects(RenCache* cache, RenSurface *rs)
  ADDED function

ren_update_rects
  NO LONGEST EXISTS

void  rencache_update_window(RenCache* cache, RenWindow *renwindow)
  REMOVED

void ren_present_surface(RenWindow *window_renderer, RenSurface *rs, int x, int y)
  ADDED function
  Calls renwin_render_surface to "present" the surface using the renderer.
  Actually calls SDL_RenderCopy, SDL_RenderPresent


void renwin_render_surface(RenWindow *ren, RenSurface *rs, int x, int y)
  Now takes the surface and the (x, y) coordinates for the destination in the
  window's surface.
