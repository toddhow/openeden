#include "loadingscreen.hpp"

#include <vsg/threading/OperationThreads.h>

#include "../mwbase/windowmanager.hpp"
#include "../mwgui/loadingscreen.hpp"
#include "shaderwaitlabel.hpp"

namespace MWState
{
    LoadingScreen::LoadingScreen(MWGui::LoadingScreen* window)
        : Gui(MWGui::GM_Loading)
        , mWindow(window)
    {
        setWallpaper(true);
        disableControls = true;
        disableEvents = true;
        updateLua = false;
    }

    bool LoadingScreen::run(float dt)
    {
        Gui::run(dt);
        if (hasQuitRequest())
            loading->abort = true;

        static auto threads = vsg::OperationThreads::create(1); // vsgopenmw-fixme(find-my-place)
        loading->threads = threads;
        if (!loading->run(dt))
        {
            quit();
            return false;
        }

        // Task 0363, stage f: while a thread waits for a shader to compile the screen says so.
        const int waiters = shaderWaiters();
        auto description = loadingScreenText(loading->getDescription(), waiters);
        setShaderLabelShown(waiters > 0);
        requiresRender = !description.empty();
        if (requiresRender)
        {
            mWindow->setLabel(description, mWm.getMessagesCount() > 0);
            mWindow->update(loading->getComplete(), dt, wallpaper);
        }
        return true;
    }
}
