-- tools/skyfollow_check (task 0239): the menu half. Saves the game once, when the global script asks (plan step 'save').
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if not asked and storage.globalSection('HSE'):get('saveNow') then
                asked = true
                print('HSE save request')
                menu.saveGame('hse', 'hse')
            end
        end,
    },
}
