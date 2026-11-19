-- tools/skygi_check (task 0358): sets the game hour, then visits the spots of scripts/skygi/cfg.lua (x, y, yaw, pitch; yaw 0
-- looks north, pitch positive looks down) in the player's current world, and takes a picture at each after `dwell` real seconds.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts/skygi/cfg')
local start, hourSet, nextAt, index, phase = nil, false, 4, 0, 'go'

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if not hourSet and t >= 1 then
                hourSet = true
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = cfg.hour end)
            end
            if t < nextAt then return end
            if phase == 'go' then
                index = index + 1
                local s = cfg.spots[index]
                if s == nil then phase = 'done'; print('SKYGI done'); return end
                local z = s[5]
                local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(s[1], s[2], 0), player.cell) end)
                if ok and h then z = h + 40 end
                print(string.format('SKYGI spot %d at %.0f,%.0f z %.0f', index, s[1], s[2], z or 0))
                player:teleport(player.cell, util.vector3(s[1], s[2], z or 0), { onGround = true })
                player:sendEvent('SkyGiLook', { yaw = s[3], pitch = s[4] })
                phase = 'shot'
                nextAt = t + cfg.dwell
            elseif phase == 'shot' then
                player:sendEvent('SkyGiShot')
                phase = 'go'
                nextAt = t + 1.5
            end
        end,
    },
}
