#ifndef GAME_MORROWIND_SCRIPT_COMMANDS_H
#define GAME_MORROWIND_SCRIPT_COMMANDS_H

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
    /// Installs the handlers of Morrowind's script commands (the run side of the command set; the compile side is
    /// Compiler::registerExtensions in game_morrowind_script_table).
    /// \param consoleOnly include console only opcodes
    void installOpcodes(Interpreter::Interpreter& interpreter, bool consoleOnly = false);
}

namespace game_morrowind
{
    /// Hands the command set (compile table and handlers) to the engine's hook, MWScript::installCommandSet. The
    /// executable calls this once at start-up, before the engine is made (apps/openmw/main.cpp).
    void installScriptCommands();
}

#endif
