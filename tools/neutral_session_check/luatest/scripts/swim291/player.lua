-- tools/neutral_session_check (task 0291): the player side of the swimming scenario. Counts frames from Swim291Go and
-- runs: fall, surface swim forward, dive (look down, forward), come up (look up, forward), then (god mode off) swim
-- down and stay under water until the drowning damage shows. Logs "S291" lines every cfg.every frames: frame, position,
-- swimming, health. Controls and phases are by frame count, so the trace does not depend on wall time.
local debug = require('openmw.debug')
local self = require('openmw.self')
local types = require('openmw.types')
local camera = require('openmw.camera')
local cfg = require('scripts.swim291.swimcfg')

local go = false
local f = 0
local water = nil
local drownGod = false

local function phase()
    local a = cfg.fall
    if f < a then return 'fall' end
    a = a + cfg.surface
    if f < a then return 'surface' end
    a = a + cfg.dive
    if f < a then return 'dive' end
    a = a + cfg.rise
    if f < a then return 'rise' end
    a = a + cfg.drown
    if f < a then return 'drown' end
    return 'done'
end

return {
    eventHandlers = {
        Swim291God = function() if not debug.isGodMode() then debug.toggleGodMode() end end,
        -- Terrain heights on a grid (loaded cells only) to find water: rows of "x:z", z = none when nothing is hit.
        Swim291Probe = function(d)
            local nearby = require('openmw.nearby')
            local util = require('openmw.util')
            for j = 0, d.n do
                local y = d.y0 + j * d.step
                local line = {}
                for i = 0, d.n do
                    local x = d.x0 + i * d.step
                    local r = nearby.castRay(util.vector3(x, y, 20000), util.vector3(x, y, -20000),
                        { collisionType = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.HeightMap })
                    line[#line + 1] = r.hit and string.format('%.0f', r.hitPos.z) or 'none'
                end
                print(string.format('S291 probe y=%d x0=%d step=%d: %s', y, d.x0, d.step, table.concat(line, ' ')))
            end
        end,
        Swim291Go = function(d) go = true; f = 0; water = d.water end,
    },
    engineHandlers = {
        onFrame = function()
            if not go then return end
            f = f + 1
            local ph = phase()
            local c = self.controls
            c.run = false
            c.movement = 0
            if ph == 'surface' then
                c.movement = 1
            elseif ph == 'dive' then
                c.movement = 1
                if f < cfg.fall + cfg.surface + 60 then c.pitchChange = 0.012 end -- look down about 0.72 rad
            elseif ph == 'rise' then
                c.movement = 1
                if f < cfg.fall + cfg.surface + cfg.dive + 80 then c.pitchChange = -0.012 end -- look up about 0.96 rad
            elseif ph == 'drown' then
                if not drownGod then
                    drownGod = true
                    if debug.isGodMode() then debug.toggleGodMode() end
                end
                c.movement = 1
                if f < cfg.fall + cfg.surface + cfg.dive + cfg.rise + 80 then c.pitchChange = 0.015 end
            end
            if f % cfg.every == 0 then
                local p = self.position
                local h = types.Actor.stats.dynamic.health(self)
                print(string.format('S291 f=%d ph=%s pos=%.1f,%.1f,%.1f swimming=%s depth=%.1f pitch=%.2f health=%.1f', f, ph,
                    p.x, p.y, p.z, tostring(types.Actor.isSwimming(self)), (water or 0) - p.z, camera.getPitch(), h.current))
            end
            if cfg.shots[f] then debug.takeScreenshot() end
        end,
    },
}
