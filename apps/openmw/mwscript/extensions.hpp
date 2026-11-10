#ifndef GAME_SCRIPT_EXTENSIONS_H
#define GAME_SCRIPT_EXTENSIONS_H

// Moved to game_packs/morrowind (task 0098 move 4): the Morrowind game pack owns the script command set. This header
// only keeps the old include path alive for the characterization tests in apps/openmw_tests/mwscript, which pin the
// behaviour and are not edited by the move; it declares MWScript::installOpcodes, which the pack defines. Only targets
// that link game_morrowind (which exports its include folder) may include it. The game's own code reaches the command
// set through MWScript::registerCommands and MWScript::installCommandOpcodes (scriptcommands.hpp).
#include <game_morrowind/script_commands.hpp>

#endif
