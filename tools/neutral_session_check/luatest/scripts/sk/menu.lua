-- tools/neutral_session_check (task 0200): saves once the global script says so, then loads that save without leaving
-- the process (the clean-up of a running game, then the restore of the progression chunk).
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local core = require('openmw.core')

local asked, reloadAt, reloaded = false, nil, false
return {
  engineHandlers = {
    onFrame = function()
      if menu.getState() ~= menu.STATE.Running then return end
      if not asked and storage.globalSection('SK'):get('saveNow') then
        asked = true
        print('SK save request')
        menu.saveGame('sk progression', 'sk')
        reloadAt = core.getRealTime() + 3
      end
      if reloadAt ~= nil and not reloaded and core.getRealTime() > reloadAt then
        reloaded = true
        local dir = menu.getCurrentSaveDir()
        for slot, info in pairs(menu.getSaves(dir)) do
          if info.description == 'sk progression' then
            print('SK in-process load ' .. slot)
            menu.loadGame(dir, slot)
            return
          end
        end
        print('SK in-process load: the save was not found')
      end
    end,
  },
}
