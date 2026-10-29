#ifndef OPENMW_GAME_MWGUI_MAINMENU_H
#define OPENMW_GAME_MWGUI_MAINMENU_H

#include <memory>

#include "windowbase.hpp"

namespace Gui
{
    class ImageButton;
}

namespace VFS
{
    class Manager;
}

namespace MWGui
{
    class BackgroundImage;
    class SaveGameDialog;
    class VideoWidget;

    class MainMenu : public WindowBase
    {
    public:
        MainMenu(int w, int h, const VFS::Manager* vfs, const std::string& versionDescription);
        ~MainMenu();

        void onResChange(int w, int h) override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

        void setVisible(bool visible) override;

        void onFrame(float dt) override;

        bool exit() override;
        bool stretch{};

    private:
        const VFS::Manager* mVFS;

        MyGUI::Widget* mButtonBox;
        MyGUI::TextBox* mVersionText;

        BackgroundImage* mBackground;
        VideoWidget* mVideo; // For animated main menus

        std::map<std::string, Gui::ImageButton*, std::less<>> mButtons;

        bool mHasAnimatedMenu;

        void onButtonClicked(MyGUI::Widget* sender);
        void updateMenu();
    };
}

#endif