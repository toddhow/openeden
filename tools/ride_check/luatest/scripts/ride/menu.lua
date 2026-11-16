-- tools/ride_check (task 0255): the menu script saves once the global script says so (a step { do = 'save' }).
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running or asked then return end
            if storage.globalSection('RIDESAVE'):get('saveNow') then
                asked = true
                print('RIDE save request')
                menu.saveGame('ride round trip', 'ride')
            end
        end,
    },
}
