#include <game_morrowind/dialogue_rules.hpp>

#include <game_morrowind/dialoguemanagerimp.hpp>
#include <game_morrowind/journalimp.hpp>
#include <game_morrowind/scripttest.hpp>

namespace game_morrowind
{
    std::unique_ptr<MWBase::Journal> makeJournal()
    {
        return std::make_unique<MWDialogue::Journal>();
    }

    std::unique_ptr<MWBase::DialogueManager> makeDialogueManager(
        const Compiler::Extensions& extensions, Translation::Storage& translationDataStorage)
    {
        return std::make_unique<MWDialogue::DialogueManager>(extensions, translationDataStorage);
    }

    std::pair<int, int> compileAllDialogueScripts(const Compiler::Extensions* extensions, int warningsMode)
    {
        return MWDialogue::ScriptTest::compileAll(extensions, warningsMode);
    }
}
