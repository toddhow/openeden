-- tools/racemenu_check (task 0225): saves the game once, SAVE_AT frames after the game started running (0: never).
local menu = require('openmw.menu')
local cfg = require('scripts.rm225.cfg')
local frames, saved = 0, false
return {
  engineHandlers = {
    onFrame = function()
      if menu.getState() ~= menu.STATE.Running then return end
      frames = frames + 1
      if cfg.saveAt > 0 and not saved and frames >= cfg.saveAt then
        saved = true
        print('RM225 save request')
        menu.saveGame('rm225', 'rm225')
      end
    end,
  },
}
