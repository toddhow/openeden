// vsgopenmw-unity-build

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwrender/screenshotinterface.hpp"
#include "../mwscript/globalscripts.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/esmstore.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/files/conversion.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/imageio.hpp>

#include "character.hpp"
#include "gamepackchunks.hpp"
#include "loading.hpp"
#include "statemanagerimp.hpp"

namespace MWState
{
    struct SaveGame : public ThreadLoading
    {
        std::string description;
        const Slot* slot{};
        Character* character{};
        double timePlayed{};
        MWRender::ScreenshotInterface* screenshotInterface{};
        StateManager* stateManager{};
        // The game pack of the session, copied on the main thread; empty with none (task 0084)
        std::string gamePackId;
        std::string gamePackVersion;
        // The game pack's save chunks, collected on the main thread just before the worker starts (task 0084, I4);
        // empty when there are none, and then nothing is written for them.
        std::vector<std::byte> gamePackChunks;
        bool gamePackChunksCollected{};

        struct ScreenshotState
        {
            bool requested{};
            bool retrieved{};
            vsg::ref_ptr<vsg::Data> data;
        } screenshot;

        SaveGame() { mDescription = "#{OMWEngine:SavingInProgress}"; }
        void threadLoad() override
        {
            const auto start = std::chrono::steady_clock::now();

            ESM::SavedGame profile;

            MWBase::World& world = *MWBase::Environment::get().getWorld();

            MWWorld::Ptr player = world.getPlayerPtr();

            profile.mContentFiles = world.getContentFiles();

            profile.mPlayerName = player.get<ESM::NPC>()->mBase->mName;
            profile.mPlayerLevel = player.getClass().getNpcStats(player).getLevel();

            const ESM::RefId& classId = player.get<ESM::NPC>()->mBase->mClass;
            if (world.getStore().get<ESM::Class>().isDynamic(classId))
                profile.mPlayerClassName = world.getStore().get<ESM::Class>().find(classId)->mName;
            else
                profile.mPlayerClassId = classId;

            const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);

            profile.mPlayerCellName = world.getCellName();
            profile.mInGameTime = world.getTimeManager()->getEpochTimeStamp();
            profile.mTimePlayed = timePlayed;
            profile.mCurrentDay = world.getTimeManager()->getTimeStamp().getDay();
            profile.mDescription = description;
            profile.mCurrentHealth = stats.getHealth().getCurrent();
            profile.mMaximumHealth = stats.getHealth().getModified();
            profile.mGamePackId = gamePackId;
            profile.mGamePackVersion = gamePackVersion;

            if (screenshot.data)
                profile.mScreenshot = vsgUtil::writeImageToMemory(screenshot.data, ".jpg");

            if (!slot)
                slot = character->createSlot(profile);
            else
                slot = character->updateSlot(slot, profile);

            // Make sure the animation state held by references is up to date before saving the game.
            MWBase::Environment::get().getMechanicsManager()->persistAnimationStates();

            Log(Debug::Info) << "Writing saved game '" << description << "' for character '" << profile.mPlayerName
                             << "'";

            // Write to a memory stream first. If there is an exception during the save process, we don't want to trash
            // the existing save file we are overwriting.
            std::stringstream stream;

            ESM::ESMWriter writer;

            for (const std::string& contentFile : MWBase::Environment::get().getWorld()->getContentFiles())
                writer.addMaster(contentFile, 0); // not using the size information anyway -> use value of 0

            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);

            // all unused
            writer.setVersion(0);
            writer.setType(0);
            writer.setAuthor("");
            writer.setDescription("");

            size_t recordCount = 1 // saved game header
                + MWBase::Environment::get().getJournal()->countSavedGameRecords()
                + MWBase::Environment::get().getLuaManager()->countSavedGameRecords()
                + MWBase::Environment::get().getWorld()->countSavedGameRecords()
                + MWBase::Environment::get().getScriptManager()->getGlobalScripts().countSavedGameRecords()
                + MWBase::Environment::get().getDialogueManager()->countSavedGameRecords()
                + MWBase::Environment::get().getMechanicsManager()->countSavedGameRecords()
                + MWBase::Environment::get().getInputManager()->countSavedGameRecords()
                + MWBase::Environment::get().getWindowManager()->countSavedGameRecords()
                + countGamePackChunkRecords(gamePackChunks);
            writer.setRecordCount(static_cast<int>(recordCount));

