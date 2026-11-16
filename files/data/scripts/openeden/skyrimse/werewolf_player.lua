-- Skyrim SE werewolf beast form, the player half (task 0259): the form is third person only (the game's own
-- PlayerWerewolfChangeScript calls ForceThirdPerson) and the claws are always out, and what the form announces shows as the game's message. Needs the
-- pack's `core.skyrimse.werewolf` namespace (the builtin line carries REQUIRES=).
local camera = require('openmw.camera')
local ui = require('openmw.ui')
local types = require('openmw.types')
local self = require('openmw.self')
local okWolf, wolf = pcall(require, 'core.skyrimse.werewolf')
if not okWolf then
  return {}
end

local shown = 0
local wasActive = false

return {
  engineHandlers = {
    onFrame = function()
      local active = wolf.active() == 1
      if active and not wasActive then
        camera.setMode(camera.MODE.ThirdPerson, true)
      end
      -- the claws are always out: the attack control attacks (it would otherwise activate what he looks at)
      if active and types.Actor.getStance(self) ~= types.Actor.STANCE.Weapon then
        types.Actor.setStance(self, types.Actor.STANCE.Weapon)
      end
      if wasActive and not active then
        types.Actor.setStance(self, types.Actor.STANCE.Nothing)
      end
      if active and camera.getMode() == camera.MODE.FirstPerson then
        camera.setMode(camera.MODE.ThirdPerson, true)
      end
      wasActive = active
      local count = wolf.notification_count()
      if count < shown then shown = 0 end
      if count > shown then
        for n = math.max(shown + 1, count - 15), count do
          local text = wolf.notification(n)
          if text ~= '' then ui.showMessage(text) end
        end
        shown = count
      end
    end,
  },
}
