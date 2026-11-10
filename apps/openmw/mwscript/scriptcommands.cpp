#include "scriptcommands.hpp"

#include <stdexcept>

namespace MWScript
{
    namespace
    {
        CommandSet& installed()
        {
            static CommandSet commands;
            return commands;
        }
    }

    void installCommandSet(CommandSet commands)
    {
        installed() = std::move(commands);
    }

    void registerCommands(Compiler::Extensions& extensions, bool consoleOnly)
    {
        if (!installed().mRegisterCompileTable)
            throw std::runtime_error(
                "No game pack provides the script commands: the executable did not install them "
                "(MWScript::installCommandSet)");
        installed().mRegisterCompileTable(extensions, consoleOnly);
    }

    void installCommandOpcodes(Interpreter::Interpreter& interpreter, bool consoleOnly)
    {
        if (!installed().mInstallOpcodes)
            throw std::runtime_error(
                "No game pack provides the script commands: the executable did not install them "
                "(MWScript::installCommandSet)");
        installed().mInstallOpcodes(interpreter, consoleOnly);
    }
}
