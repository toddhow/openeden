#ifndef VSGOPENMW_MWGUI_LOADINGSCREEN_H
#define VSGOPENMW_MWGUI_LOADINGSCREEN_H

#include "windowbase.hpp"

namespace VFS
{
    class Manager;
}
namespace MWGui
{
    class BackgroundImage;

    class LoadingScreen : public WindowBase
    {
    public:
        LoadingScreen(const VFS::Manager* vfs);
        bool stretch{};

        void setLabel(const std::string& label, bool offset);
        void update(float complete, float dt, bool showWallpaper);

    private:
        float mChangeWallpaperCountdown = 0;

        MyGUI::Widget* mLoadingBox;

        MyGUI::TextBox* mLoadingText;
        MyGUI::EditBox* mTipText; // task 0207
        MyGUI::Widget* mTipBackground; // task 0207: black behind the tip, where the wallpaper would be
        std::string mShownTip;
        MyGUI::ScrollBar* mProgressBar;
        BackgroundImage* mSplashImage;

        std::vector<std::string> mSplashScreens;
    };
}

#endif
