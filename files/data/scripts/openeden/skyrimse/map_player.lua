-- Skyrim SE compass and world map, the player half (task 0201). The compass is shown whenever the pack has the map on; the
-- world map opens with the M key and closes with Escape or its Close button. Needs `[GUI] rmlui` (without it `openeden.ui`
-- is absent and this script does nothing) and the pack's `core.skyrimse.map` namespace (the builtin line carries REQUIRES=).
local I = require('openmw.interfaces')
local self = require('openmw.self')
local input = require('openmw.input')

local okUi, ui = pcall(require, 'openeden.ui')
local okMap, map = pcall(require, 'core.skyrimse.map')
local core = require('openmw.core')
-- Task 0218: fast travel is optional (the pack setting `fast travel`)
local okTravel, travel = pcall(require, 'core.skyrimse.travel')
if not (okUi and okMap) then
  return {}
end

local MODE = 'Interface'

-- The player's place as the windows want it. The worldspace of a neutral exterior is `skyrimse:w<form id>`.
local function playerPlace()
  local cell = self.cell
  if not cell then return nil end
  local pos = self.position
  local id = tostring(cell.worldSpaceId or '')
  local world = tonumber(id:match(':w(%d+)$'))
  return { x = pos.x, y = pos.y, yaw = self.rotation:getYaw(), world = world }
end

local compass = require('openeden.skyrimse.compass').create{ ui = ui, map = map, player = playerPlace }
local seenMode, waited = false, 0
local worldMap

local function leave()
  I.UI.setMode()
end

local worldmapLib = require('openeden.skyrimse.worldmap')
worldMap = worldmapLib.create{ ui = ui, map = map, player = playerPlace, close = leave,
  travel = okTravel and travel or nil,
  requestTravel = function(marker, name)
    print('[skyrimse map] travel requested to ' .. tostring(name))
    core.sendGlobalEvent('SkyrimTravelDo', { marker = marker, name = name })
  end }

local function showMap()
  if I.UI.getMode() ~= nil then return end
  local opened = worldMap.open()
  print('[skyrimse map] open ' .. tostring(opened))
  if opened then
    seenMode, waited = false, 0
    I.UI.setMode(MODE, { windows = {} })
  end
end

return {
  eventHandlers = {
    -- From travel_global.lua: the pack refused (a reason word) or the player has been moved.
    SkyrimTravelRefused = function(data)
      worldMap.notice(worldmapLib.refusalText(data.reason))
    end,
    SkyrimTravelDone = function(data)
      print('[skyrimse map] travelled to ' .. tostring(data.name))
      worldMap.dismiss()
      leave()
    end,
  },
  engineHandlers = {
    onKeyPress = function(key)
      if key.code == input.KEY.M and not worldMap.isOpen() then
        print('[skyrimse map] M pressed, mode ' .. tostring(I.UI.getMode()))
        showMap()
      end
    end,
    onFrame = function()
      local mode = I.UI.getMode()
      if worldMap.isOpen() then
        if not seenMode then
          if mode == MODE then
            seenMode = true
          else
            waited = waited + 1
            if waited > 120 then worldMap.dismiss() end
          end
        elseif mode == nil then
          worldMap.dismiss() -- the mode was left (Escape or Close)
        end
        worldMap.update()
      end
      compass.update()
    end,
  },
}
