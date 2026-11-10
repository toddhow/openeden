#include <game_morrowind/script_commands.hpp>

#include <components/compiler/extensions0.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>

#include <apps/openmw/mwscript/scriptcommands.hpp>

#include <game_morrowind/aiextensions.hpp>
#include <game_morrowind/animationextensions.hpp>
#include <game_morrowind/cellextensions.hpp>
#include <game_morrowind/consoleextensions.hpp>
#include <game_morrowind/containerextensions.hpp>
#include <game_morrowind/controlextensions.hpp>
#include <game_morrowind/dialogueextensions.hpp>
#include <game_morrowind/guiextensions.hpp>
#include <game_morrowind/miscextensions.hpp>
#include <game_morrowind/skyextensions.hpp>
#include <game_morrowind/soundextensions.hpp>
#include <game_morrowind/statsextensions.hpp>
#include <game_morrowind/transformationextensions.hpp>
#include <game_morrowind/userextensions.hpp>

namespace MWScript
{
    void installOpcodes(Interpreter::Interpreter& interpreter, bool consoleOnly)
    {
        Interpreter::installOpcodes(interpreter);
        Cell::installOpcodes(interpreter);
        Misc::installOpcodes(interpreter);
        Gui::installOpcodes(interpreter);
        Sound::installOpcodes(interpreter);
        Sky::installOpcodes(interpreter);
        Stats::installOpcodes(interpreter);
        Container::installOpcodes(interpreter);
        Ai::installOpcodes(interpreter);
        Control::installOpcodes(interpreter);
        Dialogue::installOpcodes(interpreter);
        Animation::installOpcodes(interpreter);
        Transformation::installOpcodes(interpreter);

        if (consoleOnly)
        {
            Console::installOpcodes(interpreter);
            User::installOpcodes(interpreter);
        }
    }
}

namespace game_morrowind
{
    void installScriptCommands()
    {
        MWScript::installCommandSet({ &Compiler::registerExtensions, &MWScript::installOpcodes });
    }
}
