-- tools/neutral_session_check (task 0202): in the "save" phase saves the game once the global script says the simulation
-- time passed scripts/n9/cfg.lua's saveAt.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local phase = require('scripts.n6.phase')
local resave = require('scripts.n9.resave')

local asked = false
-- the storage section outlives the process: what an earlier run left there is not a request of this one
local stale = nil
local staleRead = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            local saving = phase == 'save' or (phase == 'load' and resave)
            local requested = storage.globalSection('N9'):get('saveNow')
            if not staleRead then
                staleRead = true
                stale = requested
            end
            if saving and not asked and requested ~= nil and requested ~= stale then
                asked = true
                print('N9 save request')
                if phase == 'save' then menu.saveGame('n9 objects', 'n9') else menu.saveGame('n9 objects again', 'n9b') end
            end
        end,
    },
}
