#ifndef VSGOPENMW_ENGINE_H
#define VSGOPENMW_ENGINE_H

#include <functional>

#include <vsg/core/ref_ptr.h>

#include <components/files/collections.hpp>
#include <components/translation/translation.hpp>

#include "mwbase/environment.hpp"

#include "arguments.hpp"

namespace Resource
{
    class ResourceSystem;
}
namespace VFS
{
    class Manager;
}

namespace L10n
{
    class Manager;
}

namespace Files
{
    struct ConfigurationManager;
}

namespace MWState
{
    class StateManager;
    class GameState;
}
namespace MWGui
{
    class WindowManager;
}
namespace MWInput
{
    class InputManager;
}
namespace MWSound
{
    class SoundManager;
}
namespace MWRender
{
    class RenderManager;
}
namespace MWLua
{
    class LuaManager;
}
namespace Render
{
    class Engine;
}
struct SDL_Window;

namespace OMW
{
    struct WindowListener;

    /*
     * Runs game states.
     */
    class Engine
    {
        friend class Boot;
        friend class Game;
        friend class WindowListener;

        std::unique_ptr<Render::Engine> mRenderEngine;
        SDL_Window* mWindow{};
        std::unique_ptr<VFS::Manager> mVFS;
        std::unique_ptr<Resource::ResourceSystem> mResourceSystem;
        std::unique_ptr<MWInput::InputManager> mInputManager;
        std::unique_ptr<MWSound::SoundManager> mSoundManager;
        std::unique_ptr<MWRender::RenderManager> mRenderManager;
        // openeden-vsg: created by the Game state, owned here so that, as in OpenMW 0.51, it is destroyed after the
        // window manager (GUI windows hold Lua-built widgets) and after the Game state's World.
        std::unique_ptr<MWLua::LuaManager> mLuaManager;
        // True while a Game state exists: Engine::frame runs Lua every frame, as OpenMW 0.51 did, but only with a World.
        bool mLuaActive = false;
        std::unique_ptr<MWGui::WindowManager> mWindowManager;
        std::unique_ptr<MWState::StateManager> mStateManager;
        std::unique_ptr<L10n::Manager> mL10nManager;
        // openeden-vsg: set by Boot; stops the L10n GMST lookup from reading the World's store once the Game state
        // destroys the World (L10n contexts keep their own copy of the lookup).
        std::function<void()> mDetachGmstLoader;
        MWBase::Environment mEnvironment;
        std::unique_ptr<ToUTF8::Utf8Encoder> mEncoder;
        Translation::Storage mTranslationDataStorage;
        Files::PathContainer mDataDirs;
        Files::Collections mFileCollections;
        Files::ConfigurationManager& mCfgMgr;

        bool mWindowVisible = true;
        std::unique_ptr<WindowListener> mWindowListener;

        // not implemented
        Engine(const Engine&);
        Engine& operator=(const Engine&);

        bool frame(float dt, std::shared_ptr<MWState::GameState>& state);

        void prepareEngine(const Arguments& args);

        void createRenderEngine();
        void setWindowIcon(const std::filesystem::path& file);

    public:
        Engine(Files::ConfigurationManager& configurationManager);
        ~Engine();

        /// Initialise and enter main loop.
        void go(const Arguments& args);
    };
}

#endif
