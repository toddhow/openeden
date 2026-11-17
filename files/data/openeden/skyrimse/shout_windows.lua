-- The Skyrim SE shout menu window (task 0266), written with `openeden.ui` (decision 0015).
--
-- The window logic only; it talks to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui       the `openeden.ui` library
--   env.shouts   the `core.skyrimse.shouts` package (Read functions; the pack owns the state, this only draws it)
--   env.unlock(id)  asks to spend a dragon soul on the next word of the shout with this editor id (a global script runs
--                   the Mutate call)
--   env.equip(id)   asks to make that shout the one the shout key uses
--   env.close()     leaves the interface mode (the host script's job)
-- One row for each shout the player has learned a word of: its name (a star marks the equipped one), its three words
-- (`Fus (Force)` unlocked, `[Ro (Balance)]` learned and not unlocked, `-----` not known) and the two buttons. The
-- document is a `nav-capture` document: with the controller focus in it the d-pad, A and B drive it (the focus entry is
-- `[GUI] rmlui focus button` or `focus key`, see components/ui_rml/CLAUDE.md). The window re-reads the pack only when
-- `shouts.revision()` moved.

local M = {}

local NAME_WIDTH = 230
local WORDS_WIDTH = 520
local BUTTON_WIDTH = 90
local LIST_ROW_HEIGHT = 34
local LIST_HEIGHT = 8 * LIST_ROW_HEIGHT
local WINDOW_WIDTH = NAME_WIDTH + WORDS_WIDTH + 2 * BUTTON_WIDTH + 60

--- One word as the row shows it. Pure.
function M.wordText(name, translation, state)
  if state == 'unknown' or name == '' then return '-----' end
  local text = translation ~= '' and (name .. ' (' .. translation .. ')') or name
  if state == 'learned' then return '[' .. text .. ']' end
  return text
end

function M.create(env)
  local ui, shouts = env.ui, env.shouts
  local self = {}

  local rows = ui.Value({}) -- the shouts in order, each { id = ... }; a row stays while its shout is listed
  local byId = ui.Value({}) -- id -> what the row shows now (the row nodes read it, so a change never rebuilds a row
  -- and the controller focus stays on the button that was pressed)
  local soulsText = ui.Value('')
  local equippedText = ui.Value('')
  local emptyText = ui.Value('')
  local doc = nil
  local isOpen = false
  local lastRevision = nil

  local function refresh()
    lastRevision = shouts.revision()
    soulsText:set(string.format('Dragon souls: %d', shouts.souls()))
    local list, current, equippedName = {}, {}, nil
    for i = 0, shouts.menu_count() - 1 do
      local r = {
        id = shouts.menu_id(i),
        name = shouts.menu_name(i),
        equipped = shouts.menu_equipped(i),
        canEquip = shouts.menu_can_equip(i),
        canUnlock = shouts.menu_can_unlock(i),
      }
      local words = {}
      for w = 0, shouts.menu_word_count(i) - 1 do
        words[#words + 1] = M.wordText(shouts.menu_word_name(i, w), shouts.menu_word_translation(i, w),
          shouts.menu_word_state(i, w))
      end
      r.words = table.concat(words, '    ')
      r.label = (r.equipped and '* ' or '  ') .. r.name
      if r.equipped then equippedName = r.name end
      list[#list + 1] = r
      current[r.id] = r
    end
    byId:set(current)
    rows:set(list)
    equippedText:set('Equipped: ' .. (equippedName or 'none'))
    emptyText:set(#list == 0 and 'You have learned no words of power.' or '')
  end

  -- one cell of a row: a fixed-width box, so a hidden button leaves its place empty
  local function cell(button)
    return ui.Row{ style = { width = BUTTON_WIDTH }, button }
  end

  local function field(id, name)
    return ui.Computed(function()
      local r = byId:get()[id]
      return r and r[name] or ''
    end)
  end

  local function flag(id, name)
    return ui.Computed(function()
      local r = byId:get()[id]
      return r ~= nil and r[name] == true
    end)
  end

  local function build()
    return ui.Document{ name = 'skyrim-shouts', class = 'panel', navCapture = true,
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Shouts', class = 'title' },
          ui.Row{
            ui.Text{ text = soulsText, class = 'info', style = { width = 220 } },
            ui.Text{ text = equippedText, class = 'info' },
          },
          ui.Text{ text = emptyText, class = 'info' },
          ui.List{
            items = rows,
            key = function(r) return r.id end,
            rowHeight = LIST_ROW_HEIGHT,
            height = LIST_HEIGHT,
            width = WINDOW_WIDTH - 20,
            render = function(r)
              local id = r.id
              return ui.Row{
                ui.Text{ text = field(id, 'label'), class = 'item-name', style = { width = NAME_WIDTH } },
                ui.Text{ text = field(id, 'words'), class = 'item-detail', style = { width = WORDS_WIDTH } },
                cell(ui.Button{ text = 'Unlock', class = 'btn', style = { width = BUTTON_WIDTH },
                  visible = flag(id, 'canUnlock'), onClick = function() env.unlock(id) end }),
                cell(ui.Button{ text = 'Equip', class = 'btn', style = { width = BUTTON_WIDTH },
                  visible = flag(id, 'canEquip'), onClick = function() env.equip(id) end }),
              }
            end,
          },
          ui.Row{
            ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
          },
        }
      end }
  end

  --- Opens the window (a second call rebuilds it).
  function self.open()
    self.dismiss()
    isOpen = true
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

  --- One call a frame: re-reads the pack when its revision moved.
  function self.update()
    if not isOpen then return end
    if shouts.revision() ~= lastRevision then refresh() end
  end

  return self
end

return M
