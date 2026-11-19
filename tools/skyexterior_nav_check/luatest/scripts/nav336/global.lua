-- Task 0336 (tools/skyexterior_nav_check): logs where the neutral NPCs whose record id contains one of cfg.tags are, every
-- cfg.every simulation seconds, and lists the active actors once. The walking itself is the pack's test rule
-- ([extension.skyrimse] actor test goto), so the NAV336 lines are the evidence next to the NACTGOTO trace lines.
local core = require('openmw.core')
local world = require('openmw.world')
local cfg = require('scripts.nav336.cfg')
local util = require('openmw.util')
local probed, shotAt, nextCam, shots = false, nil, nil, 0
local set, listed, last, real0 = false, false, -100, nil
return { engineHandlers = { onUpdate = function()
    local player = world.players[1]
    if player == nil then return end
    local t = core.getSimulationTime()
    if not set and t >= 1 then
        set = true
        pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
        player:sendEvent('N336God')
    end
    if t >= cfg.quit then core.quit() end
    if core.getRealTime() >= 40 and not listed then
        listed = true
        for _, a in ipairs(world.activeActors) do
            print(string.format('NAV336 actor %s at %.0f,%.0f,%.0f', a.recordId, a.position.x, a.position.y, a.position.z))
        end
    end
    if cfg.probe and core.getRealTime() >= 30 and not probed then
        probed = true
        local seen = 0
        local gx, gy = math.floor(cfg.probe[1] / 4096), math.floor(cfg.probe[2] / 4096)
        local ok, cell = pcall(world.getExteriorCell, gx, gy, player.cell.worldSpaceId)
        print('NAV336 probe cell', ok, cell and cell.gridX, cell and cell.gridY)
        for _, o in ipairs(ok and cell:getAll() or {}) do
            local dx, dy = o.position.x - cfg.probe[1], o.position.y - cfg.probe[2]
            if dx * dx + dy * dy <= cfg.probe[3] * cfg.probe[3] then
                print(string.format('NAV336 near %s at %.0f,%.0f,%.0f', o.recordId, o.position.x, o.position.y, o.position.z))
            end
        end
    end
    -- a camera mode (cfg.cam = { tag, dist, every, first, count }): stand the player beside the NPC and take a picture
    if cfg.cam then
        if shotAt and t >= shotAt then
            player:sendEvent('N336Shot')
            shotAt = nil
        elseif not shotAt and shots < cfg.cam[5] and t >= (nextCam or cfg.cam[4]) then
            local found
            for dx = -2, 2 do
                for dy = -2, 2 do
                    local ok, cell = pcall(world.getExteriorCell, 5 + dx, -11 + dy, player.cell.worldSpaceId)
                    if ok and cell and not found then
                        for _, o in ipairs(cell:getAll()) do
                            if o.recordId:find(cfg.cam[1], 1, true) then found = o break end
                        end
                    end
                end
            end
            do
                local o = found
                if o then
                    local p = o.position
                    local px, py = p.x - cfg.cam[2], p.y + cfg.cam[2] * 0.3
                    player:teleport(player.cell, util.vector3(px, py, p.z + 150),
                        { rotation = util.transform.rotateZ(math.atan2(p.x - px, p.y - py)) })
                    print(string.format('NAV336 cam %d guard at %.0f,%.0f,%.0f t=%.1f', shots + 1, p.x, p.y, p.z, t))
                    shots = shots + 1
                    nextCam = t + cfg.cam[3]
                    shotAt = t + 0.1
                end
            end
        end
    end
    if t - last >= cfg.every then
        last = t
        for _, a in ipairs(world.activeActors) do
            for _, tag in ipairs(cfg.tags) do
                if a.recordId:find(tag, 1, true) then
                    print(string.format('NAV336 pos %s t=%.1f %.1f,%.1f,%.1f', a.recordId, t, a.position.x, a.position.y, a.position.z))
                end
            end
        end
    end
end } }
