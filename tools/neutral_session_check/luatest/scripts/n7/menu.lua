-- tools/neutral_session_check (task 0181): in the "save" phase saves the game once the global script says the simulation
-- time passed scripts/n7/cfg.lua's saveAt.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local phase = require('scripts.n6.phase')

local asked = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if phase == 'save' and not asked and storage.globalSection('N7'):get('saveNow') then
                asked = true
                print('N7 save request')
                menu.saveGame('n7 papyrus', 'n7')
            end
        end,
    },
}
