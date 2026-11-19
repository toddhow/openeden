Task 0349 (MQ106 "A Blade in the Dark"). Input scripts for OPENEDEN_RMLUI_INPUT_SCRIPT (frames at 60 fps, fixed).
intro_talk.txt   real clicks on the conversation box: Goodbye clicks until the forced greeting is over, then clicks on the first button of each
                 menu of Delphine's room conversation (the y of the first button depends on the text: y = 349 + 8 (lines - 2) - 17 (buttons - 4)).
                 Run (run.sh is the harness; the paths inside are the author's, edit them):
                   SETTINGS_EXTRA=$'story test quest = 032926:5,032926:10@6\n' STEPS="{ 1900,'abs',-738,-700,100,0,0 },{ 2750,'abs',-800,760,30,0,0 }"
                   ACTIVATE="2800, '78968'" ACTIVATE2="4000, '78968'" INPUT=<intro_talk.txt> ./run.sh <tag> 15200
mound_shots.txt  two screenshots of the Kynesgrove run (stages 5..50 by the aid, then two teleports: Kynesgrove's marker and the mound):
                   LOADARG="--start @Tamriel,26008,-32200" SETTINGS_EXTRA=$'story test quest = 032926:5,032926:10,032926:30,032926:32,032926:35,032926:50@25\n'
                   STEPS="{ 3000,'abs',135130,9904,-11900,0,0 },{ 4300,'abs',145900,12400,-6950,0,0 }" INPUT=<mound_shots.txt> ./run.sh <tag> 12500
batch.sh intro|mound <tag> runs three at once; norm.sh prints the normalised story lines of a run (md5 them).
