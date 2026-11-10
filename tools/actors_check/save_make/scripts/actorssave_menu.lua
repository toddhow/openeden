-- Drives the 'make' run of tools/actors_check/save_check.sh: the game starts by itself (--skip-menu --start), wait, save as
-- 'a', quit.
local core = require('openmw.core')
local menu = require('openmw.menu')

local frames = 0
local stage = 0
local runningSince = nil
local savedAt = nil

return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if stage == 0 and menu.getState() == menu.STATE.Running then
                runningSince = core.getRealTime()
                stage = 2
            elseif stage == 2 and core.getRealTime() - runningSince > 16 then
                menu.saveGame('a')
                savedAt = frames
                stage = 3
            elseif stage == 3 and frames > savedAt + 120 then
                core.quit()
            end
        end,
    },
}
