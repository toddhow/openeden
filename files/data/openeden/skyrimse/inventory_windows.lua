-- The Skyrim SE inventory and container windows (task 0182, milestone M-S6), written with `openeden.ui` (decision 0015).
--
-- This module holds the whole window logic and talks only to two things it is handed, so it runs over the headless UI
-- backend in tests:
--   env.ui        the `openeden.ui` library
--   env.inv       the `core.skyrimse.inventory` package (Read functions: the pack owns the state, this only draws it)
--   env.transfer(from, to, item, count)   asks the pack to move items (a global script runs the Mutate call)
--   env.takeAll(from, to)
--   env.close()   leaves the interface mode (the host script's job)
--   env.equip(item), env.unequip(item), env.use(item)   (task 0184) optional: ask the pack to wield or wear an item,
--                 take it off, or drink a potion. A package without `equipped` and `use_kind` (version 1.0) gets no
--                 such buttons.
--   env.read(item)   (task 0203) optional: asks the pack to read a book (a Read button on a book row of the player's
--                 list); without it a book row has no button.
-- Ids are text: an item is 8 hex digits, an owner is 'player' or the placement's hex id.
--
-- Rows are keyed by item id AND count, so a changed count makes a new row (a List row is built once).

local M = {}

local ROW_HEIGHT = 22
local LIST_HEIGHT = 440
local WINDOW_WIDTH = 400
local BUTTON_ALL_WIDTH = 40
local BUTTON_USE_WIDTH = 62
local NAME_WIDTH = 180
local DETAIL_WIDTH = 84

-- `withUse` rows (the player's) also carry what the extra button does and whether the item is worn.
local function rows(inv, owner, withUse, canRead)
  local out = {}
  local canUse = withUse and inv.use_kind ~= nil
  for i, id in ipairs(inv.items(owner)) do
    local count = inv.count(owner, id)
    local useKind = canUse and inv.use_kind(id) or ''
    local equipped = canUse and inv.equipped ~= nil and inv.equipped(id) or false
    out[i] = {
      id = id,
      key = id .. ':' .. count .. (equipped and ':e' or ''),
      count = count,
      name = inv.name(id),
      category = inv.category(id),
      weight = inv.weight(id),
      value = inv.value(id),
      useKind = useKind,
      readable = withUse and canRead and inv.category(id) == 'book',
      equipped = equipped,
    }
  end
  return out
end

local function rowLabel(r)
  local count = r.count > 1 and string.format('  x%d', r.count) or ''
  local worn = r.equipped and ' [E]' or ''
  return string.format('%s%s%s', r.name, worn, count)
end

-- The label of the extra button of a row, nil when it has none.
local function useLabel(r)
  if r.readable then return 'Read' end
  if r.useKind == 'potion' or r.useKind == 'scroll' then return 'Use' end -- task 0234: a scroll is cast
  if r.useKind == 'weapon' or r.useKind == 'armor' then return r.equipped and 'Unequip' or 'Equip' end
  return nil
end

local function detail(r)
  return string.format('%.1f  %dg', r.weight * r.count, r.value * r.count)
end

function M.create(env)
  local ui, inv = env.ui, env.inv
  local self = {}

  local playerRows = ui.Value({})
  local otherRows = ui.Value({})
  local weightText = ui.Value('')
  local goldText = ui.Value('')
  local otherTitle = ui.Value('')
  local statusText = ui.Value('')
  local docs = {}
  local openOwner = nil -- the container or corpse shown on the right; nil: the inventory only
  local waiting = false -- the owner is not registered yet
  local lastRevision = nil
  local isOpen = false

  local function refresh()
    lastRevision = inv.revision()
    playerRows:set(rows(inv, 'player', true, env.read ~= nil))
    weightText:set(string.format('Weight %.1f / %d', inv.carried(), inv.capacity()))
    goldText:set(string.format('Gold %d', inv.count('player', inv.gold_item())))
    if openOwner then
      otherRows:set(rows(inv, openOwner))
    else
      otherRows:set({})
    end
  end

  local function onUse(r)
    if r.readable then
      env.read(r.id)
      return
    end
    if r.useKind == 'potion' or r.useKind == 'scroll' then
      if env.use then env.use(r.id) end
    elseif r.equipped then
      if env.unequip then env.unequip(r.id) end
    else
      if env.equip then env.equip(r.id) end
    end
  end

  local function makeList(valueRows, onTake, onTakeAll, label, withUse)
    return ui.List{
      items = valueRows,
      key = function(r) return r.key end,
      rowHeight = ROW_HEIGHT,
      height = LIST_HEIGHT,
      width = WINDOW_WIDTH - 24,
      render = function(r)
        local children = {
          ui.Button{ text = rowLabel(r), class = 'item-name', style = { width = NAME_WIDTH },
            onClick = function() onTake(r) end },
          ui.Text{ text = detail(r), class = 'item-detail', style = { width = DETAIL_WIDTH } },
          ui.Button{ text = label, class = 'item-all', style = { width = BUTTON_ALL_WIDTH },
            onClick = function() onTakeAll(r) end },
        }
        local extra = withUse and useLabel(r) or nil
        if extra then
          children[#children + 1] = ui.Button{ text = extra, class = 'item-use',
            style = { width = BUTTON_USE_WIDTH }, onClick = function() onUse(r) end }
        end
        return ui.Row(children)
      end,
    }
  end

  local function buildInventory()
    return ui.Document{ name = 'skyrim-inventory', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Inventory', class = 'title' },
          ui.Text{ text = weightText, class = 'info' },
          ui.Text{ text = goldText, class = 'info' },
          makeList(playerRows,
            function(r) if openOwner then env.transfer('player', openOwner, r.id, 1) end end,
            function(r) if openOwner then env.transfer('player', openOwner, r.id, r.count) end end,
            'Put', true),
        }
      end }
  end

  local function buildContainer()
    return ui.Document{ name = 'skyrim-container', class = 'panel',
      style = { position = 'absolute', left = 24 + WINDOW_WIDTH + 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = otherTitle, class = 'title' },
          ui.Text{ text = statusText, class = 'info' },
          makeList(otherRows,
            function(r) env.transfer(openOwner, 'player', r.id, 1) end,
            function(r) env.transfer(openOwner, 'player', r.id, r.count) end,
            'Take'),
          ui.Row{
            ui.Button{ text = 'Take all', class = 'btn', onClick = function() env.takeAll(openOwner, 'player') end },
            ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
          },
        }
      end }
  end

  --- Opens the windows: the inventory alone (owner nil) or beside a container or corpse.
  function self.open(owner, title)
    self.dismiss()
    isOpen = true
    openOwner = owner
    waiting = owner ~= nil and not inv.known(owner)
    otherTitle:set(title or '')
    statusText:set(waiting and 'Opening...' or '')
    refresh()
    docs[#docs + 1] = buildInventory()
    if owner then docs[#docs + 1] = buildContainer() end
  end

  --- Takes the windows down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    for _, d in ipairs(docs) do d:destroy() end
    docs = {}
    isOpen = false
    openOwner = nil
    waiting = false
    lastRevision = nil
  end

  function self.isOpen() return isOpen end
  function self.owner() return openOwner end

  --- One call a frame: re-reads the lists when the pack's revision moved or the owner became known. A frame where
  --- nothing changed makes two cheap reads and no UI call.
  function self.update()
    if not isOpen then return end
    if waiting then
      if inv.known(openOwner) then
        waiting = false
        statusText:set('')
        refresh()
      end
      return
    end
    if inv.revision() ~= lastRevision then refresh() end
  end

  return self
end

return M
