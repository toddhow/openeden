-- tools/neutral_session_check (task 0200): saves once the global script says so and does nothing else; the second
-- process loads the save with --load-savegame (the proof that the progression chunk was written and read).
local menu = require('openmw.menu')
local storage = require('openmw.storage')

local asked = false
return {
  engineHandlers = {
    onFrame = function()
      if menu.getState() ~= menu.STATE.Running then return end
      if not asked and storage.globalSection('SK'):get('saveNow') then
        asked = true
        print('SK save request')
        menu.saveGame('sk progression', 'sk')
      end
    end,
  },
}
