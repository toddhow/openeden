-- tools/ui_check (task 0162): the debug panel of components/ui_rml/debugpanel.cpp rewritten as `openeden.ui` documents.
-- Needs `[GUI] rmlui = true` (without it the package openeden.uibackend does not exist and this script does nothing).
-- Two documents, by the rule of research note 0045 13.5: the panel itself, and one small Hud for each value that changes
-- often (the frame rate line twice a second, a frame counter EVERY frame: it proves that a value that changes every
-- frame is drawn in every frame, task 0164). The script never calls ui.update(): the engine does, once a frame.
local core = require('openmw.core')
local debug = require('openmw.debug')
local ok, ui = pcall(require, 'openeden.ui')
if not ok then
  print('[uipanel] openeden.ui is not available: ' .. tostring(ui))
  return {}
end

local fps = ui.Value('FPS: --')
local frameText = ui.Value('frame 0')
local clicks = ui.Value(0)
local field = ui.Value('')
local sound, fog, volume, tab = ui.Value(true), ui.Value(false), ui.Value(40), ui.Value('controls')
local frames, since, frameNo = 0, 0, 0
local built = false
local lastTime
-- Consecutive screenshots for the per-frame counter check (task 0164): frames SHOT_FROM .. SHOT_FROM + SHOT_COUNT - 1
-- of this script's own count. The names screenshotNNN.png are given in request order.
local SHOT_FROM, SHOT_COUNT = 600, 6

local items = {}
for i = 1, 12 do items[i] = { id = i, name = 'List ' .. i } end

local function build()
  built = true
  -- Looks come from the theme (ui/base.rcss classes and tokens); only the place and size of the documents are here.
  ui.Document{ name = 'lua-panel', class = 'panel',
    style = { position = 'absolute', left = 16, top = 16, width = 260 },
    build = function()
      return ui.Column{
        ui.Text{ text = 'OpenEden UI (Lua)', class = 'title' },
        ui.Frame{ style = { height = 22 } }, -- room for the frame rate Hud
        ui.Text{ text = 'openeden.ui on RmlUi', class = 'indent' },
        ui.Text{ text = 'Reactive values, one effect each', class = 'indent' },
        ui.Button{ text = 'Button one', onClick = function() clicks:set(clicks:peek() + 1); print('[uipanel] click one') end },
        ui.Button{ text = 'Button two', onClick = function() clicks:set(clicks:peek() + 1); print('[uipanel] click two') end },
        ui.TextInput{ value = field, onChange = function(t) print('[uipanel] change ' .. t) end },
        ui.Text{ text = ui.Computed(function() return 'clicks: ' .. clicks:get() end), class = 'info' },
        ui.Text{ text = ui.Computed(function() return 'text: ' .. field:get() end), class = 'info' },
        ui.List{ items = items, key = function(it) return it.id end, rowHeight = 16, height = 52,
          style = { ['margin-top'] = 4 },
          render = function(it) return ui.Text{ text = it.name, style = { ['padding-left'] = 6 } } end },
      }
    end }
  -- The second component slice (task 0166), a document of its own on the right of the first.
  ui.Document{ name = 'lua-widgets', class = 'panel',
    style = { position = 'absolute', left = 300, top = 16, width = 260 },
    build = function()
      return ui.Column{
        ui.Text{ text = 'Widgets', class = 'title' },
        ui.Tabs{ value = tab, tabs = {
          { id = 'controls', label = 'Controls', content = function()
            return ui.Column{
              ui.Checkbox{ value = sound, label = 'Sound', onChange = function(v) print('[uipanel] checkbox ' .. tostring(v)) end },
              ui.Toggle{ value = fog, label = 'Fog', onChange = function(v) print('[uipanel] toggle ' .. tostring(v)) end },
              ui.Slider{ value = volume, min = 0, max = 100, step = 5,
                onChange = function(v) print('[uipanel] slider ' .. tostring(v)) end },
              ui.Text{ text = ui.Computed(function() return string.format('volume: %d', volume:get()) end), class = 'info' },
              ui.ProgressBar{ value = volume, max = 100 },
            } end },
          { id = 'list', label = 'List', content = function()
            return ui.ScrollView{ height = 60,
              ui.Text{ text = 'Scroll item 1' }, ui.Text{ text = 'Scroll item 2' }, ui.Text{ text = 'Scroll item 3' },
              ui.Text{ text = 'Scroll item 4' }, ui.Text{ text = 'Scroll item 5' }, ui.Text{ text = 'Scroll item 6' },
              ui.Text{ text = 'Scroll item 7' }, ui.Text{ text = 'Scroll item 8' } } end },
        }, onChange = function(id) print('[uipanel] tab ' .. id) end },
      }
    end }
  -- Documents draw in creation order: the small ones come after the panel so that its background does not dim them.
  -- The frame rate: its own small document, as the C++ panel's line is its own element.
  ui.Hud{ name = 'lua-fps', style = { position = 'absolute', left = 28, top = 42, width = 240 },
    build = function() return ui.Text{ text = fps, class = 'ok' } end }
  -- A value that changes often: also its own document.
  ui.Hud{ name = 'lua-frame', style = { position = 'absolute', left = 28, top = 316, width = 240 },
    build = function() return ui.Text{ text = frameText, class = 'info' } end }
end

return {
  engineHandlers = {
    onFrame = function()
      if not built then build() end
      local now = core.getRealTime()
      local dt = lastTime and (now - lastTime) or 0
      lastTime = now
      frames = frames + 1
      since = since + dt
      if since >= 0.5 then
        fps:set(string.format('FPS: %.1f', frames / since))
        frames, since = 0, 0
      end
      frameNo = frameNo + 1
      frameText:set('frame ' .. frameNo) -- every frame
      if frameNo >= SHOT_FROM and frameNo < SHOT_FROM + SHOT_COUNT then debug.takeScreenshot() end
    end,
  },
}
