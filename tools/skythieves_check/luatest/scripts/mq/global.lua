-- tools/skythieves_check (task 0271): logs, every cfg.every updates, where the player is and the cfg.near nearest objects
-- within cfg.radius (record id, position), so a moving cart or a walking prisoner shows as changing positions.
-- Every line starts with MQ and holds no wall clock.
local world = require('openmw.world')
local util = require('openmw.util')
local core = require('openmw.core')
local cfg = require('scripts.mq.cfg')

local updates = 0
local hourSet = false
local activated = false
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local steps = {} -- cfg.moves: { update, from, to, item } (hex text ids): a stand-in for the inventory windows' Take and Put clicks

local function move(m)
    if not okInv then print('MQ move: the inventory namespace is missing') return end
    if m[2] == 'open' then print('MQ open ' .. m[3]) inv.open(m[3], m[4], m[5]) return end
    print(string.format('MQ move: %s holds %s x%d, %s holds x%d', m[2], m[4], inv.count(m[2], m[4]), m[3], inv.count(m[3], m[4])))
    inv.transfer(m[2], m[3], m[4], 1)
end

-- A test aid (cfg.activate = { update, base }): the player activates the actor of that base at that update, as the real
-- activate key would (the key found nothing: the market stall of Brynjolf stands in the crosshair's way).
local function activateActor(base)
    local player = world.players[1]
    for _, o in ipairs(player.cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and id ~= nil and tostring(id):match(':b(%d+)$') == base then
            print('MQ activate: the player talks to ' .. tostring(id))
            o:activateBy(player)
            return
        end
    end
    print('MQ activate: actor ' .. base .. ' not found')
end
local aimed = false
local lastFrom = nil

-- A test aid (cfg.aim = { update, base }): stands the player 90 units from the actor of that base and turns him to face it, so
-- that the real activate key (Space) finds the actor in the crosshair. The conversation itself is the key's, not this script's.
local function aimAt(base)
    local player = world.players[1]
    local best, bestD = nil, math.huge
    for _, o in ipairs(player.cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and id ~= nil and tostring(id):match(':b(%d+)$') == base then
            local d = (o.position - player.position):length()
            if d < bestD then best, bestD = o, d end
        end
    end
    if best == nil then print('MQ aim: actor ' .. base .. ' not found') return end
    local from = best.position + util.vector3(90, 0, 0)
    local yaw = math.atan2(90, 0) -- facing -x: (dx, dy) = (-90, 0) -> atan2(-dx, dy) = atan2(90, 0)
    player:teleport(player.cell, from, { rotation = util.transform.rotateZ(yaw) })
    if lastFrom ~= nil and (lastFrom - from):length() < 25 then return end
    lastFrom = from
    print(string.format('MQ aim: player at %.0f,%.0f,%.0f facing the actor at %.0f,%.0f,%.0f', from.x, from.y, from.z,
        best.position.x, best.position.y, best.position.z))
end
-- Task 0275: cfg.steps = { { update, base, dist, fromx, fromy, pitchDegrees, nearx, neary, nearz }, ... }: at that update the player
-- is stood `dist` units from the object of that base (decimal text) nearest to (nearx, neary, nearz) (the player when absent), on
-- the side of (fromx, fromy), turned to face it, and his first-person camera is pitched down by pitchDegrees. Everything after
-- that (the key, the clicks) is the real input; this only walks him to the spot (a hand-written aid, named in the task log).
local doneSteps = {}
local startCell = nil
local lookAgain = nil
local function stepTo(s)
    local player = world.players[1]
    if s[2] == 'event' then
        -- { update, 'event', name, { field = value } }: the event the engine's activation would send (a stand-in for AIMING)
        print('MQ step event: ' .. tostring(s[3]))
        core.sendGlobalEvent(s[3], s[4])
        return
    end
    if s[2] == 'talk' then
        -- task 0314: { update, 'talk', base }: the player talks to the actor of that base (several talks in one run)
        activateActor(s[3])
        return
    end
    if s[2] == 'back' then
        -- { update, 'back', x, y, z, yawDegrees }: back into the exterior cell the game started in
        if startCell ~= nil then
            player:teleport(startCell, util.vector3(s[3], s[4], s[5]), { onGround = true, rotation = util.transform.rotateZ(math.rad(s[6] or 0)) })
            player:sendEvent('MQLook', { pitch = 0 })
            print(string.format('MQ step back: the player at %.0f,%.0f,%.0f', s[3], s[4], s[5]))
        end
        return
    end
    if s[2] == 'cell' then
        -- { update, 'cell', cellName, x, y, z, yawDegrees, pitchDegrees }: into an interior (the pack builds it)
        player:teleport(s[3], util.vector3(s[4], s[5], s[6]), { rotation = util.transform.rotateZ(math.rad(s[7])) })
        player:sendEvent('MQLook', { pitch = math.rad(s[8] or 0) })
        lookAgain = { updates + 20, math.rad(s[8] or 0) }
        print(string.format('MQ step cell: %s at %.0f,%.0f,%.0f', tostring(s[3]), s[4], s[5], s[6]))
        return
    end
    if s[2] == 'abs' then
        -- { update, 'abs', x, y, z, yawDegrees (0 looks north, positive turns right), pitchDegrees }
        player:teleport(player.cell, util.vector3(s[3], s[4], s[5]), { onGround = true, rotation = util.transform.rotateZ(math.rad(s[6])) })
        player:sendEvent('MQLook', { pitch = math.rad(s[7] or 0) })
        lookAgain = { updates + 20, math.rad(s[7] or 0) }
        print(string.format('MQ step abs: the player at %.0f,%.0f,%.0f yaw %.1f pitch %.1f', s[3], s[4], s[5], s[6], s[7] or 0))
        return
    end
    local near = (s[7] and util.vector3(s[7], s[8], s[9])) or player.position
    local best, bestD = nil, math.huge
    for _, o in ipairs(player.cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and id ~= nil and tostring(id):match(':b(%d+)$') == s[2] then
            local d = (o.position - near):length()
            if d < bestD then best, bestD = o, d end
        end
    end
    if best == nil then print('MQ step: base ' .. tostring(s[2]) .. ' not found') return end
    local p = best.position
    local cx, cy = s[4] - p.x, s[5] - p.y
    local len = math.sqrt(cx * cx + cy * cy)
    local px, py = p.x + cx / len * s[3], p.y + cy / len * s[3]
    local yaw = math.atan2(p.x - px, p.y - py) + math.rad(s[10] or 0) -- s[10]: a yaw offset in degrees (positive turns right)
    player:teleport(player.cell, util.vector3(px, py, p.z), { onGround = true, rotation = util.transform.rotateZ(yaw) })
    player:sendEvent('MQLook', { pitch = math.rad(s[6] or 0) })
    lookAgain = { updates + 20, math.rad(s[6] or 0) } -- the first call after the switch to first person is not always kept
    print(string.format('MQ step: base %s at %.0f,%.0f,%.0f; the player at %.0f,%.0f,%.0f pitch %.0f', s[2], p.x, p.y, p.z, px, py,
        p.z, s[6] or 0))
end
local teleported = false

local function sample()
    local player = world.players[1]
    local p = player.position
    print(string.format('MQ u=%d cell=%s player=%.0f,%.0f,%.0f', updates, tostring(player.cell.name), p.x, p.y, p.z))
    local list = {}
    for _, o in ipairs(player.cell:getAll()) do
        local ok, rec = pcall(function() return o.recordId end)
        if ok then
            local dx, dy, dz = o.position.x - p.x, o.position.y - p.y, o.position.z - p.z
            local d = math.sqrt(dx * dx + dy * dy + dz * dz)
            if d <= cfg.radius then list[#list + 1] = { d, rec, o.position } end
        end
    end
    table.sort(list, function(a, b) return a[1] < b[1] end)
    for i = 1, math.min(cfg.near, #list) do
        local e = list[i]
        print(string.format('MQ   %s d=%.0f at %.0f,%.0f,%.0f', e[2], e[1], e[3].x, e[3].y, e[3].z))
    end
end

return {
    engineHandlers = {
        onUpdate = function()
            if world.players[1] == nil then return end
            updates = updates + 1
            if startCell == nil and updates > 4 then startCell = world.players[1].cell end
            if cfg.teleport and not teleported and updates > 3 then
                teleported = true
                local t = cfg.teleport
                local player = world.players[1]
                player:teleport(player.cell, util.vector3(t[1], t[2], t[3]), { rotation = util.transform.rotateZ(t[4]) })
            end
            for i, m in ipairs(cfg.moves or {}) do
                if not steps[i] and updates >= m[1] then
                    steps[i] = true
                    move(m)
                end
            end
            if lookAgain and updates >= lookAgain[1] then
                world.players[1]:sendEvent('MQLook', { pitch = lookAgain[2] })
                lookAgain = nil
            end
            for i, s in ipairs(cfg.steps or {}) do
                if not doneSteps[i] and updates >= s[1] then
                    doneSteps[i] = true
                    stepTo(s)
                end
            end
            if cfg.activate and not activated and updates >= cfg.activate[1] then
                activated = true
                activateActor(cfg.activate[2])
            end
            if cfg.aim and updates >= cfg.aim[1] and updates % 20 == 0 then
                aimAt(cfg.aim[2]) -- again every 20 updates: the actor walks to its post after the first talk
            end
            if not hourSet and updates > 5 then
                hourSet = true
                -- daytime: Brynjolf's plaza packages are scheduled 8:00 to 20:00 (a test aid; the game's clock starts elsewhere)
                pcall(function() world.mwscript.getGlobalVariables(world.players[1]).gamehour = cfg.hour or 12 end)
            end
            if updates % cfg.every == 0 and updates >= cfg.from then sample() end
        end,
    },
}
