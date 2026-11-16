-- tools/shouts_check (task 0205): saves the game once the global script asks (run phase only).
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local cfg = require('scripts.sh205.cfg')

local asked = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'run' and not asked and storage.globalSection('SH205'):get('saveNow') then
                asked = true
                print('SHOUTSAVE save request')
                menu.saveGame('sh205', 'sh205')
            end
        end,
    },
}
