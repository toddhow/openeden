#ifndef GAME_MWDIALOGUE_DIALOGUEFACTORY_H
#define GAME_MWDIALOGUE_DIALOGUEFACTORY_H

#include <functional>
#include <memory>
#include <utility>

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/journal.hpp"

namespace Compiler
{
    class Extensions;
}

namespace Translation
{
    class Storage;
}

namespace MWDialogue
{
    /// Who makes the game's journal and dialogue manager (task 0085 move 3). Morrowind's implementations live in the
    /// Morrowind game pack (game_packs/morrowind); the core only knows the MWBase interfaces and this hook. The
    /// executable installs the pack's factories at start-up (apps/openmw/main.cpp), before the engine is made, so
    /// openmw-lib never names the pack. The save records stay in the engine save stream through the interfaces'
    /// write and readRecord.
    struct Factories
    {
        std::function<std::unique_ptr<MWBase::Journal>()> mMakeJournal;
        std::function<std::unique_ptr<MWBase::DialogueManager>(
            const Compiler::Extensions& extensions, Translation::Storage& translationDataStorage)>
            mMakeDialogueManager;
        /// The --script-all-dialogue check (compiles every result script for the actors that could say it).
        std::function<std::pair<int, int>(const Compiler::Extensions* extensions, int warningsMode)>
            mCompileAllDialogueScripts;
    };

    /// Replaces the installed factories. Main thread, before the engine runs.
    void installFactories(Factories factories);

    /// \throws std::runtime_error naming the missing piece when no game pack installed its factories.
    std::unique_ptr<MWBase::Journal> makeJournal();
    std::unique_ptr<MWBase::DialogueManager> makeDialogueManager(
        const Compiler::Extensions& extensions, Translation::Storage& translationDataStorage);
    std::pair<int, int> compileAllDialogueScripts(const Compiler::Extensions* extensions, int warningsMode);
}

#endif
