-- tools/neutral_session_check (task 0135 stage S6f): the global half of "n5follow". The player's camera follows one actor
-- at a time: every STEP seconds of real time it stands the player DIST units from the actor (always on the same side, OFFSET
-- radians from the actor's +Y) looking at it and, 0.25 s later, takes a picture; each actor of TARGETS gets WINDOW seconds
-- (a number in its record id; b108144 and b80802 sleep in the Bannered Mare, b80807 sits). Prints "N5FOL <tag> x y z".
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.followcfg') -- written by check.sh from FOLLOW_TARGETS / FOLLOW_WINDOW / FOLLOW_STEP
local TARGETS = cfg.targets
local WINDOW = cfg.window
local STEP = cfg.step
local DIST = cfg.dist
local OFFSET = cfg.offset
local start, nextAt, shotAt = nil, 0, nil

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if shotAt then
                if t >= shotAt then
                    player:sendEvent('N5Shot')
                    shotAt = nil
                end
                return
            end
            if t < 6 or t < nextAt then return end
            local slot = math.floor((t - 6) / WINDOW) + 1
            if slot > #TARGETS then return end
            nextAt = t + STEP
            local tag = TARGETS[slot]
            local a = find(player.cell, tag)
            if a == nil then return end
            local p = a.position
            local px, py = p.x + math.sin(OFFSET) * DIST, p.y + math.cos(OFFSET) * DIST
            local look = math.atan2(p.x - px, p.y - py)
            player:teleport(player.cell, util.vector3(px, py, p.z + 2), { rotation = util.transform.rotateZ(look) })
            print(string.format('N5FOL %s %.1f %.1f %.1f t %.1f', tag, p.x, p.y, p.z, t))
            shotAt = t + 0.25
        end,
    },
}
