Input scripts for OPENEDEN_RMLUI_INPUT_SCRIPT (task 0318). Each line is "<frame> <command>" (mouse X Y, down L X Y, up L X Y, key K down, shot).

mq318_thane.txt   the Dragonsreach outro after MQ104 stage 90: Balgruuf's blocking topic, "Dragonborn", the Greybeards, Goodbye;
                  the reward greeting (Thane, Lydia, the axe) closed by its Goodbye click at frame 13,900. Run with
                    SETTINGS_EXTRA="\nmain quest whiterun = true\nstory test quest = 02610C:90,02610C:120@25\nstory test delay = 10\n"
                    INPUT=<this file> check.sh ... 17000
mq318_arngeir.txt High Hrothgar: Arngeir's greeting by the real dialogue (run with the player teleported there:
                    STEPS="{ 3000,'cell','HighHrothgar',-1000,-416,-880,270,0 }" ACTIVATE="5000, '181959'"
                    and story test quest = 02610C:90,0242BA:5).
The frame numbers depend on the scene timings (60 fps, fixed): change them when a phase moves.
