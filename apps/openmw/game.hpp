// vsgopenmw-unity-build

#include <memory>

#include <components/compiler/extensions.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/lua_ui/util.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include "mwclass/classes.hpp"
#include "mwdialogue/dialoguefactory.hpp"
#include "mwgui/windowmanagerimp.hpp"
#include "mwlua/luamanagerimp.hpp"
#include "mwmechanics/creaturestats.hpp"
#include "mwmechanics/mechanicsmanagerimp.hpp"
#include "mwrender/rendermanager.hpp"
#include "mwscript/compilercontext.hpp"
#include "mwscript/interpretercontext.hpp"
#include "mwscript/scriptcommands.hpp"
#include "mwscript/scriptmanagerimp.hpp"
#include "mwstate/gamestate.hpp"
#include "mwstate/statemanagerimp.hpp"
#include "mwworld/class.hpp"
#include "mwworld/datetimemanager.hpp"
#include "mwworld/localscripts.hpp"
#include "mwworld/neutraldialogue.hpp"
#include "mwworld/scene.hpp"
#include "mwworld/worldimp.hpp"

#include "extensionhost.hpp"
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
                // Task 0060: an object that is loaded but not built yet (incremental cell build) runs no script until
                // it is.
                if (mWorld->getWorldScene().isPendingBuild(script.second))
                    continue;
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
        std::unique_ptr<MWBase::Journal> mJournal;
        std::unique_ptr<MWBase::DialogueManager> mDialogueManager;
        std::unique_ptr<MWMechanics::MechanicsManager> mMechanicsManager;
        MWLua::LuaManager* mLuaManager{}; // owned by Engine::mLuaManager

    public:
        Game(Engine& e, std::unique_ptr<MWWorld::World>& world, const Arguments& args)
            : mEngine(e)
            , mEnvironment(e.mEnvironment)
            , mWorld(std::move(world))
        {
            // Created by Engine::prepareEngine, before the input manager, so input during loading reaches a Lua
            // manager.
            mLuaManager = mEngine.mLuaManager.get();

            // The game pack chosen by game= starts first, before anything below is built: the crafting windows begin
            // their sessions in their constructors (initUI) and take the pack's rules from the core, and the
            // dialogue manager is made next. With no game pack this does nothing, and the other extensions start at
            // the end of this constructor as they always did.
            mEngine.mExtensionHost->startGamePack(mWorld->getPhysicsSystem());

            MWScript::registerCommands(mExtensions);
            mScriptContext.reset(new MWScript::CompilerContext(MWScript::CompilerContext::Type_Full));
            mScriptContext->setExtensions(&mExtensions);
            mScriptManager = std::make_unique<MWScript::ScriptManager>(
                mEnvironment.getWorld()->getStore(), *mScriptContext, args.warningsMode);
            mEnvironment.setScriptManager(*mScriptManager);

            mMechanicsManager = std::make_unique<MWMechanics::MechanicsManager>();
            mMechanicsManager->setUpdateThreads(mEngine.mRenderManager->getUpdateThreads());
            mEnvironment.setMechanicsManager(*mMechanicsManager);

            mJournal = MWDialogue::makeJournal();
            mEnvironment.setJournal(*mJournal);

            mDialogueManager = MWDialogue::makeDialogueManager(mExtensions, mEngine.mTranslationDataStorage);
            mEnvironment.setDialogueManager(*mDialogueManager);

            // As in OpenMW 0.51, the player is set up before Lua is initialised, so its local scripts start with a new
            // or loaded game (World::clear calls setupPlayer again) rather than at the main menu with storage inactive.
            mWorld->setupPlayer();

            auto& wm = mEngine.mWindowManager;
            wm->setStore(mEnvironment.getWorld()->getStore());
            wm->initUI(mEngine.mRenderManager->getMap(), mEngine.mRenderManager->getWorldMap(),
                mEngine.mRenderManager->getPreview());

            // As in OpenMW 0.51, permanent storage is loaded before the load scripts run (LuaManager::init).
            mLuaManager->loadPermanentStorage(mEngine.mCfgMgr.getUserConfigPath());
            // The game pack started above, so its script namespaces are declared (task 0097, stage A)
            mLuaManager->setPackBindings(&mEngine.mExtensionHost->host().bindings());
            mLuaManager->init(mEngine.mRenderManager.get());
            mEngine.mExtensionHost->setLuaManager(mLuaManager);

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
                std::pair<int, int> result = MWDialogue::compileAllDialogueScripts(&mExtensions, args.warningsMode);
                if (result.first)
                    std::cout << "compiled " << result.second << " of " << result.first
                              << " dialogue script/actor combinations ("
                              << 100 * static_cast<double>(result.second) / result.first << "%)" << std::endl;
            }

            // Extensions start here rather than with the engine, because they get the world's services for good at
            // start. Last: if the constructor threw afterwards, ~Game would not run to stop them.
            mEngine.mExtensionHost->attachWorld(*mWorld->getPhysicsSystem());
            mEngine.mExtensionHost->start();
        }
        ~Game()
        {
            MWWorld::neutralDialogueSession().abandon(); // before the extension host stops (task 0141)
            // Extensions stop while the World they may hold IDs into still exists.
            if (mEngine.mExtensionHost)
                mEngine.mExtensionHost->stop();
            mEngine.mLuaActive = false;
            if (mLuaManager)
                mLuaManager->savePermanentStorage(mEngine.mCfgMgr.getUserConfigPath().string());
            // openeden-vsg: as in OpenMW 0.51's Engine destructor, the game systems and the World (whose objects hold
            // Lua scripts) go before the Lua manager. The Lua manager itself is owned by the Engine and outlives the
            // window manager.
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

            // The game pack failed (task 0084, design doc 7: it is the game): the session ends. The state manager
            // clears the running game and shows the message at the main menu; saving was refused when the pack
            // failed, so nothing it left is written. Once, and the rest of this frame does not run.
            if (const std::string fault = mEngine.mExtensionHost->takeSessionFault(); !fault.empty())
            {
                Log(Debug::Error) << "Ending the session: " << fault;
                mEngine.mStateManager->error(fault);
                return true;
            }

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

                // Task 0060: objects of cells loaded at a border crossing are built a few milliseconds a frame, here,
                // where scripts may also add objects. Nothing is queued unless [Cells] incremental cell build is on.
                mWorld->getWorldScene().buildPendingObjects();

                if (!mEngine.mWindowManager->containsMode(MWGui::GM_MainMenu) || !paused)
                {
                    if (mWorld->getScriptsEnabled())
                    {
                        Misc::LoadTrace::Scope trace("run.scripts");
                        executeLocalScripts(dt);
                        mScriptManager->getGlobalScripts().run(dt);
                    }

                    mWorld->getWorldScene().markCellAsUnchanged();
                }
                showCursor = mEngine.mWindowManager->isGuiMode();
                Misc::LoadTrace::Scope traceTime("run.time");
                if (!paused)
                {
                    double hours = (dt * timeManager.getGameTimeScale()) / 3600.0;
                    mWorld->advanceTime(hours, true);
                    mWorld->rechargeItems(dt, true);
                }

                // Simulation phases run only while the game is unpaused, as the simulation does. dt is simulation time.
                if (!paused)
                {
                    // The game pack's mechanics hook (SDK 4.0): before the other kinds' PreSimulation callbacks
                    mEngine.mExtensionHost->runPhase(openeden::sdk::Phase::GameSimulation, dt);
                    mEngine.mExtensionHost->runPhase(openeden::sdk::Phase::PreSimulation, dt);
                }

                traceTime.end();
                {
                    Misc::LoadTrace::Scope trace("run.mechanics");
                    mMechanicsManager->update(dt, paused);
                }

                mEngine.mStateManager->/*advanceTimePlayed*/ update(dt);

                {
                    Misc::LoadTrace::Scope trace("run.physics");
                    mWorld->updatePhysics(dt, paused);
                }
                {
                    Misc::LoadTrace::Scope trace("run.world.update");
                    mWorld->update(dt, paused);
                }

                if (!paused)
                {
                    mEngine.mExtensionHost->runPhase(openeden::sdk::Phase::PostSimulation, dt);
                    mEngine.mExtensionHost->runPhase(openeden::sdk::Phase::Animation, dt);

                    timeManager.setSimulationTime(timeManager.getSimulationTime() + dt);
                    timeManager.setRenderingSimulationTime(timeManager.getRenderingSimulationTime() + dt);
                }

                // The game pack's window update (SDK 4.0), where the game's windows update, paused or not
                mEngine.mExtensionHost->runPhase(openeden::sdk::Phase::GameUpdateUi, dt);

                MWWorld::neutralDialogueSession().update(); // task 0141: one poll a frame, nothing when idle

                Misc::LoadTrace::Scope traceTail("run.window.focus.sound");
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
