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
local RUN_RAYS = false
local RUN_HOLE = false
local RUN_OBJ = false
local RUN_RAMP = false
local RUN_SEG = true
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
        SRView = function(v)
            local m = { col = debug.RENDER_MODE.CollisionDebug, nav = debug.RENDER_MODE.NavMesh, recast = debug.RENDER_MODE.RecastMesh }
            if m[v] then debug.toggleRenderMode(m[v]) end
        end,
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

            if RUN_RAYS and not mapDone and now - started > 6 then
                mapDone = true
                local self = require('openmw.self')
                local function cast(a, b)
                    local hit = nearby.castRay(a, b, { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
                    return hit.hit and string.format('hit %.1f,%.1f,%.1f', hit.hitPos.x, hit.hitPos.y, hit.hitPos.z) or 'none'
                end
                for _, y in ipairs({ -240, -258, -275 }) do
                    for _, z in ipairs({ 340, 360, 400, 450, 500 }) do
                        print(string.format('SKYRAY y=%d z=%d W->E %s | E->W %s', y, z,
                            cast(util.vector3(-1050, y, z), util.vector3(-990, y, z)),
                            cast(util.vector3(-990, y, z), util.vector3(-1050, y, z))))
                    end
                end
                return
            end

            if RUN_HOLE and not mapDone and now - started > 6 then
                mapDone = true
                local self = require('openmw.self')
                for _, y in ipairs({ -300, -320, -340, -360 }) do
                    for _, x in ipairs({ -1100, -1080, -1060, -1045 }) do
                        local ph = nearby.castRay(util.vector3(x, y, 480), util.vector3(x, y, 150),
                            { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
                        local ok, rh = pcall(nearby.castRenderingRay, util.vector3(x, y, 480), util.vector3(x, y, 150))
                        local rtxt = 'n/a'
                        if ok and rh and rh.hit then rtxt = string.format('%.1f', rh.hitPos.z) elseif ok then rtxt = 'none' end
                        print(string.format('SKYHOLE x=%d y=%d physics %s render %s', x, y,
                            ph.hit and string.format('%.1f', ph.hitPos.z) or 'none', rtxt))
                    end
                end
                return
            end

            if RUN_OBJ and not mapDone and now - started > 6 then
                mapDone = true
                local self = require('openmw.self')
                for _, y in ipairs({ -200, -240, -258, -275, -300, -340 }) do
                    for _, z in ipairs({ 340, 400, 470 }) do
                        local hit = nearby.castRay(util.vector3(-1050, y, z), util.vector3(-990, y, z),
                            { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
                        local id = 'none'
                        if hit.hit and hit.hitObject then id = hit.hitObject.recordId .. '@' .. fmt(hit.hitObject.position) end
                        print(string.format('SKYOBJ y=%d z=%d %s %s', y, z, hit.hit and fmt(hit.hitPos) or 'none', id))
                    end
                end
                return
            end

            if RUN_RAMP and not mapDone and now - started > 6 then
                mapDone = true
                local self = require('openmw.self')
                for y = -498, -522, -3 do
                    local row = {}
                    for x = -796, -836, -3 do
                        local hit = nearby.castRay(util.vector3(x, y, 130), util.vector3(x, y, -20),
                            { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
                        row[#row + 1] = hit.hit and string.format('%3.0f', hit.hitPos.z) or ' --'
                    end
                    print('SKYRAMP y=' .. y .. ' ' .. table.concat(row, ' '))
                end
                return
            end

            if RUN_SEG and not mapDone and now - started > 6 then
                mapDone = true
                local self = require('openmw.self')
                local a, b = util.vector2(-720.8, -530.4), util.vector2(-1026.8, -469.2)
                local len = (b - a):length()
                local pts = {}
                for d = 0, len, 8 do pts[#pts + 1] = a + (b - a) * (d / len) end
                pts[#pts + 1] = util.vector2(-810.7, -485.8)
                for _, p in ipairs(pts) do
                    local z, out = 130, {}
                    for k = 1, 3 do
                        local hit = nearby.castRay(util.vector3(p.x, p.y, z), util.vector3(p.x, p.y, -20),
                            { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
                        if not hit.hit then break end
                        local id = hit.hitObject and (hit.hitObject.recordId .. '@' .. string.format('%.0f,%.0f,%.0f', hit.hitObject.position.x, hit.hitObject.position.y, hit.hitObject.position.z)) or '?'
                        out[#out + 1] = string.format('%.1f(%s)', hit.hitPos.z, id)
                        z = hit.hitPos.z - 3
                    end
                    print(string.format('SKYSEG %.1f,%.1f %s', p.x, p.y, table.concat(out, ' ')))
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
