local common = require "core.common"
local style = {}

style.padding = { x = 14, y = 7 }
style.divider_size = 1
style.scrollbar_size = 4
style.expanded_scrollbar_size = 12
style.caret_width = 2
style.tab_width = 170

-- The function renderer.font.load can accept an option table as a second optional argument.
-- It shoud be like the following:
--
-- {antialiasing= "grayscale", hinting = "full"}
--
-- The possible values for each option are:
-- - for antialiasing: grayscale, subpixel
-- - for hinting: none, slight, full
--
-- The defaults values are antialiasing subpixel and hinting slight for optimal visualization
-- on ordinary LCD monitor with RGB patterns.
--
-- On High DPI monitor or non RGB monitor you may consider using antialiasing grayscale instead.
-- The antialiasing grayscale with full hinting is interesting for crisp font rendering.
local code_font, code_size = "JetBrains Mono", 15
local ui_font, ui_size = "FiraSans", 15
style.font = renderer.font.load(ui_font, ui_size)
style.big_font = style.font:copy(46)
style.icon_font = renderer.font.load("icons", 16, {antialiasing="grayscale", hinting="full"})
style.icon_big_font = style.icon_font:copy(23)
style.code_font = renderer.font.load(code_font, code_size)
style.code_font_bold = renderer.font.load(code_font .. ":bold", code_size)

style.syntax = {}

-- This can be used to override fonts per syntax group.
-- The syntax highlighter will take existing values from this table and
-- override style.code_font on a per-token basis, so you can choose to eg.
-- render comments in an italic font if you want to.
style.syntax_fonts = {}
-- style.syntax_fonts["comment"] = renderer.font.load(code_font .. ":italic", code_size)

style.log = {}

return style
