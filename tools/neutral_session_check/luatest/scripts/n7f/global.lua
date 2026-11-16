-- tools/neutral_session_check (task 0196, followers): the global half of "n7follow". On SIMULATION time (so a fixed frame
-- time gives the same lines every run): the player stands at the start spot, walks a line at WALK_SPEED, goes through the
-- Bannered Mare's front door (activateBy, the key's path), walks again in Whiterun, and a picture is taken at each step.
-- Every second it prints "N7F t=<sim> player <cell> x,y,z | follower <cell> x,y,z" for the follower (the base id in
-- followcfg.tag, found by a part of its record id). The pack settings do the recruiting and dismissing.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n7f.followcfg') -- written by check.sh
local WALK_SPEED = 150

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

local function findDoor(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

local storage = require('openmw.storage')
local t0, nextLog, state, stateSince = nil, 0, 'wait', 0
if cfg.mode == 'load' then state = 'loaded' end
local walkFrom, walkTo, walkStart
local shotsLeft = {}

local function say(player, t)
    local p = player.position
    local f = find(player.cell, cfg.tag)
    local text = string.format('N7F t=%.1f player %s %.0f,%.0f,%.0f', t, tostring(player.cell.name), p.x, p.y, p.z)
    if f then
        local q = f.position
        text = text .. string.format(' | follower %s %.0f,%.0f,%.0f dist %.0f', tostring(f.cell.name), q.x, q.y, q.z,
            math.sqrt((q.x - p.x) ^ 2 + (q.y - p.y) ^ 2))
    else
        text = text .. ' | follower none'
    end
    print(text)
end

local pending = nil

-- Turns the player to look at the follower (a little above its feet) and takes the picture 0.3 s later.
local function shot(player, t, tag)
    local f = find(player.cell, cfg.tag)
    if f then
        local p, q = player.position, f.position
        player:teleport(player.cell, p, { rotation = util.transform.rotateZ(math.atan2(q.x - p.x, q.y - p.y)) })
    end
    pending = { at = t + 0.3, tag = tag }
end

local function placeAt(player, x, y, z, yaw)
    player:teleport(player.cell, util.vector3(x, y, z), { rotation = util.transform.rotateZ(yaw) })
end

local function walkStep(player, t)
    local f = math.min(1, (t - walkStart) * WALK_SPEED / math.sqrt((walkTo[1] - walkFrom[1]) ^ 2 + (walkTo[2] - walkFrom[2]) ^ 2))
    placeAt(player, walkFrom[1] + (walkTo[1] - walkFrom[1]) * f, walkFrom[2] + (walkTo[2] - walkFrom[2]) * f, walkFrom[3],
        math.atan2(walkTo[1] - walkFrom[1], walkTo[2] - walkFrom[2]))
    return f >= 1
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if pending and t >= pending.at then
                player:sendEvent('N5Shot')
                print(string.format('N7F shot %s t=%.1f', pending.tag, t))
                pending = nil
            end
            if t >= nextLog then
                nextLog = math.floor(t) + 1
                say(player, t)
            end
            if state == 'loaded' then
                if t >= 3 then
                    shot(player, t, 'after load')
                    state = 'done'
                end
            elseif state == 'wait' and t >= cfg.start then
                state = 'stand'
                stateSince = t
                placeAt(player, cfg.stand[1], cfg.stand[2], cfg.stand[3], cfg.stand[4])
                print('N7F stand')
            elseif state == 'stand' and t >= stateSince + 3 then
                shot(player, t, 'recruited')
                state = 'walk1'
                walkFrom, walkTo, walkStart = cfg.stand, cfg.leg1, t
            elseif state == 'walk1' then
                if walkStep(player, t) then
                    state = 'rest1'
                    stateSince = t
                end
            elseif state == 'rest1' and t >= stateSince + cfg.rest then
                shot(player, t, 'after walk')
                state = 'predoor'
                stateSince = t
            elseif state == 'predoor' and t >= stateSince + 1 then
                -- stand before the front door and use it
                placeAt(player, cfg.doorStand[1], cfg.doorStand[2], cfg.doorStand[3], cfg.doorStand[4])
                state = 'door'
                stateSince = t
            elseif state == 'door' and t >= stateSince + 1 then
                local door = findDoor(player.cell, cfg.doorBase, cfg.doorNear)
                if door == nil then
                    print('N7F no door')
                    state = 'done'
                    return
                end
                door:activateBy(player)
                print('N7F door used')
                state = 'outside'
                stateSince = t
            elseif state == 'outside' and t >= stateSince + cfg.outsideWait then
                shot(player, t, 'outside')
                state = 'walk2'
                local p = player.position
                walkFrom = { p.x, p.y, p.z }
                walkTo = { p.x + cfg.leg2[1], p.y + cfg.leg2[2], p.z }
                walkStart = t
            elseif state == 'walk2' then
                if walkStep(player, t) then
                    state = 'rest2'
                    stateSince = t
                end
            elseif state == 'rest2' and t >= stateSince + cfg.rest then
                shot(player, t, 'after walk outside')
                state = 'away'
                stateSince = t
            elseif state == 'away' and t >= stateSince + cfg.awayAt then
                -- Far from the follower: a dismissed one must not come after the player.
                local p = player.position
                placeAt(player, p.x, p.y + cfg.away, p.z, 0)
                print('N7F player sent away')
                state = 'awayrest'
                stateSince = t
            elseif state == 'awayrest' and t >= stateSince + 12 then
                shot(player, t, 'far away')
                state = 'done'
            end
            if cfg.mode == 'save' and state == 'away' then
                -- saved while the player stands outside with the follower beside him
                storage.globalSection('N7F'):set('saveNow', true)
                print('N7F save requested')
                state = 'saved'
            end
        end,
    },
}
