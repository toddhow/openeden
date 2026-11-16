-- tools/firstperson_check (task 0249): prints what the character service says once a second of frames and, once the
-- creation is over, looks a little down (the first-person hands are below the view at rest) and takes pictures.
local self = require('openmw.self')
local debug = require('openmw.debug')
local core = require('openmw.core')
local cfg = require('scripts.fp249.cfg')

local okChr, chr = pcall(require, 'core.skyrimse.character')
local frames, last = 0, nil
local sawNeeded = false
local doneAt = nil
local pitched, shots = false, 0

local function state()
  if not okChr then return 'no-character-package' end
  if not chr.ready() then return 'not-ready' end
  return string.format('needed=%s race=%s female=%s', tostring(chr.needed()), chr.race_name(chr.race_index()),
    tostring(chr.female()))
end

return {
  engineHandlers = {
    onFrame = function()
      frames = frames + 1
      if okChr and chr.ready() then
        if chr.needed() then sawNeeded = true
        elseif sawNeeded and not doneAt then doneAt = core.getSimulationTime() end
      end
      if doneAt then
        local t = core.getSimulationTime() - doneAt
        if not pitched and t > 0.5 then
          pitched = true
          self.controls.pitchChange = cfg.pitch
          print('FP249 look down')
        end
        if shots < cfg.shotCount and t >= cfg.shotsAfter + shots * cfg.shotStep then
          shots = shots + 1
          debug.takeScreenshot()
          print('FP249 shot ' .. shots)
        end
      end
      if frames % 60 ~= 0 then return end
      local line = state()
      if line ~= last then
        last = line
        print('FP249 f' .. frames .. ' ' .. line)
      end
    end,
  },
}
