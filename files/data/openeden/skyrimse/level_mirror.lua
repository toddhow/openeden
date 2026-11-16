-- Skyrim SE: the load list shows the player's real level (task 0215).
--
-- The save header's level (PLLE, what `openmw.menu.getAllSaves()` gives as `playerLevel`) is the level of the engine's
-- own player stats, which in a Skyrim session stays 1: the level lives in the pack's progression (task 0200). This
-- module copies the progression's level into the engine's level stat whenever they differ, so the next save carries it
-- in the place the list already reads (no second file, no read of another save's chunk). Used only with the pack's
-- `core.skyrimse.progression` (the setting `player progression`); the engine's own mechanics read the level stat only for
-- Morrowind's rules, which a Skyrim session does not run.
--
-- env = {
--   ready = function() return bool end,        -- the progression has its data
--   level = function() return int end,         -- the progression's level
--   getStat = function() return int end,       -- the engine's level stat
--   setStat = function(n) end,
-- }
local M = {}

function M.create(env)
  local self = { writes = 0 }
  -- Returns true when it wrote the stat. A level below 1 (not known) is never written.
  function self.update()
    if not env.ready() then return false end
    local want = env.level()
    if type(want) ~= 'number' or want < 1 then return false end
    if env.getStat() == want then return false end
    env.setStat(want)
    self.writes = self.writes + 1
    return true
  end
  return self
end

return M
