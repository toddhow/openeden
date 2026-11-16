-- Skyrim SE mining and woodcutting, the player half (task 0224): shows the line the pack made of the last try as a HUD
-- message.
local ui = require('openmw.ui')

return {
  eventHandlers = {
    SkyrimMiningMessage = function(data)
      if data.text ~= nil and data.text ~= '' then ui.showMessage(data.text) end
    end,
  },
}
