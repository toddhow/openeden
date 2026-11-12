-- tools/neutral_session_check (task skyext): the player side. Aims the first person camera at a heading, surveys a grid of
-- rendering rays through the picture (what is hit, its model, how far, how high above the camera) and takes a picture.
local debug = require('openmw.debug')
local core = require('openmw.core')
local camera = require('openmw.camera')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local types = require('openmw.types')

local shotAt = nil
local aimYaw = 0

local function modelOf(obj)
    local ok, model = pcall(function()
        local t = obj.type
        if t and t.record then return t.record(obj).model end
        return nil
    end)
    return ok and model or '?'
end

local function survey()
    local from = camera.getPosition()
    local seen = {}
    for pitch = 0.05, 0.09, 0.02 do
        for dy = 0, 0, 1 do
            local yaw = aimYaw + dy
            local dir = util.vector3(math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), math.sin(pitch))
            local r = nearby.castRenderingRay(from, from + dir * 30000)
            if r.hit and r.hitObject == nil then local hp = r.hitPos; print(string.format("SKYEXT terrain-or-other hit dist %.0f at %.0f,%.0f,%.0f", (hp - from):length(), hp.x, hp.y, hp.z)) end
            if r.hit and r.hitObject ~= nil then
                local key = r.hitObject.id
                if seen[key] == nil then
                    seen[key] = true
                    local hp = r.hitPos
                    print(string.format('SKYEXT hit %s model %s dist %.0f at %.0f,%.0f,%.0f above camera %.0f', r.hitObject.recordId,
                        tostring(modelOf(r.hitObject)), (hp - from):length(), hp.x, hp.y, hp.z, hp.z - from.z))
                end
            end
        end
    end
end

return {
    eventHandlers = {
        SkyextAim = function(data)
            camera.setMode(camera.MODE.FirstPerson)
            aimYaw = data.yaw
            camera.setYaw(data.yaw)
            camera.setPitch(-0.1)
            shotAt = core.getRealTime() + 1.5
            print(string.format('SKYEXT aim yaw=%.2f', data.yaw))
        end,
    },
    engineHandlers = {
        onFrame = function()
            if shotAt ~= nil and core.getRealTime() >= shotAt then
                shotAt = nil
                survey()
                debug.takeScreenshot()
                print('SKYEXT shot')
            end
        end,
    },
}
