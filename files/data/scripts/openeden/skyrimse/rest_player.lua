-- Skyrim SE waiting and sleeping, the player half (task 0210): draws the wait and sleep window with `openeden.ui` (see
-- openeden/skyrimse/rest_windows.lua for the window logic). Needs `[GUI] rmlui`; without it `openeden.ui` is absent and
-- this script does nothing, and without the pack's `core.skyrimse.rest` namespace it is not even started.
--
-- The wait key is the engine's own rest key (T): the engine puts the mode `Rest` (its MyGUI wait dialog) on the mode stack
-- when it is pressed. As the journal does with `Journal`, this script takes that mode as its key: it replaces it with GUI
-- mode and no MyGUI window and shows its own window. A bed arrives as the global script's `SkyrimSleepOpen`.
local I = require('openmw.interfaces')
local core = require('openmw.core')

local okUi, ui = pcall(require, 'openeden.ui')
local okRest, rest = pcall(require, 'core.skyrimse.rest')
if not (okUi and okRest) then
  return {}
end
local windows = require('openeden.skyrimse.rest_windows')

local MODE = 'Interface'
local ENGINE_REST = 'Rest'
local view
local seenMode, waited = false, 0

local function leave()
  I.UI.setMode() -- drops every mode; the window follows in onFrame
end

view = windows.create{
  ui = ui,
  rest = rest,
  close = leave,
  send = function(t) core.sendGlobalEvent('SkyrimRestDo', t) end,
}

local function show(open)
  open()
  seenMode, waited = false, 0
  -- GUI mode with no MyGUI window at all: the cursor, the pause and the RmlUi overlay's input
  I.UI.setMode(MODE, { windows = {} })
end

return {
  engineHandlers = {
    onFrame = function()
      local mode = I.UI.getMode()
      if view.isOpen() then
        if not seenMode then
          if mode == MODE then
            seenMode = true
          else
            waited = waited + 1
            if waited > 120 then view.dismiss() end
          end
        elseif mode == ENGINE_REST then
          leave() -- the key again
        elseif mode == nil then
          view.dismiss() -- the mode was left (Escape, a button, or the key above)
        end
      elseif mode == ENGINE_REST and rest.ready() then
        show(function() view.open('wait') end)
      elseif mode == ENGINE_REST then
        I.UI.setMode() -- the pack is not ready: the engine's dialog is not wanted either
      end
      view.update()
    end,
  },
  eventHandlers = {
    SkyrimSleepOpen = function() show(function() view.open('sleep') end) end,
    SkyrimRestNotice = function(data) show(function() view.notice(data.text) end) end,
    SkyrimRestMessage = function(data) show(function() view.notice(windows.refusalText(data.reason)) end) end,
  },
}
