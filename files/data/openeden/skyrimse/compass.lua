-- The Skyrim SE compass (task 0201, milestone M-S7), written with `openeden.ui` (decision 0015). A strip at the top of the
-- screen with the four cardinal points, the map markers near the player (discovered ones, and undiscovered ones as a
-- question mark) and the quest targets of the displayed objectives. Next to the combat HUD (openeden/hud/skyrimcombat.lua).
--
-- This module holds the whole logic and talks only to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui        the `openeden.ui` library
--   env.map       the `core.skyrimse.map` package (Read functions)
--   env.player()  returns { x, y, yaw, world } for the player, or nil when there is no player; an interior has no world
--                 (the strip then shows the cardinal points only)
-- The strip shows the 180 degrees in front of the player; a thing behind the player is hidden. Angles are radians; the
-- game's heading is 0 facing +Y and grows clockwise (SubjectState::heading in sdk/include/openeden/game/combat_rules.hpp).
local M = {}

local STRIP_WIDTH = 520
local STRIP_HEIGHT = 32
local ROW2 = 15 -- the markers sit on a second row under the cardinal points
local FOV = math.pi -- total field of view
local MARKER_SLOTS = 16
local TARGET_SLOTS = 8
local REFRESH_DISTANCE = 256 -- re-ask the pack when the player moved this far, or its revision moved
-- ours; the same numbers as kCompassRange and kHintRange in game_packs/skyrimse/include/game_skyrimse/map_markers.hpp
local RANGE = 16384
local HINT_RANGE = 8192
local LABEL_ARC = math.rad(7) -- a marker this close to the middle names itself

local CARDINALS = { { 'N', 0 }, { 'E', math.pi / 2 }, { 'S', math.pi }, { 'W', -math.pi / 2 } }

--- The angle of a thing at (dx, dy) from the player, relative to the way the player faces, in (-pi, pi]. Positive is to
--- the right. Pure.
function M.relativeAngle(dx, dy, yaw)
  local a = math.atan2(dx, dy) - yaw -- the bearing from north, clockwise, less the heading
  while a > math.pi do a = a - 2 * math.pi end
  while a <= -math.pi do a = a + 2 * math.pi end
  return a
end

--- The left edge in pixels of something at the relative angle, or nil when it is outside the strip. Pure.
function M.stripX(angle, width, fov)
  width, fov = width or STRIP_WIDTH, fov or FOV
  if angle < -fov / 2 or angle > fov / 2 then return nil end
  return math.floor((angle / fov + 0.5) * width + 0.5)
end

M.STRIP_WIDTH = STRIP_WIDTH

