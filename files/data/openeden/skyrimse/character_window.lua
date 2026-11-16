-- The Skyrim SE character creation window (task 0225), written with `openeden.ui` (decision 0015).
--
-- The window logic only; it talks to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui     the `openeden.ui` library
--   env.chr    the `core.skyrimse.character` package (Read functions; the pack owns the choice, this only draws it)
--   env.send(t) asks for a change: { kind = 'race', index = n }, { kind = 'female', value = bool },
--               { kind = 'part', part = 'hair'|'beard'|'eyes', delta = n }, { kind = 'name', text = s },
--               { kind = 'finish' } (a global script runs the pack's Mutate call, in the order sent)
--   env.close() leaves the interface mode (the host script's job)
-- The window re-reads the pack only when `chr.revision()` moved. It cannot be cancelled: a character is needed.

local M = {}

local PANEL_WIDTH = 440
local ROW_LABEL_WIDTH = 90
local ROW_VALUE_WIDTH = 230
local PARTS = { 'hair', 'beard', 'eyes' }
local PART_LABEL = { hair = 'Hair', beard = 'Beard', eyes = 'Eyes' }

function M.create(env)
  local ui, chr = env.ui, env.chr
  local self = {}

  local raceName = ui.Value('')
  local raceDesc = ui.Value('')
  local raceSkills = ui.Value('')
  local raceAbilities = ui.Value('')
  local maleText = ui.Value('')
  local femaleText = ui.Value('')
  local partText = {}
  for _, p in ipairs(PARTS) do partText[p] = ui.Value('') end
  local nameValue = ui.Value('')

  local doc = nil
  local isOpen = false
  local finished = false
  local lastRevision = nil
  local typedName = ''
  local userEdited = false

  local function refresh()
    lastRevision = chr.revision()
    local count = chr.race_count()
    local index = chr.race_index()
    if count > 0 then
      raceName:set(chr.race_name(index))
      raceDesc:set(chr.race_desc(index))
      local skills = chr.race_skills(index)
      raceSkills:set(skills ~= '' and ('Skills: ' .. skills) or '')
      local n = chr.race_abilities(index)
      raceAbilities:set(n > 0 and string.format('Abilities: %d', n) or '')
    end
    local female = chr.female()
    maleText:set(female and '  Male' or '> Male')
    femaleText:set(female and '> Female' or '  Female')
    for _, p in ipairs(PARTS) do
      local n = chr.part_count(p)
      partText[p]:set(string.format('%s  (%d of %d)', chr.part_name(p), chr.part_index(p) + 1, n))
    end
    if not userEdited then -- the field is the player's once he types in it (the pack trims what it keeps)
      local name = chr.name()
      typedName = name
      nameValue:set(name)
    end
  end

  local function stepRace(delta)
    local count = chr.race_count()
    if count <= 0 then return end
    env.send{ kind = 'race', index = (chr.race_index() + delta) % count }
  end

  local function partRow(p)
    return ui.Row{
      ui.Text{ text = PART_LABEL[p], class = 'info', style = { width = ROW_LABEL_WIDTH } },
      ui.Button{ text = '<', class = 'btn chr-' .. p .. '-prev',
        onClick = function() env.send{ kind = 'part', part = p, delta = -1 } end },
      ui.Text{ text = partText[p], class = 'info chr-' .. p .. '-name', style = { width = ROW_VALUE_WIDTH } },
      ui.Button{ text = '>', class = 'btn chr-' .. p .. '-next',
        onClick = function() env.send{ kind = 'part', part = p, delta = 1 } end },
    }
  end

  local function build()
    return ui.Document{ name = 'skyrim-character', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = PANEL_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Create your character', class = 'title' },
          ui.Row{
            ui.Text{ text = 'Race', class = 'info', style = { width = ROW_LABEL_WIDTH } },
            ui.Button{ text = '<', class = 'btn chr-race-prev', onClick = function() stepRace(-1) end },
            ui.Text{ text = raceName, class = 'info chr-race-name', style = { width = ROW_VALUE_WIDTH } },
            ui.Button{ text = '>', class = 'btn chr-race-next', onClick = function() stepRace(1) end },
          },
          ui.Text{ text = raceDesc, class = 'item-detail', style = { width = PANEL_WIDTH - 24 } },
          ui.Text{ text = raceSkills, class = 'item-detail' },
          ui.Text{ text = raceAbilities, class = 'item-detail' },
          ui.Row{
            ui.Text{ text = 'Sex', class = 'info', style = { width = ROW_LABEL_WIDTH } },
            ui.Button{ text = maleText, class = 'btn chr-male',
              onClick = function() env.send{ kind = 'female', value = false } end },
            ui.Button{ text = femaleText, class = 'btn chr-female',
              onClick = function() env.send{ kind = 'female', value = true } end },
          },
          partRow('hair'),
          partRow('beard'),
          partRow('eyes'),
          ui.Row{
            ui.Text{ text = 'Name', class = 'info', style = { width = ROW_LABEL_WIDTH } },
            ui.TextInput{ value = nameValue, class = 'chr-name',
              style = { width = ROW_VALUE_WIDTH },
              onChange = function(text)
                typedName = text
                userEdited = true
                env.send{ kind = 'name', text = text }
              end },
          },
          ui.Row{
            ui.Button{ text = 'Done', class = 'btn chr-done', onClick = function() self.done() end },
          },
        }
      end }
  end

  --- Done: the name goes first (the pack sanitises it), then the end of the creation; the host leaves the mode.
  function self.done()
    if finished then return end
    finished = true
    env.send{ kind = 'name', text = typedName }
    env.send{ kind = 'finish' }
    env.close()
  end

  --- Opens the window (a second call rebuilds it).
  function self.open()
    self.dismiss()
    isOpen = true
    finished = false
    typedName = ''
    userEdited = false
    refresh()
    doc = build()
  end

  --- Takes the window down. Safe when nothing is open.
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
    if chr.revision() ~= lastRevision then refresh() end
  end

  return self
end

return M
