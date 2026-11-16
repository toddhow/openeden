-- The Skyrim SE quest journal window (task 0185, milestone M-S6), written with `openeden.ui` (decision 0015).
--
-- This module holds the whole window logic and talks only to what it is handed, so it runs over the headless UI backend
-- in tests:
--   env.ui      the `openeden.ui` library
--   env.jr      the `core.skyrimse.journal` package (Read functions: the pack owns the state, this only draws it)
--   env.close() leaves the interface mode (the host script's job)
-- Ids are text (8 hex digits). The window re-reads the pack only when `jr.revision()` moved.

local M = {}

local WINDOW_WIDTH = 780
local LIST_WIDTH = 270
local DETAIL_MARGIN = 10
local DETAIL_WIDTH = WINDOW_WIDTH - LIST_WIDTH - 48 - DETAIL_MARGIN
local LIST_HEIGHT = 420
local ROW_HEIGHT = 26
local DETAIL_ROW_HEIGHT = 22
local DETAIL_HEIGHT = 330
-- The bitmap font draws only the first line of a text element that wraps, so the window breaks lines itself: about 7.6
-- dp a character on the 462 dp detail column, with a margin for wide letters.
local LINE_CHARS = 52

local function readQuests(jr, completed)
  local rows = {}
  for i, id in ipairs(jr.quests(completed)) do
    rows[i] = { id = id, name = jr.name(id) }
  end
  return rows
end

--- Greedy line breaking on spaces (a word longer than the limit stays whole).
local function wrap(text, limit)
  local lines, line = {}, ''
  for word in text:gmatch('%S+') do
    if line == '' then
      line = word
    elseif #line + 1 + #word <= limit then
      line = line .. ' ' .. word
    else
      lines[#lines + 1] = line
      line = word
    end
  end
  if line ~= '' then lines[#lines + 1] = line end
  return lines
end

-- One list holds everything the detail pane shows: the current entry, the objectives, the earlier entries.
local function detailRows(jr, id)
  local rows = {}
  local function add(text, class)
    rows[#rows + 1] = { key = #rows .. ':' .. text, text = text, class = class }
  end
  local function addWrapped(text, class, indent)
    for i, line in ipairs(wrap(text, LINE_CHARS - #indent)) do
      add((i == 1 and '' or indent) .. line, class)
    end
  end
  local entry = jr.entry(id)
  if entry ~= '' then addWrapped(entry, 'quest-entry', '') end
  local objectives = jr.objectives(id)
  local done = jr.objective_done(id)
  if #objectives > 0 then
    add('', 'quest-line')
    add('Objectives', 'info')
    for i, text in ipairs(objectives) do
      addWrapped((done[i] and '[x] ' or '[ ] ') .. text, 'quest-line', '    ')
    end
  end
  local log = jr.log(id)
  if #log > 1 then
    add('', 'quest-line')
    add('Earlier', 'info')
    for i = #log - 1, 1, -1 do addWrapped(log[i], 'quest-line', '') end -- newest first
  end
  return rows
end

function M.create(env)
  local ui, jr = env.ui, env.jr
  local self = {}

  local leftRows = ui.Value({})
  local detail = ui.Value({})
  local nameText = ui.Value('')
  local statusText = ui.Value('')
  local runningLabel = ui.Value('Quests')
  local completedLabel = ui.Value('Completed')
  local emptyText = ui.Value('')
  local doc = nil
  local isOpen = false
  local tab = 'running' -- or 'completed'
  local selected = nil
  local lastRevision = nil

  local function refresh()
    lastRevision = jr.revision()
    local running = readQuests(jr, false)
    local completed = readQuests(jr, true)
    runningLabel:set(string.format('Quests (%d)', #running))
    completedLabel:set(string.format('Completed (%d)', #completed))
    local rows = tab == 'running' and running or completed
    local found = false
    for _, r in ipairs(rows) do
      if r.id == selected then found = true end
    end
    if not found then selected = rows[1] and rows[1].id or nil end
    for _, r in ipairs(rows) do
      local mark = r.id == selected
      r.label = (mark and '> ' or '  ') .. r.name
      r.key = r.id .. (mark and ':s' or ':n')
    end
    leftRows:set(rows)
    if selected then
      emptyText:set('')
      nameText:set(jr.name(selected))
      statusText:set(jr.status(selected))
      detail:set(detailRows(jr, selected))
    else
      emptyText:set(tab == 'running' and 'No active quests.' or 'No completed quests.')
      nameText:set('')
      statusText:set('')
      detail:set({})
    end
  end

  local function select(id)
    selected = id
    refresh()
  end

  local function setTab(name)
    if tab == name then return end
    tab = name
    selected = nil
    refresh()
  end

  local function build()
    return ui.Document{ name = 'skyrim-journal', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Journal', class = 'title' },
          ui.Row{
            ui.Button{ text = runningLabel, class = 'btn', onClick = function() setTab('running') end },
            ui.Button{ text = completedLabel, class = 'btn', onClick = function() setTab('completed') end },
          },
          ui.Row{
            ui.List{
              items = leftRows,
              key = function(r) return r.key end,
              rowHeight = ROW_HEIGHT,
              height = LIST_HEIGHT,
              width = LIST_WIDTH,
              render = function(r)
                return ui.Button{ text = r.label, class = 'quest-name', style = { width = LIST_WIDTH - 12 },
                  onClick = function() select(r.id) end }
              end,
            },
            ui.Column{
              style = { width = DETAIL_WIDTH, ['margin-left'] = DETAIL_MARGIN },
              ui.Text{ text = emptyText, class = 'info' },
              ui.Text{ text = nameText, class = 'title' },
              ui.Text{ text = statusText, class = 'info' },
              ui.List{
                items = detail,
                key = function(r) return r.key end,
                rowHeight = DETAIL_ROW_HEIGHT,
                height = DETAIL_HEIGHT,
                width = DETAIL_WIDTH,
                render = function(r)
                  return ui.Text{ text = r.text, class = r.class, style = { width = DETAIL_WIDTH - 12 } }
                end,
              },
            },
          },
          ui.Row{
            ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
          },
        }
      end }
  end

  --- Opens the journal (a second call rebuilds it).
  function self.open()
    self.dismiss()
    isOpen = true
    tab = 'running'
    selected = nil
    refresh()
    doc = build()
  end

  --- Takes the window down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    lastRevision = nil
  end

  function self.isOpen() return isOpen end
  function self.selected() return selected end
  function self.tab() return tab end

  --- One call a frame: re-reads the pack when its revision moved. A frame where nothing changed makes one cheap read and
  --- no UI call.
  function self.update()
    if not isOpen then return end
    if jr.revision() ~= lastRevision then refresh() end
  end

  return self
end

return M
