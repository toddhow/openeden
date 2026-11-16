-- Skyrim SE crime, the player half (task 0204): shows the guard's offer (pay, jail, resist) while the pack's state is
-- 'offer'. Needs `[GUI] rmlui`; without it `openeden.ui` is absent and this script does nothing.
local core = require('openmw.core')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okCrime, crime = pcall(require, 'core.skyrimse.crime')
if not (okUi and okCrime) then
  return {}
end
local windows = require('openeden.skyrimse.crime_windows')

local MODE = 'Interface'
local view = windows.create{
  ui = ui,
  crime = crime,
  choose = function(choice) core.sendGlobalEvent('SkyrimCrimeChoose', { choice = choice }) end,
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
      local state = crime.state()
      -- Task 0258: in 'dialogue' mode the guard's own conversation makes the offer; the window is the fallback
      if state == 'offer' and crime.offer_mode ~= nil and crime.offer_mode() == 'dialogue' then
        state = 'dialogue'
      end
      if state == 'offer' then
        if not opened then
          opened = true
          view.open()
          -- GUI mode with no MyGUI window: the cursor, the pause and the RmlUi overlay's input
          I.UI.setMode(MODE, { windows = {} })
        end
        view.update()
      elseif opened then
        leave() -- the choice was made (or the offer lapsed)
      end
    end,
  },
}
