-- The Skyrim SE world map window (task 0201, milestone M-S7), written with `openeden.ui` (decision 0015).
--
-- The land is drawn from the pack's own raster of the world (one pixel per exterior cell: the mean vertex colour lit by
-- the cell's slope, water where the cell's surface is above its land), as rows of coloured blocks; the pack hands it out a
-- row at a time and the window asks for one row a frame until it is complete. On it: the discovered map markers (the ones
-- the player has not found are not drawn), the quest targets and the player. A map has no scroll: it is scaled to fit.
--
-- This module holds the whole window logic and talks only to what it is handed, so it runs over the headless UI backend in
-- tests:
--   env.ui        the `openeden.ui` library
--   env.map       the `core.skyrimse.map` package
--   env.player()  { x, y, yaw, world } of the player, or nil
--   env.close()   leaves the interface mode (the host script's job)
-- Fast travel (task 0218), only when both are given:
--   env.travel          the `core.skyrimse.travel` package (marker_ref)
--   env.requestTravel   function(markerRef, name): asks the global script to travel (it may refuse; the host calls
--                       `notice` with the reason)
local M = {}

local CELL_UNITS = 4096 -- the side of a Skyrim SE exterior cell (WorldSpaceInfo::cellSizeUnits)
local MAX_WIDTH, MAX_HEIGHT = 780, 500
local MARKER_SLOTS = 512 -- two pages of the pack's answer
local TARGET_SLOTS = 16
local MARKER_PAGES = 2

local function packed(rgb)
  return string.format('rgb(%d, %d, %d)', math.floor(rgb / 65536) % 256, math.floor(rgb / 256) % 256, rgb % 256)
end

--- Rows of equal pixels as runs: { { x = first column (0 based), n = length, rgb = packed colour } ... }. A pixel of -1
--- (no land) ends a run and is not drawn. Pure.
function M.runs(pixels)
  local out = {}
  local i = 1
  while i <= #pixels do
    local v = pixels[i]
    if v < 0 then
      i = i + 1
    else
      local j = i
      while j < #pixels and pixels[j + 1] == v do j = j + 1 end
      out[#out + 1] = { x = i - 1, n = j - i + 1, rgb = v }
      i = j + 1
    end
  end
  return out
end

local REFUSALS = {
  combat = 'You cannot fast travel while in combat.',
  enemies = 'You cannot fast travel with enemies nearby.',
  interior = 'You cannot fast travel from an interior.',
  undiscovered = 'You cannot fast travel to a place you have not discovered.',
  unknown = 'You cannot fast travel to that place.',
  no_destination = 'There is nowhere to fast travel to there.',
  not_ready = 'You cannot fast travel yet.',
}

--- The sentence for a refusal word of the pack's `refusal` function. Pure.
function M.refusalText(reason)
  return REFUSALS[reason] or 'You cannot fast travel now.'
end

--- The pixel of a world position on the map: column and row (both fractions of a cell), north up. Pure.
function M.toMap(x, y, minX, maxY)
  return x / CELL_UNITS - minX, (maxY + 1) - y / CELL_UNITS
end

function M.create(env)
  local ui, map = env.ui, env.map
  local self = {}

  local isOpen = false
  local doc = nil
  local info = nil -- { minX, minY, width, height, scale, world }
  local rows = {}
  local nextRow = 0
  local rowsValue = ui.Value({})
  local status = ui.Value('')
  local title = ui.Value('Map')
  local markerSlots = {}
  for i = 1, MARKER_SLOTS do
    markerSlots[i] = { left = ui.Value('0px'), top = ui.Value('0px'), shown = ui.Value(false), name = ui.Value('') }
  end
  local targetSlots = {}
  for i = 1, TARGET_SLOTS do
    targetSlots[i] = { left = ui.Value('0px'), top = ui.Value('0px'), shown = ui.Value(false) }
  end
  local playerSlot = { left = ui.Value('0px'), top = ui.Value('0px'), shown = ui.Value(false) }
  local lastRevision, lastPlayerPos = nil, nil
  local shownMarkers = 0
  local travel = env.travel
  local markerRefs, markerNames = {}, {}
  local pending, promptDoc = nil, nil

  local function loadInfo(world)
    local v = map.raster_info(world)
    if #v < 4 or v[3] <= 0 or v[4] <= 0 then return nil end
    local scale = math.max(1, math.floor(math.min(MAX_WIDTH / v[3], MAX_HEIGHT / v[4])))
    return { minX = v[1], minY = v[2], width = v[3], height = v[4], scale = scale, world = world }
  end

  local function mapPixel(x, y)
    local cx, cy = M.toMap(x, y, info.minX, info.minY + info.height - 1)
    return math.floor(cx * info.scale + 0.5), math.floor(cy * info.scale + 0.5)
  end

  local function placeSlot(slot, x, y, offset)
    slot.left:set(string.format('%dpx', x - (offset or 0)))
    slot.top:set(string.format('%dpx', y - (offset or 0)))
    slot.shown:set(true)
  end

  local function refreshMarkers(p)
    lastRevision = map.revision()
    local used = 0
    for page = 0, MARKER_PAGES - 1 do
      local xs = map.marker_x(p.world, p.x, p.y, 1e9, 0, page)
      local ys = map.marker_y(p.world, p.x, p.y, 1e9, 0, page)
      local names = map.marker_name(p.world, p.x, p.y, 1e9, 0, page)
      local refs = travel and travel.marker_ref(p.world, p.x, p.y, 1e9, 0, page) or {}
      for i = 1, #xs do
        if used < MARKER_SLOTS then
          used = used + 1
          local px, py = mapPixel(xs[i], ys[i])
          placeSlot(markerSlots[used], px, py, 4)
          markerSlots[used].name:set(names[i] or '')
          markerRefs[used], markerNames[used] = refs[i], names[i] or ''
        end
      end
      if #xs < 256 then break end
    end
    for i = used + 1, MARKER_SLOTS do markerSlots[i].shown:set(false) end
    shownMarkers = used
    local txs, tys, tws = map.target_x(), map.target_y(), map.target_world()
    local tused = 0
    for i = 1, #txs do
      if tws[i] == p.world and tused < TARGET_SLOTS then
        tused = tused + 1
        local px, py = mapPixel(txs[i], tys[i])
        placeSlot(targetSlots[tused], px, py, 4)
      end
    end
    for i = tused + 1, TARGET_SLOTS do targetSlots[i].shown:set(false) end
  end

  local function refreshPlayer(p)
    lastPlayerPos = { p.x, p.y }
    local px, py = mapPixel(p.x, p.y)
    placeSlot(playerSlot, px, py, 4)
  end

  local function stepRaster(p)
    if not info or nextRow >= info.height then return end
    local pixels = map.raster_row(p.world, nextRow)
    nextRow = nextRow + 1
    -- the row index counts from the south; the display has north up
    rows[#rows + 1] = { y = info.height - nextRow, runs = M.runs(pixels) }
    if nextRow % 4 == 0 or nextRow >= info.height then
      local sorted = {}
      for i = #rows, 1, -1 do sorted[#sorted + 1] = rows[i] end -- northmost first
      rowsValue:set(sorted)
    end
    status:set(nextRow >= info.height and '' or string.format('Reading the land: %d of %d', nextRow, info.height))
  end

  local function build()
    local width, height = info.width * info.scale, info.height * info.scale
    local scale = info.scale
    local function makeOverlay()
    local overlay = { style = { position = 'absolute', left = 0, top = 0, width = width, height = height } }
    for i = 1, MARKER_SLOTS do
      overlay[#overlay + 1] = ui.Frame{
        class = 'map-marker', visible = markerSlots[i].shown,
        onClick = travel and function() self.ask(i) end or nil,
        style = { position = 'absolute', left = markerSlots[i].left, top = markerSlots[i].top, width = 8, height = 8,
          ['background-color'] = 'rgb(230, 110, 0)' } }
    end
    for i = 1, TARGET_SLOTS do
      overlay[#overlay + 1] = ui.Frame{
        class = 'map-target', visible = targetSlots[i].shown,
        style = { position = 'absolute', left = targetSlots[i].left, top = targetSlots[i].top, width = 8, height = 8,
          ['background-color'] = 'rgb(120, 240, 90)' } }
    end
    overlay[#overlay + 1] = ui.Frame{
      class = 'map-player', visible = playerSlot.shown,
      style = { position = 'absolute', left = playerSlot.left, top = playerSlot.top, width = 8, height = 8,
        ['background-color'] = 'rgb(255, 60, 60)' } }
    return overlay
    end
    return ui.Document{ name = 'skyrim-worldmap', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = width + 24 },
      build = function()
        return ui.Column{
          ui.Text{ text = title, class = 'title' },
          ui.Text{ text = status, class = 'info' },
          ui.Stack{
            style = { width = width, height = height, ['background-color'] = 'rgb(18, 24, 34)' },
            ui.List{
              items = rowsValue,
              key = function(r) return r.y end,
              rowHeight = scale,
              height = height,
              width = width,
              render = function(r)
                local row = { style = { width = width, height = scale } }
                for _, run in ipairs(r.runs) do
                  row[#row + 1] = ui.Frame{ style = { position = 'absolute', left = run.x * scale, top = 0,
                    width = run.n * scale, height = scale, ['background-color'] = packed(run.rgb) } }
                end
                return ui.Stack(row)
              end,
            },
            ui.Stack(makeOverlay()),
          },
          ui.Row{ ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end } },
        }
      end }
  end

  local function dropPrompt()
    if promptDoc then promptDoc:destroy() end
    promptDoc, pending = nil, nil
  end

  --- The player clicked marker slot i: ask before travelling (only for a marker the player has found).
  function self.ask(i)
    local ref, name = markerRefs[i], markerNames[i]
    if not travel or not isOpen or not ref or not name or name == '' then return end
    dropPrompt()
    pending = { ref = ref, name = name }
    promptDoc = ui.Document{ name = 'skyrim-travel-prompt', class = 'panel',
      style = { position = 'absolute', left = 260, top = 200, width = 300 },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Travel to ' .. name .. '?', class = 'title' },
          ui.Row{
            ui.Button{ text = 'Yes', class = 'btn', onClick = function() self.confirm() end },
            ui.Button{ text = 'No', class = 'btn', onClick = function() self.cancel() end },
          },
        }
      end }
  end

  function self.confirm()
    local p = pending
    dropPrompt()
    if p and env.requestTravel then
      status:set('Travelling to ' .. p.name .. '...')
      env.requestTravel(p.ref, p.name)
    end
  end

  function self.cancel() dropPrompt() end
  function self.asking() return pending ~= nil end
  --- A line in the status of the window (a refusal, or the result of a travel).
  function self.notice(text) status:set(text or '') end

  --- Opens the map. False (and nothing drawn) when there is no player in a worldspace or the pack has no land for it.
  function self.open()
    self.dismiss()
    local p = env.player()
    if not p or not p.world or not map.ready() then return false end
    info = loadInfo(p.world)
    if not info then return false end
    rows, nextRow = {}, 0
    rowsValue:set({})
    status:set('Reading the land')
    lastRevision, lastPlayerPos = nil, nil
    isOpen = true
    doc = build()
    refreshMarkers(p)
    refreshPlayer(p)
    return true
  end

  function self.dismiss()
    dropPrompt()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
  end

  function self.isOpen() return isOpen end
  function self.rowsRead() return nextRow end
  function self.markersShown() return shownMarkers end
  function self.scale() return info and info.scale or 0 end

  --- One call a frame while the map is open: one more raster row, the player marker, the markers when the pack's revision
  --- moved.
  function self.update()
    if not isOpen then return end
    local p = env.player()
    if not p then return end
    stepRaster(p)
    if lastRevision ~= map.revision() then refreshMarkers(p) end
    if not lastPlayerPos or lastPlayerPos[1] ~= p.x or lastPlayerPos[2] ~= p.y then refreshPlayer(p) end
  end

  return self
end

return M
