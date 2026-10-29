// vsgopenmw-unity-build

#include <filesystem>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include <components/files/conversion.hpp>
#include <components/l10n/manager.hpp>
#include <components/l10n/messagebundles.hpp>

#include "choice.hpp"
#include "statemanagerimp.hpp"

namespace MWState
{
    // Offers the save most recently loaded or written, as OpenMW 0.51 does (bug #7617).
    class AskLoadRecent : public Choice
    {
        StateManager& mStateManager;
        const Character* mCharacter;
        std::filesystem::path mLastSavegame;

    public:
        AskLoadRecent(StateManager& manager, const MWState::Character* character, std::filesystem::path lastSavegame)
            : mStateManager(manager)
            , mCharacter(character)
            , mLastSavegame(std::move(lastSavegame))
        {
            buttons = { "#{Interface:Yes}", "#{Interface:No}" };

            std::string saveName = Files::pathToUnicodeString(mLastSavegame.filename());
            // Assume the last saved game belongs to the current character's slot list.
            if (mCharacter)
            {
                for (const auto& slot : *mCharacter)
                {
                    if (slot.mPath == mLastSavegame)
                    {
                        saveName = slot.mProfile.mDescription;
                        break;
                    }
                }
            }

            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            message = l10n->formatMessage("AskLoadLastSave", { "save" }, { L10n::toUnicode(saveName) });
        }
        bool run(float dt) override
        {
            if (mStateManager.hasQuitRequest())
                return false;
            if (!Choice::run(dt))
            {
                if (pressedButton == 0 && mCharacter)
                    mStateManager.loadGame(mCharacter, mLastSavegame);
                else
                    mStateManager.mainMenu();
                return false;
            }
            return true;
        }
    };
}
