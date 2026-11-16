-- tools/services_check (task 0219): the global half. Waits for the pack's services namespace, optionally moves the player
-- into the cell of cfg.cell, finds the NPC cfg.target there, stands the player in front of it and activates it (the
-- conversation lines are chosen by the real input script), places the player at a bed (cfg.bed) and asks the menu script
-- to save. Every SVC219 line is a trace line the check compares between runs.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local storage = require('openmw.storage')
local util = require('openmw.util')
local cfg = require('scripts.svc219.cfg')
local services = require('core.skyrimse.services')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')

local frames, stage, wait = 0, 0, 0
local target = nil
local bedPlaced, activated, saved = false, false, false
local rentedAt = nil
local nextLog = 0

local function find(cell)
  for _, obj in ipairs(cell:getAll()) do
    local ok, id = pcall(function() return obj.recordId end)
    if ok and id == cfg.target then return obj end
  end
  return nil
end

local function trace()
  local gt = core.getGameTime()
  local gold = okInv and inv.ready() and inv.count('player', inv.gold_item()) or -1
  print(string.format('SVC219 t=%.1f hour=%.2f state=%s gold=%d rented=%s last=%q', core.getSimulationTime(),
    (gt / 3600) % 24, services.state(), gold, tostring(services.rented('000616ca')), services.last_result()))
end

return {
  engineHandlers = {
    onUpdate = function()
      local player = world.players[1]
      if player == nil then return end
      frames = frames + 1
      local now = core.getSimulationTime()
      if now >= nextLog then
        nextLog = now + 2
        trace()
      end
      if cfg.target ~= '' and not activated then
        if stage == 0 and frames >= cfg.startAt then
          stage = 1
          if cfg.cell ~= '' and player.cell.name ~= cfg.cell then
            print('SVC219 teleporting to ' .. cfg.cell)
            player:teleport(cfg.cell, util.vector3(0, 0, 0))
          end
          wait = frames + 60
        elseif stage == 1 and frames >= wait then
          target = find(player.cell)
          if target then
            local p = target.position
            print(string.format('SVC219 found %s at %.0f,%.0f,%.0f', cfg.target, p.x, p.y, p.z))
            player:teleport(player.cell, util.vector3(p.x, p.y - 90, p.z), { rotation = util.transform.rotateZ(0) })
            stage = 2
            wait = frames + 40
          else
            wait = frames + 30
          end
        elseif stage == 2 and frames >= wait then
          activated = true
          if cfg.direct then
            -- no real conversation line reaches the service (a gift line needs a quest): send the engine's event
            print('SVC219 direct event ' .. cfg.direct[1] .. ' for ' .. cfg.target)
            core.sendGlobalEvent('openeden_skyrimse_service_opened',
              { service = cfg.direct[1], placement = cfg.direct[2], base = cfg.direct[3], name = cfg.direct[4] })
          else
            print('SVC219 activating ' .. cfg.target)
            target:activateBy(player)
          end
        end
      end
      -- cfg.bed[6] < 0: place the player at the bed once the room is rented and the window is closed
      local bedDue = cfg.bed and (cfg.bed[6] >= 0 and frames >= cfg.bed[6]
        or cfg.bed[6] < 0 and services.state() == 'none' and services.rented('000616ca'))
      if bedDue and not bedPlaced then
        bedPlaced = true
        player:teleport(player.cell, util.vector3(cfg.bed[1], cfg.bed[2], cfg.bed[3]),
          { rotation = util.transform.rotateZ(cfg.bed[4]) })
        player:sendEvent('SvcLook', cfg.bed[5])
        print('SVC219 player placed at the bed spot')
      end
      -- cfg.saveAt < 0: save once the room is rented and the window is closed (a little later)
      if cfg.saveAt < 0 and not saved and services.state() == 'none' and services.rented('000616ca') then
        rentedAt = rentedAt or frames
        if frames >= rentedAt + 120 then
          saved = true
          storage.globalSection('SVC219'):set('saveNow', true)
        end
      end
      if cfg.saveAt > 0 and frames >= cfg.saveAt and not saved then
        saved = true
        storage.globalSection('SVC219'):set('saveNow', true)
      end
    end,
  },
}
