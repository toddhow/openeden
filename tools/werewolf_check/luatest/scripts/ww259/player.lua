-- tools/werewolf_check (task 0259): the player half. Finishes the character creation without its window (task 0225 tested
-- the window), puts the camera in third person, and prints WW259 lines about the beast form (active, seconds left) and
-- takes screenshots at frame offsets after the form began (the keys come from OPENEDEN_RMLUI_INPUT_SCRIPT).
local camera = require('openmw.camera')
local core = require('openmw.core')
local debug = require('openmw.debug')
local self = require('openmw.self')
local util = require('openmw.util')

local okChr, chr = pcall(require, 'core.skyrimse.character')
local okWolf, wolf = pcall(require, 'core.skyrimse.werewolf')
local cfg = require('scripts.ww259.cfg')
local frames, neededFrames, finished, thirdDone = 0, 0, false, false
local activeAt, last = nil, nil
local offsets = { 20, 80, 150, 158, 166, 174, 182, 190, 200, 215, 300, 420, 540, 660, 780, 900, 960, 985, 1010, 1100, 1170, 1230, 1290, 1360, 1800, 1900, 2400 }
local shot = 1
local sawEnd = false
local finishedAt = nil
local sawNeeded = false
local endShots = nil

local function cameraBehind()
  camera.setMode(camera.MODE.ThirdPerson, true)
end

return {
  engineHandlers = {
    onFrame = function()
      frames = frames + 1
      -- task 0279: pictures at fixed frames (a vampire run: before, as a vampire, after the cure)
      if cfg.vshots and #cfg.vshots > 0 and frames > 150 and frames % 30 == 0 and not (okWolf and wolf.active() == 1) then
        if camera.getMode() ~= camera.MODE.ThirdPerson then camera.setMode(camera.MODE.ThirdPerson, true) end
        camera.setYaw(self.rotation:getYaw() + math.pi * 0.85)
        camera.setPitch(0.2)
        camera.setPreferredThirdPersonDistance(260)
      end
      if cfg.vshots then
        for _, f in ipairs(cfg.vshots) do
          if frames == f then debug.takeScreenshot(); print('WW259 vshot f' .. frames) end
        end
      end
      if cfg.finish and okChr and chr.needed() and not finished then
        neededFrames = neededFrames + 1
        if neededFrames == 60 then
          finished = true
          finishedAt = frames
          core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'name', text = 'Ria' })
          core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'finish' })
        end
      end
      if finishedAt ~= nil and frames == finishedAt + 40 then
        -- the creation window holds the interface mode; closing it as its Done button does
        local okI, I = pcall(require, 'openmw.interfaces')
        if okI and I.UI then I.UI.setMode(); print('WW259 f' .. frames .. ' mode cleared') end
      end
      if okChr and chr.needed() then sawNeeded = true end
      if okChr and chr.ready() and not thirdDone and sawNeeded and not chr.needed() then
        thirdDone = true
        cameraBehind()
      end
      if not okWolf then
        if frames % 600 == 0 then print('WW259 f' .. frames .. ' no werewolf namespace') end
        return
      end
      local active = wolf.active() == 1
      if active then
        -- a test aid: windows of a side view (a static camera beside him, looking at his chest) show the claws and the body;
        -- the rest of the time the view is third person, looking down a little so that the crosshair falls on the floor
        -- where Hulda's body lies (the feed needs the activate ray to meet it)
        local since = activeAt and (frames - activeAt) or 0
        local side = (since >= 140 and since <= 235) or (since >= 1150 and since <= 1380)
        if side then
          local p = self.position
          local f = self.rotation * util.vector3(0, 1, 0)
          local r = util.vector3(f.y, -f.x, 0)
          local pos = p + r * 330 + util.vector3(0, 0, 130)
          local look = p + util.vector3(0, 0, 70)
          camera.setMode(camera.MODE.Static, true)
          camera.setStaticPosition(pos)
          camera.setYaw(math.atan2(look.x - pos.x, look.y - pos.y))
          camera.setPitch(math.atan2(pos.z - look.z, math.sqrt((look.x - pos.x) ^ 2 + (look.y - pos.y) ^ 2)))
        else
          if camera.getMode() ~= camera.MODE.ThirdPerson then camera.setMode(camera.MODE.ThirdPerson, true) end
          camera.setPitch(cfg.pitch)
          camera.setPreferredThirdPersonDistance(cfg.distance)
        end
      end
      if active and activeAt == nil then
        activeAt = frames
        sawEnd = false
        print('WW259 f' .. frames .. ' the form began')
      elseif not active and activeAt ~= nil and not sawEnd then
        sawEnd = true
        print('WW259 f' .. frames .. ' the form ended after ' .. (frames - activeAt) .. ' frames')
        activeAt = nil
        shot = 1
        debug.takeScreenshot()
        print('WW259 shot human-again')
      end
      if activeAt ~= nil and shot <= #offsets and frames - activeAt == offsets[shot] then
        debug.takeScreenshot()
        print('WW259 shot ' .. shot .. ' at +' .. offsets[shot] .. ' ' .. string.format('(%.0f,%.0f)', self.position.x, self.position.y))
        shot = shot + 1
      end
      -- task 0279: the way back (ww_bleedoutstart): pictures while the form is on with no seconds left
      if active and wolf.seconds_left() <= 0 and activeAt ~= nil and frames - activeAt > 300 then
        endShots = (endShots or 0) + 1
        if endShots % 12 == 1 and endShots < 100 then
          debug.takeScreenshot()
          print('WW259 end shot ' .. endShots .. ' f' .. frames)
        end
      end
      if frames % 60 == 0 then
        local powers = {}
        if okChr and chr.ready() then
          local now = math.floor(core.getGameTime())
          for i = 0, chr.power_count() - 1 do powers[#powers + 1] = chr.power_name(i) .. ':' .. chr.power_support(i) .. ':' .. tostring(chr.power_ready(i, now)) end
        end
        local okUI, IUI = pcall(require, 'openmw.interfaces')
        local line = string.format('active=%s left=%d powers=[%s] mode=%s', tostring(active), wolf.seconds_left(), table.concat(powers, ','), tostring(okUI and IUI.UI and IUI.UI.getMode()))
        if line ~= last then
          last = line
          print('WW259 f' .. frames .. ' ' .. line)
        end
      end
    end,
  },
}
