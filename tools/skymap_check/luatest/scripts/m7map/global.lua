-- tools/skymap_check (task 0201): walks the player through Whiterun by teleports on simulation time, takes a screenshot at
-- each stop (the compass is on screen), and in the "run" phase asks the menu script to save the game at the end. In the
-- "load" phase (a game loaded from that save) it only takes pictures where the player was saved.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.m7map.cfg')
local map = require('core.skyrimse.map')

-- { time (s), x, y, yaw, shot }: yaw is the heading as the engine stores it (0 faces +Y, clockwise); -pi/2 faces west.
local PLAN = {
    { 3, 25670, -7633, -math.pi / 2, true },    -- Whiterun's marker (19855, -7423) dead ahead, farms to both sides
    { 6, 21500, -7500, -math.pi / 2, true },    -- close to it
    { 9, 19000, -9500, math.pi, true },         -- south of the city, walking near the farm marker at (18313, -10665)
    { 12, 19000, -9500, math.pi / 2, true },    -- looking east
    { 15, 25670, -7633, -math.pi / 2, false },  -- back
}
local SAVE_AT = 20
local LOAD_SHOTS = { 3, 5 }

local t, t0 = 0, nil
local nextStep = 1
local shotAt = nil
local saved = false
local warned, lastBeat = false, 0

local function put(player, x, y, yaw)
    local ok, cell = pcall(world.getExteriorCell, math.floor(x / 4096), math.floor(y / 4096), player.cell)
    if not ok or cell == nil then cell = player.cell end
    player:teleport(cell, util.vector3(x, y, -3380), { rotation = util.transform.rotateZ(yaw), onGround = true })
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then
                if not warned then warned = true; print('M7MAP no player yet') end
                return
            end
            if not map.ready() then return end -- the plan starts when the pack has read its data
            t0 = t0 or core.getSimulationTime()
            t = core.getSimulationTime() - t0
            if t - lastBeat >= 5 then lastBeat = t; print(string.format('M7MAP beat t=%.1f', t)) end
            if cfg.phase == 'mare' then -- an interior: no teleports, only pictures
                if t >= 3 and not saved then saved = true; player:sendEvent('M7Shot') end
                return
            end
            if cfg.phase == 'load' then
                for i = #LOAD_SHOTS, 1, -1 do
                    if LOAD_SHOTS[i] and t >= LOAD_SHOTS[i] then
                        LOAD_SHOTS[i] = nil
                        print(string.format('M7MAP load shot at (%.0f, %.0f)', player.position.x, player.position.y))
                        player:sendEvent('M7Shot')
                    end
                end
                return
            end
            local step = PLAN[nextStep]
            if step and t >= step[1] then
                nextStep = nextStep + 1
                put(player, step[2], step[3], step[4])
                print(string.format('M7MAP step %d to (%d, %d)', nextStep - 1, step[2], step[3]))
                local wid = tonumber(tostring(player.cell.worldSpaceId):match(':w(%d+)$'))
                local xs, ys, found = map.marker_x(wid, step[2], step[3], 16384, 8192, 0),
                    map.marker_y(wid, step[2], step[3], 16384, 8192, 0), map.marker_discovered(wid, step[2], step[3], 16384, 8192, 0)
                local parts = {}
                for i = 1, #xs do parts[#parts + 1] = string.format('(%d,%d,%s)', xs[i], ys[i], tostring(found[i])) end
                print('M7MAP markers world ' .. tostring(wid) .. ': ' .. table.concat(parts, ' ') .. ' targets ' .. #map.target_x())
                if step[5] then shotAt = t + 1.0 end
            end
            if shotAt and t >= shotAt then
                shotAt = nil
                player:sendEvent('M7Shot')
            end
            if t >= SAVE_AT and not saved then
                saved = true
                storage.globalSection('M7MAP'):set('saveNow', true)
            end
        end,
    },
}
