-- The Skyrim SE book reading window (task 0203), written with `openeden.ui` (decision 0015).
--
-- The window logic only; it talks to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui     the `openeden.ui` library
--   env.lb     the `core.skyrimse.lockbooks` package (Read functions: the pack owns the book and what reading did)
--   env.close()   (optional) called by the Close button after the window is taken down
--   env.take(placement, item)   (task 0234, optional) asks the pack to take a book that lies in the world; with it, a book
--     opened with a placement it may take (self.open(item, placement, takeable)) shows a Take button
-- Shows the title, what the book teaches, the effect of the reading (the pack's `book_result`, refreshed every frame
-- because the pack applies the effect a frame after the click), one page of text at a time and Previous / Next / Close.
-- Items are 8 hex digits.

local M = {}

local WINDOW_WIDTH = 520
local PAGE_HEIGHT = 380

function M.create(env)
  local ui, lb = env.ui, env.lb
  local self = {}

  local titleText = ui.Value('')
  local teachesText = ui.Value('')
  local resultText = ui.Value('')
  local pageText = ui.Value('')
  local pageNumber = ui.Value('')
  local doc = nil
  local isOpen = false
  local item = nil
  local pages = {}
  local page = 1
  local placement = nil -- task 0234: where the book lies, when it is read in the world
  local takeable = false

  local function showPage()
    if #pages == 0 then
      pageText:set('(There is nothing to read.)')
      pageNumber:set('')
      return
    end
    pageText:set(pages[page])
    pageNumber:set(string.format('Page %d of %d', page, #pages))
  end

  local function build()
    return ui.Document{ name = 'skyrim-book', class = 'panel',
      style = { position = 'absolute', left = 460, top = 24, width = WINDOW_WIDTH },
      build = function()
        local buttons = {
          ui.Button{ text = 'Previous', class = 'btn', onClick = function() self.turn(-1) end },
          ui.Button{ text = 'Next', class = 'btn', onClick = function() self.turn(1) end },
        }
        if env.take and placement and takeable then
          buttons[#buttons + 1] = ui.Button{ text = 'Take', class = 'btn', onClick = function() self.take() end }
        end
        buttons[#buttons + 1] = ui.Button{ text = 'Close', class = 'btn', onClick = function() self.close() end }
        return ui.Column{
          ui.Text{ text = titleText, class = 'title' },
          ui.Text{ text = teachesText, class = 'info' },
          ui.Text{ text = resultText, class = 'info' },
          ui.Text{ text = pageText, class = 'book-page',
            style = { width = WINDOW_WIDTH - 24, height = PAGE_HEIGHT, ['white-space'] = 'pre-wrap' } },
          ui.Text{ text = pageNumber, class = 'info' },
          ui.Row(buttons),
        }
      end }
  end

  --- Opens the window on an item (a book of the pack's index; anything else shows nothing to read). `where` (task 0234)
  --- is the placement of a book lying in the world and `mayTake` whether it can be taken: with env.take they give the
  --- window a Take button.
  function self.open(itemId, where, mayTake)
    self.dismiss()
    isOpen = true
    item = itemId
    placement = where
    takeable = mayTake and true or false
    local known = lb.book_known(item)
    pages = known and lb.book_pages(item) or {}
    page = 1
    titleText:set(known and lb.book_title(item) or '')
    local teaches = known and lb.book_teaches(item) or ''
    local kind = known and lb.book_kind(item) or 'book'
    if kind == 'skill' and teaches ~= '' then
      teachesText:set('Teaches ' .. teaches)
    elseif kind == 'spell' and teaches ~= '' then
      teachesText:set('Spell tome: ' .. teaches)
    else
      teachesText:set('')
    end
    resultText:set(lb.book_result(item))
    showPage()
    doc = build()
  end

  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    item = nil
    placement = nil
    takeable = false
  end

  function self.close()
    self.dismiss()
    if env.close then env.close() end
  end

  --- Task 0234: takes the book that lies in the world (the pack's pickup path decides about theft), then closes.
  function self.take()
    if not (env.take and placement and item and takeable) then return end
    env.take(placement, item)
    self.close()
  end

  --- Turns the page by delta (-1 or 1), staying inside the book.
  function self.turn(delta)
    if #pages == 0 then return end
    local n = page + delta
    if n < 1 then n = 1 elseif n > #pages then n = #pages end
    if n ~= page then
      page = n
      showPage()
    end
  end

  function self.isOpen() return isOpen end
  function self.item() return item end
  function self.page() return page end
  function self.pageCount() return #pages end

  --- One call a frame: the effect line follows the pack.
  function self.update()
    if not isOpen then return end
    resultText:set(lb.book_result(item))
  end

  return self
end

return M
