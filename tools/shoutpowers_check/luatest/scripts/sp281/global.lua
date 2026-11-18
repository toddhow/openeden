-- tools/shoutpowers_check (task 0281): finishes the character creation without its window (task 0225 tested the window),
-- prints the powers and the chosen one through `core.skyrimse.character` every two seconds of simulation time and, in the
-- run phase, asks the menu script to save 3 s after a power got chosen.
local core = require('openmw.core')
local storage = require('openmw.storage')
local cfg = require('scripts.sp281.cfg')
local chr = require('core.skyrimse.character')

local t0, last, chosenAt, saved, neededSince, finished = nil, -10, nil, false, nil, false

local function describe(now)
    local parts = {}
    for i = 0, chr.power_count() - 1 do
        parts[#parts + 1] = string.format('%s%s/%s/ready=%s/cd=%d', chr.power_chosen() == i and '*' or '',
            chr.power_name(i), chr.power_source(i), tostring(chr.power_ready(i, now)), chr.power_cooldown(i, now))
    end
    return table.concat(parts, ' ; ')
end

return {
    engineHandlers = {
        onUpdate = function()
            if not chr.ready() then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            local now = math.floor(core.getGameTime())
            if chr.needed() and not finished then
                neededSince = neededSince or t
                if t - neededSince >= 1 then
                    finished = true
                    core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'finish' })
                end
            end
            if t - last >= 2 then
                last = t
                print(string.format('SHOUTPOWERS %s t=%.1f needed=%s chosen=%d powers: %s', cfg.phase, t,
                    tostring(chr.needed()), chr.power_chosen(), describe(now)))
            end
            if chr.power_chosen() >= 0 and not chosenAt then chosenAt = t end
            if cfg.phase == 'run' and chosenAt and t - chosenAt >= 3 and not saved then
                saved = true
                storage.globalSection('SP281'):set('saveNow', true)
            end
        end,
    },
}
