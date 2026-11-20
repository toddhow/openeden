-- tools/showcase_check (task 0360): runs the shot list of scripts/sc/cfg.lua (written by check.sh from groups/<group>.lua).
-- cfg = { start = "...", probe = nil | { radius = 3000 }, shots = { shot, ... } }; a shot:
--   rx, ry (rz): the place relative to where the player started (interiors)
--   name, hour (game hour, optional), x, y, z (player place; z nil = land + 40 outside; nothing = stay), yaw (radians, 0 = north),
--   cam = { cx, cy, cz, lx, ly, lz }          a fixed camera looking at a point
--   follow = { id = 'b80803', dx, dy, dz, ldz } a camera that stays at the offset from the first actor whose recordId contains id
--   tp = { dist, up }                          a camera behind and above the player (third person look at the player's body)
--   pitch                                      first person when there is no cam, follow or tp (pitch > 0 looks down)
--   move = 'walk' | 'run'                      the player's controls while the shot runs
--   dwell (real seconds before the first picture, default 12), n (pictures, default 1), step (simulation seconds, default 0.1)
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.sc.cfg')
local state, idx, k, nextAt, goAt, nextSim = 'init', 0, 0, 3, 0, 0
local shot, followActor, origin
local realStart

local function findIn(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and id and string.find(id .. '$', tag .. '$', 1, true) then return o end
    end
end

-- the first object whose recordId ends with tag (so 'b86459' does not match 'b864594'): the player's cell, then the 5 x 5 exterior cells round the player
local function findActor(player, tag)
    local o = findIn(player.cell, tag)
    if o then return o end
    if player.cell.isExterior then
        local cx, cy = math.floor(player.position.x / 4096), math.floor(player.position.y / 4096)
        for dx = -2, 2 do
            for dy = -2, 2 do
                local ok, cell = pcall(world.getExteriorCell, cx + dx, cy + dy, player.cell)
                if ok and cell ~= nil then
                    o = findIn(cell, tag)
                    if o then return o end
                end
            end
        end
    end
end

local function probe(player)
    local p0 = player.position
    local list = {}
    for _, o in ipairs(player.cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and id then
            local d = (o.position - p0):length()
            if d <= cfg.probe.radius then list[#list + 1] = { d = d, id = id, p = o.position, t = tostring(o.type) } end
        end
    end
    table.sort(list, function(a, b) return a.d < b.d end)
    print(string.format('SCPROBE player %.0f %.0f %.0f cell %s count %d', p0.x, p0.y, p0.z, tostring(player.cell.name), #list))
    for i = 1, math.min(#list, cfg.probe.max or 400) do
        local e = list[i]
        print(string.format('SCPROBE %s %.0f %.0f %.0f d %.0f type %s', e.id, e.p.x, e.p.y, e.p.z, e.d, e.t))
    end
end

local function landZ(player, x, y)
    local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(x, y, 0), player.cell) end)
    if ok and h then return h + 40 end
    return nil
end

local function faceEvent(player)
    if not shot.face then return end
    if followActor == nil then followActor = findActor(player, shot.face.id) end
    if followActor then
        local p = followActor.position
        local px, py = p.x + shot.face.dist, p.y
        player:teleport(player.cell, util.vector3(px, py, p.z + 2), { rotation = util.transform.rotateZ(math.atan2(p.x - px, p.y - py)) })
    end
end

local function camEvent(player)
    faceEvent(player)
    if shot.cam then
        local c = shot.cam
        player:sendEvent('ScCam', { x = c[1], y = c[2], z = c[3], lx = c[4], ly = c[5], lz = c[6] })
    elseif shot.follow or shot.face then
        if followActor == nil then followActor = findActor(player, (shot.follow or shot.face).id) end
        if followActor then
            local p = followActor.position
            local f = shot.follow or shot.face
            player:sendEvent('ScCam', { x = p.x + f.dx, y = p.y + f.dy, z = p.z + f.dz, lx = p.x, ly = p.y, lz = p.z + (f.ldz or 60) })
        end
    elseif shot.tp then
        local p = player.position
        local yaw = shot.yaw or 0
        local d, up = shot.tp[1], shot.tp[2]
        player:sendEvent('ScCam', { x = p.x - math.sin(yaw) * d, y = p.y - math.cos(yaw) * d, z = p.z + up, lx = p.x, ly = p.y, lz = p.z + (shot.tp[3] or 70) })
    end
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            realStart = realStart or core.getRealTime()
            local t = core.getRealTime() - realStart
            if state == 'init' then
                if t < nextAt then return end
                if cfg.probe then probe(player); state = 'end'; nextAt = t + 1; return end
                pcall(function() world.mwscript.getGlobalVariables(player).timescale = 0 end)
                origin = player.position
                state = 'go'
                return
            elseif state == 'end' then
                if t >= nextAt then print('SHOWCASE done'); pcall(core.quit) end
                return
            elseif state == 'go' then
                if t < nextAt then return end
                idx = idx + 1
                shot = cfg.shots[idx]
                if shot == nil then state = 'end'; nextAt = t + 1; return end
                followActor = nil
                if shot.hour then pcall(function() world.mwscript.getGlobalVariables(player).gamehour = shot.hour end) end
                if shot.rx and origin then shot.x, shot.y = origin.x + shot.rx, origin.y + shot.ry; shot.z = shot.z or (origin.z + (shot.rz or 0)) end
                if shot.x then
                    local z = shot.z or landZ(player, shot.x, shot.y) or player.position.z
                    player:teleport(player.cell, util.vector3(shot.x, shot.y, z), { rotation = util.transform.rotateZ(shot.yaw or 0), onGround = shot.z == nil })
                end
                player:sendEvent('ScSetup', { first = not (shot.cam or shot.follow or shot.face or shot.tp), yaw = shot.yaw or 0, pitch = shot.pitch or 0, move = shot.move })
                local okh, hh = pcall(function() return world.mwscript.getGlobalVariables(player).gamehour end)
                print(string.format('SHOWCASE setup %d %s hour %s', idx, shot.name, tostring(hh)))
                goAt = t; state = 'wait'; k = 0
                return
            elseif state == 'wait' then
                camEvent(player)
                if t - goAt < (shot.dwell or 12) then return end
                state = 'burst'; nextSim = 0
                return
            elseif state == 'burst' then
                camEvent(player)
                local s = core.getSimulationTime()
                if s < nextSim then return end
                local n, step = shot.n or 1, shot.step or 0.1
                k = k + 1
                nextSim = s + step
                local nm = n > 1 and string.format('%s_%02d', shot.name, k) or shot.name
                print('SHOWCASE shot ' .. nm)
                player:sendEvent('ScShot')
                if k >= n then state = 'go'; nextAt = t + 1 end
                return
            end
        end,
    },
}
