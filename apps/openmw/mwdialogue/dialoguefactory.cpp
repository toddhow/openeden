#include "dialoguefactory.hpp"

#include <stdexcept>

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/journal.hpp"

namespace MWDialogue
{
    namespace
    {
        Factories& installed()
        {
            static Factories factories;
            return factories;
        }
    }

    void installFactories(Factories factories)
    {
        installed() = std::move(factories);
    }

    std::unique_ptr<MWBase::Journal> makeJournal()
    {
        if (!installed().mMakeJournal)
            throw std::runtime_error(
                "No game pack provides the journal: the executable did not install the dialogue "
                "factories (MWDialogue::installFactories)");
        return installed().mMakeJournal();
    }

    std::unique_ptr<MWBase::DialogueManager> makeDialogueManager(
        const Compiler::Extensions& extensions, Translation::Storage& translationDataStorage)
    {
        if (!installed().mMakeDialogueManager)
            throw std::runtime_error(
                "No game pack provides the dialogue manager: the executable did not install the "
                "dialogue factories (MWDialogue::installFactories)");
        return installed().mMakeDialogueManager(extensions, translationDataStorage);
    }

    std::pair<int, int> compileAllDialogueScripts(const Compiler::Extensions* extensions, int warningsMode)
    {
        if (!installed().mCompileAllDialogueScripts)
            throw std::runtime_error(
                "No game pack provides the dialogue script check: the executable did not install "
                "the dialogue factories (MWDialogue::installFactories)");
        return installed().mCompileAllDialogueScripts(extensions, warningsMode);
    }
}
