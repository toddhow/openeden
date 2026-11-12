-- tools/neutral_session_check (task 0132, milestone M-S3 stage 1): path queries over the navmesh of a neutral Skyrim cell.
-- Started when check.sh is given "navpath" as its screenshot argument. The global script (global.lua) decides when each
-- phase runs and sends NPRun / NPRing events; this script answers with `NAV` lines:
--   NAV <phase> <name> status=<Success|PartialPath|...> nodes=<n> length=<units> straight=<units> t=<real seconds to success>
-- A query is retried every half second for up to RETRY seconds because the navmesh is built by worker threads after the
-- cell is loaded (the first success time is the build latency a player would see).
local self = require('openmw.self')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local debug = require('openmw.debug')
local camera = require('openmw.camera')

local RETRY = 40
local pending = nil
local statusNames = {}
for name, value in pairs(nearby.FIND_PATH_STATUS) do statusNames[value] = name end

local function pathLength(path)
    local sum = 0
    for i = 2, #path do sum = sum + (path[i] - path[i - 1]):length() end
    return sum
end

-- A point on the ground below (x, y): a ray from zTop down on the World and Heightmap layers.
local function groundAt(x, y, zTop)
    local hit = nearby.castRay(util.vector3(x, y, zTop), util.vector3(x, y, zTop - 3000),
        { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.HeightMap })
    if hit.hit then return hit.hitPos end
    return nil
end

local function run(phase, queries)
    pending = { phase = phase, queries = queries, index = 1, nextTry = 0 }
end

local function finish(q, status, path, t)
    local straight = (q.to - q.from):length()
    local last = path[#path]
    print(string.format('NAV %s %s status=%s nodes=%d length=%.0f straight=%.0f t=%.1f last=%s to=%s',
        pending.phase, q.name, statusNames[status] or tostring(status), #path, pathLength(path), straight, t,
        last and string.format('%.0f,%.0f,%.0f', last.x, last.y, last.z) or 'none',
        string.format('%.0f,%.0f,%.0f', q.to.x, q.to.y, q.to.z)))
end

local function step()
    if not pending then return end
    local now = core.getRealTime()
    if now < pending.nextTry then return end
    local q = pending.queries[pending.index]
    if q == nil then
        print('NAV phase ' .. pending.phase .. ' done')
        core.sendGlobalEvent('NPDone', pending.phase)
        pending = nil
        return
    end
    q.t0 = q.t0 or now
    local status, path = nearby.findPath(q.from, q.to, { destinationTolerance = q.tolerance or 40 })
    local waited = now - q.t0
    if status == nearby.FIND_PATH_STATUS.Success or waited >= (q.retry or RETRY) then
        finish(q, status, path, waited)
        pending.index = pending.index + 1
    else
        pending.nextTry = now + 0.5
    end
end

return {
    engineHandlers = {
        onUpdate = step,
    },
    eventHandlers = {
        NPRun = function(data)
            local queries = {}
            for _, q in ipairs(data.queries) do
                queries[#queries + 1] = { name = q.name, from = q.from, to = q.to, retry = q.retry, tolerance = q.tolerance }
            end
            run(data.phase, queries)
        end,
        -- A ring of targets round the player, found by ray: for each radius, 8 directions.
        NPRing = function(data)
            local p = self.object.position
            local queries = {}
            local from = groundAt(p.x, p.y, p.z + 100) or p
            for _, radius in ipairs(data.radii) do
                for k = 0, 7 do
                    local a = k * math.pi / 4
                    local g = groundAt(p.x + radius * math.cos(a), p.y + radius * math.sin(a), p.z + 100)
                    if g then
                        queries[#queries + 1] = { name = string.format('ring r=%d dir=%d', radius, k), from = from, to = g,
                            retry = data.retry or 15 }
                    end
                end
            end
            run(data.phase, queries)
        end,
        NPWire = function() debug.toggleRenderMode(debug.RENDER_MODE.NavMesh) end,
        NPShot = function() debug.takeScreenshot() end,
        NPLook = function(p) camera.setMode(camera.MODE.FirstPerson); camera.setPitch(p) end,
        -- Floor heights from rays (World layer), along a line: used to read stair risers, treads and widths from the data.
        NPScan = function(d)
            local out = {}
            local n = math.floor(d.length / d.step)
            for i = 0, n do
                local x, y = d.x + d.dx * i * d.step, d.y + d.dy * i * d.step
                local g = groundAt(x, y, d.zTop)
                out[#out + 1] = g and string.format('%.0f', g.z) or '.'
            end
            print(string.format('NAV scan %s from %.0f,%.0f dir %.0f,%.0f step %d: %s', d.name, d.x, d.y, d.dx, d.dy, d.step, table.concat(out, ' ')))
        end,
        NPPos = function()
            local p = self.object.position
            print(string.format('NAV player at %.1f,%.1f,%.1f', p.x, p.y, p.z))
        end,
    },
}
