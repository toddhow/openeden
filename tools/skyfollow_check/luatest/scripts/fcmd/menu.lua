-- tools/skyfollow_check (task 0239): the menu half. Saves the game once, when the global script asks (plan step 'save').
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if not asked and storage.globalSection('FCMD'):get('saveNow') then
                asked = true
                print('FCMD save request')
                menu.saveGame('fcmd', 'fcmd')
            end
        end,
    },
}
