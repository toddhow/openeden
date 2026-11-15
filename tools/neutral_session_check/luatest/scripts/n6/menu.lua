-- tools/neutral_session_check (task 0177): saves the game once the global script has reported (the "save" phase of
-- saveload.sh). In the "cycle" phase (a game loaded from a first save) it saves again as "n6 cycle" after the report and
-- then, 3 real seconds later, loads that second save WITHOUT leaving the process (menu.loadGame), so that the clean-up
-- of a running game and the second generation of a save are both exercised.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local core = require('openmw.core')
local phase = require('scripts.n6.phase')

local asked = false
local reloadAt = nil
local reloaded = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            local reported = storage.globalSection('N6'):get(phase == 'cycle' and 'reportedCycle' or 'reported')
            if (phase == 'save' or phase == 'cycle') and not asked and reported then
                asked = true
                print('N6 save request')
                if phase == 'save' then
                    menu.saveGame('n6 round trip', 'n6')
                else
                    menu.saveGame('n6 cycle', 'n6c')
                    reloadAt = core.getRealTime() + 3
                end
            end
            if reloadAt ~= nil and not reloaded and core.getRealTime() > reloadAt then
                reloaded = true
                local dir = menu.getCurrentSaveDir()
                for slot, info in pairs(menu.getSaves(dir)) do
                    if info.description == 'n6 cycle' then
                        print('N6 in-process load ' .. slot)
                        menu.loadGame(dir, slot)
                        return
                    end
                end
                print('N6 in-process load: the second save was not found')
            end
        end,
    },
}
