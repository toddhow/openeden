// vsgopenmw-unity-build

#include <filesystem>
#include <sstream>
#include <stdexcept>

#include <SDL_clipboard.h>

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
#include "../mwmechanics/actorutil.hpp"
#include "../mwscript/globalscripts.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/scene.hpp"
#include "../mwworld/worldmodel.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/actoridconverter.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/extension/save_chunk_store.hpp>
#include <components/files/conversion.hpp>
#include <components/l10n/manager.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/settings/values.hpp>

#include "choice.hpp"
#include "loading.hpp"
#include "statemanagerimp.hpp"

namespace MWState
{
    // A save that this version cannot read; what() is the message shown to the player.
    struct SaveFormatVersionError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct LoadGame : public ThreadLoading
    {
        const Character* character{};
        std::filesystem::path filepath;
        double& timePlayed;
        CharacterManager& characterManager;
        MWBase::StateManager::State& state;
        StateManager& stateManager;

        std::atomic_bool askForConfirmation = false;
        std::vector<std::string> missingFiles;
        std::shared_ptr<Choice> choice;
        bool firstPersonCam = false;

        // Kept for postLoad: World::saveLoaded reads the content file mapping from the reader.
        ESM::ESMReader reader;
        std::map<int, int> contentFileMap;
        ESM::ActorIdConverter actorIdConverter;

        LoadGame(double& out_timePlayed, CharacterManager& out_characterManager, MWBase::StateManager::State& out_state,
            StateManager& manager)
            : timePlayed(out_timePlayed)
            , characterManager(out_characterManager)
            , state(out_state)
            , stateManager(manager)
        {
            mDescription = "#{OMWEngine:LoadingInProgress}";
        }

        std::map<int, int> buildContentFileIndexMap(const ESM::ESMReader& esmReader) const
        {
            const std::vector<std::string>& current = MWBase::Environment::get().getWorld()->getContentFiles();
            const std::vector<ESM::Header::MasterData>& prev = esmReader.getGameFiles();
            std::map<int, int> map;
            for (int iPrev = 0; iPrev < static_cast<int>(prev.size()); ++iPrev)
            {
                for (int iCurrent = 0; iCurrent < static_cast<int>(current.size()); ++iCurrent)
                    if (Misc::StringUtils::ciEqual(prev[iPrev].name, current[iCurrent]))
                    {
                        map.insert(std::make_pair(iPrev, iCurrent));
                        break;
                    }
            }
            return map;
        }

        void verifyProfile(const ESM::SavedGame& profile)
        {
            // A save of another game, or of a newer game pack, is refused before anything is read (task 0084)
            const MWBase::ExtensionManager& extensions = *MWBase::Environment::get().getExtensionManager();
            const std::string packMessage = openeden::extension::checkSavedGamePack(
                profile.mGamePackId, profile.mGamePackVersion, extensions.gamePackId(), extensions.gamePackVersion());
            if (!packMessage.empty())
                throw std::runtime_error(packMessage);

            const auto& selectedContentFiles = MWBase::Environment::get().getWorld()->getContentFiles();
            for (std::string_view contentFile : profile.getMissingContentFiles(selectedContentFiles))
            {
                Log(Debug::Warning) << "Warning: Saved game dependency " << contentFile << " is missing.";
                missingFiles.emplace_back(contentFile);
            }
            if (!missingFiles.empty())
                askForConfirmation = true;
        }

        // The confirmation OpenMW 0.51 shows for missing content files, with the list and a copy button.
        std::string missingFilesMessage() const
        {
            constexpr size_t missingPluginsDisplayLimit = 12;

            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            std::string message = "#{OMWEngine:MissingContentFilesConfirmation}";
            message += l10n->formatMessage(
                "MissingContentFilesList", { "files" }, { static_cast<int>(missingFiles.size()) });
            const size_t cappedSize = std::min(missingFiles.size(), missingPluginsDisplayLimit);
            for (size_t i = 0; i < cappedSize; ++i)
            {
                if (i + 1 == cappedSize && cappedSize < missingFiles.size())
                {
                    message += "...";
                    break;
                }
                message += missingFiles[i];
                if (i + 1 < cappedSize)
                    message += "\n";
            }
            message += l10n->formatMessage(
                "MissingContentFilesListCopy", { "files" }, { static_cast<int>(missingFiles.size()) });
            return message;
        }

