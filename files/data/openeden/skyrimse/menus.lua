-- The Skyrim SE main menu and pause menu (task 0207), written with `openeden.ui` (decision 0015).
--
-- The window logic only; it talks to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui       the `openeden.ui` library
--   env.menu     the engine's `openmw.menu` (getState, STATE, getAllSaves, loadGame, saveGame, newGame, quit)
--   env.engine   the engine-side switches (the underscore functions of `openmw.menu`, task 0207):
--                  setReplaced(bool)   the engine's own (MyGUI) main menu draws nothing while true
--                  mainMenuOpen()      is the engine's main menu mode (pause menu or main menu) on the GUI stack
--                  closeMainMenu()     leaves it (the Resume button)
--                  settingsVisible(), toggleSettings()   the engine's own settings window
--                  setTip(text)        the tip the engine's loading screen shows
--   env.menus    the pack's `core.skyrimse.menus` (ready(), tip(seed))
--   env.date(t)  text of a time (default os.date), env.now() the time now (default os.time)
--   env.seedBase an offset for the tip seeds (a new session shows other tips)
-- The engine's main menu mode is the trigger: it is open in a pause (Escape in a game) and always in the main menu of a
-- session with no game running. This script draws the page, the engine draws nothing of its own while it is replaced.
-- Pages: main (no game), pause (a game runs), load, save, confirm (quit from the pause menu).

local M = {}

local PAGE_WIDTH = 420
local LIST_HEIGHT = 320
local ROW_HEIGHT = 34
local TIP_PERIOD = 120 -- updates between two tips (two seconds at 60 frames a second)
local SAVE_WAIT = 120 -- updates to wait for a saved game to show up before saying the save failed
local LOAD_HIDE = 90 -- updates the menu stays down after a load or a new game was asked, if the menu mode stays open
local GAME_CONTENT = 'skyrim.esm'

--- Hours and minutes played: 3725 -> "1:02".
function M.playedText(seconds)
  local n = tonumber(seconds) or 0
  if n < 0 then n = 0 end
  local h = math.floor(n / 3600)
  local m = math.floor((n % 3600) / 60)
  return string.format('%d:%02d', h, m)
end

local function defaultDate(t)
  if os and os.date then return os.date('%d %b %Y %H:%M', math.floor(t)) end
  return tostring(t)
end

local function isThisGame(slot)
  for _, file in ipairs(slot.contentFiles or {}) do
    if string.lower(file) == GAME_CONTENT then return true end
  end
  return false
end

