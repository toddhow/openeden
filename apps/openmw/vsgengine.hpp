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
namespace UiRml
{
    class Overlay;
}
namespace OMW
{
    class UiInputScript;
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
    namespace MergedBodies
    {
        class Session;
    }
}
namespace MWLua
{
    class LuaManager;
}
namespace Render
{
    class Engine;
    class ScreenshotWriter;
}
struct SDL_Window;

namespace OMW
{
    struct WindowListener;
    class ExtensionHost;
    class PuddleBaker;

    /*
     * Runs game states.
     */
    class Engine
    {
        friend class Boot;
        friend class Game;
        friend class WindowListener;

        // First of all, so that it is destroyed last. MWBase::Environment::get() is null once it is gone, and things
        // that tear down late look managers up through it: a movie still playing at exit stops its audio track
        // through the sound manager. The OSG-era Engine got the same effect by resetting its managers in ~Engine.
        MWBase::Environment mEnvironment;
        // Next, so that it is destroyed last of the rest: a Game state still alive when the Engine goes uses it to
        // stop extensions. ~Engine stops extensions before any other member goes.
        std::unique_ptr<ExtensionHost> mExtensionHost;
        std::unique_ptr<Render::Engine> mRenderEngine;
        SDL_Window* mWindow{};
        std::unique_ptr<VFS::Manager> mVFS;
        std::unique_ptr<Resource::ResourceSystem> mResourceSystem;
        std::unique_ptr<MWInput::InputManager> mInputManager;
        std::unique_ptr<MWSound::SoundManager> mSoundManager;
        // Task 0104 (decision 0015): OpenEden's new UI drawn over the GUI, only with `[GUI] rmlui` on (and RmlUi built
        // in). Declared before the render manager so that it is destroyed after the graph that holds its node, and
        // after the render engine's other members that its Vulkan objects are released through.
        std::unique_ptr<UiRml::Overlay> mRmlUi;
        // Task 0111: the synthetic input of OPENEDEN_RMLUI_INPUT_SCRIPT (a test aid), and its frame counter.
        std::unique_ptr<UiInputScript> mUiScript;
        std::uint64_t mUiScriptFrame = 0;
        std::unique_ptr<MWRender::RenderManager> mRenderManager;
        // Task 0069 stage d: the merged bodies' cache and templates (GPU objects). Declared after the render engine, so
        // destroyed before it and its device.
        std::unique_ptr<MWRender::MergedBodies::Session> mMergedBodies;
        // OpenEden: the screenshots given to RenderManager's operation thread, which go() waits for after the frame
        // loop: the thread stops without running what is still queued when RenderManager goes. Made in go(), emptied
        // there after the wait; declared after the render manager, so destroyed before its thread.
        std::unique_ptr<Render::ScreenshotWriter> mScreenshotWriter;
        // Task 0095 stage W-b: the puddle map baker (--bake-puddles), only when asked for; ended in go().
        std::unique_ptr<PuddleBaker> mPuddleBaker;
        // openeden-vsg: created by the Game state, owned here so that, as in OpenMW 0.51, it is destroyed after the
        // window manager (GUI windows hold Lua-built widgets) and after the Game state's World.
        std::unique_ptr<MWLua::LuaManager> mLuaManager;
        // True while a Game state exists: Engine::frame runs Lua every frame, as OpenMW 0.51 did, but only with a
        // World.
        bool mLuaActive = false;
        std::unique_ptr<MWGui::WindowManager> mWindowManager;
        std::unique_ptr<MWState::StateManager> mStateManager;
        std::unique_ptr<L10n::Manager> mL10nManager;
        // openeden-vsg: set by Boot; stops the L10n GMST lookup from reading the World's store once the Game state
        // destroys the World (L10n contexts keep their own copy of the lookup).
        std::function<void()> mDetachGmstLoader;
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

        /// Zero, or non-zero when a check mode found problems (openmw --check-bodies).
        int exitCode() const { return mExitCode; }

    private:
        int mExitCode = 0;
    };
}

#endif