        void copyMissingFilesToClipboard() const
        {
            std::ostringstream stream;
            for (const std::string& contentFile : missingFiles)
                stream << contentFile << "\n";
            std::string fullList = stream.str();
            if (!fullList.empty())
                fullList.pop_back();
            SDL_SetClipboardText(fullList.c_str());
        }

        void threadLoad() override
        {
            Log(Debug::Info) << "Reading save file " << filepath.filename().string();

            reader.open(filepath);

            const ESM::FormatVersion version = reader.getFormatVersion();
            if (version > ESM::CurrentSaveGameFormatVersion)
                throw SaveFormatVersionError("#{OMWEngine:LoadingRequiresNewVersionError}");
            else if (version < ESM::MinSupportedSaveGameFormatVersion)
            {
                const char* release;
                // Report the last version still capable of reading this save
                if (version < ESM::OpenMW0_49MinSaveGameFormatVersion)
                    release = "OpenMW 0.48.0";
                else
                {
                    // Insert additional else if statements above to cover future releases
                    static_assert(ESM::MinSupportedSaveGameFormatVersion <= ESM::OpenMW0_49MinSaveGameFormatVersion);
                    release = "OpenMW 0.51.0";
                }
                auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
                throw SaveFormatVersionError(
                    l10n->formatMessage("LoadingRequiresOldVersionError", { "version" }, { release }));
            }

            contentFileMap = buildContentFileIndexMap(reader);
            reader.setContentFileMapping(&contentFileMap);
            MWBase::Environment::get().getLuaManager()->setContentFileMapping(contentFileMap);

            if (version <= ESM::MaxActorIdSaveGameFormatVersion)
                reader.mActorIdConverter = &actorIdConverter;

            size_t total = reader.getFileSize();
            while (reader.hasMoreRecs() && !abort)
            {
                ESM::NAME n = reader.getRecName();
                reader.getRecHeader();

                switch (n.toInt())
                {
                    case ESM::REC_SAVE:
                    {
                        ESM::SavedGame profile;
                        profile.load(reader);
                        verifyProfile(profile);
                        timePlayed = profile.mTimePlayed;
                        Log(Debug::Info) << "Loading saved game '" << profile.mDescription << "' for character '"
                                         << profile.mPlayerName << "'";
                        break;
                    }

                    case ESM::REC_JOUR:
                    case ESM::REC_QUES:
                        MWBase::Environment::get().getJournal()->readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_DIAS:
                        MWBase::Environment::get().getDialogueManager()->readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_ALCH:
                    case ESM::REC_ARMO:
                    case ESM::REC_BOOK:
                    case ESM::REC_CLAS:
                    case ESM::REC_CLOT:
                    case ESM::REC_ENCH:
                    case ESM::REC_NPC_:
                    case ESM::REC_SPEL:
                    case ESM::REC_WEAP:
                    case ESM::REC_GLOB:
                    case ESM::REC_PLAY:
                    case ESM::REC_CSTA:
                    case ESM::REC_WTHR:
                    case ESM::REC_DYNA:
                    case ESM::REC_ACTC:
                    case ESM::REC_PROJ:
                    case ESM::REC_MPRJ:
                    case ESM::REC_ENAB:
                    case ESM::REC_LEVC:
                    case ESM::REC_LEVI:
                    case ESM::REC_LIGH:
                    case ESM::REC_CREA:
                    case ESM::REC_CONT:
                    case ESM::REC_RAND:
                    case ESM::REC_STAT:
                    case ESM::REC_DOOR:
                        MWBase::Environment::get().getWorld()->readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_CAM_:
                        reader.getHNT(firstPersonCam, "FIRS");
                        break;

                    case ESM::REC_GSCR:
                        MWBase::Environment::get().getScriptManager()->getGlobalScripts().readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_GMAP:
                    case ESM::REC_KEYS:
                    case ESM::REC_ASPL:
                    case ESM::REC_MARK:
                        MWBase::Environment::get().getWindowManager()->readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_DCOU:
                    case ESM::REC_STLN:
                        MWBase::Environment::get().getMechanicsManager()->readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_INPU:
                        MWBase::Environment::get().getInputManager()->readRecord(reader, n.toInt());
                        break;

                    case ESM::REC_LUAM:
                        MWBase::Environment::get().getLuaManager()->readRecord(reader, n.toInt());
                        break;

                    default:
                        // ignore invalid records
                        Log(Debug::Warning) << "Warning: Ignoring unknown record: " << n.toStringView();
                        reader.skipRecord();
                }
                setComplete(float(reader.getFileOffset()) / total);
            }
        }

