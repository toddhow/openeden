-- Task 0069 stage e, second review (finding 1): the motion scene of tools/mergedbody_check (SCENE=motion). Run it
-- with "[Rendering]\nhdr = true\ngbuffer = true\ngbuffer debug view = velocity" in EXTRA_SETTINGS and
-- OPENEDEN_FIXED_FRAME_TIME=0.0166667: each frame is then the G-buffer's screen motion (grey 0.5 is none) of a
-- sixtieth of a second. (A "[Video]\nframerate limit = 3" used to space the screenshots, which took one file name and
-- later frames when asked for in frames close together; since 2026-11-04 each holds the frame it was asked for.) The
-- player puts on the full iron set with the arms down, raises the fists (hand-to-hand) and with the arms up takes
-- everything off. A merged draw that the set hid entirely had its skin left out, so its previous-frame matrices are of
-- the arms-down pose; it must restart without motion (Npc::applyMergedRegions, Anim::Skin::forgetPreviousMatrices), or
-- the first frame after the change shows the whole swing as motion. The camera holds still from the first frame on,
-- so the world has no motion. Screenshots: one frame before the change, the change frame and the four after it
-- ("MBSHOT motion <n> frame <f>"). The items come from mbequip_global.lua (no NPC row). The phases are counted in
-- frames after the real-time settling.
local self = require('openmw.self')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local camera = require('openmw.camera')
local debug = require('openmw.debug')

local SETTLE = 8 -- seconds of real time before anything happens (cells and actors load)
local ARMOUR_AT = 30 -- frames after the items were asked for: put on the full set
local FISTS_AFTER = 60 -- frames in the set before the fists go up
local OFF_AFTER = 90 -- frames with the fists up before the set comes off
local SHOTS_AFTER = 4 -- frames after the change frame with a screenshot

local FULL = { Shirt = 'shirt', Pants = 'pants', Cuirass = 'cuirass', Greaves = 'greaves', Boots = 'boots',
    LeftGauntlet = 'lgauntlet', RightGauntlet = 'rgauntlet', LeftPauldron = 'lpauldron',
    RightPauldron = 'rpauldron', Helmet = 'helmet' }

local startReal = nil
local origin = nil
local yaw = 0
local ids = nil
local stage = 'settle'
local phaseFrame = 0
local frame = 0

local function toWorld(v)
    return origin + util.transform.rotateZ(yaw) * v
end

local function look(eye, at)
    -- The mode changes in the next frame, and a static position may only be set in the static mode.
    if camera.getMode() ~= camera.MODE.Static then
        camera.setMode(camera.MODE.Static)
        return
    end
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

local function enter(name)
    stage = name
    phaseFrame = frame
    print('MBPHASE ' .. name .. ' frame ' .. frame)
end

local function shoot(name)
    debug.takeScreenshot()
    print('MBSHOT motion ' .. name .. ' frame ' .. frame)
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
            frame = frame + 1
            local c = self.controls
            c.movement = 0
            c.sideMovement = 0
            c.yawChange = 0
            c.use = 0
            c.run = false
            if core.getRealTime() - startReal < SETTLE then
                return
            end
            if origin then
                -- In front of the player, the whole body in view, from the first frame on.
                look(toWorld(util.vector3(0, 170, 75)), toWorld(util.vector3(0, 0, 62)))
            end
            local n = frame - phaseFrame
            if stage == 'settle' then
                types.Actor.setEquipment(self, {})
                origin = self.position
                yaw = self.rotation:getYaw()
                core.sendGlobalEvent('MBEquipSetup', { origin = origin, yaw = yaw, row = false,
                    forward = util.transform.rotateZ(yaw) * util.vector3(0, 1, 0),
                    right = util.transform.rotateZ(yaw) * util.vector3(1, 0, 0) })
                enter('setup')
            elseif stage == 'setup' then
                if ids and n >= ARMOUR_AT then
                    types.Actor.setEquipment(self, equipment(FULL))
                    enter('armour')
                end
            elseif stage == 'armour' then
                if n >= FISTS_AFTER then
                    -- No weapon: the hand-to-hand stance raises both fists.
                    types.Actor.setStance(self, types.Actor.STANCE.Weapon)
                    enter('fists')
                end
            elseif stage == 'fists' then
                if n == OFF_AFTER - 1 then
                    shoot('before')
                elseif n >= OFF_AFTER then
                    types.Actor.setEquipment(self, {})
                    enter('off')
                    shoot('0')
                end
            elseif stage == 'off' then
                if n <= SHOTS_AFTER then
                    shoot(tostring(n))
                elseif n >= SHOTS_AFTER + 3 then
                    enter('quit')
                    core.quit()
                end
            end
        end,
    },
}