            writer.save(stream);

            ::Loading::Listener /*dummy*/ listener;
            // Using only Cells for progress information, since they typically have the largest records by far
            progressStep = 1.f / MWBase::Environment::get().getWorld()->countSavedGameCells();

            writer.startRecord(ESM::REC_SAVE);
            slot->mProfile.save(writer);
            writer.endRecord(ESM::REC_SAVE);

            MWBase::Environment::get().getJournal()->write(writer, listener);
            MWBase::Environment::get().getDialogueManager()->write(writer, listener);
            // LuaManager::write should be called before World::write because world also saves
            // local scripts that depend on LuaManager.
            MWBase::Environment::get().getLuaManager()->write(writer, listener);
            MWBase::Environment::get().getWorld()->write(writer, *this);
            MWBase::Environment::get().getScriptManager()->getGlobalScripts().write(writer, listener);
            MWBase::Environment::get().getMechanicsManager()->write(writer, listener);
            MWBase::Environment::get().getInputManager()->write(writer, listener);
            MWBase::Environment::get().getWindowManager()->write(writer, listener);
            writeGamePackChunkRecord(writer, gamePackChunks);

            // Ensure we have written the number of records that was estimated
            if (static_cast<size_t>(writer.getRecordCount()) != recordCount + 1) // 1 extra for TES3 record
                Log(Debug::Warning) << "Warning: number of written savegame records does not match. Estimated: "
                                    << recordCount + 1 << ", written: " << writer.getRecordCount();

            writer.close();

            if (stream.fail())
                throw std::runtime_error(
                    "Write operation failed (memory stream): " + std::generic_category().message(errno));

            // All good, write to file
            std::ofstream filestream(slot->mPath, std::ios::binary);
            filestream << stream.rdbuf();

            if (filestream.fail())
                throw std::runtime_error(
                    "Write operation failed (file stream): " + std::generic_category().message(errno));

            Settings::saves().mCharacter.set(Files::pathToUnicodeString(slot->mPath.parent_path().filename()));
            stateManager->setLastSavegame(slot->mPath);
            MWBase::Environment::get().getExtensionManager()->gameSaved();

            const auto finish = std::chrono::steady_clock::now();

            Log(Debug::Info)
                << '\'' << description << "' is saved in "
                << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(finish - start).count() << "ms";
        }
        bool run(float dt) override
        {
            if (!screenshot.requested)
            {
                requiresScene = true;
                // *2 to get some nice antialiasing
                screenshotInterface->request(259 * 2, 133 * 2);
                screenshot.requested = true;
                return true;
            }
            else if (!screenshot.retrieved)
            {
                requiresScene = false;
                auto [data, ready] = screenshotInterface->retrieve();
                if (ready)
                {
                    screenshot.data = data;
                    screenshot.retrieved = true;
                }
                return true;
            }

            try
            {
                if (!gamePackChunksCollected)
                {
                    std::string reason;
                    if (!MWBase::Environment::get().getExtensionManager()->collectGamePackSave(gamePackChunks, reason))
                        throw std::runtime_error(reason);
                    gamePackChunksCollected = true;
                }
                return ThreadLoading::run(dt);
            }
            catch (const std::exception& e)
            {
                std::stringstream error;
                error << "Failed to save game: " << e.what();
                Log(Debug::Error) << error.str();

                MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
                    error.str(), { "#{Interface:OK}" });

                // If no file was written, clean up the slot
                if (character && slot && !std::filesystem::exists(slot->mPath))
                {
                    character->deleteSlot(slot);
                    character->cleanup();
                }
                return false;
            }
        }
    };
}
