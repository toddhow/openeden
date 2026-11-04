-- Task 0066 stage e: a scripted climb from the ground to a height and back, with a screenshot every few seconds, so
-- that a crack, hole or pop at a level boundary shows in frames and in the frame-time trace of one continuous run.
-- Run it with `tgm` and `tcl` (god mode, no collision: a player placed in the air falls and dies otherwise).
-- It does nothing unless tools/mapview_check is added to the data paths, so it is never part of the game.
local self = require('openmw.self')
local core = require('openmw.core')
local util = require('openmw.util')
local camera = require('openmw.camera')
local debug = require('openmw.debug')

local START_DELAY = 25 -- seconds: the far packs are adopted about 10 s after the start (stage d)
local CLIMB_SECONDS = 40
local HOLD_TOP_SECONDS = 5
local TOP_HEIGHT = 25000 -- units above the start
local SHOT_EVERY = 5 -- seconds between screenshots while moving
local PITCH_DOWN = 0.61 -- radians (35 degrees), as in the stage d pose scripts

local seconds = 0
local base = nil
local nextShot = 0
local startReal = nil
local lastPrinted = -1

local function heightAt(t)
    local up = CLIMB_SECONDS
    local hold = HOLD_TOP_SECONDS
    if t < 0 then
        return 0
    elseif t < up then
        local s = t / up
        return TOP_HEIGHT * s * s * (3 - 2 * s) -- smooth start and stop
    elseif t < up + hold then
        return TOP_HEIGHT
    elseif t < 2 * up + hold then
        local s = (t - up - hold) / up
        return TOP_HEIGHT * (1 - s * s * (3 - 2 * s))
    end
    return 0
end

return {
    engineHandlers = {
        onFrame = function()
            -- Real time: with `--skip-menu` the game waits in character generation, where the simulation time stands still.
            seconds = core.getRealTime() - (startReal or core.getRealTime())
            startReal = startReal or core.getRealTime()
            if math.floor(seconds) ~= lastPrinted then
                lastPrinted = math.floor(seconds)
                if lastPrinted % 5 == 0 then
                    print("ascent: t=" .. lastPrinted .. " z=" .. tostring(self.position.z))
                end
            end
            camera.setPitch(PITCH_DOWN)
            if not base then
                base = self.position
            end
            local t = seconds - START_DELAY
            if t < 0 then
                return
            end
            core.sendGlobalEvent('MapViewAscentMove', { position = base + util.vector3(0, 0, heightAt(t)) })
            if t >= nextShot then
                debug.takeScreenshot()
                nextShot = nextShot + SHOT_EVERY
            end
        end,
    },
}
