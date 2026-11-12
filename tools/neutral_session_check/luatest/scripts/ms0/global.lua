-- tools/neutral_session_check (task 0131, stage N3a-6): a broad sweep of the Lua object, cell, world and mwscript APIs
-- over every object of the player's neutral cell. Every call goes through pcall; an error that the engine reports as a
-- Lua error ("Requires type ...") is a refusal and counts as safe. What the sweep looks for is a crash (no further
-- "MS0 alive" lines, a crash dump) or an engine-side error line in the log. Prints "MS0 ..." lines only.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local util = require('openmw.util')

local stats = {} -- label -> { ok, err, first }
local function try(label, fn)
    local ok, res = pcall(fn)
    local s = stats[label]
    if not s then s = { ok = 0, err = 0 }; stats[label] = s end
    if ok then
        s.ok = s.ok + 1
    else
        s.err = s.err + 1
        if not s.first then s.first = (tostring(res):gsub('\n.*', '')) end
    end
    return ok, res
end

local function report()
    local labels = {}
    for k in pairs(stats) do labels[#labels + 1] = k end
    table.sort(labels)
    for _, k in ipairs(labels) do
        local s = stats[k]
        print(string.format('MS0 api %-40s ok=%d err=%d%s', k, s.ok, s.err, s.first and (' first="' .. s.first .. '"') or ''))
    end
end

local frames, start, phase = 0, nil, 0
local player
local objects = {}

local readOnly = {
    'id', 'recordId', 'position', 'rotation', 'scale', 'startingPosition', 'startingRotation', 'cell', 'count',
    'contentFile', 'enabled', 'parentContainer', 'globalVariable', 'type',
}

local function sweepObject(o)
    try('tostring', function() return tostring(o) end)
    try('isValid', function() return o:isValid() end)
    for _, k in ipairs(readOnly) do
        try('get.' .. k, function() return o[k] end)
    end
    try('owner.recordId', function() return o.owner.recordId end)
    try('owner.factionId', function() return o.owner.factionId end)
    try('owner.factionRank', function() return o.owner.factionRank end)
    try('getBoundingBox', function() local b = o:getBoundingBox(); return b.center, b.halfSize, b.vertices end)
    try('hasScript', function() return o:hasScript('scripts/ms0/none.lua') end)
    try('sendEvent', function() o:sendEvent('MS0None', { a = 1 }) end)
    try('equal', function() return o == player end)
    for name, t in pairs(types) do
        if type(t) == 'table' and t.objectIsInstance then
            try('objectIsInstance', function() return t.objectIsInstance(o) end)
        end
    end
    -- the typed APIs must refuse (a Lua error), not crash: call a spread of them on the neutral object
    try('Actor.stats.level', function() return types.Actor.stats.level(o) end)
    try('Actor.stats.level.current', function() return types.Actor.stats.level(o).current end)
    try('Actor.stats.health.current', function() return types.Actor.stats.dynamic.health(o).current end)
    try('Actor.inventory', function() return types.Actor.inventory(o) end)
    try('Actor.inventory:getAll', function() return types.Actor.inventory(o):getAll() end)
    try('Container.inventory:getAll', function() return types.Container.inventory(o):getAll() end)
    try('Container.content:getAll', function() return types.Container.content(o):getAll() end)
    try('Item.itemData.condition', function() return types.Item.itemData(o).condition end)
    try('Item.itemData.soul', function() return types.Item.itemData(o).soul end)
    try('Lockable.isLocked.value', function() return types.Lockable.isLocked(o) end)
    try('Actor.getEquipment', function() return types.Actor.getEquipment(o) end)
    try('Actor.activeSpells', function() return types.Actor.activeSpells(o) end)
    try('Actor.isDead', function() return types.Actor.isDead(o) end)
    try('Actor.getEncumbrance', function() return types.Actor.getEncumbrance(o) end)
    try('Actor.canMove', function() return types.Actor.canMove(o) end)
    try('NPC.record', function() return types.NPC.record(o) end)
    try('Creature.record', function() return types.Creature.record(o) end)
    try('Item.itemData', function() return types.Item.itemData(o) end)
    try('Item.isRestocking', function() return types.Item.isRestocking(o) end)
    try('Item.isCarriable', function() return types.Item.isCarriable(o) end)
    try('Container.inventory', function() return types.Container.inventory(o) end)
    try('Container.content', function() return types.Container.content(o) end)
    try('Container.encumbrance', function() return types.Container.getEncumbrance(o) end)
    try('Container.capacity', function() return types.Container.getCapacity(o) end)
    try('Door.isOpen', function() return types.Door.isOpen(o) end)
    try('Door.destCell', function() return types.Door.destCell(o) end)
    try('Door.isTeleport', function() return types.Door.isTeleport(o) end)
    try('Lockable.isLocked', function() return types.Lockable.isLocked(o) end)
    try('Lockable.getLockLevel', function() return types.Lockable.getLockLevel(o) end)
    try('Lockable.getKeyRecord', function() return types.Lockable.getKeyRecord(o) end)
    try('Light.record', function() return types.Light.record(o) end)
    try('Static.record', function() return types.Static.record(o) end)
    try('Activator.record', function() return types.Activator.record(o) end)
    try('Player.isCharGenFinished', function() return types.Player.isCharGenFinished(o) end)
    try('mwscript.getLocalScript', function() return world.mwscript.getLocalScript(o) end)
    try('mwscript.getLocalScript(player)', function() return world.mwscript.getLocalScript(o, player) end)
end

local function cellSweep()
    local cell = player.cell
    try('cell.name', function() return cell.name end)
    try('cell.id', function() return cell.id end)
    try('cell.region', function() return cell.region end)
    try('cell.isExterior', function() return cell.isExterior end)
    try('cell.hasWater', function() return cell.hasWater end)
    try('cell.waterLevel', function() return cell.waterLevel end)
    try('cell.worldSpaceId', function() return cell.worldSpaceId end)
    try('cell.gridX', function() return cell.gridX end)
    try('cell.hasTag', function() return cell:hasTag('NoSleep') end)
    try('cell.pathGrid', function() return cell.pathGrid end)
    try('cell.isInSameSpace', function() return cell:isInSameSpace(player) end)
    try('cell.tostring', function() return tostring(cell) end)
    for name, t in pairs({ Actor = types.Actor, Door = types.Door, Container = types.Container, Static = types.Static,
        Light = types.Light, Item = types.Item, NPC = types.NPC, Activator = types.Activator, Armor = types.Armor }) do
        try('cell.getAll(' .. name .. ')', function() return #cell:getAll(t) end)
    end
    try('world.getCellByName', function() return world.getCellByName(cell.name) end)
    try('world.getCellByName(none)', function() return world.getCellByName('NoSuchCell') end)
    try('world.getExteriorCell', function() return world.getExteriorCell(0, 0) end)
    try('world.cells', function() return #world.cells end)
    try('world.activeActors', function() return #world.activeActors end)
    try('world.players', function() return #world.players end)
    try('world.createObject(missing)', function() return world.createObject('no_such_record_id', 1) end)
    try('world.getObjectByFormId', function() return world.getObjectByFormId(core.getFormId('Skyrim.esm', 1)) end)
    try('world.mwscript.getGlobalVariables', function() return world.mwscript.getGlobalVariables(player) end)
    try('core.getGameTime', function() return core.getGameTime() end)
    try('core.getGMST', function() return core.getGMST('fJumpHeightMin') end)
    try('core.contentFiles.list', function() return #core.contentFiles.list end)
    try('core.stats', function() return core.stats.Skill.records end)
    try('core.magic.effects', function() return core.magic.effects.records end)
    try('core.sound.isEnabled', function() return core.sound.isEnabled() end)
    try('core.getRealTime', function() return core.getRealTime() end)
    try('types.Player.stats', function() return types.Player.stats end)
    try('player.record', function() return types.Player.record(player) end)
    try('player.inventory', function() return types.Actor.inventory(player):getAll() end)
    try('player.stats.level', function() return types.Actor.stats.level(player).current end)
    try('player.stats.health', function() return types.Actor.stats.dynamic.health(player).current end)
    try('player.equipment', function() return types.Actor.getEquipment(player) end)
    try('player.activeSpells', function() return types.Actor.activeSpells(player):isSpellActive('x') end)
    try('player.spells', function() return types.Actor.spells(player) end)
    try('player.encumbrance', function() return types.Actor.getEncumbrance(player) end)
end

return {
    engineHandlers = {
        onUpdate = function()
            player = world.players[1]
            if player == nil then return end
            frames = frames + 1
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if frames % 120 == 0 then print(string.format('MS0 alive frame %d t=%.1f phase %d', frames, t, phase)) end
            if phase == 0 and t > 6 then
                phase = 1
                objects = {}
                for _, o in ipairs(player.cell:getAll()) do objects[#objects + 1] = o end
                print(string.format('MS0 cell %s objects %d', tostring(player.cell.name), #objects))
                local seen, listed = {}, 0
                for _, o in ipairs(objects) do
                    if not seen[o.recordId] and listed < 60 then
                        seen[o.recordId] = true; listed = listed + 1
                        print(string.format('MS0 base %s at %.0f,%.0f,%.0f', o.recordId, o.position.x, o.position.y, o.position.z))
                    end
                end
                cellSweep()
                for _, o in ipairs(objects) do sweepObject(o) end
                report()
                print('MS0 read-only sweep done')
            elseif phase == 1 and t > 9 then
                phase = 2
                -- mutations on a sample of objects (every 7th): the actions are queued and run in the engine's own
                -- synchronised update, where an exception is logged and the world goes on.
                local n = 0
                for i, o in ipairs(objects) do
                    if i % 7 == 0 and o:isValid() then
                        n = n + 1
                        try('mut.setScale', function() o:setScale(1.25) end)
                        try('mut.teleport(in cell)', function() o:teleport(player.cell, o.position + util.vector3(0, 0, 1), o.rotation) end)
                        try('mut.addScript', function() o:addScript('scripts/ms0/none.lua') end)
                        try('mut.removeScript', function() o:removeScript('scripts/ms0/none.lua') end)
                        try('mut.activateBy(non door)', function() o:activateBy(player) end)
                        try('mut.moveInto(player)', function() o:moveInto(types.Actor.inventory(player)) end)
                    end
                end
                print('MS0 mutations queued on ' .. n)
            elseif phase == 2 and t > 14 then
                phase = 3
                local n, alive = 0, 0
                for _, o in ipairs(objects) do
                    n = n + 1
                    if o:isValid() then alive = alive + 1 end
                end
                print(string.format('MS0 after mutations: %d of %d objects valid', alive, n))
                local removed = 0
                for i, o in ipairs(objects) do
                    if i % 11 == 0 and o:isValid() then
                        try('mut.remove', function() o:remove() end)
                        removed = removed + 1
                    end
                end
                print('MS0 removals queued ' .. removed)
                report()
            elseif phase == 3 and t > 18 then
                phase = 4
                local alive = 0
                for _, o in ipairs(objects) do if o:isValid() then alive = alive + 1 end end
                print(string.format('MS0 after removals: %d valid; cell now has %d objects', alive, #player.cell:getAll()))
                print('MS0 sweep complete')
            end
        end,
    },
}
