-- tools/shouts_check (task 0205): prints the shout state through the pack's `core.skyrimse.shouts` namespace every two
-- seconds of simulation time and, in the "run" phase, asks the menu script to save the game at 9 s.
local core = require('openmw.core')
local storage = require('openmw.storage')
local cfg = require('scripts.sh205.cfg')
local shouts = require('core.skyrimse.shouts')

local t0, last, saved = nil, -10, false

return {
    engineHandlers = {
        onUpdate = function()
            if not shouts.ready() then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if t - last >= 2 then
                last = t
                print(string.format('SHOUTSAVE %s t=%.1f name=%s unlocked=%d words=%d souls=%d cooldown=%.2f total=%.2f',
                    cfg.phase, t, shouts.equipped_name(), shouts.unlocked_count(), shouts.word_count(), shouts.souls(),
                    shouts.cooldown(), shouts.cooldown_total()))
            end
            if cfg.phase == 'run' and t >= 9 and not saved then
                saved = true
                storage.globalSection('SH205'):set('saveNow', true)
            end
        end,
    },
}
