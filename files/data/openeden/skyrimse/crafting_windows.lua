-- The Skyrim SE crafting windows (task 0199, milestone M-S7), written with `openeden.ui` (decision 0015).
--
-- One window for the bench the player activated: an alchemy lab (ingredients, up to three chosen, Brew and Eat), an arcane
-- enchanter (gear, learned enchantments, filled soul gems, Enchant and Disenchant) or any other bench (its recipe list:
-- forge, skyforge, grindstone, armour table, smelter, cook pot, tanning rack; Make). The module holds the whole window
-- logic and talks only to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui        the `openeden.ui` library
--   env.inv       the `core.skyrimse.inventory` package (names, counts, the revision)
--   env.crafting  the `core.skyrimse.crafting` package (state, bench, lists, last result)
--   env.brew(a, b, c), env.eat(item), env.craft(recipe), env.temper(recipe), env.disenchant(item),
--   env.enchant(item, enchantment, gem)   ask the pack to do it (a global script runs the Mutate call)
--   env.close()   ends the session and leaves the interface mode (the host script's job)
-- Ids are text (8 hex digits). Rows carry their numbers in their key, so a changed row is a new row.

local M = {}

local ROW_HEIGHT = 22
local LIST_HEIGHT = 300
local SMALL_LIST_HEIGHT = 100 -- the three lists of the enchanting window share the screen
local WINDOW_WIDTH = 520
local RECIPES_WIDTH = 760 -- a recipe's materials are long text: the recipe window is wider
local NAME_WIDTH = 230
local DETAIL_WIDTH = 230
local SMALL_WIDTH = 60

function M.create(env)
  local ui, inv, crafting = env.ui, env.inv, env.crafting
  local self = {}

  local title = ui.Value('')
  local status = ui.Value('')
  local info = ui.Value('')
  local picked = ui.Value('')
  local rowsA = ui.Value({})
  local rowsB = ui.Value({})
  local rowsC = ui.Value({})
  local doc
  local winWidth, detailWidth = WINDOW_WIDTH, DETAIL_WIDTH
  local isOpen = false
  local mode = 'none'
  local last = {}
  -- the selections (ids): ingredients (at most three), then the enchanting choices
  local chosen = {}
  local pickItem, pickEnchant, pickGem

  local function benchMode()
    local bench = crafting.bench()
    if bench:find('alchemy', 1, true) then return 'alchemy' end
    if bench:find('enchanting', 1, true) then return 'enchanting' end
    return 'recipes'
  end

  local function isChosen(id)
    for _, c in ipairs(chosen) do if c == id then return true end end
    return false
  end

  local function nameOrUnknown(id)
    if id == nil or id == '' then return '-' end
    return inv.name(id)
  end

  local function preview()
    local a, b, c = chosen[1] or '', chosen[2] or '', chosen[3] or ''
    if #chosen < 2 then
      info:set('Choose two or three ingredients')
    else
      info:set('Potion: ' .. crafting.preview(a, b, c))
    end
    local names = {}
    for _, c2 in ipairs(chosen) do names[#names + 1] = inv.name(c2) end
    picked:set(#names > 0 and ('Chosen: ' .. table.concat(names, ', ')) or 'Chosen: -')
  end

  local function refresh()
    last = { inv.revision(), crafting.results(), crafting.revision(), crafting.state() }
    mode = benchMode()
    status:set(crafting.state() == 'refused' and crafting.reason() or crafting.last_result())
    if crafting.state() ~= 'ready' then
      rowsA:set({}); rowsB:set({}); rowsC:set({})
      return
    end
    if mode == 'alchemy' then
      local rows = {}
      -- drop a choice that is no longer held
      local kept = {}
      for _, id in ipairs(chosen) do
        if inv.count('player', id) > 0 then kept[#kept + 1] = id end
      end
      chosen = kept
      for _, id in ipairs(crafting.ingredients()) do
        local effects = crafting.ingredient_effects(id)
        local on = isChosen(id)
        rows[#rows + 1] = {
          id = id, count = inv.count('player', id), effects = effects, on = on,
          key = id .. ':' .. inv.count('player', id) .. ':' .. effects .. ':' .. tostring(on),
          name = inv.name(id),
        }
      end
      rowsA:set(rows)
      preview()
    elseif mode == 'enchanting' then
      local items, enchants, gems = {}, {}, {}
      if pickItem and inv.count('player', pickItem) <= 0 then pickItem = nil end
      if pickGem and inv.count('player', pickGem) <= 0 then pickGem = nil end
      for _, id in ipairs(crafting.gear()) do
        local e = crafting.gear_enchant(id)
        items[#items + 1] = { id = id, name = inv.name(id), detail = e, on = id == pickItem,
          key = id .. ':' .. e .. ':' .. tostring(id == pickItem) }
      end
      for _, id in ipairs(crafting.enchantments()) do
        enchants[#enchants + 1] = { id = id, name = crafting.enchant_name(id), detail = '', on = id == pickEnchant,
          key = id .. ':' .. tostring(id == pickEnchant) }
      end
      for _, id in ipairs(crafting.gems()) do
        gems[#gems + 1] = { id = id, name = inv.name(id), detail = 'soul ' .. crafting.gem_size(id), on = id == pickGem,
          key = id .. ':' .. tostring(id == pickGem) }
      end
      rowsA:set(items); rowsB:set(enchants); rowsC:set(gems)
      picked:set(string.format('Item %s, enchantment %s, soul gem %s', nameOrUnknown(pickItem),
        pickEnchant and crafting.enchant_name(pickEnchant) or '-', nameOrUnknown(pickGem)))
      if pickItem and pickEnchant and pickGem then
        info:set('Result: ' .. crafting.enchant_preview(pickItem, pickEnchant, pickGem))
      else
        info:set('Choose an item, an enchantment and a filled soul gem')
      end
    else
      local rows = {}
      for _, id in ipairs(crafting.recipes()) do
        local okNow = crafting.recipe_ok(id)
        local needs = crafting.recipe_needs(id)
        local temper = crafting.recipe_temper(id)
        rows[#rows + 1] = { id = id, name = (temper and 'Improve ' or '') .. crafting.recipe_name(id), temper = temper,
          needs = needs, ok = okNow, key = id .. ':' .. needs .. ':' .. tostring(okNow) }
      end
      rowsA:set(rows)
      info:set(#rows > 0 and 'Choose a recipe' or 'This bench has nothing you can make')
      picked:set('')
    end
  end

  local function selectIngredient(r)
    if isChosen(r.id) then
      local kept = {}
      for _, id in ipairs(chosen) do if id ~= r.id then kept[#kept + 1] = id end end
      chosen = kept
    elseif #chosen < 3 then
      chosen[#chosen + 1] = r.id
    end
    refresh()
  end

  local function toggle(which, r)
    if which == 'item' then pickItem = (pickItem ~= r.id) and r.id or nil
    elseif which == 'enchant' then pickEnchant = (pickEnchant ~= r.id) and r.id or nil
    else pickGem = (pickGem ~= r.id) and r.id or nil end
    refresh()
  end

  local function makeList(valueRows, render, height)
    return ui.List{
      items = valueRows,
      key = function(r) return r.key end,
      rowHeight = ROW_HEIGHT,
      height = height or LIST_HEIGHT,
      width = winWidth - 24,
      render = render,
    }
  end

  local function alchemyBody()
    return ui.Column{
      ui.Text{ text = picked, class = 'info' },
      ui.Text{ text = info, class = 'info' },
      makeList(rowsA, function(r)
        return ui.Row{
          ui.Button{ text = (r.on and '* ' or '') .. r.name .. '  x' .. r.count, class = 'item-name',
            style = { width = NAME_WIDTH }, onClick = function() selectIngredient(r) end },
          ui.Text{ text = r.effects, class = 'item-detail', style = { width = DETAIL_WIDTH } },
          ui.Button{ text = 'Eat', class = 'item-all', style = { width = SMALL_WIDTH }, onClick = function() env.eat(r.id) end },
        }
      end),
      ui.Row{
        ui.Button{ text = 'Brew', class = 'btn', onClick = function()
          env.brew(chosen[1] or '', chosen[2] or '', chosen[3] or '')
        end },
        ui.Button{ text = 'Clear', class = 'btn', onClick = function() chosen = {}; refresh() end },
        ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
      },
    }
  end

  local function recipesBody()
    return ui.Column{
      ui.Text{ text = info, class = 'info' },
      makeList(rowsA, function(r)
        return ui.Row{
          ui.Button{ text = r.name .. (r.ok and '' or '  (cannot)'), class = 'item-name', style = { width = NAME_WIDTH },
            onClick = function() if r.temper then env.temper(r.id) else env.craft(r.id) end end },
          ui.Text{ text = r.needs, class = 'item-detail', style = { width = detailWidth } },
        }
      end),
      ui.Row{ ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end } },
    }
  end

  local function pickList(valueRows, which)
    return makeList(valueRows, function(r)
      return ui.Row{
        ui.Button{ text = (r.on and '* ' or '') .. r.name, class = 'item-name', style = { width = NAME_WIDTH },
          onClick = function() toggle(which, r) end },
        ui.Text{ text = r.detail, class = 'item-detail', style = { width = DETAIL_WIDTH } },
      }
    end, SMALL_LIST_HEIGHT)
  end

  local function enchantingBody()
    return ui.Column{
      ui.Text{ text = picked, class = 'info' },
      ui.Text{ text = info, class = 'info' },
      ui.Text{ text = 'Items', class = 'title' },
      pickList(rowsA, 'item'),
      ui.Text{ text = 'Enchantments', class = 'title' },
      pickList(rowsB, 'enchant'),
      ui.Text{ text = 'Soul gems', class = 'title' },
      pickList(rowsC, 'gem'),
      ui.Row{
        ui.Button{ text = 'Enchant', class = 'btn', onClick = function()
          if pickItem and pickEnchant and pickGem then env.enchant(pickItem, pickEnchant, pickGem) end
        end },
        ui.Button{ text = 'Disenchant', class = 'btn', onClick = function()
          if pickItem then env.disenchant(pickItem) end
        end },
        ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
      },
    }
  end

  local builtMode
  local function buildDoc()
    if doc then doc:destroy() end
    builtMode = mode
    winWidth = mode == 'recipes' and RECIPES_WIDTH or WINDOW_WIDTH
    detailWidth = mode == 'recipes' and (RECIPES_WIDTH - NAME_WIDTH - 40) or DETAIL_WIDTH
    doc = ui.Document{ name = 'skyrim-crafting', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = winWidth },
      build = function()
        local body
        if mode == 'alchemy' then body = alchemyBody()
        elseif mode == 'enchanting' then body = enchantingBody()
        else body = recipesBody() end
        return ui.Column{
          ui.Text{ text = title, class = 'title' },
          ui.Text{ text = status, class = 'info' },
          body,
        }
      end }
  end

  --- Opens the window for the bench the session was started on.
  function self.open(name)
    self.dismiss()
    isOpen = true
    chosen, pickItem, pickEnchant, pickGem = {}, nil, nil, nil
    title:set(name or '')
    refresh()
    buildDoc()
  end

  --- Takes the window down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    last = {}
  end

  function self.isOpen() return isOpen end

  --- One call a frame: re-reads the lists when the inventory's revision, the results counter, the crafting revision or the
  --- session state moved. A frame where nothing changed makes four cheap reads and no UI call.
  function self.update()
    if not isOpen then return end
    if inv.revision() ~= last[1] or crafting.results() ~= last[2] or crafting.revision() ~= last[3]
        or crafting.state() ~= last[4] then
      refresh()
      -- the session became ready after the window was drawn: build the window of the bench it turned out to be
      if crafting.state() == 'ready' and mode ~= builtMode then buildDoc() end
    end
  end

  return self
end

return M
