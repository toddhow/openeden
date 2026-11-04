-- Task 0069 stage d: the global half of tools/mergedbody_check. On request of the player script it places one NPC of
-- every race and sex in a row in front of the player, takes their clothes off (so the merged body is drawn: in stage d
-- anything worn over a merged slot keeps the stock parts) and leaves them standing there. It is only part
-- of the check, never of the game, and uses the player's own game data.
local world = require('openmw.world')
local types = require('openmw.types')
local util = require('openmw.util')

local RACES = { 'breton', 'dark elf', 'high elf', 'imperial', 'nord', 'redguard', 'wood elf', 'orc', 'argonian',
    'khajiit' }
local SPACING = 60 -- units between two NPCs
local DISTANCE = 260 -- units in front of the player

local function lower(s)
    return string.lower(tostring(s or ''))
end

local function pickRecords()
    local picked = {}
    local ok, err = pcall(function()
        for _, record in ipairs(types.NPC.records) do
            local race = lower(record.race)
            local sex = record.isMale and 'm' or 'f'
            for _, wanted in ipairs(RACES) do
                local key = wanted .. '|' .. sex
                if race == wanted and not picked[key] then
                    picked[key] = record.id
                end
            end
        end
    end)
    if not ok then
        print('MBCHECK could not list the NPC records: ' .. tostring(err))
    end
    return picked
end

return {
    eventHandlers = {
        MBCheckSpawn = function(data)
            local player = world.players[1]
            local picked = pickRecords()
            local index = 0
            for _, race in ipairs(RACES) do
                for _, sex in ipairs({ 'm', 'f' }) do
                    local id = picked[race .. '|' .. sex]
                    if id then
                        local object = world.createObject(id, 1)
                        -- NPCs put their best clothes back on by themselves: the clothes leave the inventory before
                        -- the NPC enters the world (removing them from a placed NPC stalled the game in a test run).
                        pcall(function()
                            for _, item in ipairs(types.Actor.inventory(object):getAll()) do
                                item:remove()
                            end
                        end)
                        local x = (index - 9.5) * SPACING
                        local position = data.origin + util.vector3(x, DISTANCE, 0)
                        object:teleport(player.cell, position, util.transform.rotateZ(math.pi))
                        print('MBCHECK spawned ' .. race .. ' ' .. sex .. ' ' .. tostring(id))
                    else
                        print('MBCHECK no record for ' .. race .. ' ' .. sex)
                    end
                    index = index + 1
                end
            end
        end,
    },
}
