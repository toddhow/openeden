-- tools/neutral_session_check (task 0200): reports the player's progression and, once a level up and a perk were
-- bought, tells the menu script to save (the menu script then loads that save in the same process).
local storage = require('openmw.storage')
local okProg, prog = pcall(require, 'core.skyrimse.progression')
if not okProg then return {} end

local updates, ready, reported = 0, false, false
local function state()
  local perks = prog.perks(0)
  local owned = {}
  for _, id in ipairs(perks) do
    if prog.perk_rank(id) > 0 then owned[#owned + 1] = id .. 'x' .. prog.perk_rank(id) end
  end
  return string.format('SKSTATE level %d xp %d points %d one-handed %d perks %s', prog.level(), prog.level_xp(),
    prog.perk_points(), prog.skill_level(0), table.concat(owned, ','))
end

return {
  engineHandlers = {
    onUpdate = function()
      updates = updates + 1
      if not prog.ready() then return end
      if not ready then
        ready = true
        print(state()) -- the first state of this session (after a load: the restored one)
      end
      if not reported and prog.level() >= 2 and prog.perk_points() == 0 and state():find('x1', 1, true) then
        reported = true
        print(state())
        storage.globalSection('SK'):set('saveNow', true)
      end
    end,
  },
}
