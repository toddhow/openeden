-- Skyrim SE harvesting, the player half (task 0221): shows the line the pack made of the last try as a HUD message.
local ui = require('openmw.ui')

return {
  eventHandlers = {
    SkyrimHarvestMessage = function(data)
      if data.text ~= nil and data.text ~= '' then ui.showMessage(data.text) end
    end,
  },
}
