-- tools/neutral_session_check (task 0323): a scripted walk or flight across Whiterun's hold, so that the exterior grid
-- streams across many cell borders in a repeatable way. Simulation time (use OPENEDEN_FIXED_FRAME_TIME), so the same
-- frames cross the same borders in every run. cfg.lua: mode 'walk' or 'fly' (position set every frame, onGround for
-- a walk, a fixed z, rise units above the start, for a flight), start (real second since the process started), speed (units/s), dir {x,y}, from {x,y}, rise (flight), duration.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local cfg = require('scripts.stream323.cfg')

local set = false
local godAt = 1
local lastCell = nil
local crossings = 0
local z0 = nil
local realStarted = false
local posAt = nil
local t0 = nil
local quitAt = nil
local real0 = nil

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            if not set and t >= godAt then
                set = true
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
                player:sendEvent('S323God')
            end
            -- The pack's records and the Papyrus host are read on workers: movement starts cfg.start REAL seconds after the
            -- process started (the path itself runs on simulation time from there).
            if t0 == nil then
                real0 = real0 or core.getRealTime()
                if core.getRealTime() - real0 < cfg.start then return end
                t0 = t
                print(string.format('S323 start sim=%.2f real=%.1f', t, core.getRealTime() - real0))
            end
            local e = t - t0
            if quitAt ~= nil and t >= quitAt then core.quit() end
            if e < 0 or e > cfg.duration then
                if e > cfg.duration and lastCell ~= 'done' then
                    lastCell = 'done'
                    print(string.format('S323 done crossings=%d t=%.2f', crossings, t))
                    quitAt = t + 3
                    pcall(function() player:sendEvent('S323Stop') end)
                end
                return
            end
            z0 = z0 or player.position.z + (cfg.rise or 0)
            if cfg.mode == 'real' then
                if not realStarted then
                    realStarted = true
                    local yaw = math.atan2(cfg.dir[1], cfg.dir[2])
                    player:teleport(player.cell, util.vector3(cfg.from[1], cfg.from[2], player.position.z + (cfg.rise or 0)),
                        { rotation = util.transform.rotateZ(yaw) })
                    player:sendEvent('S323Go', { speed = cfg.speedAttr, athletics = cfg.athletics })
                    posAt = { x = cfg.from[1], y = cfg.from[2], t = t }
                end
                local p = player.position
                if t - posAt.t >= 5 then
                    print(string.format('S323 speed %.0f u/s pos=%.0f,%.0f,%.0f', math.sqrt((p.x - posAt.x) ^ 2 + (p.y - posAt.y) ^ 2) / (t - posAt.t), p.x, p.y, p.z))
                    posAt = { x = p.x, y = p.y, t = t }
                end
                local c = player.cell
                local key = tostring(c.gridX) .. ',' .. tostring(c.gridY)
                if key ~= lastCell then
                    if lastCell ~= nil then crossings = crossings + 1 end
                    print(string.format('S323 cell %s t=%.2f pos=%.0f,%.0f,%.0f', key, t, p.x, p.y, p.z))
                    lastCell = key
                end
                return
            end
            local dx, dy = cfg.dir[1], cfg.dir[2]
            local len = math.sqrt(dx * dx + dy * dy)
            local x = cfg.from[1] + dx / len * cfg.speed * e
            local y = cfg.from[2] + dy / len * cfg.speed * e
            if cfg.mode == 'fly' then
                player:teleport(player.cell, util.vector3(x, y, z0), {})
            else
                player:teleport(player.cell, util.vector3(x, y, player.position.z), { onGround = true })
            end
            local c = player.cell
            local key = tostring(c.gridX) .. ',' .. tostring(c.gridY)
            if key ~= lastCell then
                if lastCell ~= nil then crossings = crossings + 1 end
                print(string.format('S323 cell %s t=%.2f pos=%.0f,%.0f,%.0f', key, t, x, y, player.position.z))
                lastCell = key
            end
        end,
    },
}
