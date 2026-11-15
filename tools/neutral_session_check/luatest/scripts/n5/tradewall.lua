-- tools/neutral_session_check (task 0171, combat stage S5-3b): like trade.lua, but the player stands DIST units from Hulda
-- on a ring of DIRECTIONS directions, west of her first, STAY seconds at each, with a picture in the middle of each stay. Where the bar
-- counter or a wall lies between the two, the combat trace says "blocked by geometry" and nobody is hurt. Started by
-- "n5tradewall" (use at least 9000 frames; the same melee test settings as n5trade).
local core = require('openmw.core')

local ACTOR = { 336, -416, 64 } -- Hulda
local DIST = 100
local DIRECTIONS, STAY, FIRST = 4, 6, 6
local startedAt, placedFor, shotFor = nil, -1, -1

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if startedAt == nil then
                if t < FIRST then return end
                startedAt = t
            end
            local want = math.floor((t - startedAt) / STAY)
            if want >= DIRECTIONS then return end
            if placedFor ~= want then
                placedFor = want
                -- the ring starts WEST of her (behind the bar counter), so the blocked case is seen first, before the player is worn down
                local a = ((want + DIRECTIONS / 2) % DIRECTIONS / DIRECTIONS) * 2 * math.pi
                local px, py = ACTOR[1] + math.cos(a) * DIST, ACTOR[2] + math.sin(a) * DIST
                local yaw = math.atan2(ACTOR[1] - px, ACTOR[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = ACTOR[3] + 2, yaw = yaw })
                print(string.format('N5TRADEWALL direction %d at %.0f %.0f', want, px, py))
            end
            if shotFor ~= want and t - startedAt >= want * STAY + STAY / 2 then
                shotFor = want
                require('openmw.debug').takeScreenshot()
                print('N5TRADEWALL shot direction ' .. want)
            end
        end,
    },
}
