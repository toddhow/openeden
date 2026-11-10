#ifndef GAME_SCRIPT_SCRIPTCOMMANDS_H
#define GAME_SCRIPT_SCRIPTCOMMANDS_H

#include <functional>

namespace Compiler
{
    class Extensions;
}

namespace Interpreter
{
    class Interpreter;
}

namespace MWScript
{
    /// Who supplies the game's script commands (task 0098 move 4). Morrowind's command set lives in the Morrowind game
    /// pack (game_packs/morrowind): its compile table (names, arguments, opcode numbers) and its handlers. The core
    /// only knows this hook; the executable installs the pack's functions at start-up (apps/openmw/main.cpp), before
    /// the engine is made, so openmw-lib never names the pack.
    struct CommandSet
    {
        std::function<void(Compiler::Extensions& extensions, bool consoleOnly)> mRegisterCompileTable;
        std::function<void(Interpreter::Interpreter& interpreter, bool consoleOnly)> mInstallOpcodes;
    };

    /// Replaces the installed command set. Main thread, before the engine runs.
    void installCommandSet(CommandSet commands);

    /// The compile side: registers every command with the compiler.
    /// \throws std::runtime_error naming the missing piece when no game pack installed its commands.
    void registerCommands(Compiler::Extensions& extensions, bool consoleOnly = false);

    /// The run side: installs the handler of every command into the interpreter.
    /// \param consoleOnly include console only opcodes
    /// \throws std::runtime_error as registerCommands
    void installCommandOpcodes(Interpreter::Interpreter& interpreter, bool consoleOnly = false);
}

#endif
