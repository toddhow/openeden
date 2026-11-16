-- Skyrim SE lock picking window, the player half (task 0203): draws the pack's minigame with `openeden.ui` (see
-- openeden/skyrimse/lockpick_window.lua for the window logic). Needs `[GUI] rmlui`; without it `openeden.ui` is absent
-- and this script does nothing. Keys: A and D (or the arrows) turn the pick, Space turns the lock (polled each frame).
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okLb, lb = pcall(require, 'core.skyrimse.lockbooks')
if not (okUi and okLb) then
  return {}
end
local windows = require('openeden.skyrimse.lockpick_window')

local MODE = 'Interface'
local view
local seenMode, waited = false, 0

local function leave()
  I.UI.setMode() -- drops every mode; the window follows in onFrame
end

view = windows.create{
  ui = ui,
  lb = lb,
  begin = function(placement, level)
    core.sendGlobalEvent('SkyrimLockpickBegin', { placement = placement, level = level })
  end,
  input = function(angle, turning, dtMs)
    core.sendGlobalEvent('SkyrimLockpickInput', { angle = angle, turning = turning, dtMs = dtMs })
  end,
  cancel = function() core.sendGlobalEvent('SkyrimLockpickCancel', {}) end,
  close = leave,
}

-- The game is paused in the interface mode (the frame's simulation duration is 0, and so is getRealFrameDuration in a
-- paused game), so the minigame's time is the wall clock between two frames, at most MAX_STEP.
local MAX_STEP = 0.1
local lastReal = nil
local function frameStep()
  local now = core.getRealTime()
  local dt = lastReal and (now - lastReal) or 0
  lastReal = now
  if dt < 0 then dt = 0 elseif dt > MAX_STEP then dt = MAX_STEP end
  return dt
end

-- The keys are polled each frame (input.isKeyPressed reads the keyboard state, which works in the interface mode where
-- the key press handlers are not called).
local function pollKeys()
  view.setKey('left', input.isKeyPressed(input.KEY.A) or input.isKeyPressed(input.KEY.LeftArrow))
  view.setKey('right', input.isKeyPressed(input.KEY.D) or input.isKeyPressed(input.KEY.RightArrow))
  view.setKey('turn', input.isKeyPressed(input.KEY.Space))
end

return {
  engineHandlers = {
    onFrame = function()
      if view.isOpen() then
        if I.UI.getMode() ~= nil then
          seenMode = true
        elseif seenMode or waited > 120 then
          view.dismiss() -- the mode was left (Escape, or Cancel)
        else
          waited = waited + 1
        end
        pollKeys()
      end
      if view.isOpen() then view.update(frameStep()) else lastReal = nil end
    end,
  },
  eventHandlers = {
    SkyrimLockpickOpen = function(data)
      view.open(data.placement, data.level, data.name)
      seenMode, waited = false, 0
      I.UI.setMode(MODE, { windows = {} })
    end,
  },
}
