-- The restyled Skyrim combat HUD's player script (task 0244). Started only when the session's game pack declares the
-- package `core.skyrimse.hudstyle` (the pack setting [extension.skyrimse] hud style; the REQUIRES tag in
-- builtin.omwscripts) and needs `[GUI] rmlui` (without it `openeden.ui` does not exist and this script does nothing).
-- It replaces the plain bars of skyrim_combat_hud.lua, which steps aside when this package exists.
--
-- The camera is this script's: a head position the pack gives (world units) is turned into a point of the screen with
-- the camera's view transform and field of view (the same maths as camera.worldToViewportVector, but a point behind
-- the camera gives nil there, which that function does not).
local okUi, ui = pcall(require, 'openeden.ui')
if not okUi then return {} end
local camera = require('openmw.camera')
local core = require('openmw.core')
local uiCore = require('openmw.ui')
local style = require('core.skyrimse.hudstyle')

local NEAR = 1          -- units in front of the camera under which a point is behind it
local MARGIN = 0.08     -- a point this far outside the screen (a fraction of its size) is still drawn
local env = {
  now = core.getRealTime,
  screen = function() local s = uiCore.screenSize(); return s.x, s.y end,
  project = function(x, y, z)
    local view = camera.getViewTransform():apply(require('openmw.util').vector3(x, y, z))
    local depth = -view.z
    if depth <= NEAR then return nil end
    local w, h = uiCore.screenSize().x, uiCore.screenSize().y
    local aspect = h > 0 and w / h or 1
    local tanHalf = math.tan(camera.getFieldOfView() / 2) -- openmw.camera gives radians
    local nx = 0.5 + 0.5 * (view.x / depth) / (aspect * tanHalf)
    local ny = 0.5 - 0.5 * (view.y / depth) / tanHalf
    if nx < -MARGIN or nx > 1 + MARGIN or ny < -MARGIN or ny > 1 + MARGIN then return nil end
    return nx, ny
  end,
}
local hud = require('openeden.hud.skyrimstyle').create(ui, style, env)

return {
  engineHandlers = {
    onFrame = function() hud:update() end,
  },
}