function M.create(env)
  local ui, menu, engine, menus = env.ui, env.menu, env.engine, env.menus
  local date = env.date or defaultDate
  local now = env.now or function() return os and os.time and os.time() or 0 end
  local self = {}

  local replaced = false
  local sub = nil -- nil, 'load', 'save' or 'confirm'
  local doc, docKey = nil, nil
  local status = ui.Value('')
  local listRows = ui.Value({})
  local rowActions = {} -- the rows of the current list page, in order
  local tipSeed = (env.seedBase or 0)
  local tipAge = TIP_PERIOD
  local hidden = 0 -- updates left in which the page stays down (a load or new game was asked)
  local pendingSave = nil -- a save was asked: { before = signature of the saves, age = updates since }

  -- ---- the tip ----

  local function refreshTip()
    if not menus.ready() then return end
    tipAge = 0
    tipSeed = tipSeed + 1
    local text = menus.tip(tipSeed)
    if text ~= nil and text ~= '' then engine.setTip(text) end
  end

  -- ---- the saves ----

  local function collectSaves()
    local rows = {}
    for dir, slots in pairs(menu.getAllSaves()) do
      for name, slot in pairs(slots) do
        if isThisGame(slot) then
          rows[#rows + 1] = {
            dir = dir, slot = name, description = slot.description or '',
            created = slot.creationTime or 0,
            text = string.format('%s   Level %d   %s   %s   %s', slot.playerName or '?', slot.playerLevel or 0,
              (menu._getSaveCell and menu._getSaveCell(dir, name)) or slot.playerCell or '', M.playedText(slot.timePlayed), date(slot.creationTime or 0)),
          }
        end
      end
    end
    table.sort(rows, function(a, b)
      if a.created ~= b.created then return a.created > b.created end
      if a.dir ~= b.dir then return a.dir < b.dir end
      return a.slot < b.slot
    end)
    return rows
  end

  local function basePage()
    if menu.getState() == menu.STATE.Running then return 'pause' end
    return 'main'
  end

  local function currentPage() return sub or basePage() end

  -- ---- what the buttons do ----

  local function goBack() sub = nil end

  local destroyDocument -- below

  -- A load or a new game was asked: the engine's loading screen follows, and Lua does not run during it, so the page
  -- comes down NOW (it would stay drawn over the loading screen). If the mode is still open after LOAD_HIDE updates
  -- (the load was refused) it comes back.
  local function hideForLoad()
    destroyDocument()
    hidden = LOAD_HIDE
  end

  local function startLoad(row)
    refreshTip() -- the loading screen that follows shows a fresh tip
    menu.loadGame(row.dir, row.slot)
    hideForLoad()
  end

  -- What the saves are, as one text: it changes when a save was written (a new slot or a newer one).
  local function savesSignature()
    local count, sum = 0, 0
    for _, slots in pairs(menu.getAllSaves()) do
      for _, slot in pairs(slots) do
        count = count + 1
        sum = sum + (slot.creationTime or 0)
      end
    end
    return count .. ':' .. sum
  end

  -- The engine may refuse a save (a neutral session needs [Game] neutral saves): success is reported only when the
  -- list of saves changed, failure when it did not within SAVE_WAIT updates.
  local function doSave(row)
    pendingSave = { before = savesSignature(), age = 0 }
    if row.isNew then
      menu.saveGame('Save ' .. date(now()), nil)
    else
      menu.saveGame(row.description ~= '' and row.description or ('Save ' .. date(now())), row.slot)
    end
    status:set('Saving...')
    sub = nil
  end

  local function prepareRows()
    local rows = collectSaves()
    rowActions = {}
    if sub == 'save' then
      rowActions[1] = { text = 'New Save', isNew = true, key = 'new' }
    end
    for _, r in ipairs(rows) do
      r.key = r.dir .. '/' .. r.slot
      rowActions[#rowActions + 1] = r
    end
    listRows:set(rowActions)
  end

  local function button(text, onClick)
    return ui.Button{ text = text, class = 'btn', style = { width = PAGE_WIDTH }, onClick = onClick }
  end

  local function savesList()
    if #rowActions == 0 then
      return ui.Text{ text = 'No saved games', class = 'info' }
    end
    return ui.List{
      items = listRows,
      key = function(r) return r.key end,
      rowHeight = ROW_HEIGHT,
      height = LIST_HEIGHT,
      width = PAGE_WIDTH + 200,
      render = function(r)
        return ui.Button{ text = r.text, class = 'btn', style = { width = PAGE_WIDTH + 188 },
          onClick = function()
            if sub == 'save' then doSave(r) else startLoad(r) end
          end }
      end,
    }
  end

  local function buildPage(page)
    local entries = {}
    entries[#entries + 1] = ui.Text{ text = env.title or 'Skyrim', class = 'title' }
    if page == 'main' then
      if #collectSaves() > 0 then
        entries[#entries + 1] = button('Continue', function()
          local rows = collectSaves()
          if rows[1] then startLoad(rows[1]) end
        end)
      end
      entries[#entries + 1] = button('New Game', function()
        refreshTip()
        menu.newGame()
        hideForLoad()
      end)
      entries[#entries + 1] = button('Load Game', function() sub = 'load' end)
      entries[#entries + 1] = button('Settings', function() engine.toggleSettings() end)
      entries[#entries + 1] = button('Quit', function() menu.quit() end)
    elseif page == 'pause' then
      entries[#entries + 1] = button('Resume', function() engine.closeMainMenu() end)
      entries[#entries + 1] = button('Save Game', function() sub = 'save' end)
      entries[#entries + 1] = button('Load Game', function() sub = 'load' end)
      entries[#entries + 1] = button('Settings', function() engine.toggleSettings() end)
      entries[#entries + 1] = button('Quit', function() sub = 'confirm' end)
      entries[#entries + 1] = ui.Text{ text = status, class = 'info' }
    elseif page == 'confirm' then
      entries[#entries + 1] = ui.Text{ text = 'Quit to desktop?', class = 'info' }
      entries[#entries + 1] = button('Yes', function() menu.quit() end)
      entries[#entries + 1] = button('No', goBack)
    else -- load or save
      entries[#entries + 1] = ui.Text{ text = page == 'save' and 'Save Game' or 'Load Game', class = 'info' }
      entries[#entries + 1] = savesList()
      entries[#entries + 1] = button('Back', goBack)
    end
    return ui.Column(entries)
  end

  destroyDocument = function()
    if doc then doc:destroy() end
    doc, docKey = nil, nil
  end

  local function rebuild(page)
    destroyDocument()
    if page == 'load' or page == 'save' then prepareRows() else rowActions = {} end
    -- With no game running the engine draws nothing behind the page (its own menu background is switched off with the
    -- menu), so the main page covers the screen; the pause menu lets the paused game show through.
    local style = { position = 'absolute', left = 0, top = 0, width = '100%', height = '100%' }
    if basePage() == 'main' then style['background-color'] = '#000000' end
    doc = ui.Document{ name = 'skyrim-menu', class = 'panel', style = style,
      build = function()
        return ui.Column{ style = { width = PAGE_WIDTH + 220, ['margin-left'] = 120, ['margin-top'] = 80 },
          buildPage(page) }
      end }
    docKey = page .. ':' .. tostring(menu.getState())
  end

  -- ---- the host's calls ----

  --- One call a frame.
  function self.update()
    if not replaced then
      replaced = true
      engine.setReplaced(true)
    end
    tipAge = tipAge + 1
    if tipAge >= TIP_PERIOD then refreshTip() end

    if pendingSave then
      pendingSave.age = pendingSave.age + 1
      if savesSignature() ~= pendingSave.before then
        status:set('Game saved')
        pendingSave = nil
      elseif pendingSave.age >= SAVE_WAIT then
        status:set('The game could not be saved')
        pendingSave = nil
      end
    end

    if not engine.mainMenuOpen() then
      destroyDocument()
      sub = nil
      hidden = 0
      pendingSave = nil
      status:set('')
      return
    end
    if hidden > 0 then
      hidden = hidden - 1
      destroyDocument()
      return
    end
    if engine.settingsVisible() then
      destroyDocument()
      return
    end
    local page = currentPage()
    local key = page .. ':' .. tostring(menu.getState())
    if doc == nil or docKey ~= key then rebuild(page) end
  end

  function self.isOpen() return doc ~= nil end
  function self.page()
    if not engine.mainMenuOpen() then return nil end
    return currentPage()
  end

  --- The texts of the rows of the current list page (what the player sees, in order).
  function self.saveRowTexts()
    local out = {}
    for i, r in ipairs(rowActions) do out[i] = r.text end
    return out
  end

  --- Presses row `index` (1 based) of the current list page.
  function self.clickSave(index)
    local row = rowActions[index]
    if row == nil then return end
    if sub == 'save' then doSave(row) else startLoad(row) end
  end

  return self
end

return M
