-- tools/neutral_session_check (task 0177): the Morrowind half of the save compatibility check. 5 real seconds after the
-- start (of a new game or a loaded save) it logs the player's cell, position and a digest of the objects of the cell
-- as "N6MW ..." lines, so a save written by one executable and loaded by another can be compared.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local storage = require('openmw.storage')

local start, done = nil, false
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil or done then return end
            start = start or core.getRealTime()
            if core.getRealTime() - start < 5 then return end
            done = true
            local p = player.position
            local count, disabled = 0, 0
            local digest = 0
            for _, o in ipairs(player.cell:getAll()) do
                count = count + 1
                if not o.enabled then disabled = disabled + 1 end
                digest = (digest + #tostring(o.recordId) * 7 + math.floor(o.position.x) % 997) % 1000003
            end
            print(string.format('N6MW cell=%s pos=%.1f,%.1f,%.1f objects=%d disabled=%d digest=%d gold=%d',
                tostring(player.cell.name), p.x, p.y, p.z, count, disabled, digest,
                types.Actor.inventory(player):countOf('gold_001')))
            storage.globalSection('N6'):set('reported', true)
        end,
    },
}
