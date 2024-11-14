local core = require "core"
local common = require "core.common"
local config = require "core.config"
local style = require "core.style"
local keymap = require "core.keymap"
local translate = require "core.doc.translate"
local ime = require "core.ime"
local TiledView = require "core.tiledview"

local TILE_CHARACTERS, TILE_LINES = 160, 80

-- DocView inherits from TiledView to display the document's text
-- content. The gutter is drawn separately using specific methods.
local DocView = TiledView:extend()

DocView.context = "session"


local function move_to_line_offset(dv, line, col, offset)
  local xo = dv.last_x_offset
  if xo.line ~= line or xo.col ~= col then
    xo.offset = dv:get_col_x_offset(line, col)
  end
  xo.line = line + offset
  xo.col = dv:get_x_offset_col(line + offset, xo.offset)
  return xo.line, xo.col
end


local function gutter_tile_id(tile_1)
  return ":" .. tile_1
end


function DocView:clear_unused_tiles()
  for id in pairs(self.named_surfaces) do
    if string.match(id, "^[:>]") and not self.used_tiles_ids[id] then
      self.named_surfaces[id] = nil
    end
  end
end


function DocView:setup_tiles_for_drawing()
  local lh = self:get_line_height()
  local cw = math.ceil(self:get_font():get_width(' '))
  local metric = self.tiles_metric
  metric.gutter_width, metric.gutter_padding = self:get_gutter_width()
  metric.line_height = lh
  -- Note that get_content_body_offset need metric.gutter_width to be set
  metric.x, metric.y = self:get_content_body_offset()
  metric.w, metric.h = cw * TILE_CHARACTERS, lh * TILE_LINES
  self.used_tiles_ids = { }
  self.drawing_tiles = true
end


function DocView:activate_gutter_tiles_for_region(y1, y2, background, present_only)
  local x, y = self:get_gutter_content_offset()
  local w, h = self.tiles_metric.gutter_width, self.tiles_metric.h
  local j1, j2 = math.floor((y1 - y) / h), math.floor((y2 - 1 - y) / h)
  local min_draw_j, max_draw_j
  for j = j1, j2 do
    if self:prepare_tile(gutter_tile_id(j), x, y + j * h, w, h, background, present_only) then
      min_draw_j = min_draw_j or j
      max_draw_j = j
    end
  end
  return min_draw_j, max_draw_j
end


function DocView:get_content_body_offset()
  local x, y = self:get_content_offset()
  return x + self.tiles_metric.gutter_width, y + style.padding.y
end


function DocView:get_gutter_content_offset()
  local _, y = self:get_content_offset()
  return self.position.x, y + style.padding.y
end


DocView.translate = {
  ["previous_page"] = function(doc, line, col, dv)
    local min, max = dv:get_visible_line_range()
    return line - (max - min), 1
  end,

  ["next_page"] = function(doc, line, col, dv)
    if line == #doc.lines then
      return #doc.lines, #doc.lines[line]
    end
    local min, max = dv:get_visible_line_range()
    return line + (max - min), 1
  end,

  ["previous_line"] = function(doc, line, col, dv)
    if line == 1 then
      return 1, 1
    end
    return move_to_line_offset(dv, line, col, -1)
  end,

  ["next_line"] = function(doc, line, col, dv)
    if line == #doc.lines then
      return #doc.lines, math.huge
    end
    return move_to_line_offset(dv, line, col, 1)
  end,
}


function DocView:new(doc)
  DocView.super.new(self)
  self.cursor = "ibeam"
  self.scrollable = true
  self.doc = assert(doc)
  self.font = "code_font"
  self.last_x_offset = {}
  self.ime_selection = { from = 0, size = 0 }
  self.ime_status = false
  self.hovering_gutter = false
  self.need_redraw = true
  self.tiles_metric = {
    x = 0, y = 0, w = 0, h = 0,
    line_height = 0, gutter_width = 0, gutter_padding = 0,
    limits = { x1 = 0, y1 = 0, x2 = 0, y2 = 0 }
  }
  self.used_tiles_ids = { }
  self.v_scrollbar:set_forced_status(config.force_scrollbar_status)
  self.h_scrollbar:set_forced_status(config.force_scrollbar_status)
