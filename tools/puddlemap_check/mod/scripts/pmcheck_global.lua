-- Task 0095, stage W-d check: puts the weather and the hour where the scenario says, then visits each pose of
-- scripts/pmcheck_poses.lua (written by check.sh): teleport the player, wait `settle` seconds of simulation time, ask the
-- player script for a screenshot. Prints PM lines that check.sh reads. The poses file also names the weather to start
-- (and the one to change to after `change_after` seconds, to watch the puddles shrink).
local world = require('openmw.world')
local core = require('openmw.core')
local util = require('openmw.util')

local cfg = require('scripts.pmcheck_poses')

local started = nil
local index = 0
local nextAt = 0
local weatherSet = false
local changed = false

local function weatherRecord(id)
    for _, w in ipairs(core.weather.records) do
        if w.recordId == id then return w end
    end
    print('PM no weather ' .. tostring(id))
    return nil
end

local desired = nil
local lastRegion = nil

-- Sets the weather of the region the player is in now (a pose may be in another region than the last one).
local function setWeather(id)
    desired = id
    local record = weatherRecord(id)
    local player = world.players[1]
    local region = player and player.cell and player.cell.region
    if region == nil or region == '' then region = cfg.region end
    if record then
        core.weather.changeWeather(region, record)
        lastRegion = region
        print('PM weather ' .. id .. ' in ' .. region)
    end
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            started = started or core.getSimulationTime()
            local t = core.getSimulationTime() - started
            if not weatherSet then
                weatherSet = true
                world.mwscript.getGlobalVariables().gamehour = cfg.hour
                world.setGameTimeScale(cfg.timescale or 1)
                setWeather(cfg.weather)
                print('PM start')
                nextAt = cfg.first or 4
            end
            if desired then
                local region = player.cell and player.cell.region
                if region and region ~= '' and region ~= lastRegion then
                    setWeather(desired)
                end
            end
            if cfg.change_after and not changed and t >= cfg.change_after then
                changed = true
                setWeather(cfg.change_to)
            end
            if t >= nextAt and index < #cfg.poses then
                index = index + 1
                local p = cfg.poses[index]
                world.mwscript.getGlobalVariables().gamehour = cfg.hour
                player:teleport('', util.vector3(p.x, p.y, p.z),
                    { rotation = util.transform.rotateZ(p.yaw or 0) })
                player:sendEvent('PMPose', { name = p.name, pitch = p.pitch or 0.4, index = index, t = t, probes = p.probes })
                nextAt = t + (p.hold or cfg.hold or 2)
            elseif index >= #cfg.poses and t >= nextAt then
                player:sendEvent('PMQuit', {})
                nextAt = 1e9
            end
        end,
    },
}