function M.create(env)
  local ui, map = env.ui, env.map
  local self = {}

  local cardinal = {}
  for i = 1, #CARDINALS do cardinal[i] = { left = ui.Value('0px'), shown = ui.Value(false) } end
  local marker = {}
  for i = 1, MARKER_SLOTS do
    marker[i] = { left = ui.Value('0px'), shown = ui.Value(false), text = ui.Value('O'), class = ui.Value('info') }
  end
  local target = {}
  for i = 1, TARGET_SLOTS do target[i] = { left = ui.Value('0px'), shown = ui.Value(false) } end
  local label = ui.Value('')

  local doc = nil
  local cache = { revision = nil, x = nil, y = nil, world = nil, markers = {}, targets = {} }

  local function readMarkers(world, x, y)
    local xs = map.marker_x(world, x, y, RANGE, HINT_RANGE, 0)
    local ys = map.marker_y(world, x, y, RANGE, HINT_RANGE, 0)
    local found = map.marker_discovered(world, x, y, RANGE, HINT_RANGE, 0)
    local names = map.marker_name(world, x, y, RANGE, HINT_RANGE, 0)
    local list = {}
    for i = 1, #xs do
      list[i] = { x = xs[i], y = ys[i], discovered = found[i], name = names[i] or '' }
    end
    return list
  end

  -- An exterior target counts when the pack says its cell is in the player's worldspace; a target in an interior has
  -- world 0 and is not shown (the way through the door is not worked out).
  local function readTargets(world)
    local xs, ys, ws, texts = map.target_x(), map.target_y(), map.target_world(), map.target_text()
    local list = {}
    for i = 1, #xs do
      if ws[i] == world then
        list[#list + 1] = { x = xs[i], y = ys[i], text = texts[i] or '' }
      end
    end
    return list
  end

  local function refresh(p)
    cache.revision = map.revision()
    cache.x, cache.y, cache.world = p.x, p.y, p.world
    if p.world then
      cache.markers = readMarkers(p.world, p.x, p.y)
      cache.targets = readTargets(p.world)
    else
      cache.markers, cache.targets = {}, {}
    end
  end

  local function needsRefresh(p)
    if cache.revision ~= map.revision() or cache.world ~= p.world then return true end
    local dx, dy = p.x - (cache.x or 0), p.y - (cache.y or 0)
    return dx * dx + dy * dy > REFRESH_DISTANCE * REFRESH_DISTANCE
  end

  local function place(slot, x)
    if x then
      slot.left:set(string.format('%dpx', x))
      slot.shown:set(true)
    else
      slot.shown:set(false)
    end
  end

  -- the elements must be made inside the document's build function (they need its reactive scope)
  local function children()
    local children = {}
    for i, c in ipairs(CARDINALS) do
      children[#children + 1] = ui.Text{
        text = c[1], class = 'title',
        visible = cardinal[i].shown,
        style = { position = 'absolute', top = 0, left = cardinal[i].left },
      }
    end
    for i = 1, MARKER_SLOTS do
      children[#children + 1] = ui.Text{
        text = marker[i].text, class = marker[i].class,
        visible = marker[i].shown,
        style = { position = 'absolute', top = ROW2, left = marker[i].left },
      }
    end
    for i = 1, TARGET_SLOTS do
      children[#children + 1] = ui.Text{
        text = '!', class = 'quest-entry',
        visible = target[i].shown,
        style = { position = 'absolute', top = ROW2, left = target[i].left, color = 'rgb(240, 220, 90)' },
      }
    end
    return children
  end

  local function build()
    return ui.Hud{
      name = 'skyrim-compass',
      style = { position = 'absolute', left = '50%', top = 4, width = STRIP_WIDTH, height = STRIP_HEIGHT + 22,
        ['margin-left'] = -math.floor(STRIP_WIDTH / 2) },
      build = function()
        local strip = { style = { width = STRIP_WIDTH, height = STRIP_HEIGHT } }
        strip[1] = ui.Frame{ style = { position = 'absolute', left = 0, top = STRIP_HEIGHT - 1, width = STRIP_WIDTH,
          height = 1, ['background-color'] = 'rgba(255, 255, 255, 120)' } }
        for _, c in ipairs(children()) do strip[#strip + 1] = c end
        return ui.Column{
          ui.Stack(strip),
          ui.Text{ text = label, class = 'info', style = { width = STRIP_WIDTH, ['text-align'] = 'center' } },
        }
      end }
  end

  --- Builds the strip the first time it is called with a player; later calls move what is on it. Cheap when nothing
  --- changed: the player's position and angle are the only reads (the pack is asked again when the player moved 256 units
  --- or the pack's revision changed).
  function self.update()
    local p = env.player()
    if not p then
      if doc then doc:destroy(); doc = nil end
      return
    end
    if not doc then doc = build() end
    for i, c in ipairs(CARDINALS) do
      place(cardinal[i], M.stripX(M.relativeAngle(math.sin(c[2]), math.cos(c[2]), p.yaw), STRIP_WIDTH, FOV))
    end
    if not map.ready() then return end
    if needsRefresh(p) then refresh(p) end
    local best, bestAngle = nil, LABEL_ARC
    local used = 0
    for _, m in ipairs(cache.markers) do
      local a = M.relativeAngle(m.x - p.x, m.y - p.y, p.yaw)
      local x = M.stripX(a, STRIP_WIDTH, FOV)
      if x and used < MARKER_SLOTS then
        used = used + 1
        local slot = marker[used]
        slot.text:set(m.discovered and 'O' or '?')
        slot.class:set(m.discovered and 'title' or 'info')
        place(slot, x)
        if m.discovered and m.name ~= '' and math.abs(a) < bestAngle then best, bestAngle = m, math.abs(a) end
      end
    end
    for i = used + 1, MARKER_SLOTS do marker[i].shown:set(false) end
    local tused = 0
    local targetText = ''
    for _, t in ipairs(cache.targets) do
      local a = M.relativeAngle(t.x - p.x, t.y - p.y, p.yaw)
      local x = M.stripX(a, STRIP_WIDTH, FOV)
      if x and tused < TARGET_SLOTS then
        tused = tused + 1
        place(target[tused], x)
        if math.abs(a) < LABEL_ARC and t.text ~= '' then targetText = t.text end
      end
    end
    for i = tused + 1, TARGET_SLOTS do target[i].shown:set(false) end
    label:set(targetText ~= '' and targetText or (best and best.name or ''))
  end

  function self.destroy()
    if doc then doc:destroy() end
    doc = nil
  end

  function self.isBuilt() return doc ~= nil end
  --- The left edge (text like '260px') of each cardinal point in the order N E S W, false for one off the strip.
  function self.cardinalPositions()
    local out = {}
    for i = 1, #CARDINALS do out[i] = cardinal[i].shown:get() and cardinal[i].left:get() or false end
    return out
  end

  return self
end

return M