end


function DocView:try_close(do_close)
  if self.doc:is_dirty()
  and #core.get_views_referencing_doc(self.doc) == 1 then
    core.command_view:enter("Unsaved Changes; Confirm Close", {
      submit = function(_, item)
        if item.text:match("^[cC]") then
          do_close()
        elseif item.text:match("^[sS]") then
          self.doc:save()
          do_close()
        end
      end,
      suggest = function(text)
        local items = {}
        if not text:find("^[^cC]") then table.insert(items, "Close Without Saving") end
        if not text:find("^[^sS]") then table.insert(items, "Save And Close") end
        return items
      end
    })
  else
    do_close()
  end
end


function DocView:get_name()
  local post = self.doc:is_dirty() and "*" or ""
  local name = self.doc:get_name()
  return name:match("[^/%\\]*$") .. post
end


function DocView:get_filename()
  if self.doc.abs_filename then
    local post = self.doc:is_dirty() and "*" or ""
    return common.home_encode(self.doc.abs_filename) .. post
  end
  return self:get_name()
end


function DocView:get_scrollable_size()
  if not config.scroll_past_end then
    local _, _, _, h_scroll = self.h_scrollbar:get_track_rect()
    return self:get_line_height() * (#self.doc.lines) + style.padding.y * 2 + h_scroll
  end
  return self:get_line_height() * (#self.doc.lines - 1) + self.size.y
end

function DocView:get_h_scrollable_size()
  return math.huge
end


function DocView:get_font()
  return style[self.font]
end


function DocView:get_line_height()
  return math.floor(self:get_font():get_height() * config.line_height)
end


function DocView:get_gutter_width()
  local padding = math.floor(style.padding.x * 2 + 0.5)
  return math.ceil(self:get_font():get_width(#self.doc.lines)) + padding, padding
end


function DocView:get_line_screen_position(line, col)
  local x, y = self.tiles_metric.x, self.tiles_metric.y
  local lh = self.tiles_metric.line_height
  y = y + (line-1) * lh
  if col then
    return x + self:get_col_x_offset(line, col), y
  else
    return x, y
  end
end


function DocView:get_line_text_y_offset()
  local lh = self.tiles_metric.line_height
  local th = self:get_font():get_height()
  return math.floor((lh - th) / 2 + 0.5)
end


function DocView:get_visible_line_range()
  local x, y, x2, y2 = self:get_content_bounds()
  local lh = self:get_line_height()
  local minline = math.max(1, math.floor((y - style.padding.y) / lh) + 1)
  local maxline = math.min(#self.doc.lines, math.floor((y2 - style.padding.y) / lh) + 1)
  return minline, maxline
end


function DocView:get_col_x_offset(line, col)
  local default_font = self:get_font()
  local _, indent_size = self.doc:get_indent_info()
  default_font:set_tab_size(indent_size)
  local column = 1
  local xoffset = 0
  for _, type, text in self.doc.highlighter:each_token(line) do
    local font = style.syntax_fonts[type] or default_font
    if font ~= default_font then font:set_tab_size(indent_size) end
    local length = #text
    if column + length <= col then
      xoffset = xoffset + font:get_width(text)
      column = column + length
      if column >= col then
        return xoffset
      end
    else
      for char in common.utf8_chars(text) do
        if column >= col then
          return xoffset
        end
        xoffset = xoffset + font:get_width(char)
        column = column + #char
      end
    end
  end

  return xoffset
end


function DocView:get_x_offset_col(line, x)
  local line_text = self.doc.lines[line]

  local xoffset, last_i, i = 0, 1, 1
  local default_font = self:get_font()
  local _, indent_size = self.doc:get_indent_info()
  default_font:set_tab_size(indent_size)
  for _, type, text in self.doc.highlighter:each_token(line) do
    local font = style.syntax_fonts[type] or default_font
    if font ~= default_font then font:set_tab_size(indent_size) end
    local width = font:get_width(text)
    -- Don't take the shortcut if the width matches x,
    -- because we need last_i which should be calculated using utf-8.
    if xoffset + width < x then
      xoffset = xoffset + width
      i = i + #text
    else
      for char in common.utf8_chars(text) do
        local w = font:get_width(char)
        if xoffset >= x then
          return (xoffset - x > w / 2) and last_i or i
        end
        xoffset = xoffset + w
        last_i = i
        i = i + #char
      end
    end
  end

  return #line_text
end


function DocView:resolve_line(y)
  local yo = self.tiles_metric.y
  local line = math.floor((y - yo) / self.tiles_metric.line_height) + 1
  return common.clamp(line, 1, #self.doc.lines)
end


function DocView:resolve_screen_position(x, y)
  local ox, oy = self:get_line_screen_position(1)
  local line = math.floor((y - oy) / self.tiles_metric.line_height) + 1
  line = common.clamp(line, 1, #self.doc.lines)
  local col = self:get_x_offset_col(line, x - ox)
  return line, col
end


function DocView:scroll_to_line(line, ignore_if_visible, instant)
  local min, max = self:get_visible_line_range()
  if not (ignore_if_visible and line > min and line < max) then
    local x, y = self:get_line_screen_position(line)
    local _, _, _, scroll_h = self.h_scrollbar:get_track_rect()
    self.scroll.to.y = math.max(0, y - (self.size.y - scroll_h) / 2)
    if instant then
      self.scroll.y = self.scroll.to.y
    end
  end
end


function DocView:scroll_to_make_visible(line, col)
  local _, oy = self:get_content_offset()
  local _, ly = self:get_line_screen_position(line, col)
  local lh = self:get_line_height()
  local _, _, _, scroll_h = self.h_scrollbar:get_track_rect()
  self.scroll.to.y = common.clamp(self.scroll.to.y, ly - oy - self.size.y + scroll_h + lh * 2, ly - oy - lh)
  local gw = self.tiles_metric.gutter_width
  local xoffset = self:get_col_x_offset(line, col)
  local xmargin = 3 * self:get_font():get_width(' ')
  local xsup = xoffset + gw + xmargin
  local xinf = xoffset - xmargin
  local _, _, scroll_w = self.v_scrollbar:get_track_rect()
  local size_x = math.max(0, self.size.x - scroll_w)
  if xsup > self.scroll.x + size_x then
    self.scroll.to.x = xsup - size_x
  elseif xinf < self.scroll.x then
    self.scroll.to.x = math.max(0, xinf)
  end
end

function DocView:on_mouse_moved(x, y, ...)
  DocView.super.on_mouse_moved(self, x, y, ...)

  self.hovering_gutter = false
  local gw = self.tiles_metric.gutter_width

  if self:scrollbar_hovering() or self:scrollbar_dragging() then
    self.cursor = "arrow"
  elseif gw > 0 and x >= self.position.x and x <= (self.position.x + gw) then
    self.cursor = "arrow"
    self.hovering_gutter = true
  else
    self.cursor = "ibeam"
  end

  if self.mouse_selecting then
    local l1, c1 = self:resolve_screen_position(x, y)
    local l2, c2, snap_type = table.unpack(self.mouse_selecting)
    if keymap.modkeys["ctrl"] then
      if l1 > l2 then l1, l2 = l2, l1 end
      self.doc.selections = { }
      for i = l1, l2 do
        self.doc:set_selections(i - l1 + 1, i, math.min(c1, #self.doc.lines[i]), i, math.min(c2, #self.doc.lines[i]))
      end
    else
      if snap_type then
        l1, c1, l2, c2 = self:mouse_selection(self.doc, snap_type, l1, c1, l2, c2)
      end
      self.doc:set_selection(l1, c1, l2, c2)
    end
  end
end


function DocView:mouse_selection(doc, snap_type, line1, col1, line2, col2)
  local swap = line2 < line1 or line2 == line1 and col2 <= col1
  if swap then
    line1, col1, line2, col2 = line2, col2, line1, col1
  end
  if snap_type == "word" then
    line1, col1 = translate.start_of_word(doc, line1, col1)
    line2, col2 = translate.end_of_word(doc, line2, col2)
  elseif snap_type == "lines" then
    col1, col2 = 1, math.huge
  end
  if swap then
    return line2, col2, line1, col1
  end
  return line1, col1, line2, col2
end


function DocView:on_mouse_pressed(button, x, y, clicks)
  if button ~= "left" or not self.hovering_gutter then
    return DocView.super.on_mouse_pressed(self, button, x, y, clicks)
  end
  local line = self:resolve_screen_position(x, y)
  if keymap.modkeys["shift"] then
    local sline, scol, sline2, scol2 = self.doc:get_selection(true)
    if line > sline then
      self.doc:set_selection(sline, 1, line,  #self.doc.lines[line])
    else
      self.doc:set_selection(line, 1, sline2, #self.doc.lines[sline2])
    end
  else
    if clicks == 1 then
      self.doc:set_selection(line, 1, line, 1)
    elseif clicks == 2 then
      self.doc:set_selection(line, 1, line, #self.doc.lines[line])
    end
  end
  return true
end


function DocView:on_mouse_released(...)
  DocView.super.on_mouse_released(self, ...)
  self.mouse_selecting = nil
end


function DocView:on_text_input(text)
  self.doc:text_input(text)
end

function DocView:on_ime_text_editing(text, start, length)
  self.doc:ime_text_editing(text, start, length)
  self.ime_status = #text > 0
  self.ime_selection.from = start
  self.ime_selection.size = length

  -- Set the composition bounding box that the system IME
  -- will consider when drawing its interface
  local line1, col1, line2, col2 = self.doc:get_selection(true)
  local col = math.min(col1, col2)
  self:update_ime_location()
  self:scroll_to_make_visible(line1, col + start)
end

---Update the composition bounding box that the system IME
---will consider when drawing its interface
function DocView:update_ime_location()
  if not self.ime_status then return end

  local line1, col1, line2, col2 = self.doc:get_selection(true)
  local x, y = self:get_line_screen_position(line1)
  local h = self:get_line_height()
  local col = math.min(col1, col2)

  local x1, x2 = 0, 0

  if self.ime_selection.size > 0 then
    -- focus on a part of the text
    local from = col + self.ime_selection.from
    local to = from + self.ime_selection.size
    x1 = self:get_col_x_offset(line1, from)
    x2 = self:get_col_x_offset(line1, to)
  else
    -- focus the whole text
    x1 = self:get_col_x_offset(line1, col1)
    x2 = self:get_col_x_offset(line2, col2)
  end

  ime.set_location(x + x1, y, x2 - x1, h)
end

function DocView:update()
  -- Check if document content changed
  if self.doc.ui_dirty then
    self.need_redraw = true
    self.doc:clear_ui_dirty()
  end

  -- scroll to make caret visible and reset blink timer if it moved
  local line1, col1, line2, col2 = self.doc:get_selection()
  if (line1 ~= self.last_line1 or col1 ~= self.last_col1 or
      line2 ~= self.last_line2 or col2 ~= self.last_col2) and self.size.x > 0 then
    if core.active_view == self and not ime.editing then
      self:scroll_to_make_visible(line1, col1)
    end
    core.blink_reset()
    self.last_line1, self.last_col1 = line1, col1
    self.last_line2, self.last_col2 = line2, col2
  end

  -- update blink timer
  if self == core.active_view and not self.mouse_selecting then
    local T, t0 = config.blink_period, core.blink_start
    local ta, tb = core.blink_timer, system.get_time()
    if ((tb - t0) % T < T / 2) ~= ((ta - t0) % T < T / 2) then
      core.redraw = true
    end
    core.blink_timer = tb
  end

  self:update_ime_location()

  DocView.super.update(self)
end


function DocView:draw_line_highlight(line)
  local _, y = self:get_line_screen_position(line)
  local h = self.tiles_metric.line_height
  local limits = self.tiles_metric.limits
  local line_size = math.max(1, SCALE)
  renderer.render_fill_rect(limits.x1, y, limits.x2 - limits.x1, line_size, style.line_number)
  renderer.render_fill_rect(limits.x1, y + h - line_size, limits.x2 - limits.x1, line_size, style.line_number)

  -- draw gutter highlight
  local x = self.position.x
  local gw, gpad = self.tiles_metric.gutter_width, self.tiles_metric.gutter_padding
  self:set_surface_for("gh", x, y, gw, h, style.background)
  self:draw_line_gutter_highlight(line, x, y, gw - gpad, style.line_number2)
end


function DocView:draw_line_text(line, x, y)
  local default_font = self:get_font()
  local tx, ty = x, y + self:get_line_text_y_offset()
  local last_token = nil
  local tokens = self.doc.highlighter:get_line(line).tokens
  local tokens_count = #tokens
  if string.sub(tokens[tokens_count], -1) == "\n" then
    last_token = tokens_count - 1
  end

  for tidx, type, text in self.doc.highlighter:each_token(line) do
    local color = style.syntax[type]
    local font = style.syntax_fonts[type] or default_font
    -- do not render newline, fixes issue #1164
    if tidx == last_token then text = text:sub(1, -2) end
    tx = self:draw_text(font, text, tx, ty, color)
    if tx > self.tiles_metric.limits.x2 then break end
  end
  return self.tiles_metric.line_height
end


function DocView:draw_caret(x, y)
    local w, h = style.caret_width, self.tiles_metric.line_height
    renderer.render_fill_rect(x, y, w, h, style.caret)
end


function DocView:draw_line_selection(line, x, y)
  -- draw selection if it overlaps this line
  local lh = self.tiles_metric.line_height
  for lidx, line1, col1, line2, col2 in self.doc:get_selections(true) do
    if line >= line1 and line <= line2 then
      local text = self.doc.lines[line]
      if line1 ~= line then col1 = 1 end
      if line2 ~= line then col2 = #text + 1 end
      local x1 = x + self:get_col_x_offset(line, col1)
      local x2 = x + self:get_col_x_offset(line, col2)
      if x1 ~= x2 then
        local line_id = ">" .. line
        self:set_surface_for(line_id, x1, y, x2 - x1, lh, style.selection)
        self:draw_line_text(line, x, y)
        self.used_tiles_ids[line_id] = true
      end
    end
  end
  return y + lh
end


function DocView:draw_line_gutter(line, x, y, width)
  -- The code below should maybe grouped in a function like self:draw_text() but dedicated
  -- to drawing the gutter's text
  local font = self:get_font()
  x = x + style.padding.x
  y = y + self:get_line_text_y_offset()
  local tw = font:get_width(line)
  local _, tile_j = self:get_tile_indexes(x, y)
  local surface = self.named_surfaces[gutter_tile_id(tile_j)]
  if surface then
    renderer.set_current_surface(surface)
    renderer.draw_text(font, line, x + (width - tw), y, style.line_number)
  end
  return self.tiles_metric.line_height
end


-- The same of draw_line_gutter() but does not use gutter tiles,
-- just the current surface set. The caller function is supposed
-- to have set the surface.
function DocView:draw_line_gutter_highlight(line, x, y, width)
  local font = self:get_font()
  x = x + style.padding.x
  y = y + self:get_line_text_y_offset()
  local tw = font:get_width(line)
  renderer.draw_text(font, line, x + (width - tw), y, style.line_number2)
end


function DocView:draw_ime_decoration(line1, col1, line2, col2)
  local x, y = self:get_line_screen_position(line1)
  local line_size = math.max(1, SCALE)
  local lh = self.tiles_metric.line_height

  -- Draw IME underline
  local x1 = self:get_col_x_offset(line1, col1)
  local x2 = self:get_col_x_offset(line2, col2)
  renderer.render_fill_rect(x + math.min(x1, x2), y + lh - line_size, math.abs(x1 - x2), line_size, style.text)

  -- Draw IME selection
  local col = math.min(col1, col2)
  local from = col + self.ime_selection.from
  local to = from + self.ime_selection.size
  x1 = self:get_col_x_offset(line1, from)
  if from ~= to then
    x2 = self:get_col_x_offset(line1, to)
    line_size = style.caret_width
    renderer.render_fill_rect(x + math.min(x1, x2), y + lh - line_size, math.abs(x1 - x2), line_size, style.caret)
  end
  self:draw_caret(x + x1, y)
end


function DocView:draw_overlay()
  local minline, maxline = self:get_visible_line_range()
  local tx, ty = self:get_line_screen_position(minline)
  for line = minline, maxline do
    ty = self:draw_line_selection(line, tx, ty)
  end

  if core.active_view == self then
    local hcl = config.highlight_current_line
    local highlight_line = true
    -- draw caret if it overlaps this line
    local T = config.blink_period
    for _, line1, col1, line2, col2 in self.doc:get_selections() do
      if line1 >= minline and line1 <= maxline
      and system.window_has_focus() then
        if highlight_line and (hcl ~= "no_selection" or ((line1 == line2) and (col1 == col2))) then
          self:draw_line_highlight(line1)
          highlight_line = false -- only highlight the first line
        end
        if ime.editing then
          self:draw_ime_decoration(line1, col1, line2, col2)
        else
          if config.disable_blink
          or (core.blink_timer - core.blink_start) % T < T / 2 then
            self:draw_caret(self:get_line_screen_position(line1, col1))
          end
        end
      end
    end
  end
end


function DocView:draw()
  self:setup_tiles_for_drawing()

  local _, indent_size = self.doc:get_indent_info()
  self:get_font():set_tab_size(indent_size)

  local lh = self.tiles_metric.line_height
  local gw, gpad = self.tiles_metric.gutter_width, self.tiles_metric.gutter_padding
  local xo, yo = self.tiles_metric.x, self.tiles_metric.y

  local pos = self.position
  local sx, sy = self.size.x, self.size.y
  local x1, y1, x2, y2 = self:activate_tiles_for_region(pos.x + gw, pos.y + style.padding.y, pos.x + sx, pos.y + sy, style.background, not self.need_redraw)
  local min_draw_j, max_draw_j = self:activate_gutter_tiles_for_region(pos.y + style.padding.y, pos.y + sy, style.background, not self.need_redraw)

  if y1 > pos.y then
    local xb, yb = self:get_content_offset()
    renderer.render_fill_rect(xb, yb, self.size.x, style.padding.y, style.background)
  end

  local minline, maxline
  if min_draw_j then
    minline, maxline = min_draw_j * TILE_LINES + 1, math.min((max_draw_j + 1) * TILE_LINES, #self.doc.lines)
  end

  local limits = self.tiles_metric.limits
  limits.x1, limits.y1, limits.x2, limits.y2 = x1, y1, x2, y2


  if minline then
    local _, y = self:get_line_screen_position(minline)
    local x = pos.x
    for i = minline, maxline do
      y = y + self:draw_line_gutter(i, x, y, gpad and gw - gpad or gw)
    end

    x, y = self:get_line_screen_position(minline)
    -- the clip below ensure we don't write on the gutter region. On the
    -- right side it is redundant with the Node's clip.
    for i = minline, maxline do
      y = y + (self:draw_line_text(i, x, y) or lh)
    end
  end

  self:present_surfaces()
  self:end_drawing_tiles()
  self:draw_overlay()
  self:draw_scrollbar()
  self:present_surfaces()
  self:clear_unused_tiles()
  self.need_redraw = false
end


return DocView
