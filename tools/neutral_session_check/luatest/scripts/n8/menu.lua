-- tools/neutral_session_check (task 0204): the "save" phase of n8crime asks the menu to save once the global script says so.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running or asked then return end
            if storage.globalSection('N8CRIME'):get('saveNow') then
                asked = true
                print('N8CRIME save request')
                menu.saveGame('crime round trip', 'crime')
            end
        end,
    },
}
