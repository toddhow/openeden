-- openeden.ui.reactive (task 0162, decision 0015, research note 0045 section 6.2 and the U0 spike's layer in 13.7).
--
-- Value, Computed, Effect, Scope and one flush per frame. The rules:
--   * A component function runs once. A reactive property installs ONE effect that writes the property when its
--     source changed; there is no virtual tree and no diff.
--   * Effects run only when a source's version moved, at one defined point (`flush`), once each, in creation order.
--     A flush with nothing queued allocates nothing and runs nothing.
--   * A Computed is lazy and cached; a recompute that gives an equal result keeps its version, so its readers are not
--     woken.
--   * A Scope owns effects, cleanups and child scopes; destroying it stops the effects and runs the cleanups in reverse.
--   * An effect that keeps dirtying effects for more than MAX_ROUNDS rounds is a located error, not a hang.
local R = {}

local MAX_ROUNDS = 100

local current = nil      -- the Effect or Computed being evaluated (dependency tracking target)
local currentScope = nil -- the scope components are built in
local queue, queued = {}, {} -- dirty effects waiting for the flush
local queueN = 0
local seq = 0
local stats = { effectRuns = 0, computeRuns = 0 }

R.MAX_ROUNDS = MAX_ROUNDS

-- ---- dependency graph ----

local function addSub(src, consumer)
  src.subs[consumer] = true
end

local release

local function dropSub(src, consumer)
  src.subs[consumer] = nil
  -- A computed nobody reads any more stops listening to its own sources (so abandoned computeds do not pile up).
  if src.kind == 'computed' and next(src.subs) == nil then release(src) end
end

function release(c)
  local deps = c.deps
  if next(deps) == nil then return end
  c.deps = {}
  c.dirty = true
  for src in pairs(deps) do dropSub(src, c) end
end

local function track(src)
  local c = current
  if c then c.newDeps[src] = src.ver end
end

-- After an evaluation: make the subscription sets match what was actually read.
local function commitDeps(c)
  local old, new = c.deps, c.newDeps
  c.newDeps = nil
  c.deps = new
  for src in pairs(old) do
    if new[src] == nil then dropSub(src, c) end
  end
  for src in pairs(new) do
    if old[src] == nil then addSub(src, c) end
  end
end

local function schedule(e)
  if not queued[e] then
    queued[e] = true
    queueN = queueN + 1
    queue[queueN] = e
  end
end

local function mark(src)
  for c in pairs(src.subs) do
    if c.kind == 'computed' then
      if not c.dirty then
        c.dirty = true
        mark(c)
      end
    else
      schedule(c)
    end
  end
end

-- ---- Value ----

local ValueMT = {}
ValueMT.__index = ValueMT

function ValueMT:get()
  track(self)
  return self.v
end

function ValueMT:peek()
  return self.v
end

function ValueMT:set(nv)
  if nv ~= self.v then
    self.v = nv
    self.ver = self.ver + 1
    mark(self)
  end
end

function R.Value(v)
  return setmetatable({ kind = 'value', v = v, subs = {}, ver = 0 }, ValueMT)
end

-- ---- Computed ----

local ComputedMT = {}
ComputedMT.__index = ComputedMT

function ComputedMT:get()
  if self.dirty then
    local prev = current
    current = self
    self.newDeps = {}
    stats.computeRuns = stats.computeRuns + 1
    local ok, result = pcall(self.fn)
    current = prev
    if not ok then
      commitDeps(self)
      error(result, 0)
    end
    commitDeps(self)
    self.dirty = false
    local old = self.v
    self.v = result
    -- An unchanged result keeps the version, so readers are not woken.
    if self.ver == 0 or result ~= old then self.ver = self.ver + 1 end
  end
  track(self)
  return self.v
end

function ComputedMT:peek()
  local prev = current
  current = nil
  local v = self:get()
  current = prev
  return v
end

function R.Computed(fn)
  return setmetatable({ kind = 'computed', fn = fn, dirty = true, deps = {}, subs = {}, ver = 0 }, ComputedMT)
end

function R.isReactive(x)
  if type(x) ~= 'table' then return false end
  local mt = getmetatable(x)
  return mt == ValueMT or mt == ComputedMT
end

-- ---- Scope ----

local ScopeMT = {}
ScopeMT.__index = ScopeMT

function R.Scope(parent)
  local s = setmetatable({ alive = true, cascading = false, parent = parent }, ScopeMT)
  if parent then
    local children = parent.children
    if not children then
      children = {}
      parent.children = children
      parent.childCount = 0
      parent.holes = 0
    end
    local n = parent.childCount + 1
    parent.childCount = n
    children[n] = s
    s.slot = n
  end
  return s
end

function ScopeMT:onCleanup(f)
  local c = self.cleanups
  if not c then
    c = {}
    self.cleanups = c
  end
  c[#c + 1] = f
end

local function compact(parent)
  local children, kept = parent.children, 0
  local out = {}
  for i = 1, parent.childCount do
    local ch = children[i]
    if ch then
      kept = kept + 1
      out[kept] = ch
      ch.slot = kept
    end
  end
  parent.children = out
  parent.childCount = kept
  parent.holes = 0
end

-- `cascading` is true while an ancestor is being destroyed: cleanups may skip work that the ancestor's own
-- destruction already covers (for example destroying an element whose parent element is being destroyed).
function ScopeMT:destroy(cascading)
  if not self.alive then return end
  self.alive = false
  self.cascading = cascading or false
  local parent = self.parent
  if parent and not cascading and parent.alive then
    parent.children[self.slot] = false
    parent.holes = parent.holes + 1
    if parent.holes > 32 and parent.holes * 2 > parent.childCount then compact(parent) end
  end
  local children = self.children
  if children then
    for i = self.childCount, 1, -1 do
      local ch = children[i]
      if ch then ch:destroy(true) end
    end
    self.children = nil
  end
  local effects = self.effects
  if effects then
    for i = 1, #effects do
      local e = effects[i]
      e.dead = true
      local deps = e.deps
      e.deps = {}
      for src in pairs(deps) do dropSub(src, e) end
    end
    self.effects = nil
  end
  local cleanups = self.cleanups
  if cleanups then
    self.cleanups = nil
    for i = #cleanups, 1, -1 do cleanups[i]() end
  end
end

function R.withScope(scope, fn, ...)
  local prev = currentScope
  currentScope = scope
  local ok, a = pcall(fn, ...)
  currentScope = prev
  if not ok then error(a, 0) end
  return a
end

function R.currentScope()
  return currentScope
end

-- ---- Effect ----

local function runEffect(e)
  local prev = current
  current = e
  e.newDeps = {}
  stats.effectRuns = stats.effectRuns + 1
  local ok, err = pcall(e.fn)
  current = prev
  commitDeps(e)
  if not ok then error(err, 0) end
end

function R.EffectIn(scope, fn)
  if not scope then
    error('no active scope: create effects and components inside a ui.Document build function or ui.withScope', 3)
  end
  seq = seq + 1
  local e = { kind = 'effect', fn = fn, deps = {}, seq = seq }
  local list = scope.effects
  if not list then
    list = {}
    scope.effects = list
  end
  list[#list + 1] = e
  runEffect(e)
  return e
end

function R.Effect(fn)
  if not currentScope then
    error('no active scope: create effects and components inside a ui.Document build function or ui.withScope', 2)
  end
  return R.EffectIn(currentScope, fn)
end

-- Runs fn without tracking dependencies (list rows build inside the list's own effect).
function R.untracked(fn, ...)
  local prev = current
  current = nil
  local ok, a = pcall(fn, ...)
  current = prev
  if not ok then error(a, 0) end
  return a
end

local function bySeq(a, b)
  return a.seq < b.seq
end

function R.pending()
  return queueN > 0
end

-- One flush per frame: dirty effects run once each per round, in creation order. A round that dirties more effects
-- starts another round. The first error is raised after the flush has run everything it could.
function R.flush()
  if queueN == 0 then return end
  local rounds = 0
  local firstError
  while queueN > 0 do
    rounds = rounds + 1
    if rounds > MAX_ROUNDS then
      for i = 1, queueN do queued[queue[i]] = nil end
      queue, queueN = {}, 0
      error('effect cycle: effects kept changing values for ' .. MAX_ROUNDS .. ' rounds' ..
        (firstError and (' (first error: ' .. tostring(firstError) .. ')') or ''), 0)
    end
    local q, n = queue, queueN
    queue, queueN, queued = {}, 0, {}
    table.sort(q, bySeq)
    for i = 1, n do
      local e = q[i]
      if not e.dead then
        local needs = false
        local prev = current
        current = nil
        for src, ver in pairs(e.deps) do
          if src.kind == 'computed' and src.dirty then
            local ok = pcall(src.get, src) -- refresh: an unchanged recompute keeps the version
            if not ok then needs = true end
          end
          if src.ver ~= ver then needs = true end
        end
        current = prev
        if needs then
          local ok, err = pcall(runEffect, e)
          if not ok and firstError == nil then firstError = err end
        end
      end
    end
  end
  if firstError ~= nil then error(firstError, 0) end
end

function R.stats()
  return stats
end

-- Bind a plain or reactive value to a protocol write; an unchanged value never reaches the protocol.
function R.bind(scope, v, apply)
  if R.isReactive(v) then
    local last
    local first = true
    R.EffectIn(scope, function()
      local x = v:get()
      if first or x ~= last then
        first = false
        last = x
        apply(x)
      end
    end)
  else
    apply(v)
  end
end

return R
