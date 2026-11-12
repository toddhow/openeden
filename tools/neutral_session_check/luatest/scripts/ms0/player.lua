-- tools/neutral_session_check (task 0131): the player-script half of the sweep: nearby, the stub player's own APIs and
-- ray casts onto the neutral cell.
local nearby = require('openmw.nearby')
local core = require('openmw.core')
local self = require('openmw.self')
local types = require('openmw.types')
local util = require('openmw.util')
local ui = require('openmw.ui')

local stats = {}
local function try(label, fn)
    local ok, res = pcall(fn)
    local s = stats[label] or { ok = 0, err = 0 }
    stats[label] = s
    if ok then s.ok = s.ok + 1 else s.err = s.err + 1; s.first = s.first or (tostring(res):gsub('\n.*', '')) end
    return ok, res
end

local done, start = false, nil
return {
    engineHandlers = {
        onUpdate = function()
            start = start or core.getRealTime()
            if done or core.getRealTime() - start < 7 then return end
            done = true
            try('nearby.actors', function() return #nearby.actors end)
            try('nearby.items', function() return #nearby.items end)
            try('nearby.doors', function() return #nearby.doors end)
            try('nearby.containers', function() return #nearby.containers end)
            try('nearby.activators', function() return #nearby.activators end)
            try('nearby.players', function() return #nearby.players end)
            try('nearby.castRay', function()
                local r = nearby.castRay(self.position + util.vector3(0, 0, 50), self.position - util.vector3(0, 0, 500),
                    { collisionType = nearby.COLLISION_TYPE.World })
                return r.hit, r.hitObject and r.hitObject.recordId
            end)
            try('nearby.castRenderingRay', function()
                local r = nearby.castRenderingRay(self.position + util.vector3(0, 0, 50), self.position - util.vector3(0, 0, 500))
                return r.hit
            end)
            try('nearby.findNearestNavMeshPosition', function() return nearby.findNearestNavMeshPosition(self.position) end)
            try('nearby.findPath', function() return nearby.findPath(self.position, self.position + util.vector3(300, 0, 0)) end)
            try('self.cell', function() return self.cell.name end)
            try('self.controls', function() return self.controls.movement end)
            try('self.getBoundingBox', function() return self:getBoundingBox() end)
            try('Actor.stats', function() return types.Actor.stats.dynamic.health(self).current end)
            try('Actor.spells', function() return types.Actor.spells(self) end)
            try('Actor.activeEffects', function() return types.Actor.activeEffects(self):getEffect('x') end)
            try('Actor.getStance', function() return types.Actor.getStance(self) end)
            try('Actor.isOnGround', function() return types.Actor.isOnGround(self) end)
            try('Actor.isSwimming', function() return types.Actor.isSwimming(self) end)
            try('Actor.inventory.getAll', function() return #types.Actor.inventory(self):getAll() end)
            try('Player.getCrimeLevel', function() return types.Player.getCrimeLevel(self) end)
            try('Player.quests', function() return types.Player.quests(self) end)
            try('Player.birthSign', function() return types.Player.getBirthSign(self) end)
            try('ui.showMessage', function() ui.showMessage('MS0 sweep message') end)
            try('core.sendGlobalEvent', function() core.sendGlobalEvent('MS0None', {}) end)
            local labels = {}
            for k in pairs(stats) do labels[#labels + 1] = k end
            table.sort(labels)
            for _, k in ipairs(labels) do
                local s = stats[k]
                print(string.format('MS0P api %-40s ok=%d err=%d%s', k, s.ok, s.err, s.first and (' first="' .. s.first .. '"') or ''))
            end
            print('MS0P player sweep complete')
        end,
    },
}
