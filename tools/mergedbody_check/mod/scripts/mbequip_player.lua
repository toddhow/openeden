-- Task 0069 stage e: the player half of the equipment scene of tools/mergedbody_check (SCENE=equipment). The player
-- stands still and wears one combination after another; each is photographed from a static camera in front (the whole
-- body) and close to the right wrist, knee and ankle (where gloves, greaves and boots end). Then the NPC row (each NPC
-- wearing one combination, mbequip_global.lua), then 200 cycles of the full set on and off for the leak check: the
-- frame counters before ("MBPHASE baseline") and after ("MBPHASE after") are compared by the caller. Every phase is
-- marked with a log line "MBPHASE <name>", every screenshot with "MBSHOT <combination> <view>".
local self = require('openmw.self')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local camera = require('openmw.camera')
local debug = require('openmw.debug')

local SETTLE = 6 -- seconds of real time before anything happens (cells and actors load)
local COMBO_START = 2 -- seconds after SETTLE: the items have arrived
local CYCLES = 200 -- full set on, then off: one cycle
local FRAMES_PER_CHANGE = 2 -- frames between two equipment changes

-- slot name (types.Actor.EQUIPMENT_SLOT) = item kind (mbequip_global.lua)
local FULL = { Shirt = 'shirt', Pants = 'pants', Cuirass = 'cuirass', Greaves = 'greaves', Boots = 'boots',
    LeftGauntlet = 'lgauntlet', RightGauntlet = 'rgauntlet', LeftPauldron = 'lpauldron',
    RightPauldron = 'rpauldron', Helmet = 'helmet' }
local COMBOS = {
    { name = 'naked', slots = {} },
    { name = 'shirt', slots = { Shirt = 'shirt' } },
    { name = 'pants', slots = { Pants = 'pants' } },
    { name = 'cuirass_greaves', slots = { Cuirass = 'cuirass', Greaves = 'greaves' } },
    { name = 'robe', slots = { Robe = 'robe' } },
    { name = 'skirt', slots = { Skirt = 'skirt' } },
    { name = 'gloves_shoes', slots = { LeftGauntlet = 'lglove', RightGauntlet = 'rglove', Boots = 'shoes' } },
    { name = 'gauntlets_boots', slots = { LeftGauntlet = 'lgauntlet', RightGauntlet = 'rgauntlet', Boots = 'boots' } },
    { name = 'pauldrons', slots = { LeftPauldron = 'lpauldron', RightPauldron = 'rpauldron' } },
    { name = 'helmet', slots = { Helmet = 'helmet' } },
    { name = 'full', slots = FULL },
    { name = 'naked2', slots = {} },
}
-- view name, start and screenshot (seconds after the combination starts), camera and target in the player's frame
-- (x right, y forward, z up)
local VIEWS = {
    { name = 'front', from = 0, shot = 1.1, eye = util.vector3(0, 300, 80), at = util.vector3(0, 0, 65) },
    { name = 'wrist', from = 1.3, shot = 1.6, eye = util.vector3(60, 50, 75), at = util.vector3(20, 0, 70) },
    { name = 'knee', from = 1.8, shot = 2.1, eye = util.vector3(55, 55, 42), at = util.vector3(10, 0, 40) },
    { name = 'ankle', from = 2.3, shot = 2.6, eye = util.vector3(50, 50, 16), at = util.vector3(10, 0, 10) },
}
local COMBO_LENGTH = 2.8

local startReal = nil
local origin = nil
local yaw = 0
local ids = nil
local stage = 'settle'
local comboIndex = 0
local comboStart = 0
local shots = {}
local phaseStart = 0
local changes = 0
local frame = 0

local function toWorld(v)
    return origin + util.transform.rotateZ(yaw) * v
end

local function look(eye, at)
    camera.setMode(camera.MODE.Static)
    camera.setStaticPosition(eye)
    local d = at - eye
    camera.setYaw(math.atan2(d.x, d.y))
    camera.setPitch(-math.atan2(d.z, math.sqrt(d.x * d.x + d.y * d.y)))
end

local function equipment(slots)
    local result = {}
    for slot, kind in pairs(slots) do
        if ids[kind] then
            result[types.Actor.EQUIPMENT_SLOT[slot]] = ids[kind]
        end
    end
    return result
end

local function enter(name, t)
    stage = name
    phaseStart = t
    print('MBPHASE ' .. name)
end

return {
    eventHandlers = {
        MBEquipItems = function(data)
            ids = data.ids
        end,
    },
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            local t = core.getRealTime() - startReal - SETTLE
            frame = frame + 1
            local c = self.controls
            c.movement = 0
            c.sideMovement = 0
            c.yawChange = 0
            c.use = 0
            c.run = false
            if t < 0 then
                return
            end
            if stage == 'settle' then
                types.Actor.setEquipment(self, {})
                origin = self.position
                yaw = self.rotation:getYaw()
                core.sendGlobalEvent('MBEquipSetup', { origin = origin, yaw = yaw,
                    forward = util.transform.rotateZ(yaw) * util.vector3(0, 1, 0),
                    right = util.transform.rotateZ(yaw) * util.vector3(1, 0, 0) })
                enter('setup', t)
            elseif stage == 'setup' then
                if ids and t >= COMBO_START then
                    comboIndex = 1
                    comboStart = t
                    shots = {}
                    types.Actor.setEquipment(self, equipment(COMBOS[1].slots))
                    enter('combo ' .. COMBOS[1].name, t)
                end
            elseif stage:sub(1, 5) == 'combo' then
                local combo = COMBOS[comboIndex]
                local dt = t - comboStart
                if dt >= COMBO_LENGTH then
                    comboIndex = comboIndex + 1
                    if comboIndex > #COMBOS then
                        enter('row', t)
                    else
                        comboStart = t
                        shots = {}
                        types.Actor.setEquipment(self, equipment(COMBOS[comboIndex].slots))
                        enter('combo ' .. COMBOS[comboIndex].name, t)
                    end
                else
                    local view = VIEWS[1]
                    for _, v in ipairs(VIEWS) do
                        if dt >= v.from then
                            view = v
                        end
                    end
                    look(toWorld(view.eye), toWorld(view.at))
                    if dt >= view.shot and not shots[view.name] then
                        shots[view.name] = true
                        debug.takeScreenshot()
                        print('MBSHOT ' .. combo.name .. ' ' .. view.name)
                    end
                end
            elseif stage == 'row' then
                -- Behind the player, over its head, looking at the row.
                look(toWorld(util.vector3(0, -200, 200)), toWorld(util.vector3(0, 300, 60)))
                if t - phaseStart >= 2.5 and not shots.row then
                    shots.row = true
                    debug.takeScreenshot()
                    print('MBSHOT row front')
                end
                if t - phaseStart >= 3.5 then
                    enter('baseline', t)
                end
            elseif stage == 'baseline' then
                if t - phaseStart >= 3 then
                    frame = 0
                    changes = 0
                    enter('cycles', t)
                end
            elseif stage == 'cycles' then
                if frame % FRAMES_PER_CHANGE == 0 then
                    changes = changes + 1
                    if changes > 2 * CYCLES then
                        types.Actor.setEquipment(self, {})
                        enter('after', t)
                    else
                        -- Odd changes put the full set on, even ones take it off: the last change leaves it off.
                        types.Actor.setEquipment(self, changes % 2 == 1 and equipment(FULL) or {})
                    end
                end
            elseif stage == 'after' then
                if t - phaseStart >= 4 then
                    enter('quit', t)
                    core.quit()
                end
            end
        end,
    },
}
