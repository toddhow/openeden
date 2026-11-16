-- Skyrim SE pickpocket window, the player half (task 0208): the inventory windows (openeden/skyrimse/inventory_windows.lua)
-- beside a living actor's list. A take is a try for the pack (a global event), not a transfer: a failure closes the
-- window. Needs `[GUI] rmlui`; without it `openeden.ui` is absent and this script does nothing.
local core = require('openmw.core')
local I = require('openmw.interfaces')
local mui = require('openmw.ui')

local okUi, ui = pcall(require, 'openeden.ui')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local okPick, pick = pcall(require, 'core.skyrimse.pickup')
if not (okUi and okInv and okPick) then
  return {}
end
local windows = require('openeden.skyrimse.inventory_windows')

local MODE = 'Interface'
local view
local pickOwner
local seenMode, waited = false, 0
local lastRevision

local function leave()
  I.UI.setMode()
end

view = windows.create{
  ui = ui,
  inv = inv,
  transfer = function(from, to, item, count)
    -- only taking from the actor is allowed; putting items into its pockets is not
    if from == pickOwner and to == 'player' then
      core.sendGlobalEvent('SkyrimPickpocketTake', { owner = from, item = item, count = count })
    end
  end,
  takeAll = function() end,
  close = leave,
}

local function show(owner, title)
  pickOwner = owner
  lastRevision = pick.revision()
  view.open(owner, title)
  seenMode, waited = false, 0
  I.UI.setMode(MODE, { windows = {} })
end

return {
  engineHandlers = {
    onFrame = function()
      if view.isOpen() then
        if I.UI.getMode() ~= nil then
          seenMode = true
        elseif seenMode or waited > 120 then
          view.dismiss()
          pickOwner = nil
        else
          waited = waited + 1
        end
        local revision = pick.revision()
        if revision ~= lastRevision then
          lastRevision = revision
          local state = pick.state()
          if state == 'caught' then
            mui.showMessage(pick.message())
            leave() -- the world runs again, so the crime is judged
          elseif state == 'success' then
            mui.showMessage(pick.message())
          end
        end
      end
      view.update()
    end,
  },
  eventHandlers = {
    SkyrimPickpocketOpen = function(data) show(data.owner, data.name) end,
  },
}