        void postLoad()
        {
            characterManager.setCurrentCharacter(character);

            if (character)
                Settings::saves().mCharacter.set(Files::pathToUnicodeString(character->getPath().filename()));
            stateManager.setLastSavegame(filepath);

            MWBase::Environment::get().getWindowManager()->setNewGame(false);
            MWBase::Environment::get().getWorld()->saveLoaded(reader);
            actorIdConverter.apply();
            MWBase::Environment::get().getWorld()->setupPlayer();
            MWBase::Environment::get().getWorld()->renderPlayer();
            MWBase::Environment::get().getWindowManager()->updatePlayer();
            MWBase::Environment::get().getMechanicsManager()->playerLoaded();
            MWBase::Environment::get().getWorld()->toggleVanityMode(false);

            if (firstPersonCam != MWBase::Environment::get().getWorld()->isFirstPerson())
                MWBase::Environment::get().getWorld()->togglePOV();

            MWWorld::ConstPtr ptr = MWMechanics::getPlayer();

            if (ptr.isInCell())
            {
                const auto& cellId = ptr.getCell()->getCell()->getId();

                // Use detectWorldSpaceChange=false, otherwise some of the data we just loaded would be cleared again
                MWBase::Environment::get().getWorld()->changeToCell(
                    cellId, ptr.getRefData().getPosition(), false, false);
            }
            else
            {
                // Cell no longer exists (i.e. changed game files), choose a default cell
                Log(Debug::Warning) << "Player character's cell no longer exists, changing to the default cell";
                ESM::ExteriorCellLocation loc = { 0, 0, ESM::Cell::sDefaultWorldspaceId };
                auto& cell = MWBase::Environment::get().getWorldModel()->getExterior(loc);
                const vsg::vec2 p = ESM::indexToPosition(loc, false);
                ESM::Position pos;
                pos.pos[0] = p.x;
                pos.pos[1] = p.y;
                pos.pos[2] = 0.f; // should be adjusted automatically (adjustPlayerPos=true)
                pos.rot[0] = 0.f;
                pos.rot[1] = 0.f;
                pos.rot[2] = 0.f;
                MWBase::Environment::get().getWorld()->changeToCell(cell.getCell()->getId(), pos, true, false);
            }

            MWBase::Environment::get().getWorld()->updateProjectilesCasters();

            // Vanilla MW will restart startup scripts when a save game is loaded. This is unintuitive,
            // but some mods may be using it as a reload detector.
            MWBase::Environment::get().getScriptManager()->getGlobalScripts().addStartup();

            // Since we passed "changeEvent=false" to changeCell, we shouldn't have triggered the cell change flag.
            // But make sure the flag is cleared anyway in case it was set from an earlier game.
            MWBase::Environment::get().getWorldScene()->markCellAsUnchanged();

            state = StateManager::State_Running;
            MWBase::Environment::get().getLuaManager()->gameLoaded();
            MWBase::Environment::get().getExtensionManager()->gameLoaded();
            for (int actorId : actorIdConverter.mGraveyard)
            {
                auto mapped = actorIdConverter.mMappings.find(actorId);
                if (mapped != actorIdConverter.mMappings.end())
                    MWBase::Environment::get().getMechanicsManager()->cleanupSummonedCreature(mapped->second);
            }
        }

        bool run(float dt) override
        {
            if (askForConfirmation && !abort)
            {
                if (!choice)
                {
                    choice = std::make_shared<Choice>();
                    choice->buttons = { "#{Interface:Yes}", "#{Interface:Copy}", "#{Interface:No}" };
                    choice->message = missingFilesMessage();
                    stateManager.pushGameState(choice);
                    return true;
                }
                else if (choice->pressedButton == 1)
                {
                    // Copy the list and ask again.
                    copyMissingFilesToClipboard();
                    choice.reset();
                    return true;
                }
                else if (choice->pressedButton != 0)
                {
                    stateManager.mainMenu();
                    return false;
                }
            }

            try
            {
                auto threadRunning = ThreadLoading::run(dt);
                if (!threadRunning && !abort)
                    postLoad();
                return threadRunning;
            }
            catch (const SaveFormatVersionError& e)
            {
                Log(Debug::Error) << "Failed to load saved game: " << e.what();
                stateManager.error(e.what());
                return false;
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to load saved game: " << e.what();
                stateManager.error("#{OMWEngine:LoadingFailed}: " + std::string(e.what()));
                return false;
            }
        }
    };
}
