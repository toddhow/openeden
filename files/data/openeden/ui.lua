-- openeden.ui (task 0162, decision 0015, research note 0045 section 6): OpenEden's UI component library.
--
-- Components are plain Lua functions that run once and return nodes; properties are plain values or reactive values
-- (Value, Computed); a reactive property installs one effect that talks to the backend only when the value changed.
-- The library talks to ONE thing, the element protocol (package `openeden.uibackend`, components/ui/elementprotocol.hpp),
-- so the UI engine behind it (RmlUi today) can change. Looks come from style sheets; this file only decides structure,
-- behaviour and the few inline properties a component cannot work without (flex layout, positions of list rows).
--
--   local ui = require('openeden.ui')
--   local fps = ui.Value('FPS: --')
--   local hud = ui.Hud{ name = 'fps', build = function() return ui.Text{ text = fps } end }
--   -- the engine calls ui.update() once per frame by itself (task 0164); calling it yourself is harmless
--
-- A component must be built inside the `build` function of a Document/Hud (or inside ui.withScope): the scope owns its
-- effects and handlers and is destroyed with the document.
local R = require('openeden.ui.reactive')
local backend = require('openeden.uibackend').open()

local ui = {}

ui.Value = R.Value
ui.Computed = R.Computed
ui.Effect = R.Effect
ui.Scope = R.Scope
ui.withScope = R.withScope
ui.flush = R.flush
ui.stats = R.stats
ui.untracked = R.untracked
ui.isReactive = R.isReactive

local isReactive = R.isReactive
local currentScope = R.currentScope

-- ---- event dispatch ----

local handlers = {} -- element id -> { [eventKind] = function }
local firstError

local function dispatch(id, kind, value)
  local h = handlers[id]
  local f = h and h[kind]
  if f then
    local ok, err = pcall(f, value)
    if not ok and firstError == nil then firstError = err end
  end
end

local function listen(id, kind, f)
  local h = handlers[id]
  if not h then
    h = {}
    handlers[id] = h
  end
  if h[kind] == nil then backend.listen(id, kind, true) end
  h[kind] = f
end

-- The one per-frame call: events in, reactive flush, one commit. A frame in which nothing happened makes no protocol
-- call and allocates nothing.
function ui.update()
  backend.poll(dispatch)
  local flushError
  if R.pending() then
    local ok, err = pcall(R.flush)
    if not ok then flushError = err end
  end
  backend.commit()
  local err = firstError or flushError
  if err ~= nil then
    firstError = nil
    error(err, 0)
  end
end

-- Task 0164: the engine calls ui.update() once a frame for every script that required this library (after the script's
-- own onFrame, so what onFrame changed is flushed in the same frame). A manual ui.update() stays harmless: a second
-- flush in one frame finds nothing to do. Packages without the hook (a headless test backend) are left as they are.
if backend.setUpdater then
  backend.setUpdater(function() return ui.update() end)
end

function ui.focus(node)
  backend.focus(node.id)
end

-- ---- helpers ----

local unitless = {
  opacity = true, ['z-index'] = true, ['flex-grow'] = true, ['flex-shrink'] = true, order = true,
  ['line-height'] = true, flex = true,
}

local function cssValue(name, v)
  if type(v) == 'number' then
    if unitless[name] then return tostring(v) end
    return tostring(v) .. 'dp'
  end
  return tostring(v)
end

local nodeMarker = {}

-- The classes a component carries: its own name first, then the ones the base style sheet (ui/base.rcss, task 0163)
-- styles. Looks come from there, through tokens; nothing in this file names a colour.
local baseClasses = { button = 'button btn', input = 'input text', list = 'list scroll' }

local function isNode(x)
  return type(x) == 'table' and x[nodeMarker] == true
end

local commonProps = {
  class = true, style = true, visible = true, focusable = true, autofocus = true, nav = true,
  onClick = true, onFocus = true, onBlur = true,
}

local function checkProps(who, props, allowed)
  if type(props) ~= 'table' then error(who .. ': expected a table of properties', 3) end
  for k in pairs(props) do
    if type(k) ~= 'number' and not commonProps[k] and not (allowed and allowed[k]) then
      error(who .. ": unknown property '" .. tostring(k) .. "'", 3)
    end
  end
end

local function requireScope(who)
  local scope = currentScope()
  if not scope then
    error(who .. ': no active scope: build components inside the build function of ui.Document or ui.Hud, or inside ui.withScope', 3)
  end
  return scope
end

local pendingAutofocus

local function ensureDomId(node)
  if not node.domId then
    node.domId = 'e' .. tostring(node.id)
    backend.setAttribute(node.id, 'id', node.domId)
  end
  return node.domId
end

