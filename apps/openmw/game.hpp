// vsgopenmw-unity-build

#include <memory>

#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/lua_ui/util.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include "mwdialogue/dialoguemanagerimp.hpp"
#include "mwdialogue/journalimp.hpp"
#include "mwdialogue/scripttest.hpp"
#include "mwgui/windowmanagerimp.hpp"
#include "mwlua/luamanagerimp.hpp"
#include "mwmechanics/creaturestats.hpp"
#include "mwmechanics/mechanicsmanagerimp.hpp"
#include "mwscript/interpretercontext.hpp"
#include "mwscript/scriptmanagerimp.hpp"
#include "mwstate/gamestate.hpp"
#include "mwstate/statemanagerimp.hpp"
#include "mwworld/class.hpp"
#include "mwworld/datetimemanager.hpp"
#include "mwworld/localscripts.hpp"
#include "mwworld/worldimp.hpp"
#include "mwworld/scene.hpp"
#include "mwclass/classes.hpp"
#include "mwrender/rendermanager.hpp"

#include "pluginhost.hpp"
#include "vsgengine.hpp"

namespace OMW
{
    /*
     * Updates running game.
     */
    class Game : public MWState::GameState
    {
        void executeLocalScripts(float dt)
        {
            auto& localScripts = mWorld->getLocalScripts();
            localScripts.startIteration();
            std::pair<ESM::RefId, MWWorld::Ptr> script;
            while (localScripts.getNext(script))
            {
                MWScript::InterpreterContext interpreterContext(&script.second.getRefData().getLocals(), script.second);
                interpreterContext.dt = dt;
                mScriptManager->run(script.first, interpreterContext);
            }
        }

        Engine& mEngine;
        MWBase::Environment& mEnvironment;
        Compiler::Extensions mExtensions;
        std::unique_ptr<Compiler::Context> mScriptContext;
        std::unique_ptr<MWWorld::World> mWorld;
        std::unique_ptr<MWScript::ScriptManager> mScriptManager;
        std::unique_ptr<MWDialogue::Journal> mJournal;
        std::unique_ptr<MWDialogue::DialogueManager> mDialogueManager;
        std::unique_ptr<MWMechanics::MechanicsManager> mMechanicsManager;
        MWLua::LuaManager* mLuaManager{}; // owned by Engine::mLuaManager

