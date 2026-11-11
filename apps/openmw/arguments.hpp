#ifndef VSGOPENMW_ARGUMENTS_H
#define VSGOPENMW_ARGUMENTS_H

#include <components/esm/refid.hpp>
#include <components/files/multidircollection.hpp>
#include <components/toutf8/toutf8.hpp>

#include "builtinpack.hpp"

namespace OMW
{
    /*
     * Arguments is simply a struct of options that can vary per invocation/run of the game/application, typically
     * specified on the command line.
     */
    struct Arguments
    {
        std::vector<std::string> contentFiles;
        std::vector<std::string> groundcoverFiles;
        Files::PathContainer dataDirs;
        std::filesystem::path dataLocal; ///< Also the last of dataDirs, if set and it exists
        std::vector<std::string> archives;
        std::vector<std::string> extensions;
        /// The ID of the game pack of this session (game= or --game), empty for none (task 0084)
        std::string gamePack;
        /// Game packs linked into the executable, usable by game= when none of that id is installed (stage I1b)
        std::vector<BuiltInGamePack> builtInGamePacks;
        std::filesystem::path resourceDir;
        ToUTF8::FromType encoding = ToUTF8::WINDOWS_1252;

        bool useSound = true;
        bool grabMouse = true;

        int activationDistanceOverride = -1;
        unsigned int randomSeed = 0;

        bool compileAll = false;
        bool compileAllDialogue = false;
        int warningsMode = 1;
        bool scriptConsoleMode = false;

        std::filesystem::path saveGameFile;
        std::string cellName;
        std::string startupScript;
        bool newGame = false;
        bool skipMenu = false;

        // Reference / A/B gold-render helpers. 0 disables.
        // autoScreenshotFrame: fire A_Screenshot after N frames of State_Running.
        // autoQuitFrame:       requestQuit() after N frames of State_Running.
        unsigned int autoScreenshotFrame = 0;
        unsigned int autoQuitFrame = 0;

        // Task 0028: bake light probes for this interior cell, writing a data folder layout to bakeGiOut (the user data
        // folder if empty), then quit.
        std::string bakeGiCell;
        std::string bakeGiOut;
        // Task 0036: bake the sky visibility of these exterior cells ("x,y" or "x0,y0:x1,y1"), into bakeGiOut too.
        std::string bakeGiExterior;
        // Task 0055: bake the terrain tiles of the loaded game data (draft layout), into bakeGiOut too.
        bool bakeLodTerrain = false;
        // Task 0095 stage W-b: bake the puddle map ("all", "x,y" or "x0,y0:x1,y1"; empty when not asked), into
        // bakeGiOut too.
        std::string bakePuddles;
        bool bakeLodObjects = false;
        bool analyseBodies = false;
        // Task 0069 stage c: build the merged body cache (--bake-bodies) and check it against the stock parts.
        bool bakeBodies = false;
        bool checkBodies = false;
    };
}

#endif
