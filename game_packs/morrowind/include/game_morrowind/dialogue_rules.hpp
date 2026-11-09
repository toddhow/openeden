#ifndef GAME_MORROWIND_DIALOGUE_RULES_H
#define GAME_MORROWIND_DIALOGUE_RULES_H

#include <memory>
#include <utility>

namespace Compiler
{
    class Extensions;
}

namespace Translation
{
    class Storage;
}

namespace MWBase
{
    class DialogueManager;
    class Journal;
}

namespace game_morrowind
{
    /// Morrowind's journal and dialogue manager (task 0085 move 3): the unchanged MWDialogue::Journal and
    /// MWDialogue::DialogueManager, moved here from apps/openmw/mwdialogue, behind the engine's own interfaces. They
    /// keep writing and reading REC_QUES, REC_JOUR and REC_DIAS in the engine save stream. Main thread only.
    [[nodiscard]] std::unique_ptr<MWBase::Journal> makeJournal();
    [[nodiscard]] std::unique_ptr<MWBase::DialogueManager> makeDialogueManager(
        const Compiler::Extensions& extensions, Translation::Storage& translationDataStorage);
    /// The --script-all-dialogue check, moved with the filter it uses (MWDialogue::ScriptTest::compileAll).
    [[nodiscard]] std::pair<int, int> compileAllDialogueScripts(
        const Compiler::Extensions* extensions, int warningsMode);
}

#endif
