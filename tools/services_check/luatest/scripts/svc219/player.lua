-- tools/services_check (task 0219): the player half: looks where the scenario asks.
local camera = require('openmw.camera')
return {
  eventHandlers = {
    SvcLook = function(pitch)
      camera.setMode(camera.MODE.FirstPerson)
      camera.setPitch(pitch)
    end,
  },
}
