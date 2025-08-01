local core = require "core"
local config = require "core.config"
local common = require "core.common"
local style = require "core.style"
local Object = require "core.object"
local Scrollbar = require "core.scrollbar"

local View = Object:extend()

-- context can be "application" or "session". The instance of objects
-- with context "session" will be closed when a project session is
-- terminated. The context "application" is for functional UI elements.
View.context = "application"

function View:new()
  self.position = { x = 0, y = 0 }
  self.size = { x = 0, y = 0 }
  self.scroll = { x = 0, y = 0, to = { x = 0, y = 0 } }
  self.cursor = "arrow"
  self.scrollable = false
  self.v_scrollbar = Scrollbar(self, {direction = "v", alignment = "e"})
  self.h_scrollbar = Scrollbar(self, {direction = "h", alignment = "e"})

  -- drawing surfaces variables
  -- name_surfaces is used to store and persist drawing surfaces. The keys are
  -- strings that must be unique only within the view instance.
  self.named_surfaces = { }

  -- array of surfaces that needs to be rendered at the end of the draw() function
  self.surface_to_draw = { }
end


-- add the surface to the list to be rendered on the screen
function View:set_surface_to_draw(surface)
  table.insert(self.surface_to_draw, surface)
end


-- select a surface from a list, create one and add to it if it doesn't exist.
-- adjust the size if it does not match with the existing surface.
-- ensure the surface is associated with the given position
function View.surface_from_list(surface_list, id, x, y, w, h)
  local surface = surface_list[id]
  local surf_x, surf_y, surf_w, surf_h
  local needs_drawing = false
  if surface then
    surf_x, surf_y, surf_w, surf_h = surface:get_rect()
  end
  if not surface or surf_w ~= w or surf_h ~= h then
    -- if we have no surface or the size does not match create a new one under the same id
    surface = renderer.surface.create(x, y, w, h)
    surface_list[id] = surface
    needs_drawing = true
  elseif surf_x ~= x or surf_y ~= y then
    -- here we may call set_position() unconditionally
    surface:set_position(x, y)
  end
  return surface, needs_drawing
end


function View:set_surface_for(name, x, y, w, h, background)
  local surface = View.surface_from_list(self.named_surfaces, name, x, y, w, h)
  renderer.set_current_surface(surface)
  renderer.begin_frame(surface, background or style.background)
  self:set_surface_to_draw(surface)
end


function View:present_surfaces()
  for _, surface in ipairs(self.surface_to_draw) do
    renderer.present_surface(surface)
  end
  self.surface_to_draw = { }
end


function View:move_towards(t, k, dest, rate, name)
  if type(t) ~= "table" then
    return self:move_towards(self, t, k, dest, rate, name)
  end
  local val = t[k]
  local diff = math.abs(val - dest)
  if not config.transitions or diff < 0.5 or config.disabled_transitions[name] then
    t[k] = dest
  else
    rate = rate or 0.5
    if config.fps ~= 60 or config.animation_rate ~= 1 then
      local dt = 60 / config.fps
      rate = 1 - common.clamp(1 - rate, 1e-8, 1 - 1e-8)^(config.animation_rate * dt)
    end
    t[k] = common.lerp(val, dest, rate)
  end
  if diff > 1e-8 then
    core.redraw = true
  end
end


function View:try_close(do_close)
  do_close()
end


---@return string
function View:get_name()
  return "---"
end


---@return number
function View:get_scrollable_size()
  return math.huge
end

---@return number
function View:get_h_scrollable_size()
  return 0
end


---@param x number
---@param y number
---@return boolean
function View:scrollbar_overlaps_point(x, y)
  return not (not (self.v_scrollbar:overlaps(x, y) or self.h_scrollbar:overlaps(x, y)))
end


---@return boolean
function View:scrollbar_dragging()
  return self.v_scrollbar.dragging or self.h_scrollbar.dragging
end


---@return boolean
function View:scrollbar_hovering()
  return self.v_scrollbar.hovering.track or self.h_scrollbar.hovering.track
end


