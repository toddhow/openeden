-- tools/neutral_session_check (task 0237): "stn" stands the player in front of the standing stones and shrines in turn (cfg.steps:
-- t = simulation seconds, base = the record id, near = x,y,z the object stands near, from = the side the player stands on,
-- dist = how far) and takes pictures at cfg.shots. The activations and the button clicks come from the real input script
-- (OPENEDEN_RMLUI_INPUT_SCRIPT). cfg is written by check.sh from STN_STEPS.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.stn.cfg')
local saved = false

local step, nextShot = 1, 1
local spellsPrinted = false

local function nearest(cell, base, near)
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

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            local s = cfg.steps[step]
            if s and t >= s.t then
                step = step + 1
                local target = nearest(player.cell, s.base, s.near)
                if target == nil then
                    print('STN target ' .. s.base .. ' not found; the cell has ' .. #player.cell:getAll() .. ' objects')
                else
                    local p = target.position
                    local len = math.sqrt(s.from[1] ^ 2 + s.from[2] ^ 2)
                    local px, py = p.x + s.from[1] / len * s.dist, p.y + s.from[2] / len * s.dist
                    local yaw = math.atan2(p.x - px, p.y - py) + (s.yaw or 0)
                    print(string.format('STN t %.1f stand at %.1f,%.1f facing %s at %.1f,%.1f,%.1f', t, px, py, s.base, p.x, p.y, p.z))
                    player:teleport(player.cell, util.vector3(px, py, p.z + (s.dz or 0)), { onGround = true, rotation = util.transform.rotateZ(yaw) })
                    player:sendEvent('StnPitch', { pitch = s.pitch or 0 })
                end
            end
            if cfg.saveAt and not saved and t >= cfg.saveAt then
                saved = true
                print('STN saving at ' .. string.format('%.1f', t))
                storage.globalSection('N8CRIME'):set('saveNow', true) -- the menu script (n8/menu.lua) saves
            end
            if cfg.shots[nextShot] and t >= cfg.shots[nextShot] then
                nextShot = nextShot + 1
                player:sendEvent('N5Shot')
            end
        end,
    },
}
