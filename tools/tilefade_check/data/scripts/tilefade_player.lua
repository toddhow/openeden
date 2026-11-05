-- Task 0087: a frame-counted camera path with screenshots, for comparing the world LOD terrain tile fade (and the
-- build before it). Run with `tgm` and `tcl` in the start commands (god mode, no collision), `--start Balmora` and
-- OPENEDEN_FIXED_FRAME_TIME so that the same frames come out twice. run.sh fills in the scenario below.
--
-- Scenario "still": holds each pose of POSES for HOLD frames and takes a screenshot on the last two frames.
-- Scenario "flight": moves from FROM to TO (offsets from the start position, in units) over FRAMES frames, with a
--   screenshot every SHOT_EVERY frames.
-- The far packs are adopted about 10 s after the start, so nothing moves for START_DELAY real seconds.
local self = require('openmw.self')
local core = require('openmw.core')
local util = require('openmw.util')
local camera = require('openmw.camera')
local debug = require('openmw.debug')

local SCENARIO = '@SCENARIO@'
local START_DELAY = @START_DELAY@
local PITCH = @PITCH@
local HOLD = @HOLD@
local POSES = { @POSES@ }
local FROM = util.vector3(@FROM@)
local TO = util.vector3(@TO@)
local FRAMES = @FRAMES@
local SHOT_EVERY = @SHOT_EVERY@

local startReal = nil
local base = nil
local frame = 0

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            base = base or self.position
            camera.setPitch(PITCH)
            if core.getRealTime() - startReal < START_DELAY then
                return
            end
            frame = frame + 1
            if SCENARIO == 'still' then
                local pose = math.floor((frame - 1) / HOLD) + 1
                local offset = POSES[pose]
                if not offset then
                    return
                end
                core.sendGlobalEvent('TileFadeMove', { position = base + offset })
                local k = (frame - 1) % HOLD
                if k == HOLD - 2 or k == HOLD - 1 then
                    debug.takeScreenshot()
                end
            else
                if frame > FRAMES then
                    return
                end
                local s = (frame - 1) / math.max(1, FRAMES - 1)
                core.sendGlobalEvent('TileFadeMove', { position = base + FROM + (TO - FROM) * s })
                if (frame - 1) % SHOT_EVERY == 0 then
                    debug.takeScreenshot()
                end
            end
        end,
    },
}