---@param button core.view.mousebutton
---@param x number
---@param y number
---@param clicks integer
---return boolean
function View:on_mouse_pressed(button, x, y, clicks)
  if not self.scrollable then return end
  local result = self.v_scrollbar:on_mouse_pressed(button, x, y, clicks)
  if result then
    if result ~= true then
      self.scroll.to.y = result * self:get_scrollable_size()
    end
    return true
  end
  result = self.h_scrollbar:on_mouse_pressed(button, x, y, clicks)
  if result then
    if result ~= true then
      self.scroll.to.x = result * self:get_h_scrollable_size()
    end
    return true
  end
end


---@param button core.view.mousebutton
---@param x number
---@param y number
function View:on_mouse_released(button, x, y)
  if not self.scrollable then return end
  self.v_scrollbar:on_mouse_released(button, x, y)
  self.h_scrollbar:on_mouse_released(button, x, y)
end


---@param x number
---@param y number
---@param dx number
---@param dy number
function View:on_mouse_moved(x, y, dx, dy)
  if not self.scrollable then return end
  local result
  if self.h_scrollbar.dragging then goto skip_v_scrollbar end
  result = self.v_scrollbar:on_mouse_moved(x, y, dx, dy)
  if result then
    if result ~= true then
      self.scroll.to.y = result * self:get_scrollable_size()
      if not config.animate_drag_scroll then
        self:clamp_scroll_position()
        self.scroll.y = self.scroll.to.y
      end
    end
    -- hide horizontal scrollbar
    self.h_scrollbar:on_mouse_left()
    return true
  end
  ::skip_v_scrollbar::
  result = self.h_scrollbar:on_mouse_moved(x, y, dx, dy)
  if result then
    if result ~= true then
      self.scroll.to.x = result * self:get_h_scrollable_size()
      if not config.animate_drag_scroll then
        self:clamp_scroll_position()
        self.scroll.x = self.scroll.to.x
      end
    end
    return true
  end
end


function View:on_mouse_left()
  if not self.scrollable then return end
  self.v_scrollbar:on_mouse_left()
  self.h_scrollbar:on_mouse_left()
end


---@param filename string
---@param x number
---@param y number
---@return boolean
function View:on_file_dropped(filename, x, y)
  return false
end


---@param text string
function View:on_text_input(text)
  -- no-op
end


function View:on_ime_text_editing(text, start, length)
  -- no-op
end


---@param y number @Vertical scroll delta; positive is "up"
---@param x number @Horizontal scroll delta; positive is "left"
---@return boolean @Capture event
function View:on_mouse_wheel(y, x)
  -- no-op
end

function View:get_content_bounds()
  local x = self.scroll.x
  local y = self.scroll.y
  return x, y, x + self.size.x, y + self.size.y
end


function View:get_content_offset()
  local x = common.round(self.position.x - self.scroll.x)
  local y = common.round(self.position.y - self.scroll.y)
  return x, y
end


function View:clamp_scroll_position()
  local max = self:get_scrollable_size() - self.size.y
  self.scroll.to.y = common.clamp(self.scroll.to.y, 0, max)

  max = self:get_h_scrollable_size() - self.size.x
  self.scroll.to.x = common.clamp(self.scroll.to.x, 0, max)
end


function View:update_scrollbar()
  local v_scrollable = self:get_scrollable_size()
  self.v_scrollbar:set_size(self.position.x, self.position.y, self.size.x, self.size.y, v_scrollable)
  self.v_scrollbar:set_percent(self.scroll.y/v_scrollable)
  self.v_scrollbar:update()

  local h_scrollable = self:get_h_scrollable_size()
  self.h_scrollbar:set_size(self.position.x, self.position.y, self.size.x, self.size.y, h_scrollable)
  self.h_scrollbar:set_percent(self.scroll.x/h_scrollable)
  self.h_scrollbar:update()
end


function View:update()
  self:clamp_scroll_position()
  self:move_towards(self.scroll, "x", self.scroll.to.x, 0.3, "scroll")
  self:move_towards(self.scroll, "y", self.scroll.to.y, 0.3, "scroll")
  if not self.scrollable then return end
  self:update_scrollbar()
end


function View:draw_scrollbar()
  self.v_scrollbar:draw()
  self.h_scrollbar:draw()
end


function View:draw()
end


return View
