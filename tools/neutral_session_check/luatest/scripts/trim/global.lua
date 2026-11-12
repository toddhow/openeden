-- tools/neutral_session_check (task 0151, floating mountain slabs): from an exterior start near @Tamriel,104403,18631 it prints the land height grid
-- the engine reports ("TRIM LANDH") and the position of the MountainTrim01_LightSN placement, then visits a few vantage points,
-- aiming the camera at the slab with a screenshot and a ray survey each ("TRIM ..." lines, see player.lua).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local SX, SY = 104403, 18631
local VANTAGES = { {-1400, -1400}, {1400, -1000}, {0, -2200}, {-2200, 0} }
local start, done, shots, nextShot = nil, false, 0, 0

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if done and shots >= 1 and shots <= #VANTAGES and t >= nextShot then
                local o = VANTAGES[shots]
                shots = shots + 1
                nextShot = t + 6
                local x, y = SX + o[1], SY + o[2]
                local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(x, y, 0), player.cell) end)
                local z = (ok and h or -8000) + 140
                player:teleport(player.cell, util.vector3(x, y, z), { rotation = player.rotation })
                local dx, dy = SX - x, SY - y
                local yaw = math.atan2(dx, dy)
                local dist = math.sqrt(dx * dx + dy * dy)
                local pitch = math.atan2(-7990 - z, dist)
                print(string.format('TRIM vantage %d at %.0f,%.0f,%.0f yaw %.3f pitch %.3f', shots - 1, x, y, z, yaw, pitch))
                player:sendEvent('TrimAim', { yaw = yaw, pitch = pitch })
                return
            end
            if done or t < 8 then return end
            done = true
            world.mwscript.getGlobalVariables(player).gamehour = 12
            for dy = -768, 768, 128 do
                local row = {}
                for dx = -768, 768, 128 do
                    local x = 104320 + dx
                    local y = 18560 + dy
                    local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(x, y, 0), player.cell) end)
                    row[#row + 1] = ok and string.format('%.0f', h) or 'ERR'
                end
                print(string.format('TRIM LANDH y=%d x0=%d: %s', 18560 + dy, 104320 - 768, table.concat(row, ' ')))
            end
            for x = 24, 26 do
                for y = 3, 5 do
                    local cell = world.getExteriorCell(x, y, player.cell)
                    for _, obj in ipairs(cell:getAll()) do
                        if string.find(obj.recordId, 'ountainTrim') or string.find(obj.recordId, 'b197124') then
                            local z, yy, xx = obj.rotation:getAnglesZYX()
                            print(string.format('TRIM obj %s at %.1f,%.1f,%.1f anglesZYX %.4f %.4f %.4f', obj.recordId, obj.position.x, obj.position.y, obj.position.z, z, yy, xx))
                        end
                    end
                end
            end
            local variant = require('scripts.trim.variant')
            if variant ~= 'none' then
                -- (Lua's rotateX/Y/Z turn clockwise for a positive angle, so the raw angles go in as they are.) Raw stored angles of skyrimse:b197124 (Skyrim.esm 0x000A7D22). 'old' re-applies the pack's old order (Rz*Ry*Rx, a control: the
                -- picture must not change), 'fix' the new one (Rx*Ry*Rz, Z first).
                local ax, ay, az = 0.073, 0.382, 2.943
                local R
                if variant == 'old' then
                    R = util.transform.rotateZ(az) * util.transform.rotateY(ay) * util.transform.rotateX(ax)
                else
                    R = util.transform.rotateX(ax) * util.transform.rotateY(ay) * util.transform.rotateZ(az)
                end
                local cell = world.getExteriorCell(25, 4, player.cell)
                for _, obj in ipairs(cell:getAll()) do
                    if obj.recordId == 'skyrimse:b197124' and math.abs(obj.position.x - SX) < 5 and math.abs(obj.position.y - SY) < 5 then
                        obj:teleport(obj.cell, obj.position, { rotation = R })
                        print('TRIM rotated the slab: ' .. variant)
                    end
                end
            end
            shots = 1
        end,
    },
}
