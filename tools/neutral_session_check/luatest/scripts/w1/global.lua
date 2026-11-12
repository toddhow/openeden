-- tools/neutral_session_check (task 0127, stage W-S1): leaves the Bannered Mare by its front door into WhiterunWorld,
-- looks at the sky at several hours of the day (screenshots), then speeds up the clock so that the regional weather is
-- chosen again and again, and takes a picture whenever the weather changes. Logs "W1 ..." lines. Real seconds.
local core = require('openmw.core')
local world = require('openmw.world')

local start = nil
local step = 0
local DOORBASE = 'skyrimse:b152263'
local HOURS = { 6.0, 7.5, 9.0, 12.0, 16.0, 18.5, 20.0, 22.5 }
local hourIndex = 0
local nextAt = nil
local fastFrom = nil
local lastWeather = nil
local shots = 0
local lastLog = 0
local midShot = false

local function nearestDoor(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

local function weatherName(w)
    if w == nil then return 'none' end
    local ok, n = pcall(function() return w.name end)
    return ok and tostring(n) or '?'
end

local function describe(player)
    local ok, cur = pcall(core.weather.getCurrent, player.cell)
    local ok2, nxt = pcall(core.weather.getNext, player.cell)
    local ok3, tr = pcall(core.weather.getTransition, player.cell)
    local hour = world.mwscript.getGlobalVariables(player).gamehour
    return string.format('cur=%s next=%s transition=%s hour=%.2f', ok and weatherName(cur) or 'err',
        ok2 and weatherName(nxt) or 'err', ok3 and tostring(tr) or 'err', hour)
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if step == 0 and t >= 6 then
                step = 1
                world.mwscript.getGlobalVariables(player).gamehour = 12
                local door = nearestDoor(player.cell, DOORBASE, { -128, -1152, 120 })
                if door == nil then print('W1 door not found') else
                    print('W1 activating the front door')
                    door:activateBy(player)
                end
            elseif step == 1 and t >= 14 then
                step = 2
                nextAt = t
                print('W1 outside: ' .. describe(player))
                player:sendEvent('W1Sky')
            elseif step == 2 and t >= nextAt then
                hourIndex = hourIndex + 1
                if hourIndex > #HOURS then
                    step = 3
                    fastFrom = t
                    -- game minutes pass fast now: regional weather is chosen again every 12 game hours
                    world.mwscript.getGlobalVariables(player).timescale = 4000
                    print('W1 fast clock on: ' .. describe(player))
                    return
                end
                world.mwscript.getGlobalVariables(player).gamehour = HOURS[hourIndex]
                nextAt = t + 3
                print(string.format('W1 hour %.2f set: %s', HOURS[hourIndex], describe(player)))
                -- the picture is taken 2 s later, after the sky has followed the clock
                player:sendEvent('W1ShotLater', 2)
            elseif step == 3 then
                local cur = describe(player)
                if t - lastLog >= 1 then
                    lastLog = t
                    print('W1 fast ' .. cur)
                end
                local ok, w = pcall(core.weather.getCurrent, player.cell)
                local name = ok and weatherName(w) or 'err'
                if lastWeather ~= nil and name ~= lastWeather and shots < 4 then
                    shots = shots + 1
                    print('W1 weather changed ' .. lastWeather .. ' -> ' .. name)
                    player:sendEvent('W1ShotLater', 0.5)
                end
                lastWeather = name
                local okT, tr = pcall(core.weather.getTransition, player.cell)
                if okT and type(tr) == 'number' and tr > 0 and tr <= 0.5 and not midShot then
                    midShot = true
                    print('W1 mid transition ' .. cur)
                    player:sendEvent('W1ShotLater', 0.5)
                end
                if t - fastFrom > 140 then
                    step = 4
                    world.mwscript.getGlobalVariables(player).timescale = 30
                    print('W1 done: ' .. describe(player))
                end
            end
        end,
    },
}
