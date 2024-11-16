local core = require "core"
local common = require "core.common"
local View = require "core.view"


local TiledView = View:extend()


local function compose_tile_id(tile_1, tile_2)
  return ":" .. tile_1 .. " " .. tile_2
end


function TiledView:new()
  TiledView.super.new(self)
  self.tiles_metric = { w = 0, h = 0 }
  self.used_tiles_ids = { }
  self.drawing_tiles = false
end


function TiledView:get_tile_indexes(x, y)
  local xo, yo = self.tiles_metric.x, self.tiles_metric.y
  local w, h = self.tiles_metric.w, self.tiles_metric.h
  return math.floor((x - xo) / w), math.floor((y - yo) / h)
end


-- should be called only once for each tile at the beginning of the draw()
-- function.
-- Retrieve the extisting surface from the previous frame or create a new one.
-- Draw the background of the surface.
-- Set the surface to be renderer when present_surfaces() is called.
-- Mark the tile as used.
function TiledView:prepare_tile(tile_id, x, y, w, h, background, present_only)
  local surface, needs_drawing = self.surface_from_list(self.named_surfaces, tile_id, x, y, w, h)
  if not present_only or needs_drawing then
    renderer.set_current_surface(surface)
    renderer.begin_frame(surface, background)
  end
  self:set_surface_to_draw(surface)
  self.used_tiles_ids[tile_id] = true
  return not present_only or needs_drawing
end


-- We remove from named_surfaces the tile surfaces no longer in view
-- so that the garbage collector can dispose of them.
function TiledView:clear_unused_tiles()
  for id in pairs(self.named_surfaces) do
    if string.match(id, "^:") and not self.used_tiles_ids[id] then
      self.named_surfaces[id] = nil
    end
  end
end


-- compute the tiles metric and clear the used tiles list
function TiledView:setup_tiles_for_drawing()
  local metric = self.tiles_metric
  metric.x, metric.y = self:get_content_offset()
  metric.w, metric.h = self.tiles_width or 400, self.tiles_height or 600
  self.used_tiles_ids = { }
  self.drawing_tiles = true
end


function TiledView:end_drawing_tiles()
  self.drawing_tiles = false
end

-- activate/prepare the tiles needed to cover the given region
-- Parameters:
--   x1, y1, x2, y2: pixel bounds of the region to cover
--   background: (optional) background color or data for new tiles
--   present_only: (optional) if true, do not set the tile to be redrawn
--                 if it already exists
-- Returns:
--   x1, y1, x2, y2: pixel bounds of the covered region (slightly expanded to tile boundaries)
--   redraw_min_j, redraw_max_j: row index bounds of tiles needing redraw, or nil if none
function TiledView:activate_tiles_for_region(x1, y1, x2, y2, background, present_only)
  local xo, yo = self.tiles_metric.x, self.tiles_metric.y
  local w, h = self.tiles_metric.w, self.tiles_metric.h

  -- compute min/max indexes of tiles needed to cover the region (x1, y1, x2, y2)
  local min_i, max_i = math.floor((x1 - xo) / w), math.floor((x2 - 1 - xo) / w)
  local min_j, max_j = math.floor((y1 - yo) / h), math.floor((y2 - 1 - yo) / h)

  -- Determine the range of rows (j indexes) that need redraw.
  -- Note: While we could compute a similar range for columns (i indexes), 
  -- the specific use case (docview) only requires row information for redraw optimization.
  local redraw_min_j, redraw_max_j

  -- prepare the tiles for drawing
  for j = min_j, max_j do
    local y = yo + j * h
    for i = min_i, max_i do
      local x = xo + i * w
      local tile_id = compose_tile_id(i, j)
      local needs_redraw = self:prepare_tile(tile_id, x, y, w, h, background, present_only)
      if needs_redraw then
        redraw_min_j = redraw_min_j or j
        redraw_max_j = j
      end
    end
  end

  -- Return the pixel bounds of the covered region (expanded to tile boundaries)
  -- and the row index bounds of tiles needing redraw.
  return xo + min_i * w, yo + min_j * h, xo + (max_i + 1) * w, yo + (max_j + 1) * h, redraw_min_j, redraw_max_j
end


function TiledView:activate_tiles(background, present_only)
  local x, y = self.position.x, self.position.y
  return self:activate_tiles_for_region(x, y, x + self.size.x, y + self.size.y, background, present_only)
end


function TiledView:draw_text(font, text, x, y, color)
  if not self.drawing_tiles then
    return renderer.draw_text(font, text, x, y, color)
  end
  local i1, j1 = self:get_tile_indexes(x, y)
  local surface = self.named_surfaces[compose_tile_id(i1, j1)]
  local xp, yp = 0, y + font:get_height()
  if surface then
    renderer.set_current_surface(surface)
    xp = renderer.draw_text(font, text, x, y, color)
  else
    xp = x + font:get_width(text)
  end
  -- compute the indexes of the tile that contains the lower-right
  -- corner (xp, yp) of the element
  local i2, j2 = self:get_tile_indexes(xp, yp)
  -- if i2 > i1 or j2 > 11 we need to draw the element in a matrix of
  -- surfaces from (i1, j1) to (i2, j2) but the drawing in (i1, j1) is
  -- already done.
  -- In the more likely case i2 = i1 and j2 = j1 i.e. the element does
  -- not cross the boundaries of the surface where its upper-left corner
  -- is located and the loop before should not do anything.
  -- Draw the element in the other surfaces the element overflows to.
  for j = j1, j2 do
    for i = i1, i2 do
      if i > i1 or j > j1 then
        surface = self.named_surfaces[compose_tile_id(i, j)]
        if surface then
          renderer.set_current_surface(surface)
          renderer.draw_text(font, text, x, y, color)
        end
      end
    end
  end
  return xp
end


function TiledView:draw_justified_text(font, color, text, align, x, y, w, h)
  local tw, th = font:get_width(text), font:get_height()
  if align == "center" then
    x = x + (w - tw) / 2
  elseif align == "right" then
    x = x + (w - tw)
  end
  y = y + common.round((h - th) / 2)
  return self:draw_text(font, text, x, y, color), y + th
end


function TiledView:draw_rect(x, y, w, h, color)
  if not self.drawing_tiles then
    renderer.draw_rect(x, y, w, h, color)
    return
  end
  local xp, yp = x + w, y + h
  local i1, j1 = self:get_tile_indexes(x, y)
  -- compute the indexes of the tile that contains the lower-right
  -- corner (xp, yp) of the element
  local i2, j2 = self:get_tile_indexes(xp, yp)
  -- if i2 > i1 or j2 > j1 we need to draw the element in a matrix of
  -- surfaces from (i1, j1) to (i2, j2).
  -- In the more likely case i2 = i1 and j2 = j1 i.e. the element does
  -- not cross the boundaries of the surface where its upper-left corner
  -- is located and the loop before should not do anything.
  -- Draw the element in the other surfaces the element overflows to.
  for j = j1, j2 do
    for i = i1, i2 do
      local surface = self.named_surfaces[compose_tile_id(i, j)]
      if surface then
        renderer.set_current_surface(surface)
        renderer.draw_rect(x, y, w, h, color)
      end
    end
  end
end


return TiledView
