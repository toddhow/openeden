-- The scene of tools/extension_world_check/check.sh. Each step prints a LUA_STEP line; the extension writes what it saw to
-- its own log, and check.sh reads both. Waits are in updates, about 60 a second at the check's frame limit.
local types = require('openmw.types')
local util = require('openmw.util')
local world = require('openmw.world')

-- Constants::UnitsPerMeter (components/misc/constants.hpp).
local UNITS_PER_METER = 69.99125109
-- The same point as PROBE_M in check.sh. 58.5216 m is 4096 units, and (4096, 4096, 1745) is where the Lua integration
-- tests place the player on the hub island (scripts/data/integration_tests/test_lua_api/global.lua, initPlayer).
local PROBE_M = 58.5216
local PROBE = util.vector3(PROBE_M * UNITS_PER_METER, PROBE_M * UNITS_PER_METER, 1745)
local FALL_HEIGHT = util.vector3(0, 0, 300)
-- Museum of Wonders positions from the same file ('load while teleporting').
local MUSEUM = 'Museum of Wonders'
local MUSEUM_PLAYER = util.vector3(0, -1500, 111)
local MUSEUM_CREATURE = util.vector3(0, -1000, 111)
local MELEE_OFFSET = util.vector3(0, 90, 0)

local SETTLE_UPDATES = 300
local SHORT_UPDATES = 30
local CELL_UPDATES = 120
local ATTACK_TIMEOUT_UPDATES = 1800
local REVIVE_TIMEOUT_UPDATES = 600

local scene = nil
local replies = {}

local function step(text)
    print('LUA_STEP ' .. text)
end

local function wait(updates)
    for _ = 1, updates do
        coroutine.yield()
    end
end

local function waitFor(condition, updates)
    for _ = 1, updates do
        if condition() then
            return true
        end
        coroutine.yield()
    end
    return condition()
end

local function runScene()
    local player = world.players[1]
    local script = world.mwscript.getGlobalScript('OE_WorldCheck', player)
    if script == nil then
        step('no-mwscript')
        return
    end

    -- Long enough for the extension to see the player at rest and cast.
    player:teleport('', PROBE, util.transform.identity)
    wait(SETTLE_UPDATES)
    player:sendEvent('OeWorldCheck_Ground', {})
    waitFor(function() return replies.ground end, SHORT_UPDATES)
    step('settled')

    -- A fall onto the ground, for a contact begin.
    player:teleport('', player.position + FALL_HEIGHT)
    wait(SETTLE_UPDATES)
    step('fell')

    -- An object created and removed in the active cell.
    local dagger = world.createObject('basic_dagger1h', 1)
    dagger:teleport(player.cell, player.position + util.vector3(0, 100, 50))
    wait(SHORT_UPDATES)
    step('created')
    dagger:remove()
    wait(SHORT_UPDATES)
    step('removed')

    -- A creature placed in a cell that is not loaded yet: it can only spawn with the cell.
    local landracer = world.createObject('landracer')
    landracer:teleport(MUSEUM, MUSEUM_CREATURE)
    wait(SHORT_UPDATES)
    step('placed')
    player:teleport(MUSEUM, MUSEUM_PLAYER, util.transform.identity)
    wait(CELL_UPDATES)
    step('entered ' .. tostring(player.cell.name))

    -- A real melee attack by the player, the same way as the integration test 'player weapon attack'.
    world.createObject('basic_dagger1h', 1):moveInto(player)
    landracer:teleport(player.cell, player.position + MELEE_OFFSET)
    wait(SHORT_UPDATES)
    player:sendEvent('OeWorldCheck_Attack', { target = landracer })
    waitFor(function() return replies.attack ~= nil end, ATTACK_TIMEOUT_UPDATES)
    print('LUA_ATTACK hit=' .. tostring(replies.attack))
    step('attacked')

    -- Lua cannot set another actor's health or resurrect anyone, so mwscript does (see check.sh).
    script.variables.state = 1
    wait(SHORT_UPDATES)
    step('creature dead=' .. tostring(types.Actor.isDead(landracer)))

    script.variables.state = 2
    waitFor(function() return script.variables.state == 0 end, REVIVE_TIMEOUT_UPDATES)
    wait(SHORT_UPDATES)
    step('player dead=' .. tostring(types.Actor.isDead(player)))

    script.variables.state = 4
    wait(SHORT_UPDATES)
    step('creature resurrected dead=' .. tostring(types.Actor.isDead(landracer)))

    -- Leaving unloads the museum, which despawns what is in it.
    player:teleport('', PROBE, util.transform.identity)
    wait(CELL_UPDATES)
    step('left')

    print('LUA_SCENE_DONE')
    types.Player.sendMenuEvent(player, 'OeWorldCheck_SceneDone')
end

return {
    engineHandlers = {
        onUpdate = function()
            if scene == nil then
                return
            end
            local ok, err = coroutine.resume(scene)
            if not ok then
                print('LUA_SCENE_ERROR ' .. tostring(err))
                scene = nil
            elseif coroutine.status(scene) == 'dead' then
                scene = nil
            end
        end,
    },
    eventHandlers = {
        OeWorldCheck_Start = function()
            scene = coroutine.create(runScene)
        end,
        OeWorldCheck_GroundDone = function()
            replies.ground = true
        end,
        OeWorldCheck_AttackDone = function(data)
            replies.attack = data.hit
        end,
    },
}
