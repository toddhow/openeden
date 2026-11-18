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
--
-- Task 0281: the powers section. With `env.chr` (the `core.skyrimse.character` package), `env.now()` (the game time in
-- seconds) and `env.choosePower(name)` (asks a global script to make the power with that editor id the one the P key
-- uses; "" clears) the window also lists every power the player has: name, source (race, standing stone, vampirism,
-- lycanthropy), the cooldown (once a game day) and an Equip or Unequip button. The section re-reads the pack when
-- `chr.revision()` moves and once every game minute (the cooldown text).

local M = {}

local NAME_WIDTH = 230
local WORDS_WIDTH = 520
local BUTTON_WIDTH = 90
local LIST_ROW_HEIGHT = 34
local LIST_HEIGHT = 8 * LIST_ROW_HEIGHT
local SHORT_LIST_HEIGHT = 5 * LIST_ROW_HEIGHT -- with the powers section below it
local POWER_LIST_HEIGHT = 4 * LIST_ROW_HEIGHT
local POWER_NAME_WIDTH = 330
local POWER_SOURCE_WIDTH = 150
local POWER_STATUS_WIDTH = 270
local WINDOW_WIDTH = NAME_WIDTH + WORDS_WIDTH + 2 * BUTTON_WIDTH + 60

--- One word as the row shows it. Pure.
function M.wordText(name, translation, state)
  if state == 'unknown' or name == '' then return '-----' end
  local text = translation ~= '' and (name .. ' (' .. translation .. ')') or name
  if state == 'learned' then return '[' .. text .. ']' end
  return text
end

--- The game seconds until a power is ready, as text. Pure.
function M.cooldownText(seconds)
  if seconds == nil or seconds <= 0 then return 'Ready' end
  if seconds < 60 then return 'Ready in under a minute' end
  local hours = math.floor(seconds / 3600)
  local minutes = math.floor((seconds % 3600) / 60)
  if hours > 0 then return string.format('Ready in %dh %dm', hours, minutes) end
  return string.format('Ready in %dm', minutes)
end

--- A power's editor id as words ("PowerNordBattleCry" -> "Power Nord Battle Cry"). Pure.
function M.powerTitle(name)
  if name == nil or name == '' then return '' end
  local text = name:gsub('(%l)(%u)', '%1 %2'):gsub('(%u)(%u%l)', '%1 %2')
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
  local chr = env.chr
  local powerRows = ui.Value({}) -- the powers in order, each { id = editor id }
  local powerById = ui.Value({})
  local chosenText = ui.Value('')
  local noPowersText = ui.Value('')
  local lastChrRevision, lastMinute = nil, nil

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

  local function refreshPowers()
    if not chr then return end
    local now = env.now and env.now() or 0
    lastChrRevision = chr.revision()
    lastMinute = math.floor(now / 60)
    local chosen = chr.power_chosen()
    local list, current, chosenName = {}, {}, nil
    for i = 0, chr.power_count() - 1 do
      local name = chr.power_name(i)
      local usable = chr.power_support(i) ~= 'none'
      local isChosen = i == chosen
      local r = {
        id = name,
        label = (isChosen and '* ' or '  ') .. M.powerTitle(name),
        source = chr.power_source(i),
        status = usable and M.cooldownText(chr.power_cooldown(i, now)) or 'Cannot be used',
        canEquip = usable and not isChosen,
        canUnequip = isChosen,
      }
      if isChosen then chosenName = M.powerTitle(name) end
      list[#list + 1] = r
      current[r.id] = r
    end
    powerById:set(current)
    powerRows:set(list)
    chosenText:set(chosenName and ('Equipped power: ' .. chosenName)
      or 'Equipped power: none (the P key uses the first ready power)')
    noPowersText:set(#list == 0 and 'You have no powers.' or '')
  end

  local function powerField(id, name)
    return ui.Computed(function()
      local r = powerById:get()[id]
      return r and r[name] or ''
    end)
  end

  local function powerFlag(id, name)
    return ui.Computed(function()
      local r = powerById:get()[id]
      return r ~= nil and r[name] == true
    end)
  end

  local function powersSection()
    if not chr then return {} end
    return {
      ui.Text{ text = 'Powers', class = 'title' },
      ui.Text{ text = chosenText, class = 'info' },
      ui.Text{ text = noPowersText, class = 'info' },
      ui.List{
        items = powerRows,
        key = function(r) return r.id end,
        rowHeight = LIST_ROW_HEIGHT,
        height = POWER_LIST_HEIGHT,
        width = WINDOW_WIDTH - 20,
        render = function(r)
          local id = r.id
          return ui.Row{
            ui.Text{ text = powerField(id, 'label'), class = 'item-name', style = { width = POWER_NAME_WIDTH } },
            ui.Text{ text = powerField(id, 'source'), class = 'item-detail', style = { width = POWER_SOURCE_WIDTH } },
            ui.Text{ text = powerField(id, 'status'), class = 'item-detail', style = { width = POWER_STATUS_WIDTH } },
            ui.Row{ style = { width = 2 * BUTTON_WIDTH },
              ui.Button{ text = 'Equip', class = 'btn', style = { width = BUTTON_WIDTH },
                visible = powerFlag(id, 'canEquip'), onClick = function() env.choosePower(id) end },
              ui.Button{ text = 'Unequip', class = 'btn', style = { width = BUTTON_WIDTH },
                visible = powerFlag(id, 'canUnequip'), onClick = function() env.choosePower('') end },
            },
          }
        end,
      },
    }
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
        local children = {
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
            height = chr and SHORT_LIST_HEIGHT or LIST_HEIGHT,
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
        }
        for _, node in ipairs(powersSection()) do children[#children + 1] = node end
        children[#children + 1] = ui.Row{
          ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
        }
        return ui.Column(children)
      end }
  end

  --- Opens the window (a second call rebuilds it).
  function self.open()
    self.dismiss()
    isOpen = true
    refresh()
    refreshPowers()
    doc = build()
  end

  --- Takes the window down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    lastRevision = nil
    lastChrRevision, lastMinute = nil, nil
  end

  function self.isOpen() return isOpen end

  --- One call a frame: re-reads the pack when its revision moved.
  function self.update()
    if not isOpen then return end
    if shouts.revision() ~= lastRevision then refresh() end
    if chr then
      local minute = math.floor((env.now and env.now() or 0) / 60)
      if chr.revision() ~= lastChrRevision or minute ~= lastMinute then refreshPowers() end
    end
  end

  return self
end

return M
