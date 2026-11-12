-- tools/neutral_session_check (task 0129 swimming): the player side. Logs swim state and position each half second; in
-- walk mode it also holds "forward" so the player swims and walks east.
local debug = require('openmw.debug')
local core = require('openmw.core')
local self = require('openmw.self')
local types = require('openmw.types')

local tracing = false
local walk = false
local nextLog = 0
local shot = false

return {
    eventHandlers = {
        SwimGod = function() if not debug.isGodMode() then debug.toggleGodMode() end end,
        SwimTrace = function(d) tracing = true; walk = d.walk end,
        -- Terrain height probe: rays straight down along y = d.y for x = d.x0, d.x0 + d.step, ... (loaded cells only).
        SwimProbe = function(d)
            local nearby = require('openmw.nearby')
            local util = require('openmw.util')
            local line = {}
            for i = 0, d.n do
                local x = d.x0 + i * d.step
                local r = nearby.castRay(util.vector3(x, d.y, 6000), util.vector3(x, d.y, -9000), { collisionType = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.HeightMap })
                line[#line + 1] = string.format('%d:%s', x, r.hit and string.format('%.0f', r.hitPos.z) or 'none')
            end
            print('E2W probe y=' .. d.y .. ' ' .. table.concat(line, ' '))
        end,
    },
    engineHandlers = {
        onFrame = function()
            if not tracing then return end
            if walk then
                self.controls.movement = 1
                self.controls.run = true
            end
            local now = core.getRealTime()
            if now >= nextLog then
                nextLog = now + 0.5
                local p = self.position
                if types.Actor.isSwimming(self) and not shot and not walk then shot = true; debug.takeScreenshot() end
                local nearby = require('openmw.nearby')
                local wr = nearby.castRay(p + require('openmw.util').vector3(0, 0, 300), p - require('openmw.util').vector3(0, 0, 20000), { collisionType = nearby.COLLISION_TYPE.Water })
                print(string.format('E2W swim t=%.1f pos=%.0f,%.0f,%.0f cell=%s,%s swimming=%s walk=%s plane=%s', now, p.x, p.y, p.z,
                    tostring(self.cell.gridX), tostring(self.cell.gridY), tostring(types.Actor.isSwimming(self)), tostring(walk), wr.hit and string.format('%.0f', wr.hitPos.z) or 'none'))
            end
        end,
    },
}
