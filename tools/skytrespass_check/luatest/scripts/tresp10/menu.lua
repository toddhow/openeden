-- tools/skyrest_check (task 0210): in the "run" phase saves the game once the global script asks.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local cfg = require('scripts.tresp10.cfg')
local asked = false
return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'run' and not asked and storage.globalSection('TRESP10'):get('saveNow') then
                asked = true
                print('TRESPCHECK save request')
                menu.saveGame('tresp10', 'tresp10')
            end
        end,
    },
}
