## renwindow.c

- renwin_get_size no longer exists: use renwin_get_size_pixels or renwin_get_size_window_coord
- attention now RenCache should work all in pixels coordinates, *including* macOS hard-scaling
- renwin_render_surface now needs the coordinates in pixels, *including* macOS hard-scaling
  * check all functions calling renwin_render_surface
- renwin_set_clip_rect now needs the rectangle "r" in pixels, *including* macOS hard-scaling
  * check all functions calling renwin_set_clip_rect
- renwin_render_fill_rect now needs the rectangle "r" in pixels, *including* macOS hard-scaling
  * checked all functions calling renwin_render_fill_rect, done
- renwin_global_scale is a new function. Returns the global scaling, soft x hard, as a float.
  That scale is used to convert from window's coordinates to pixels

## rensurface.c

- rensurf_init now no longer take a "scale" parameter (it was the hard-scale of *macOS*)
  * now RenSurface wants all coordinates in pixels, *including* macOS hard-scaling
- RenSurface no longer store the "scale" factor, was the hard-scale of *macOS*
- rensurf_update_rect now take a rect "r" in pixels, *including* macOS hard-scaling
- rensurf_get_rect and rensurf_get_size now returns size in pixels, *including* macOS hard-scaling

## rencache.c

- rencache_set_clip_rect needs the coordinates in pixels:
  * verified all the functions calling it
- rencache_draw_text, ok, now it takes coordinates in pixels.
  Checks done:
  * ren_font_group_get_width/height now takes size in pixels and returns width in pixels
  * ren_font_group_get_tab_size returns value in pixels
  * verified all the functions calling it
- rencache_draw_rect is ok, it takes coordinates in pixels
  * verified all the functions calling it

## renderer.c

- ren_draw_rect now takes a rect "r" in pixels, *including* macOS hard-scaling
  * called, amond others, from rencache. No change done because now RenCache should work
    itself in pixel coordinates
- ren_draw_text now takes the x and y coordinates in pixels, *including* macOS hard-scaling
  * **important:** it does also returns a value in pixels
  * it is only called in rencache.c and it is already ok
- ren_set_clip_rect now take a rect "r" in pixels, *including* macOS hard-scaling
  * the function calling it from rencache are correct. RenCache now use pixels coordinates
    and it pass to ren_set_clip_rect a rectangle in pixel coordinates
- ren_font_get_scale is **removed**: the fonts now gets their sizes in pixels and we
  extract information in pixels
- ren_font_load now accept font's size in pixels
  * are functions calling it are verified
- ren_font_group_set_size: ok now accept font's size in pixels
  * are functions calling it are verified

## api/renderer.c

- f_set_clip_rect now pass pixels coordinates to rencache_set_clip_rect
- f_clear_clip_rect is ok, rensurf_get_rect returns pixels and rencache_set_clip_rect
  consumes pixels
- f_draw_rect is now ok: it passes pixels coordinates to rencache_draw_rect
- f_draw_text is now ok: it passes pixels coordinates to rencache_draw_text
- f_rensurf_create is now ok: it passes pixels coordinates to rensurf_init
- f_font_load, now it scales the size into pixels and call ren_font_load.
  The function ren_font_load accept noew the font's size in pixels as a float. It just
  round its value to the closest integer.
- f_font_set_size: ok, now it calls ren_font_group_set_size by giving a size scaled in
  pixels

## Flow

### Surface initialization

View in core/view.lua calls:

- renderer.surface.create, which is implemented by f_rensurf_create
- f_rensurf_create calls rensurf_init
- rensurf_init calls rencache_init, SDL_CreateSurface/Texture

Now the coordinates becomes in pixels already in f_rensurf_create and from there
they stay in pixels.

### Drawing text

All begins with renderer.draw_text which is implemented in f_draw_text in api/renderer.c. In f_draw_text the
coordinates are scaled to pixels and passed to rencache_draw_text which in turns store the draw-text command
just with some font-metric information like ren_font_group_get_width/height/tab_size.

The text is really drawn in RenCache when calling rencache_end_frame. This is where ren_draw_text is really
called, so it gets pixels coordinates.

### Drawing rectangles

We can pass by the RenCache or speak directly to the SDL renderer.
