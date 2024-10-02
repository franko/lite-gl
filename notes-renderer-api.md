# Lua API Functions

## Renderer
- `show_debug(rs: RenSurface, value: boolean)`: Shows or hides debug information for a RenSurface.
- `get_size()`: Retrieves the window size and scale.
- `begin_frame(rs: RenSurface)`: Begins rendering a new frame on a RenSurface.
- `end_frame(rs: RenSurface)`: Ends the current frame on a RenSurface.
- `clear_font_refs()`: Clears references to fonts in the registry.
- `set_clip_rect(x: number, y: number, w: number, h: number)`: Sets the clipping rectangle for rendering.
- `clear_clip_rect()`: Set the clip rect to the whole surface area.
- `draw_rect(x: number, y: number, w: number, h: number, color: table)`: Draws a rectangle at the specified position with the given color.
- `draw_text(font: Font|table, text: string, x: number, y: number, color: table)`: Draws text using specified font(s) at the given position with color.
- `set_viewport(x?: number, y?: number, w?: number, h?: number)`: Sets the rendering viewport, or resets it if no arguments are provided.
- `present_surface(rs: RenSurface)`: Presents the rendered surface to the window.
- `present_window()`: Presents the window, showing all rendered content.
- `render_fill_rect(x: number, y: number, w: number, h: number, color: table)`: Fills a rectangle on the window at the specified position with color.
- `set_current_surface(surface: RenSurface)`: Sets the current surface for rendering operations.

## Font
- `load(filename: string, size: number, options?: table)`: Loads a font from a file with the specified size and optional styling.
- `copy(font: Font|table, size?: number, options?: table)`: Copies a font with optional size and styling options.
- `group(fonts: table)`: Creates a font group from a table of Font objects.
- `set_tab_size(font: Font|table, size: number)`: Sets the tab size for a font or font group.
- `get_width(font: Font|table, text: string)`: Gets the width of the rendered text using the specified font(s).
- `get_height(font: Font|table)`: Gets the height of the specified font(s).
- `get_size(font: Font|table)`: Gets the size of the specified font(s).
- `set_size(font: Font|table, size: number)`: Sets the size of the specified font(s).
- `get_path(font: Font|table)`: Gets the file path of the specified font(s).

## RenSurface
- `create(x: integer, y: integer, w: integer, h: integer)`: Creates a new RenSurface with the specified position and size.
- `get_rect(surface: RenSurface)`: Gets the rectangle (position and size) of the specified surface.
- `set_position(surface: RenSurface, x: integer, y: integer)`: Sets the position of the specified surface.

