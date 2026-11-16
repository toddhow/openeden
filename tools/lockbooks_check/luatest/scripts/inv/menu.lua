-- tools/inventory_check (task 0182): the "save" phase asks the menu to save once the global script says so.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running or asked then return end
            if storage.globalSection('N6INV'):get('saveNow') then
                asked = true
                print('N6INV save request')
                menu.saveGame('inv round trip', 'inv')
            end
        end,
    },
}
