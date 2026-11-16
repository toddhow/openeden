-- tools/racemenu_check (task 0228): logs what the pack's character service and progression say, once a second of real
-- frames (60 frames at the fixed frame time), as NP228 lines. Nothing here depends on a worker's timing except the frame
-- on which the creation becomes needed, which is not printed.
local self = require('openmw.self')
local camera = require('openmw.camera')
local core = require('openmw.core')
local types = require('openmw.types')

local okChr, chr = pcall(require, 'core.skyrimse.character')
local okProg, prog = pcall(require, 'core.skyrimse.progression')
local frames, last = 0, nil
local sawNeeded, thirdDone = false, false
local neededFrames, finished = 0, false

local function skills()
  if not (okProg and prog.ready()) then return 'no-progression' end
  local parts = {}
  for slot = 0, prog.skill_count() - 1 do parts[#parts + 1] = tostring(prog.skill_level(slot)) end
  return table.concat(parts, ',')
end

local function state()
  if not okChr then return 'no-character-package' end
  if not chr.ready() then return 'not-ready' end
  local race = chr.race_name(chr.race_index())
  local powers = {}
  for i = 0, chr.power_count() - 1 do
    powers[#powers + 1] = chr.power_name(i) .. ':' .. chr.power_support(i) .. ':' .. tostring(chr.power_ready(i, math.floor(core.getGameTime())))
  end
  return string.format('record-name=%s powers=[%s] needed=%s race=%s female=%s hair=%s(%d) beard=%s(%d) eyes=%s(%d) name=%s',
    types.NPC.record(self.object).name, table.concat(powers, ','), tostring(chr.needed()), race, tostring(chr.female()),
    chr.part_name('hair'), chr.part_index('hair'), chr.part_name('beard'), chr.part_index('beard'),
    chr.part_name('eyes'), chr.part_index('eyes'), chr.name())
end

return {
  engineHandlers = {
    onFrame = function()
      frames = frames + 1
      -- task 0228: the test finishes the creation without the window (the window was tested in task 0225)
      if okChr and chr.needed() and not finished then
        neededFrames = neededFrames + 1
        if neededFrames == 60 then
          finished = true
          core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'name', text = 'Ria' })
          core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'finish' })
        end
      end
      if frames == 1050 then camera.setMode(camera.MODE.ThirdPerson, true) end
      -- a test aid: once the creation is over (or was never needed: a loaded game) the camera goes to third person so
      -- that frames show the body
      if okChr and chr.ready() and not thirdDone then
        if chr.needed() then sawNeeded = true
        elseif frames > (sawNeeded and 40 or 120) then
          thirdDone = true
          camera.setMode(camera.MODE.ThirdPerson, true)
        end
      end
      if frames % 60 ~= 0 then return end
      local line = state() .. ' | skills ' .. skills()
      if line ~= last then
        last = line
        print('NP228 f' .. frames .. ' ' .. line)
      end
    end,
  },
}
