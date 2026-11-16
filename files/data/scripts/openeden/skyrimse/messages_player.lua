-- Skyrim SE message boxes, the player half (task 0237): shows the box a Papyrus script asked (a standing stone's "choose this
-- sign?") while the pack's state is active. Needs `[GUI] rmlui`; without it `openeden.ui` is absent and this script does
-- nothing. Not started without the pack's `core.skyrimse.messages` namespace.
local core = require('openmw.core')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okM, messages = pcall(require, 'core.skyrimse.messages')
if not (okUi and okM) then
  return {}
end
local windows = require('openeden.skyrimse.message_windows')

local MODE = 'Interface'
local view = windows.create{
  ui = ui,
  messages = messages,
  choose = function(n) core.sendGlobalEvent('SkyrimMessageChoose', { button = n }) end,
}
local opened = false

local function leave()
  if opened then
    opened = false
    view.dismiss()
    I.UI.setMode()
  end
end

return {
  engineHandlers = {
    onFrame = function()
      if messages.active() then
        if not opened then
          opened = true
          view.open()
          -- GUI mode with no MyGUI window: the cursor, the pause and the RmlUi overlay's input
          I.UI.setMode(MODE, { windows = {} })
        end
        view.update()
      elseif opened then
        leave()
      end
    end,
  },
}
