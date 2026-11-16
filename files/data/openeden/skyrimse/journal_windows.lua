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
local DETAIL_HEIGHT = 330
-- The detail pane is a fixed set of text slots in a scroll view: text elements wrap on their own (task 0188), so a row
-- can be any number of lines tall, which a fixed-height list row cannot. A quest with more rows than slots shows the
-- first ones (the log is newest first).
local DETAIL_SLOTS = 40

local function readQuests(jr, completed)
  local rows = {}
  for i, id in ipairs(jr.quests(completed)) do
    rows[i] = { id = id, name = jr.name(id) }
  end
  return rows
end

-- One list of rows holds everything the detail pane shows: the current entry, the objectives, the earlier entries.
local function detailRows(jr, id)
  local rows = {}
  local function add(text, class)
    rows[#rows + 1] = { text = text, class = class }
  end
  -- A blank row is a space: an empty element has no height and a space is kept by `white-space: pre-wrap`.
  local function addGap() add(' ', 'quest-line') end
  local entry = jr.entry(id)
  if entry ~= '' then add(entry, 'quest-entry') end
  local objectives = jr.objectives(id)
  local done = jr.objective_done(id)
  if #objectives > 0 then
    addGap()
    add('Objectives', 'info')
    for i, text in ipairs(objectives) do
      add((done[i] and '[x] ' or '[ ] ') .. text, 'quest-line')
    end
  end
  local log = jr.log(id)
  if #log > 1 then
    addGap()
    add('Earlier', 'info')
    for i = #log - 1, 1, -1 do add(log[i], 'quest-line') end -- newest first
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

  -- The slots must be made inside the document's build function (they need its reactive scope).
  local function detailSlots()
    local slots = {}
    for i = 1, DETAIL_SLOTS do
      slots[i] = ui.Text{
        text = ui.Computed(function() local r = detail:get()[i]; return r and r.text or '' end),
        class = ui.Computed(function() local r = detail:get()[i]; return r and r.class or '' end),
        visible = ui.Computed(function() return detail:get()[i] ~= nil end),
        style = { width = DETAIL_WIDTH - 12, ['white-space'] = 'pre-wrap' },
      }
    end
    return slots
  end

  local function detailScroll()
    local props = { height = DETAIL_HEIGHT, width = DETAIL_WIDTH }
    for i, slot in ipairs(detailSlots()) do props[i] = slot end
    return props
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
              ui.ScrollView(detailScroll()),
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
