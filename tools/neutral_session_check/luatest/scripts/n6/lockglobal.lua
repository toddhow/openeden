-- tools/neutral_session_check (task 0177): the lock half of the save round trip. Runs the key scenario of task 0128
-- (scripts/n4doors/key.lua: the locked door of Angarvunde01 opens with OPENEDEN_NEUTRAL_KEYS=skyrimse:8715F, the player
-- goes through, comes back and goes through again) and, after it (24 s), tells the menu script that it may save. The save
-- must then hold the opened lock and the held key (the engine logs "Neutral save: ... 1 opened locks, 1 keys").
local core = require('openmw.core')
local storage = require('openmw.storage')
local base = require('scripts.n4doors.key')

local start = nil
return {
    engineHandlers = {
        onUpdate = function(...)
            base.engineHandlers.onUpdate(...)
            start = start or core.getRealTime()
            if core.getRealTime() - start > 26 then
                storage.globalSection('N6'):set('reported', true)
            end
        end,
    },
}