    public:
        Game(Engine& e, std::unique_ptr<MWWorld::World>& world, const Arguments& args)
            : mEngine(e)
            , mEnvironment(e.mEnvironment)
            , mWorld(std::move(world))
        {
            mEngine.mLuaManager = std::make_unique<MWLua::LuaManager>(mEngine.mVFS.get(), args.resourceDir / "lua_libs");
            mLuaManager = mEngine.mLuaManager.get();
            mEnvironment.setLuaManager(*mLuaManager);

            Compiler::registerExtensions(mExtensions);
            mScriptContext.reset(new MWScript::CompilerContext(MWScript::CompilerContext::Type_Full));
            mScriptContext->setExtensions(&mExtensions);
            mScriptManager = std::make_unique<MWScript::ScriptManager>(
                mEnvironment.getWorld()->getStore(), *mScriptContext, args.warningsMode);
            mEnvironment.setScriptManager(*mScriptManager);

            mMechanicsManager = std::make_unique<MWMechanics::MechanicsManager>();
            mMechanicsManager->setUpdateThreads(mEngine.mRenderManager->getUpdateThreads());
            mEnvironment.setMechanicsManager(*mMechanicsManager);

            mJournal = std::make_unique<MWDialogue::Journal>();
            mEnvironment.setJournal(*mJournal);

            mDialogueManager
                = std::make_unique<MWDialogue::DialogueManager>(mExtensions, mEngine.mTranslationDataStorage);
            mEnvironment.setDialogueManager(*mDialogueManager);

            // As in OpenMW 0.51, the player is set up before Lua is initialised, so its local scripts start with a new or
            // loaded game (World::clear calls setupPlayer again) rather than at the main menu with storage inactive.
            mWorld->setupPlayer();

            auto& wm = mEngine.mWindowManager;
            wm->setStore(mEnvironment.getWorld()->getStore());
            wm->initUI(mEngine.mRenderManager->getMap(), mEngine.mRenderManager->getWorldMap(), mEngine.mRenderManager->getPreview());

            // As in OpenMW 0.51, permanent storage is loaded before the load scripts run (LuaManager::init).
            mLuaManager->loadPermanentStorage(mEngine.mCfgMgr.getUserConfigPath());
            mLuaManager->init(mEngine.mRenderManager.get());
            mEngine.mPluginHost->setLuaManager(mLuaManager);

            mEngine.mLuaActive = true;

            if (args.compileAll)
            {
                std::pair<int, int> result = mEnvironment.getScriptManager()->compileAll();
                if (result.first)
                    std::cout << "compiled " << result.second << " of " << result.first << " scripts ("
                                     << 100 * static_cast<double>(result.second) / result.first << "%)" << std::endl;
            }
            if (args.compileAllDialogue)
            {
                std::pair<int, int> result = MWDialogue::ScriptTest::compileAll(&mExtensions, args.warningsMode);
                if (result.first)
                    std::cout << "compiled " << result.second << " of " << result.first
                                     << " dialogue script/actor combinations ("
                                     << 100 * static_cast<double>(result.second) / result.first << "%)" << std::endl;
            }

            // Plugins start here rather than with the engine, because they get the world's services for good at start.
            // Last: if the constructor threw afterwards, ~Game would not run to stop them.
            mEngine.mPluginHost->attachWorld(*mWorld->getPhysicsSystem());
            mEngine.mPluginHost->start();
        }
        ~Game()
        {
            // Plugins stop while the World they may hold IDs into still exists.
            if (mEngine.mPluginHost)
                mEngine.mPluginHost->stop();
            mEngine.mLuaActive = false;
            if (mLuaManager)
                mLuaManager->savePermanentStorage(mEngine.mCfgMgr.getUserConfigPath().string());
            // openeden-vsg: as in OpenMW 0.51's Engine destructor, the game systems and the World (whose objects hold
            // Lua scripts) go before the Lua manager. The Lua manager itself is owned by the Engine and outlives the window manager.
            mMechanicsManager = nullptr;
            mDialogueManager = nullptr;
            mJournal = nullptr;
            mScriptManager = nullptr;
            if (mEngine.mDetachGmstLoader)
                mEngine.mDetachGmstLoader();
            mWorld = nullptr;
            // Lua UI elements hold references into the Lua state; clear them with the game, before ~WindowManager does.
            LuaUi::clearGameInterface();
            LuaUi::clearMenuInterface();
        }
        bool run(float dt) override
        {
            if (mEngine.mStateManager->hasQuitRequest())
                return false;

            try
            {
                // openeden-vsg: pausing and simulation time follow OpenMW 0.51's Engine::frame and Engine::go: the
                // DateTimeManager decides whether the game is paused (Lua pause tags such as the UI's, the console,
                // interactive message boxes), frame time is scaled by the simulation time scale, and simulation time
                // advances while unpaused. vsgopenmw paused whenever any GUI mode was open and never advanced
                // simulation time, so Lua simulation timers never fired.
                MWWorld::DateTimeManager& timeManager = *mWorld->getTimeManager();
                timeManager.updateIsPaused();
                const bool paused = timeManager.isPaused();
                dt = std::min(dt, 0.2f) * timeManager.getSimulationTimeScale();

                if (!mEngine.mWindowManager->containsMode(MWGui::GM_MainMenu) || !paused)
                {
                    if (mWorld->getScriptsEnabled())
                    {
                        executeLocalScripts(dt);
                        mScriptManager->getGlobalScripts().run(dt);
                    }

                    mWorld->getWorldScene().markCellAsUnchanged();
                }
                showCursor = mEngine.mWindowManager->isGuiMode();
                if (!paused)
                {
                    double hours = (dt * timeManager.getGameTimeScale()) / 3600.0;
                    mWorld->advanceTime(hours, true);
                    mWorld->rechargeItems(dt, true);
                }

                // Simulation phases run only while the game is unpaused, as the simulation does. dt is simulation time.
                if (!paused)
                    mEngine.mPluginHost->runPhase(openeden::sdk::Phase::PreSimulation, dt);

                mMechanicsManager->update(dt, paused);

                mEngine.mStateManager->/*advanceTimePlayed*/ update(dt);

                mWorld->updatePhysics(dt, paused);
                mWorld->update(dt, paused);

                if (!paused)
                {
                    mEngine.mPluginHost->runPhase(openeden::sdk::Phase::PostSimulation, dt);
                    mEngine.mPluginHost->runPhase(openeden::sdk::Phase::Animation, dt);

                    timeManager.setSimulationTime(timeManager.getSimulationTime() + dt);
                    timeManager.setRenderingSimulationTime(timeManager.getRenderingSimulationTime() + dt);
                }

                mEngine.mWindowManager->update(dt);
                mWorld->updateFocusObject();

                mEngine.mSoundManager->updateRegionSound(dt);
                mEngine.mSoundManager->updateWaterSound();
            }
            catch (const std::exception& e)
            {
                std::cerr << "!Game::run(" << e.what() << ")" << std::endl;
                mEngine.mStateManager->saveGame("Emergency Save");
                return false;
            }
            return true;
        }
    };
}