-- Creates an element and the node that stands for it. `top` is true for a node that is not attached to a parent by the
-- library (documents, list rows): its destruction destroys the element; every other node dies with its parent.
local function makeNode(who, scope, kind, tag, props, shownDisplay)
  local id = backend.create(tag)
  local node = { [nodeMarker] = true, id = id, kind = kind, scope = scope, owned = false }
  scope:onCleanup(function()
    handlers[id] = nil
    if not node.owned and not scope.cascading then backend.destroy(id) end
  end)
  return node
end

local function applyCommon(who, node, scope, props, shownDisplay)
  local id = node.id
  local base = baseClasses[node.kind] or node.kind
  local cls = props.class
  if isReactive(cls) then
    R.bind(scope, cls, function(x)
      backend.setClass(id, (x ~= nil and x ~= '') and (base .. ' ' .. tostring(x)) or base)
    end)
  elseif cls ~= nil and cls ~= '' then
    backend.setClass(id, base .. ' ' .. tostring(cls))
  else
    backend.setClass(id, base)
  end
end

local function applyStyle(node, scope, props)
  local style = props.style
  if style == nil then return end
  local names = {}
  for name in pairs(style) do names[#names + 1] = name end
  table.sort(names)
  local id = node.id
  for _, name in ipairs(names) do
    R.bind(scope, style[name], function(x)
      backend.setProperty(id, name, cssValue(name, x))
    end)
  end
end

local function applyVisible(node, scope, props, shownDisplay)
  if props.visible == nil then return end
  local id = node.id
  R.bind(scope, props.visible, function(x)
    backend.setProperty(id, 'display', x and shownDisplay or 'none')
  end)
end

local function applyBehaviour(who, node, scope, props, extraFocusable)
  local id = node.id
  if props.focusable or extraFocusable then backend.setAttribute(id, 'tabindex', 'auto') end
  if props.nav then
    for dir, target in pairs(props.nav) do
      if not isNode(target) then error(who .. ": nav." .. tostring(dir) .. ' must be a node', 3) end
      backend.setProperty(id, 'nav-' .. dir, '#' .. ensureDomId(target))
    end
  end
  if props.onClick then listen(id, 'click', props.onClick) end
  if props.onFocus then listen(id, 'focus', props.onFocus) end
  if props.onBlur then listen(id, 'blur', props.onBlur) end
  if props.autofocus and not pendingAutofocus then pendingAutofocus = node end
end

local function attachChildren(who, node, props)
  local n = 0
  for k in pairs(props) do
    if type(k) == 'number' and k > n then n = k end
  end
  for i = 1, n do
    local child = props[i]
    if child then
      if not isNode(child) then error(who .. ': child ' .. i .. ' is not a component node', 3) end
      child.owned = true
      backend.insertBefore(node.id, child.id, 0)
    end
  end
end

local function textOf(x)
  if x == nil then return '' end
  return tostring(x)
end

-- ---- the base components ----

local containerProps = { gap = true }

local function container(kind, structural)
  local who = 'ui.' .. kind:sub(1, 1):upper() .. kind:sub(2)
  return function(props)
    props = props or {}
    checkProps(who, props, structural and containerProps or nil)
    local scope = requireScope(who)
    local node = makeNode(who, scope, kind, 'div', props)
    applyCommon(who, node, scope, props)
    if kind == 'row' or kind == 'column' then
      backend.setProperty(node.id, 'display', 'flex')
      backend.setProperty(node.id, 'flex-direction', kind)
      if props.gap ~= nil then
        R.bind(scope, props.gap, function(x) backend.setProperty(node.id, 'gap', cssValue('gap', x)) end)
      end
    elseif kind == 'stack' then
      backend.setProperty(node.id, 'position', 'relative')
    end
    applyStyle(node, scope, props)
    applyVisible(node, scope, props, (kind == 'row' or kind == 'column') and 'flex' or 'block')
    applyBehaviour(who, node, scope, props)
    attachChildren(who, node, props)
    return node
  end
end

ui.Frame = container('frame', false)
ui.Row = container('row', true)
ui.Column = container('column', true)
ui.Stack = container('stack', false)

local textProps = { text = true }

local function textual(kind, focusable)
  local who = 'ui.' .. kind:sub(1, 1):upper() .. kind:sub(2)
  return function(props)
    props = props or {}
    checkProps(who, props, textProps)
    local scope = requireScope(who)
    local node = makeNode(who, scope, kind, 'div', props)
    applyCommon(who, node, scope, props)
    applyStyle(node, scope, props)
    applyVisible(node, scope, props, 'block')
    if props.text ~= nil then
      local id = node.id
      R.bind(scope, props.text, function(x) backend.setText(id, textOf(x)) end)
    end
    applyBehaviour(who, node, scope, props, focusable)
    attachChildren(who, node, props)
    return node
  end
end

ui.Text = textual('text', false)
ui.Button = textual('button', true)

local imageProps = { src = true }

function ui.Image(props)
  props = props or {}
  checkProps('ui.Image', props, imageProps)
  local scope = requireScope('ui.Image')
  local node = makeNode('ui.Image', scope, 'image', 'img', props)
  applyCommon('ui.Image', node, scope, props)
  applyStyle(node, scope, props)
  applyVisible(node, scope, props, 'block')
  if props.src ~= nil then
    local id = node.id
    R.bind(scope, props.src, function(x) backend.setAttribute(id, 'src', textOf(x)) end)
  end
  applyBehaviour('ui.Image', node, scope, props)
  return node
end

local inputProps = { value = true, onChange = true }

function ui.TextInput(props)
  props = props or {}
  checkProps('ui.TextInput', props, inputProps)
  local scope = requireScope('ui.TextInput')
  local node = makeNode('ui.TextInput', scope, 'input', 'input', props)
  local id = node.id
  applyCommon('ui.TextInput', node, scope, props)
  applyStyle(node, scope, props)
  applyVisible(node, scope, props, 'block')
  backend.setAttribute(id, 'type', 'text')
  local value = props.value
  local lastKnown
  if value ~= nil then
    local first = true
    R.bind(scope, value, function(x)
      x = textOf(x)
      if first or x ~= lastKnown then
        first = false
        lastKnown = x
        backend.setAttribute(id, 'value', x)
      end
    end)
  end
  local onChange = props.onChange
  listen(id, 'change', function(text)
    lastKnown = text
    if isReactive(value) and value.set then value:set(text) end
    if onChange then onChange(text) end
  end)
  applyBehaviour('ui.TextInput', node, scope, props, true)
  return node
end

-- ---- List: keyed rows, virtualised from the start ----

local listProps = { items = true, key = true, render = true, rowHeight = true, height = true, overscan = true, width = true }

function ui.List(props)
  props = props or {}
  checkProps('ui.List', props, listProps)
  if type(props.rowHeight) ~= 'number' or props.rowHeight <= 0 then
    error('ui.List: rowHeight (a positive number of dp) is required', 2)
  end
  if type(props.height) ~= 'number' or props.height <= 0 then
    error('ui.List: height (a positive number of dp) is required', 2)
  end
  if type(props.key) ~= 'function' or type(props.render) ~= 'function' then
    error('ui.List: key and render functions are required', 2)
  end
  local scope = requireScope('ui.List')
  local rowH, height, keyOf, render = props.rowHeight, props.height, props.key, props.render
  local overscan = props.overscan or 2

  local outer = makeNode('ui.List', scope, 'list', 'div', props)
  applyCommon('ui.List', outer, scope, props)
  backend.setProperty(outer.id, 'height', cssValue('height', height))
  backend.setProperty(outer.id, 'overflow-y', 'auto')
  backend.setProperty(outer.id, 'width', props.width and cssValue('width', props.width) or '100%')
  applyStyle(outer, scope, props)
  applyVisible(outer, scope, props, 'block')
  applyBehaviour('ui.List', outer, scope, props)

  local body = makeNode('ui.List', scope, 'list-body', 'div', props)
  backend.setClass(body.id, 'list-body')
  backend.setProperty(body.id, 'position', 'relative')
  body.owned = true
  backend.insertBefore(outer.id, body.id, 0)

  local scrollTop = R.Value(0)
  listen(outer.id, 'scroll', function(v)
    local n = tonumber(v)
    if n then scrollTop:set(n) end
  end)

  local entries = {}   -- key -> { scope, id, index }
  local shown = {}     -- keys in the window, in order
  local lastCount

  local function makeRow(item, index)
    local rowScope = R.Scope(scope)
    local rowId = backend.create('div')
    backend.setClass(rowId, 'list-row')
    backend.setProperty(rowId, 'position', 'absolute')
    backend.setProperty(rowId, 'left', '0dp')
    backend.setProperty(rowId, 'width', props.width and cssValue('width', props.width) or '100%')
    backend.setProperty(rowId, 'height', cssValue('height', rowH))
    backend.setProperty(rowId, 'top', cssValue('top', index * rowH))
    rowScope:onCleanup(function()
      if not rowScope.cascading then backend.destroy(rowId) end
    end)
    local child = R.withScope(rowScope, render, item, index + 1)
    if child ~= nil then
      if not isNode(child) then error('ui.List: render must return a component node', 0) end
      child.owned = true
      backend.insertBefore(rowId, child.id, 0)
    end
    backend.insertBefore(body.id, rowId, 0)
    return { scope = rowScope, id = rowId, index = index }
  end

  R.EffectIn(scope, function()
    local source = props.items
    local arr = isReactive(source) and source:get() or source
    local n = #arr
    scrollTop:get()
    local st = scrollTop:peek()
    if n ~= lastCount then
      lastCount = n
      backend.setProperty(body.id, 'height', cssValue('height', n * rowH))
    end
    local maxScroll = n * rowH - height
    if maxScroll < 0 then maxScroll = 0 end
    if st > maxScroll then st = maxScroll end
    if st < 0 then st = 0 end
    local first = math.floor(st / rowH) - overscan
    local last = math.ceil((st + height) / rowH) - 1 + overscan
    if first < 0 then first = 0 end
    if last > n - 1 then last = n - 1 end

    local newShown, seen = {}, {}
    for i = first, last do
      local item = arr[i + 1]
      local k = R.untracked(keyOf, item)
      if seen[k] then error('ui.List: duplicate key ' .. tostring(k), 0) end
      seen[k] = true
      newShown[#newShown + 1] = k
      local entry = entries[k]
      if not entry then
        entries[k] = R.untracked(makeRow, item, i)
      elseif entry.index ~= i then
        entry.index = i
        backend.setProperty(entry.id, 'top', cssValue('top', i * rowH))
      end
    end
    for _, k in ipairs(shown) do
      if not seen[k] then
        local entry = entries[k]
        entries[k] = nil
        entry.scope:destroy()
      end
    end
    shown = newShown
  end)

  scope:onCleanup(function()
    entries, shown = {}, {}
  end)
  return outer
end

-- ---- documents ----

local documentProps = { name = true, build = true }

local DocumentMT = {}
DocumentMT.__index = DocumentMT

function DocumentMT:destroy()
  self.scope:destroy()
end

local function makeDocument(who, props, isHud)
  checkProps(who, props, documentProps)
  if props.focusable or props.autofocus or props.nav or props.onClick or props.onFocus or props.onBlur or props.visible then
    error(who .. ': documents take class, style, name and build only', 2)
  end
  if type(props.name) ~= 'string' or props.name == '' then error(who .. ': name is required', 2) end
  if type(props.build) ~= 'function' then error(who .. ': build (a function returning the content) is required', 2) end

  local scope = R.Scope()
  local id = backend.createDocument(props.name)
  local alive = true
  scope:onCleanup(function()
    handlers[id] = nil
    if alive then backend.destroy(id) end
  end)
  local doc = setmetatable({ id = id, scope = scope, name = props.name }, DocumentMT)
  local savedAutofocus = pendingAutofocus
  pendingAutofocus = nil
  local ok, err = pcall(function()
    local docNode = { [nodeMarker] = true, id = id, kind = isHud and 'hud' or 'document', owned = true }
    if isHud or props.class ~= nil then
      if isReactive(props.class) then
        R.bind(scope, props.class, function(x)
          local base = isHud and 'hud' or ''
          backend.setClass(id, (base ~= '' and x ~= nil and x ~= '') and (base .. ' ' .. tostring(x)) or
            (base ~= '' and base or textOf(x)))
        end)
      else
        local cls = props.class
        local base = isHud and 'hud' or nil
        backend.setClass(id, base and (cls and cls ~= '' and (base .. ' ' .. tostring(cls)) or base) or tostring(cls))
      end
    end
    if isHud then backend.setProperty(id, 'pointer-events', 'none') end
    applyStyle(docNode, scope, props)
    local result = R.withScope(scope, props.build)
    if result ~= nil then
      local list = isNode(result) and { result } or result
      if type(list) ~= 'table' then error(who .. ': build must return a component node or a list of them', 0) end
      local n = 0
      for k in pairs(list) do
        if type(k) == 'number' and k > n then n = k end
      end
      for i = 1, n do
        local child = list[i]
        if child then
          if not isNode(child) then error(who .. ': build returned something that is not a component node', 0) end
          child.owned = true
          backend.insertBefore(id, child.id, 0)
        end
      end
    end
    if pendingAutofocus then backend.focus(pendingAutofocus.id) end
  end)
  pendingAutofocus = savedAutofocus
  if not ok then
    scope:destroy()
    error(err, 0)
  end
  return doc
end

function ui.Document(props)
  return makeDocument('ui.Document', props or {}, false)
end

-- A document that takes no pointer events: a HUD. Put a value that changes every frame in its own small Hud (the
-- document is RmlUi's unit of layout: a text change re-lays out the whole document, note 0045 13.5).
function ui.Hud(props)
  return makeDocument('ui.Hud', props or {}, true)
end

return ui
