## renwindow.c
- renwin_get_size no longer exists: use renwin_get_size_pixels or renwin_get_size_window_coord
- attention now RenCache should work all in pixels coordinates, *including* macOS hard-scaling
- renwin_render_surface now needs the coordinates in pixels, *including* macOS hard-scaling
  * check all functions calling renwin_render_surface
- renwin_set_clip_rect now needs the rectangle "r" in pixels, *including* macOS hard-scaling
  * check all functions calling renwin_set_clip_rect
- renwin_render_fill_rect now needs the rectangle "r" in pixels, *including* macOS hard-scaling
  * check all functions calling renwin_render_fill_rect

## rensurface.c
- rensurf_init now no longer take a "scale" parameter (it was the hard-scale of *macOS*)
  * now RenSurface wants all coordinates in pixels, *including* macOS hard-scaling
- RenSurface no longer store the "scale" factor, was the hard-scale of *macOS*
- rensurf_update_rect now take a rect "r" in pixels, *including* macOS hard-scaling
- rensurf_get_rect and rensurf_get_size now returns size in pixels, *including* macOS hard-scaling

## rencache.c
- rencache_set_clip_rect needs the coordinates in pixels:
  * fixing calls

## renderer.c
- ren_draw_rect now takes a rect "r" in pixels, *including* macOS hard-scaling
  * called, amond others, from rencache. No change done because now RenCache should work
    itself in pixel coordinates
- ren_draw_text now takes the x and y coordinates in pixels, *including* macOS hard-scaling
  * **important:** it does also returns a value in pixels
  * it is only called in rencache.c and it is already ok

## api/renderer.c

- f_set_clip_rect now pass pixels coordinates to rencache_set_clip_rect
- f_clear_clip_rect is ok, rensurf_get_rect returns pixels and rencache_set_clip_rect
  consumes pixels
- f_draw_rect is now ok: it passes pixels coordinates to rencache_draw_rect
- f_draw_text is now ok: it passes pixels coordinates to rencache_draw_text
- f_rensurf_create is now ok: it passes pixels coordinates to rensurf_init


## Flow

### Surface initialization

View in core/view.lua calls:

- renderer.surface.create, which is implemented by f_rensurf_create
- f_rensurf_create calls rensurf_init
- rensurf_init calls rencache_init, SDL_CreateSurface/Texture

Now the coordinates becomes in pixels already in f_rensurf_create and from there
they stay in pixels.

### Drawing text

All begins with renderer.draw_text ...

### Drawing rectangles

We can pass by the RenCache or speak directly to the SDL renderer.
