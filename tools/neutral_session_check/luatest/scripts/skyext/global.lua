-- tools/neutral_session_check (task skyext): bisection helper. At 8 s it lists the objects of the exterior cells near the
-- player that lie in the picture's direction (sorted by id), disables those with index LO..HI (the markers below are
-- rewritten by the driver script), prints the count ("SKYEXT cand N") and asks the player to take a picture.
local core = require('openmw.core')
local world = require('openmw.world')

local VAR = 0 -- VARIANT
local LO, HI = 1, 0 -- BISECT
local start = nil
local done = false
local util = require('openmw.util')
local shots, nextShot = 0, 0
local OFFS = { {0,-1000}, {700,0}, {0,-2000}, {-700,0} }

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if done and shots >= 1 and shots <= #OFFS and t >= nextShot then
                local o = OFFS[shots]
                shots = shots + 1
                nextShot = t + 5
                player:teleport(player.cell, util.vector3(104000 + o[1], 18000 + o[2], -8001), { rotation = player.rotation })
                print(string.format('SKYEXT moved to %d,%d', 104000 + o[1], 18000 + o[2]))
                player:sendEvent('SkyextAim', { yaw = 0 })
                return
            end
            if done or t < 8 then return end
            done = true
            world.mwscript.getGlobalVariables(player).gamehour = 12
            local list = {}
            for x = 23, 27 do
                for y = 3, 7 do
                    local cell = world.getExteriorCell(x, y, player.cell)
                    for _, obj in ipairs(cell:getAll()) do
                        local p = obj.position
                        local dx, dy = p.x - 104000, p.y - 18000
                        if dx > -5500 and dx < 2000 and dy > 300 and dy < 7000 then
                            list[#list + 1] = obj
                        end
                    end
                end
            end
            table.sort(list, function(a, b) return tostring(a.id) < tostring(b.id) end)
            print('SKYEXT cand ' .. #list)
            for i = LO, math.min(HI, #list) do
                local o = list[i]
                local z, y, x = o.rotation:getAnglesZYX()
                print(string.format('SKYEXT culprit %s at %.0f,%.0f,%.0f scale %.2f anglesZYX %.4f %.4f %.4f', o.recordId, o.position.x, o.position.y, o.position.z, o.scale, z, y, x))
                local sy, sx = 1, 1
                if VAR == 2 then sx = -1 elseif VAR == 3 then sy = -1 elseif VAR == 4 then sx, sy = -1, -1 end
                if VAR > 0 then
                    o:teleport(o.cell, o.position, { rotation = util.transform.rotateZ(z) * util.transform.rotateY(y * sy) * util.transform.rotateX(x * sx) })
                end
            end
            player:sendEvent('SkyextAim', { yaw = 0 })
            shots = 1
            nextShot = t + 5
        end,
    },
}
