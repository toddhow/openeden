-- tools/neutral_session_check (task 0157): path queries with several agent sizes over the Bannered Mare, to find which
-- agent size / which segment loses the way to the upstairs beds. Prints
--   SKYR r=<radius> <from>-><to> status=<name> nodes=<n> last=<x,y,z> distToGoal=<units>
-- Each query is retried every half second for up to 60 s (the navmesh of a new agent size is built on workers).
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local debug = require('openmw.debug')
local camera = require('openmw.camera')

local statusNames = {}
for name, value in pairs(nearby.FIND_PATH_STATUS) do statusNames[value] = name end

local P = {
    start1 = util.vector3(44, 28, 68),
    start2 = util.vector3(-122, -55, 70),
    start3 = util.vector3(-241, -1071, 68),
    door = util.vector3(-105, 339, 90),
    stairfoot = util.vector3(-1200, -340, 68),
    bed1 = util.vector3(157, 458, 68),
    bedS = util.vector3(-209, 234, 320),
    bedL = util.vector3(-482, -503, 320),
    bedL2 = util.vector3(-1357, -291, 318),
}

local PAIRS = {
    { 'start1', 'bed1' }, { 'start2', 'bedS' }, { 'start3', 'bedL' }, { 'start2', 'stairfoot' }, { 'start1', 'stairfoot' },
    { 'start3', 'stairfoot' }, { 'stairfoot', 'bedL2' }, { 'stairfoot', 'bedL' }, { 'start2', 'start3' },
}
local RADII = { 18, 20, 22 }

local queue = {}
for _, r in ipairs(RADII) do
    for _, pr in ipairs(PAIRS) do queue[#queue + 1] = { r = r, a = pr[1], b = pr[2] } end
end

local index, nextTry, t0, started = 1, 0, nil, nil
local RUN_QUERIES = false -- the picture run does not need them
local RUN_MAP = false
local RUN_SCAN = false
-- A map of what the navmesh reaches from START (Mare ground floor): for each cell of a grid, the first floor hit by a
-- ray from z 420 down, and whether a path from START reaches it (U/L upper/lower floor reached, u/l not reached).
local MAP = { x0 = -1500, x1 = 300, y0 = -1100, y1 = 600, step = 50, from = util.vector3(44, 28, 68) }
local mapY, mapDone = nil, false
local function mapRow()
    local self = require('openmw.self')
    local y = mapY
    local chars = {}
    for x = MAP.x0, MAP.x1, MAP.step do
        local hit = nearby.castRay(util.vector3(x, y, 420), util.vector3(x, y, -200),
            { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
        if not hit.hit then chars[#chars + 1] = '.'
        else
            local status, path = nearby.findPath(MAP.from, hit.hitPos, { destinationTolerance = 30 })
            local z = hit.hitPos.z
            local ok = status == nearby.FIND_PATH_STATUS.Success
            local c = z < 200 and 'L' or (z < 250 and 'M' or (z < 400 and 'F' or 'R'))
            chars[#chars + 1] = ok and c or string.lower(c)
        end
    end
    print(string.format('SKYMAP y=%5d %s', y, table.concat(chars)))
end

local function fmt(v) return string.format('%.0f,%.0f,%.0f', v.x, v.y, v.z) end

return {
    eventHandlers = {
        SRWire = function() debug.toggleRenderMode(debug.RENDER_MODE.NavMesh) end,
        SRShot = function() debug.takeScreenshot() end,
        SRLook = function(d) camera.setMode(camera.MODE.FirstPerson); camera.setPitch(d.pitch); camera.setYaw(d.yaw) end,
    },
    engineHandlers = {
        onUpdate = function()
            local now = core.getRealTime()
            started = started or now
            if RUN_SCAN and not mapDone and now - started > 6 then
                mapDone = true
                local self = require('openmw.self')
                for _, p in ipairs({ { -809.3, -517.9 }, { -795, -520 }, { -780, -523 }, { -770, -525 } }) do
                    local hit = nearby.castRay(util.vector3(p[1], p[2], 130), util.vector3(p[1], p[2], -50),
                        { ignore = self.object, collisionType = nearby.COLLISION_TYPE.Default })
                    local id = 'none'
                    if hit.hit and hit.hitObject then id = hit.hitObject.recordId end
                    print(string.format('SKYSCAN %.0f,%.0f -> z %s obj %s', p[1], p[2], hit.hit and string.format('%.1f', hit.hitPos.z) or 'none', id))
                    local objs = {}
                    for _, o in ipairs(nearby.activators) do
                        local d = (o.position - util.vector3(p[1], p[2], o.position.z)):length()
                        if d < 60 then objs[#objs + 1] = o.recordId .. '@' .. string.format('%.0f,%.0f,%.0f', o.position.x, o.position.y, o.position.z) end
                    end
                    print('SKYSCAN near activators: ' .. table.concat(objs, ' '))
                end
                return
            end
            if RUN_MAP and not mapDone and now - started > 6 then
                mapY = mapY or MAP.y1
                mapRow()
                mapY = mapY - MAP.step
                if mapY < MAP.y0 then mapDone = true end
                return
            end
            if not RUN_QUERIES or now - started < 4 or now < nextTry then return end
            local q = queue[index]
            if q == nil then return end
            t0 = t0 or now
            local from, to = P[q.a], P[q.b]
            local status, path = nearby.findPath(from, to, {
                destinationTolerance = 24,
                agentBounds = { shapeType = nearby.COLLISION_SHAPE_TYPE.Aabb, halfExtents = util.vector3(q.r, q.r, 66.5) },
            })
            local waited = now - t0
            if status == nearby.FIND_PATH_STATUS.Success or status == nearby.FIND_PATH_STATUS.PartialPath
                or waited > 60 then
                local last = path[#path]
                print(string.format('SKYR r=%d %s->%s status=%s nodes=%d last=%s distToGoal=%.0f t=%.1f', q.r, q.a, q.b,
                    statusNames[status] or tostring(status), #path, last and fmt(last) or 'none',
                    last and (last - to):length() or -1, waited))
                index = index + 1
                t0 = nil
            end
            nextTry = now + 0.5
        end,
    },
}
