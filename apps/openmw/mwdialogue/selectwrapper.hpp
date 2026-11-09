#ifndef OPENMW_MWDIALOGUE_SELECTWRAPPER_FORWARD_H
#define OPENMW_MWDIALOGUE_SELECTWRAPPER_FORWARD_H

// Moved to game_packs/morrowind (task 0085 move 3): the Morrowind game pack owns this class. This header only keeps the
// old include path alive for the characterization tests in apps/openmw_tests/mwdialogue, which pin the behaviour and
// are not edited by the move. Only targets that link game_morrowind (which exports its include folder) may include it.
// The game's own code reaches the journal and the dialogue manager through the MWBase interfaces and
// MWDialogue::makeJournal() / makeDialogueManager().
#include <game_morrowind/selectwrapper.hpp>

#endif
