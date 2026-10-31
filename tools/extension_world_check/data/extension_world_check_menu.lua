-- Drives one run of tools/extension_world_check/check.sh: new game, the global script's scene, save, load, quit.
local core = require('openmw.core')
local menu = require('openmw.menu')

local FRAMES_BETWEEN_STEPS = 60
local FRAMES_AFTER_LOAD = 120
-- Frames, so about two minutes at the check's frame limit: long enough for the scene's waits and one slow attack.
local SCENE_TIMEOUT_FRAMES = 7200

local sceneDone = false
local frames = 0
local stage = 'start'

return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if stage == 'start' and frames >= FRAMES_BETWEEN_STEPS then
                menu.newGame()
                stage, frames = 'new game', 0
            elseif stage == 'new game' and frames >= FRAMES_BETWEEN_STEPS then
                core.sendGlobalEvent('OeWorldCheck_Start')
                stage, frames = 'scene', 0
            elseif stage == 'scene' and (sceneDone or frames >= SCENE_TIMEOUT_FRAMES) then
                if not sceneDone then
                    print('LUA_SCENE_TIMEOUT')
                end
                menu.saveGame('w')
                stage, frames = 'saved', 0
            elseif stage == 'saved' and frames >= FRAMES_BETWEEN_STEPS then
                menu.loadGame(' - 1', 'w.omwsave')
                stage, frames = 'loaded', 0
            elseif stage == 'loaded' and frames >= FRAMES_AFTER_LOAD then
                print('LUA_STEP loaded')
                core.quit()
                stage = 'quit'
            end
        end,
    },
    eventHandlers = {
        OeWorldCheck_SceneDone = function()
            sceneDone = true
        end,
    },
}
