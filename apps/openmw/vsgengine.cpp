#include "vsgengine.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <SDL_events.h>

#include <vsg/nodes/Group.h>
#include <vsg/threading/OperationThreads.h>
#include <vsg/vk/DeviceFeatures.h>
#include <vsgXchange/images.h>

#include <components/archivekinds/archivekinds.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/misc/quietwindow.hpp>
#include <components/render/attachmentformat.hpp>
#include <components/render/devicereport.hpp>
#include <components/render/engine.hpp>
#include <components/render/screenshot.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sdlutil/init.hpp>
#include <components/settings/shadermanager.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/ui_rml/overlay.hpp>
#include <components/version/version.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>
#include <components/vsgadapters/sdl/surface.hpp>
#include <components/vsgadapters/sdl/window.hpp>
#include <components/vsgutil/readimage.hpp>

#include "bodyanalyser.hpp"
#include "bodybaker.hpp"
#include "lodobjectbaker.hpp"
#include "lodterrainbaker.hpp"
#include "mwgui/windowmanagerimp.hpp"
#include "mwinput/actions.hpp"
#include "mwinput/inputmanagerimp.hpp"
#include "mwrender/bin.hpp"
#include "mwrender/mergedbody.hpp"
#include "mwrender/rendermanager.hpp"
#include "mwsound/soundmanagerimp.hpp"
#include "mwstate/loadingscreen.hpp"
#include "mwstate/statemanagerimp.hpp"
#include "probebaker.hpp"
#include "puddlebaker.hpp"
#include "skyvisbaker.hpp"

#include <components/render/defaultsampler.hpp>

#include "boot.hpp"
#include "extensionhost.hpp"

namespace
{
    void screenshot(Render::Engine& engine, Render::ScreenshotWriter& writer, const std::filesystem::path& path,
        vsg::ref_ptr<const vsg::Options> writeOptions)
    {
        // OpenEden: the frame being recorded is copied in its own submission, before it is presented, and handed over
        // once it has finished on the GPU, a few frames later (Render::FrameCapture); only then is it named and given
        // to the writer thread, through the writer that Engine::go waits for at quit. FrameCapture::finish delivers
        // the last copies when loop() ends, so this callback never runs after go() has left.
        const std::string ext = Settings::general().mScreenshotFormat;
        engine.requestScreenshot([&writer, path, writeOptions, ext](vsg::ref_ptr<Render::Screenshot> shot) {
            Render::nameAndQueueScreenshot(std::move(shot), path, ext, writeOptions,
                [&writer](vsg::ref_ptr<Render::Screenshot> named) { writer.write(std::move(named)); });
        });
    }

    // How long the game waits at quit, in all, for screenshots still queued for the writer thread: it writes one in
    // about 0.15 s at 1280 x 720 (components/render/CLAUDE.md, "Screenshots"), and it is shared with the cell
    // preloader.
    constexpr std::chrono::seconds sScreenshotWriteWait{ 10 };

}
namespace OMW
{
    struct WindowListener : public SDLUtil::WindowListener
    {
        Engine& mEngine;
        WindowListener(Engine& engine)
            : mEngine(engine)
        {
        }

        void windowVisibilityChange(bool visible)
        {
            mEngine.mWindowVisible = visible;
            if (!visible)
                mEngine.mSoundManager->pausePlayback();
            else
                mEngine.mSoundManager->resumePlayback();
        }
        void windowClosed() { mEngine.mStateManager->requestQuit(); }
        void windowResized(int x, int y)
        {
            Settings::Manager::setInt("resolution x", "Video", x);
            Settings::Manager::setInt("resolution y", "Video", y);
            mEngine.mWindowManager->windowResized(x, y);
            mEngine.mRenderManager->updateExtents(x, y);
        }
    };

    Engine::Engine(Files::ConfigurationManager& configurationManager)
        : mCfgMgr(configurationManager)
    {
        SDLUtil::init();
    }

