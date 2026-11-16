-- tools/services_check (task 0219): saves the game once the global script asks.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
  engineHandlers = {
    onFrame = function()
      if menu.getState() ~= menu.STATE.Running then return end
      if not asked and storage.globalSection('SVC219'):get('saveNow') then
        asked = true
        print('SVC219 save request')
        menu.saveGame('svc219', 'svc219')
      end
    end,
  },
}
