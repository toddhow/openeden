#include "menu.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwgui/sessionpolicy.hpp"
#include "../mwsound/constants.hpp"

namespace MWState
{
    Menu::Menu()
        : Gui(MWGui::GM_MainMenu)
    {
    }

    bool Menu::run(float dt)
    {
        auto state = MWBase::Environment::get().getStateManager()->getState();
        auto soundMgr = MWBase::Environment::get().getSoundManager();

        // TODO: could we split up this class into Menu/MenuSplash classes, and retire the StateManager::mState
        // variable?
        // Task 0215: a game pack's own menus or music replace Morrowind's title music (SoundManager::update plays the
        // pack's theme)
        if (state == MWBase::StateManager::State_NoGame && !soundMgr->isMusicPlaying()
            && !MWGui::SessionPresentation::currentPolicy().replacesTitleMusic())
        {
            soundMgr->streamMusic(MWSound::titleMusic, MWSound::MusicType::Normal);
        }

        return !hasQuitRequest() && Gui::run(dt);
    }
}
