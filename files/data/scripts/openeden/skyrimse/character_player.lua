-- Skyrim SE character creation, the player half (task 0225): at the start of a game the creation window opens over the
-- paused world with the camera in front of the player's body, which follows every choice. Needs `[GUI] rmlui` (without it
-- `openeden.ui` is absent and this script does nothing) and the pack's `core.skyrimse.character` namespace (the setting
-- `[extension.skyrimse] character creation`, which needs `menus` and `player body`).
--
-- The window cannot be cancelled: leaving the interface mode (Escape) puts it back. Done leaves the mode and gives the
-- camera back to the mode it had.
local core = require('openmw.core')
local camera = require('openmw.camera')
local self = require('openmw.self')
local util = require('openmw.util')
local nearby = require('openmw.nearby')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okChr, chr = pcall(require, 'core.skyrimse.character')
if not (okUi and okChr) then
  return {}
end
local windows = require('openeden.skyrimse.character_window')

local MODE = 'Interface'
local CAMERA_DISTANCE = 230 -- in front of the body, game units
local CAMERA_HEIGHT = 105
local CAMERA_PITCH = 0.08
local REOPEN_AFTER = 180 -- frames: a finish that was refused offers the window again

local view
local previousMode
local cameraSet = false
local leftAt = nil
local frames = 0

-- The camera stands at the best of a few angles round the player: the first whose line from his head is free for the whole
-- distance (a column or a wall between would hide the body), else the freest. Chosen once when the window opens.
local AZIMUTHS = { 0, 0.7, -0.7, 1.4, -1.4, 2.1, -2.1, math.pi }
local camPos, camYaw

local function chooseCamera()
  local p = self.object.position
  local yaw = self.object.rotation:getYaw()
  local head = p + util.vector3(0, 0, CAMERA_HEIGHT)
  local bestFree, bestPos, bestYaw = -1, nil, nil
  for _, a in ipairs(AZIMUTHS) do
    local dx, dy = math.sin(yaw + a), math.cos(yaw + a)
    local target = head + util.vector3(dx, dy, 0) * CAMERA_DISTANCE
    local hit = nearby.castRay(head, target, { ignore = self.object })
    local free = CAMERA_DISTANCE
    if hit.hit and hit.hitPos then free = (hit.hitPos - head):length() - 25 end
    if free > bestFree then
      bestFree = free
      bestPos = head + util.vector3(dx, dy, 0) * math.max(free, 60)
      bestYaw = math.atan2(-dx, -dy)
    end
    if free >= CAMERA_DISTANCE then break end
  end
  camPos, camYaw = bestPos, bestYaw
end

local function placeCamera()
  if camera.getMode() ~= camera.MODE.Static then camera.setMode(camera.MODE.Static, true) end
  camera.setStaticPosition(camPos)
  camera.setYaw(camYaw)
  camera.setPitch(CAMERA_PITCH)
end

local function takeCamera()
  if cameraSet then return end
  previousMode = camera.getMode()
  chooseCamera()
  camera.setMode(camera.MODE.Static, true)
  placeCamera()
  cameraSet = true
end

local function giveCameraBack()
  if not cameraSet then return end
  cameraSet = false
  camera.setMode(previousMode or camera.MODE.ThirdPerson, true)
end

local function leave()
  leftAt = frames
  view.dismiss()
  giveCameraBack()
  I.UI.setMode()
end

view = windows.create{
  ui = ui,
  chr = chr,
  send = function(t) core.sendGlobalEvent('SkyrimCharacterRequest', t) end,
  close = leave,
}

local function show()
  view.open()
  takeCamera()
  I.UI.setMode(MODE, { windows = {} })
end

return {
  engineHandlers = {
    onFrame = function()
      frames = frames + 1
      if view.isOpen() then
        if I.UI.getMode() == nil then
          I.UI.setMode(MODE, { windows = {} }) -- Escape does not leave a creation
        end
        placeCamera()
      elseif chr.needed() and I.UI.getMode() == nil and (leftAt == nil or frames - leftAt > REOPEN_AFTER) then
        show()
      end
      view.update()
    end,
  },
}
