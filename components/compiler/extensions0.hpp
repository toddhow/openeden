#ifndef COMPILER_EXTENSIONS0_H
#define COMPILER_EXTENSIONS0_H

namespace Compiler
{
    class Extensions;

    /// Registers the script commands of the game. DEFINED BY THE MORROWIND GAME PACK, not by this library
    /// (game_packs/morrowind/src/script_table.cpp, task 0098 move 4): the components know the script compiler and not
    /// the commands. A program that calls this links the pack's table library (game_morrowind_script_table); the game's
    /// own code does not call it and reaches the table through MWScript::registerCommands.
    void registerExtensions(Extensions& extensions, bool consoleOnly = false);
}

#endif
