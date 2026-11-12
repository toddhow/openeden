-- tools/neutral_session_check (task 0135, decision 0017 stage N5-3): proves the physics body of the Bannered Mare's
-- Skyrim NPCs. For each actor of the cell the player is put DIST units from the actor's placement, on the side of the
-- cell's middle, facing it, settles for SETTLE frames, then walks toward it at running speed for WALK frames. The script
-- logs where the player started and ended and what the ray straight ahead hits (a body in the way is reported as the
-- object the ray found), so "the player is stopped by the NPC" is two distances: moved vs. the distance to the actor.
-- With [Game] neutral actors off, or before this task, the player walks through the spot.
local self = require('openmw.self')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

-- x, y, z and z rotation of the actors of WhiterunBanneredMare (task 0121's list).
local ACTORS = {
    { 336, -416, 64, -1.52699 },
    { 44.0677, 27.7724, 68, 2.88541 },
    { -136.195, -699.55, 69.1957, -1.68318 },
    { 66.3002, -579.985, 67.5885, -3.14159 },
    { -122.041, -55.383, 64, -3.14159 },
    { -241.652, -1071.21, 68, 0 },
}
local DIST = 200
local SETTLE = 40
local WALK = 300
local START = 200

local frame, index, phaseStart, state, startPos = 0, 0, 0, 'wait', nil

local function fmt(p) return string.format('%.1f,%.1f,%.2f', p.x, p.y, p.z) end

local function ahead()
    local p = self.object.position + util.vector3(0, 0, 60)
    local yaw = self.object.rotation:getYaw()
    local dir = util.vector3(math.sin(yaw), math.cos(yaw), 0)
    local hit = nearby.castRay(p, p + dir * 400, { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
    if not hit.hit then return 'clear' end
    local o = hit.hitObject
    return string.format('%.1f units: %s', (hit.hitPos - p):length(), o and tostring(o.recordId) or '?')
end

return {
    engineHandlers = {
        onFrame = function()
            frame = frame + 1
            if state == 'wait' and frame >= START then state = 'next' end
            if state == 'next' then
                index = index + 1
                if index > #ACTORS then
                    state = 'done'
                    print('N5B done')
                    return
                end
                local a = ACTORS[index]
                local cx, cy = -50 - a[1], -450 - a[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = a[1] + cx / len * DIST, a[2] + cy / len * DIST
                local yaw = math.atan2(a[1] - px, a[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = a[3] + 2, yaw = yaw })
                phaseStart = frame
                state = 'settle'
            elseif state == 'settle' then
                self.controls.movement = 0
                if frame - phaseStart >= SETTLE then
                    startPos = self.object.position
                    local a = ACTORS[index]
                    local d = (util.vector3(a[1], a[2], 0) - util.vector3(startPos.x, startPos.y, 0)):length()
                    print(string.format('N5B [actor %d] start %s, %.1f units from the placement, ahead: %s', index,
                        fmt(startPos), d, ahead()))
                    phaseStart = frame
                    state = 'walk'
                end
            elseif state == 'walk' then
                self.controls.movement = 1
                self.controls.run = true
                if frame - phaseStart >= WALK then
                    local p = self.object.position
                    local moved = (util.vector3(p.x, p.y, 0) - util.vector3(startPos.x, startPos.y, 0)):length()
                    print(string.format('N5B [actor %d] end %s, moved %.1f units, ahead: %s', index, fmt(p), moved, ahead()))
                    self.controls.movement = 0
                    state = 'next'
                end
            end
        end,
    },
}
