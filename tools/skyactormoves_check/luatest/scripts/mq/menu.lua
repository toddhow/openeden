-- tools/skyactormoves_check (task 0286): the menu half. Saves the game once, when the global script asks.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if not asked and storage.globalSection('MQ'):get('saveNow') then
                asked = true
                print('MQ menu saves the game')
                menu.saveGame('amoves', 'amoves')
            end
        end,
    },
}