    bool /*requiresRender*/ Engine::frame(float dt, std::shared_ptr<MWState::GameState>& gameState)
    {
        Misc::LoadTrace::Scope traceInput("frame.input");
        try
        {
            mInputManager->update(dt, gameState->disableControls, gameState->disableEvents);
        }
        catch (const std::exception& e)
        {
            std::cerr << "!InputManager::update(" << e.what() << ")" << std::endl;
        }

        if (mStateManager->getGameState() != gameState)
            return false;

        if (gameState->showCursor)
            mInputManager->enableMouse(*gameState->showCursor);

        if (!mWindowVisible)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return false;
        }
        traceInput.end();
        mExtensionHost->deliverWorldEvents();
        mExtensionHost->runPhase(openeden::sdk::Phase::Input, dt);
        if (gameState->updateSound)
            mSoundManager->update(dt);

        // openeden-vsg: as in OpenMW 0.51's Engine::frame, Lua runs every frame whatever game state is on top (the main
        // menu, a dialogue or the game): synchronizedUpdate applies the previous frame's queued changes before the
        // world updates, and update runs the scripts after the frame's game logic. In vsgopenmw both ran inside
        // Game::run, so menu scripts stopped while a menu state was on top.
        Misc::LoadTrace::Scope traceLuaSync("frame.lua.sync");
        const float luaDt = std::min(dt, 0.2f);
        if (mLuaActive && gameState->updateLua)
        {
            mExtensionHost->sendPendingLuaEvents();
            mLuaManager->synchronizedUpdate(luaDt);
        }
        traceLuaSync.end();
        // Delayed Lua actions such as teleports may push a state (ChangeCell); 0.51 changed the cell inside
        // synchronizedUpdate, so scripts must not run again until that state has finished.
        if (mStateManager->getGameState() != gameState)
            return false;

        Misc::LoadTrace::Scope traceRun("frame.game.run");
        try
        {
            if (!gameState->run(dt))
            {
                mStateManager->popGameState(gameState);
                return false;
            }
            traceRun.end();
            Misc::LoadTrace::Scope traceLuaUpdate("frame.lua.update");
            if (mLuaActive && gameState->updateLua)
                mLuaManager->update(luaDt);
        }
        catch (const std::exception& e)
        {
            std::cerr << "!GameState<" << typeid(*gameState).name() << ">::run(" << e.what() << ")" << std::endl;
            mStateManager->popGameState(gameState);
            return false;
        }
        // Here, not in the Game state, which does not run while the menu state is on top of it
        if (mWindowManager->containsMode(MWGui::GM_MainMenu))
        {
            if (const std::string report = mExtensionHost->takeFailureReport(); !report.empty())
            {
                Log(Debug::Info) << "Showing extension failure message";
                mWindowManager->messageBox("#{OMWEngine:ExtensionsFailed}\n" + report);
            }
        }
        Misc::LoadTrace::Scope traceGui("frame.gui");
        mExtensionHost->runPhase(openeden::sdk::Phase::PreRender, dt);
        mWindowManager->onFrame(dt);
        traceGui.end();
        mExtensionHost->runPhase(openeden::sdk::Phase::Ui, dt);
        // The extensions' debug draw of this frame, collected at the end of the Ui phase; null when it is off
        mRenderManager->setExtensionDebugDraw(mExtensionHost->debugDrawFrame());
        mRenderManager->setViewMode(MWRender::ViewMode::Scene, gameState->requiresScene);
        mRenderManager->syncExtents();
        if (mRmlUi)
        {
            const VkExtent2D extent = mRenderEngine->swapchainExtent();
            mRmlUi->setExtent(extent.width, extent.height);
        }
        Misc::LoadTrace::Scope traceRender("frame.render.onframe");
        if (gameState->requiresScene)
            mRenderManager->onFrame(dt);
        return gameState->requiresRender;
    }

    Engine::~Engine()
    {
        Misc::LoadTrace::stop();
        if (mExtensionHost)
            mExtensionHost->stop();

        if (mWindow)
        {
            SDL_DestroyWindow(mWindow);
            mWindow = nullptr;
        }

        SDL_Quit();
    }

    void Engine::createRenderEngine()
    {
        int screen = Settings::video().mScreen;
        int width = Settings::video().mResolutionX;
        int height = Settings::video().mResolutionY;
        bool fullscreen = Settings::video().mWindowMode.get() != Settings::WindowMode::Windowed;
        bool windowBorder = Settings::video().mWindowBorder;

        int pos_x = SDL_WINDOWPOS_CENTERED_DISPLAY(screen), pos_y = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);

        if (fullscreen)
        {
            pos_x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
            pos_y = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
        }

        bool indirect = getenv("VSGOPENMW_INDIRECT") != 0;
        if (indirect)
            std::cout << "Using VSGOPENMW_INDIRECT - expect lower performance." << std::endl;

        Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE; // SDL_WINDOW_ALLOWHIGHDPI
        if (!indirect)
            flags |= SDL_WINDOW_VULKAN;
        if (fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;

        if (!windowBorder)
            flags |= SDL_WINDOW_BORDERLESS;

        SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, Settings::video().mMinimizeOnFocusLoss ? "1" : "0");

        // OpenEden: test and check runs set OPENEDEN_QUIET_WINDOW, so the window opens far off the desktop, out of
        // the taskbar and without taking focus. It is not minimised: a minimised window has a 0x0 swapchain and
        // draws nothing.
        // Unset, empty or "0" means off (Misc::isQuietWindowRequested, shared with the editor and the crash catcher).
        if (!fullscreen && Misc::isQuietWindowRequested())
        {
            constexpr int quietWindowPosition = -30000; // far enough to be off every monitor layout
            pos_x = quietWindowPosition;
            pos_y = quietWindowPosition;
            flags |= SDL_WINDOW_SKIP_TASKBAR;
            SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
        }

        mWindow = SDL_CreateWindow("OpenMW", pos_x, pos_y, width, height, flags);
        if (!mWindow)
            throw std::runtime_error(SDL_GetError());

