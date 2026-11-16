-- tools/neutral_session_check (task 0196, followers): saves the game when the global script of "n7follow" (mode save) asks.
local menu = require('openmw.menu')
local storage = require('openmw.storage')

local asked = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if not asked and storage.globalSection('N7F'):get('saveNow') then
                asked = true
                print('N7F save request')
                menu.saveGame('n7 follower', 'n7')
            end
        end,
    },
}