#if 0
    // Since we /*vsgopenmw-fixme*/ use physical resolution internally, we have to create the window with scaled resolution,
    // but we can't get the scale before the window exists, so instead we have to resize aftewards.
   int w,h;
   SDL_GetWindowSize(mWindow, &w, &h);
   int dw,dh;
   SDL_GL_GetDrawableSize(mWindow, &dw, &dh);
   if (dw != w || dh != h)
       SDL_SetWindowSize(mWindow, width / (dw / w), height / (dh / h));
#endif

        int vsync = Settings::video().mVsyncMode;
        unsigned int antialiasing = Settings::video().mAntialiasing;

        auto traits = vsg::WindowTraits::create();
        // if we are multisampling then to enable copying of the depth buffer we have to enable a depth buffer resolve
        // extension for vsg::RenderPass
        if (antialiasing)
        {
            traits->deviceExtensionNames.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME); /* .dependencies = */
            traits->deviceExtensionNames.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
            traits->deviceExtensionNames.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);

            traits->deviceExtensionNames.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
        }
        traits->width = width;
        traits->height = height;
        traits->debugLayer = getenv("VSGOPENMW_VALIDATION") != 0;
        if (traits->debugLayer && vsg::isExtensionSupported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            traits->instanceExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        traits->queueFlags |= VK_QUEUE_COMPUTE_BIT;
        traits->depthFormat = Render::compatibleDepthFormat;
        // OpenEden: post-processing copies its result back to the swapchain image (task 0012, V4p).
        if (Settings::postProcessing().mEnabled)
            traits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        traits->swapchainPreferences.presentMode
            = vsync != 0 ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
        // if (vsync == 2) {}
        traits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // enableScreenshot
        traits->depthImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // enableReflectionDepth
        traits->swapchainPreferences.surfaceFormat.format = Render::compatibleColorFormat;
        if (antialiasing)
            traits->samples = antialiasing;

        if (!indirect)
        {
            vsg::ref_ptr<vsg::WindowAdapter> adapter(new vsgAdapters::sdl::Window(mWindow, traits));
            mRenderEngine = std::make_unique<Render::Engine>(adapter);
        }
        else
        {
            auto callback = [this](vsg::ref_ptr<vsg::Data> data) {
                vsgAdapters::sdl::fillSurface(SDL_GetWindowSurface(mWindow), data);
                SDL_UpdateWindowSurface(mWindow);
            };
            mRenderEngine = std::make_unique<Render::Engine>(callback, traits);
        }
        bool useFrameQueue = (getenv("VSGOPENMW_NO_FRAME_QUEUE") == nullptr);
        if (!useFrameQueue)
            std::cout << "Using VSGOPENMW_NO_FRAME_QUEUE - expect lower performance." << std::endl;
        mRenderEngine->useFrameQueue = useFrameQueue;

        if (mRenderEngine->getDevice())
            throw std::runtime_error("!requestDeviceFeatures(device != nullptr)");
        const auto& supportedFeatures = mRenderEngine->getOrCreatePhysicalDevice()->getFeatures();
        traits->deviceFeatures = vsg::DeviceFeatures::create();
        auto& featureStruct = traits->deviceFeatures->get();
        if (supportedFeatures.samplerAnisotropy)
            featureStruct.samplerAnisotropy = VK_TRUE;
        if (supportedFeatures.occlusionQueryPrecise)
            featureStruct.occlusionQueryPrecise = VK_TRUE;
        if (supportedFeatures.depthClamp)
            featureStruct.depthClamp = VK_TRUE;
        // if (supportedFeatures.fillModeNonSolid
        featureStruct.fillModeNonSolid = true;
    }

    void Engine::setWindowIcon(const std::filesystem::path& file)
    {
        auto options = vsg::Options::create();
        options->readerWriters = { vsgXchange::images::create() };
        if (auto image = vsgUtil::readOptionalImage(Files::pathToUnicodeString(file), options))
        {
            if (auto surface = vsgAdapters::sdl::createSurface(image))
                SDL_SetWindowIcon(mWindow, surface.get());
        }
    }

    void Engine::prepareEngine(const Arguments& args)
    {
        if (Settings::cells().mLoadTrace)
        {
            const char* tracePath = std::getenv("OPENEDEN_LOAD_TRACE_FILE");
            Misc::LoadTrace::start(tracePath != nullptr
                    ? std::string(tracePath)
                    : Files::pathToUnicodeString(mCfgMgr.getUserDataPath() / "load-trace.jsonl"));
        }
        createRenderEngine();

        Misc::Rng::init(args.randomSeed);

        Settings::ShaderManager::get().load(mCfgMgr.getUserConfigPath() / "shaders.yaml");
        mExtensionHost = std::make_unique<ExtensionHost>(ExtensionHost::Config{
            { mCfgMgr.getLocalPath() / "extensions", mCfgMgr.getUserDataPath() / "extensions",
                mCfgMgr.getLocalPath() / "game_packs", mCfgMgr.getUserDataPath() / "game_packs" },
            args.extensions, args.gamePack, args.builtInGamePacks, Settings::game().mNeutralWorld.get() });
        mEnvironment.setExtensionManager(*mExtensionHost);

        mVFS = std::make_unique<VFS::Manager>();

        mDataDirs = args.dataDirs;
        mCfgMgr.filterOutNonExistingPaths(mDataDirs);
        mDataDirs.insert(mDataDirs.begin(), args.resourceDir / "vfs");
        mFileCollections = Files::Collections(mDataDirs);
        // OpenMW 0.51 decodes archive file names with the content encoding.
        mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(args.encoding);
        VFS::registerBethesdaArchiveKinds();
        if (mExtensionHost->dataDirs().empty())
            VFS::registerArchives(mVFS.get(), mFileCollections, args.archives, true, &mEncoder->getStatelessEncoder());
        else
        {
            // Extension data goes above data= folders and below data-local (task 0016). They are data folders in full,
            // so an extension can carry content files such as an .omwscripts.
            Files::PathContainer vfsDirs = mDataDirs;
            auto insertAt = std::find(vfsDirs.begin(), vfsDirs.end(), args.dataLocal);
            for (const openeden::extension::ExtensionDataDir& dir : mExtensionHost->dataDirs())
                insertAt = std::next(vfsDirs.insert(insertAt, dir.directory));
            VFS::registerArchives(
                mVFS.get(), Files::Collections(vfsDirs), args.archives, true, &mEncoder->getStatelessEncoder());

            VFS::Manager gameData;
            VFS::registerArchives(&gameData, mFileCollections, args.archives, true, &mEncoder->getStatelessEncoder());
            mExtensionHost->reportDataOverrides(gameData);

            mDataDirs = std::move(vfsDirs);
            mFileCollections = Files::Collections(mDataDirs);
        }
        mExtensionHost->setEngineVfs(mVFS.get());
        // Task 0107 (neutral world stage N3a-2b): the game pack is given the content list; nothing happens with
        // [Game] neutral world off.
        mExtensionHost->setSessionContent(args.contentFiles, mFileCollections);

        // OpenEden, 2026-11-09: keep the driver's compiled pipelines between runs
        // (components/render/pipelinecache.hpp).
        if (Settings::rendering().mPipelineCache)
            mRenderEngine->enablePipelineCache(mCfgMgr.getUserDataPath() / "openeden" / "shadercache");

        auto physicalDevice = mRenderEngine->getOrCreatePhysicalDevice();
        bool useTextureCompression = physicalDevice->getFeatures().textureCompressionBC;
        if (auto overrideString = getenv("VSGOPENMW_DECOMPRESS_BC"))
            useTextureCompression = overrideString == std::string("0");
        mResourceSystem = std::make_unique<Resource::ResourceSystem>(mVFS.get(),
            Files::pathToUnicodeString(args.resourceDir / "shaders"),
            Render::createDefaultSampler(physicalDevice, mRenderEngine->getEnabledFeatures().samplerAnisotropy),
            useTextureCompression, MWRender::Bin_Compute, MWRender::Bin_DepthSorted,
            // Task 0072, stage h: where the Slang compiler in the game keeps what it compiled at run time.
            Files::pathToUnicodeString(mCfgMgr.getUserDataPath() / "openeden" / "shadercache"));

        setWindowIcon(args.resourceDir / "openmw.png");

        // OpenEden (task 0023, F0c): what the machine's Vulkan supports, to the log and a JSON file. Off by default.
        if (Settings::rendering().mDeviceReport)
        {
            const Render::DeviceReport report = mRenderEngine->deviceReport();
            Log(Debug::Info) << "Vulkan device report:\n" << report.toLogText();
            const std::filesystem::path reportPath = mCfgMgr.getUserDataPath() / "device-report.json";
            std::error_code error;
            std::filesystem::create_directories(reportPath.parent_path(), error);
            std::ofstream out(reportPath, std::ios::binary);
            if (out << report.toJson())
                Log(Debug::Info) << "Wrote " << Files::pathToUnicodeString(reportPath);
            else
                Log(Debug::Warning) << "Could not write " << Files::pathToUnicodeString(reportPath);
        }

        // vsgopenmw-fixme(global-state)
        mEnvironment.setResourceSystem(*mResourceSystem);

        // Create input and UI first to set up a bootstrapping environment for
        // showing a loading screen and keeping the window responsive while doing so

        // vsgopenmw-move-me(mwinput) {
        auto keybinderUser = mCfgMgr.getUserConfigPath() / "input_v3.xml";
        bool keybinderUserExists = std::filesystem::exists(keybinderUser);
        if (!keybinderUserExists)
        {
            auto input2 = mCfgMgr.getUserConfigPath() / "input_v2.xml";
            if (std::filesystem::exists(input2))
            {
                std::filesystem::copy_file(input2, keybinderUser);
                keybinderUserExists = std::filesystem::exists(keybinderUser);
                std::cout << "Loading keybindings file: " << keybinderUser << std::endl;
            }
        }
        else
            std::cout << "Loading keybindings file: " << keybinderUser << std::endl;

        auto userdefault = mCfgMgr.getUserConfigPath() / "gamecontrollerdb.txt";
        auto localdefault = mCfgMgr.getLocalPath() / "gamecontrollerdb.txt";
        auto globaldefault = mCfgMgr.getGlobalPath() / "gamecontrollerdb.txt";

        std::filesystem::path userGameControllerdb;
        if (std::filesystem::exists(userdefault))
        {
            userGameControllerdb = userdefault;
        }
        else
            userGameControllerdb = "";

        std::filesystem::path gameControllerdb;
        if (std::filesystem::exists(localdefault))
            gameControllerdb = localdefault;
        else if (std::filesystem::exists(globaldefault))
            gameControllerdb = globaldefault;
        //}

        mTranslationDataStorage.setEncoder(mEncoder.get());
        for (auto& f : args.contentFiles)
            mTranslationDataStorage.loadTranslationData(mFileCollections, f);

        mL10nManager = std::make_unique<L10n::Manager>(mVFS.get());
        mL10nManager->setPreferredLocales(Settings::general().mPreferredLocales);
        mEnvironment.setL10nManager(*mL10nManager);

        auto compile = mRenderEngine->createCompileContext();

        mWindowManager = std::make_unique<MWGui::WindowManager>(mWindow, compile, mResourceSystem.get(),
            mCfgMgr.getLogPath(), args.scriptConsoleMode, mTranslationDataStorage, args.encoding,
            Version::getOpenmwVersionDescription(), mCfgMgr);
        mEnvironment.setWindowManager(*mWindowManager);

        // As in OpenMW, the Lua manager exists before the input manager: loading screens pump input events, and every
        // input handler passes them to Lua (inputEvent only queues them). Game::Game initialises it once the world
        // exists.
        mLuaManager = std::make_unique<MWLua::LuaManager>(mVFS.get(), args.resourceDir / "lua_libs");
        mEnvironment.setLuaManager(*mLuaManager);

        mInputManager = std::make_unique<MWInput::InputManager>(
            mWindow, keybinderUser, keybinderUserExists, userGameControllerdb, gameControllerdb, args.grabMouse);
        mWindowListener = std::make_unique<WindowListener>(*this);
        mInputManager->setWindowListener(mWindowListener.get());
        mEnvironment.setInputManager(*mInputManager);

        // Task 0104: with `[GUI] rmlui` the RmlUi overlay is drawn after the game's own GUI. Off: nothing is created.
        vsg::ref_ptr<vsg::Node> guiRoot{ mWindowManager->node() };
        if (Settings::gui().mRmlui)
        {
#ifdef OPENEDEN_HAS_RMLUI
            try
            {
                UiRml::OverlayConfig overlayConfig;
                overlayConfig.mBitmapFont = std::string(Fallback::Map::getString("Fonts_Font_0"));
                overlayConfig.mEncoding = args.encoding;
                const VkExtent2D extent = mRenderEngine->extent2D();
                mRmlUi = std::make_unique<UiRml::Overlay>(
                    compile, mResourceSystem->shaderOptions, *mVFS, overlayConfig, extent.width, extent.height);
                auto group = vsg::Group::create();
                group->addChild(guiRoot);
                group->addChild(mRmlUi->node());
                guiRoot = group;
                Log(Debug::Info) << "[UI] RmlUi overlay on ([GUI] rmlui)";
            }
            catch (const std::exception& e)
            {
                mRmlUi.reset();
                Log(Debug::Error) << "[UI] [GUI] rmlui is on, but the overlay could not be created: " << e.what();
            }
#else
            Log(Debug::Warning) << "[UI] [GUI] rmlui is on, but this build has no RmlUi (OPENEDEN_BUILD_RMLUI is off)";
#endif
        }

        mRenderManager = std::make_unique<MWRender::RenderManager>(
            *mRenderEngine, guiRoot, mResourceSystem.get() /*, *mNavigator*/);

        mRenderManager->setLodTerrainPack(mCfgMgr.getUserDataPath() / "openeden" / "lod" / "terrain.edenpak");
        mRenderManager->setLodClusterPack(mCfgMgr.getUserDataPath() / "openeden" / "lod" / "objects.edenpak");
        // Task 0069 stage d: the merged body cache ([Bodies] merged body) lives under <user data>/openeden/bodies.
        mMergedBodies = std::make_unique<MWRender::MergedBodies::Session>(mCfgMgr.getUserDataPath());

        mSoundManager = std::make_unique<MWSound::SoundManager>(mVFS.get(), args.useSound);
        mEnvironment.setSoundManager(*mSoundManager);

        mStateManager = std::make_unique<MWState::StateManager>(
            mCfgMgr.getUserDataPath() / "saves", args.contentFiles, mRenderManager->getScreenshotInterface());
        mEnvironment.setStateManager(*mStateManager);

        auto loadingScreen = mWindowManager->createLoadingScreen();
        loadingScreen->loading = std::make_shared<Boot>(*this, args);
        mStateManager->pushGameState(loadingScreen);
    }

    void Engine::go(const Arguments& args)
    {
        try
        {
            prepareEngine(args);
            mRenderEngine->compile();
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            return;
        }

        // Reference / A/B automation counters. Count only frames where the world is
        // in State_Running (skips loading screens etc.). Fire once for the screenshot.
        unsigned int runningFrame = 0;
        // Task 0028: the light probe baker (--bake-gi), only when asked for.
        std::unique_ptr<ProbeBaker> probeBaker;
        if (!args.bakeGiCell.empty())
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            probeBaker = std::make_unique<ProbeBaker>(args.bakeGiCell, out, *mRenderManager);
        }
        // Task 0036: the sky visibility baker (--bake-gi-exterior), only when asked for.
        std::unique_ptr<SkyVisBaker> skyVisBaker;
        if (!args.bakeGiExterior.empty())
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            skyVisBaker = std::make_unique<SkyVisBaker>(args.bakeGiExterior, out, *mRenderManager);
        }
        bool autoScreenshotFired = false;
        // OpenEden: the screenshots given to RenderManager's operation thread, waited for after the loop: the thread
        // stops without running what is still queued (vsg::OperationThreads::stop) when RenderManager goes.
        auto writerThreads = mRenderManager->getOperationThreads();
        mScreenshotWriter = std::make_unique<Render::ScreenshotWriter>(
            [writerThreads](vsg::ref_ptr<Render::Screenshot> screenshot) { writerThreads->add(screenshot); });
        bool autoQuitFired = false;
        // Task 0055: the world LOD terrain baker (--bake-lod-terrain), only when asked for.
        std::unique_ptr<LodTerrainBaker> lodTerrainBaker;
        if (args.bakeLodTerrain)
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            lodTerrainBaker = std::make_unique<LodTerrainBaker>(out, *mResourceSystem);
        }
        // Task 0095 stage W-b: the puddle map baker (--bake-puddles), only when asked for.
        if (!args.bakePuddles.empty())
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            mPuddleBaker = std::make_unique<PuddleBaker>(args.bakePuddles, out, *mResourceSystem);
        }
        // Task 0059: the world LOD object cluster baker (--bake-lod-objects), only when asked for.
        std::unique_ptr<LodObjectBaker> lodObjectBaker;
        if (args.bakeLodObjects)
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            lodObjectBaker = std::make_unique<LodObjectBaker>(out, *mResourceSystem);
        }
        // The body tools' runs draw stock bodies whatever the user's [Bodies] merged body says (task 0069 review).
        if (args.analyseBodies || args.bakeBodies || args.checkBodies)
            MWRender::MergedBodies::disableForThisRun();
        // Task 0069: the stock body analysis (--analyse-bodies), only when asked for.
        std::unique_ptr<BodyAnalyser> bodyAnalyser;
        if (args.analyseBodies)
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            bodyAnalyser = std::make_unique<BodyAnalyser>(out, *mResourceSystem);
        }
        // Task 0069 stage c: the merged body cache (--bake-bodies) and its check (--check-bodies).
        std::unique_ptr<BodyBaker> bodyBaker;
        if (args.bakeBodies || args.checkBodies)
        {
            const std::filesystem::path out
                = args.bakeGiOut.empty() ? mCfgMgr.getUserDataPath() : std::filesystem::path(args.bakeGiOut);
            bodyBaker = std::make_unique<BodyBaker>(
                args.bakeBodies ? BodyBaker::Mode::Bake : BodyBaker::Mode::Check, out, *mResourceSystem);
        }
        auto callback
            = [this, &args, &probeBaker, &skyVisBaker, &lodTerrainBaker, &lodObjectBaker, &bodyAnalyser, &bodyBaker,
                  &runningFrame, &autoScreenshotFired, &autoQuitFired](double dt, bool& requiresRender, bool& quit) {
                  auto state = mStateManager->getGameState();
                  quit = !state;
                  if (quit)
                      return;
                  requiresRender = frame(dt, state);

                  // openeden-vsg: loading and cell changes are game states of their own here (the OSG build loaded
                  // inside one frame), so frames under a loading screen or an operation do not count.
                  if (mStateManager->getState() == MWBase::StateManager::State_Running && state->requiresScene
                      && state->updateLua)
                  {
                      ++runningFrame;
                      if (probeBaker && probeBaker->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      if (skyVisBaker && skyVisBaker->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      if (lodTerrainBaker && lodTerrainBaker->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      if (mPuddleBaker && mPuddleBaker->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      if (lodObjectBaker && lodObjectBaker->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      if (bodyAnalyser && bodyAnalyser->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      if (bodyBaker && bodyBaker->update() && !autoQuitFired)
                      {
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                      // Test aid (task 0017): opens the debug window at running frame 20, so that a
                      // --auto-screenshot shows its Extensions tab (OPENEDEN_DEBUG_WINDOW_TAB=extensions, see
                      // docs/debug-environment-variables.md)
                      if (runningFrame == 20 && std::getenv("OPENEDEN_OPEN_DEBUG_WINDOW") != nullptr)
                          mInputManager->executeAction(MWInput::A_ToggleDebug);
                      if (args.autoScreenshotFrame && !autoScreenshotFired && runningFrame >= args.autoScreenshotFrame)
                      {
                          Log(Debug::Info) << "Auto-screenshot at running frame " << runningFrame;
                          mInputManager->executeAction(MWInput::A_Screenshot);
                          autoScreenshotFired = true;
                      }
                      if (args.autoQuitFrame && !autoQuitFired && runningFrame >= args.autoQuitFrame)
                      {
                          Log(Debug::Info) << "Auto-quit at running frame " << runningFrame;
                          mStateManager->requestQuit();
                          autoQuitFired = true;
                      }
                  }
                  // OpenEden: a screenshot asked for during this callback (F12 in the input update, Lua's
                  // takeScreenshot when synchronizedUpdate applies it, --auto-screenshot above) is copied from the
                  // frame recorded next, the one this callback prepared. Until 2026-11-04 it was read at the start of
                  // frame() and copied from the image presented last, which Vulkan forbids using until it is acquired
                  // again.
                  if (mInputManager->getScreenshotRequest())
                      screenshot(*mRenderEngine, *mScreenshotWriter, mCfgMgr.getScreenshotPath(),
                          mResourceSystem->imageOptions);
              };
        mRenderEngine->loop(callback, Settings::video().mFramerateLimit);
        if (bodyBaker && bodyBaker->problems() > 0)
            mExitCode = 3;
        // OpenEden: a screenshot asked for just before quitting is still queued; it was dropped without a word.
        // The main thread is not in the frame loop while it waits: pump the window's messages every half second so that
        // the freeze monitor, which calls a window hung after 5 s without them, does not end the game (round four,
        // finding 2).
        for (const auto& unwritten : mScreenshotWriter->waitForAll(sScreenshotWriteWait, [] { SDL_PumpEvents(); }))
            Log(Debug::Warning) << "Screenshot not written before quitting: "
                                << Files::pathToUnicodeString(unwritten->writeFilename);
        mScreenshotWriter.reset();
        mPuddleBaker.reset();

        std::cout << "Quitting peacefully." << std::endl;

        mExtensionHost->stop();

        Settings::Manager::saveUser(mCfgMgr.getUserConfigPath() / "settings.cfg");
        Settings::ShaderManager::get().save();
    }
}
