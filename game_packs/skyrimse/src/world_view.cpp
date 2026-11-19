#include "game_skyrimse/world_view.hpp"

#include "game_skyrimse/dragonborn.hpp"
#include "game_skyrimse/dragons.hpp"
#include "game_skyrimse/quest_priority.hpp"
#include "game_skyrimse/vampirism.hpp"

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/actors.hpp"
#include "game_skyrimse/appearance.hpp"
#include "game_skyrimse/character_service.hpp"
#include "game_skyrimse/dialogue_runtime.hpp"
#include "game_skyrimse/harvest.hpp"
#include "game_skyrimse/location.hpp"
#include "game_skyrimse/music.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/player_body.hpp"
#include "game_skyrimse/quest_script.hpp"
#include "game_skyrimse/story_events.hpp"
#include "game_skyrimse/weather.hpp"
#include "game_skyrimse/weather_source.hpp"

#include <cstdio>
#include <components/script_papyrus/pexfile.hpp>

#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
#include <system_error>

namespace game_skyrimse
{
    namespace sdkgame = openeden::sdk::game;

    namespace
    {
        constexpr float kCellSize = 4096.0f; // Skyrim's exterior cell is 4096 units square
        constexpr std::size_t kMaxErrorsKept = 256;

        std::string lowerAscii(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
            return out;
        }

        [[maybe_unused]] Content::Error makeError(
            const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error e;
            e.mFile = file;
            e.mOffset = offset;
            e.mRecordType = type;
            e.mMessage = std::move(message);
            return e;
        }

        sdkgame::BaseKind kindOfModelType(std::uint32_t type)
        {
            switch (type)
            {
                case Content::fourCC("ACTI"):
                    return sdkgame::BaseKind::Activator;
                case Content::fourCC("CONT"):
                    return sdkgame::BaseKind::Container;
                case Content::fourCC("DOOR"):
                    return sdkgame::BaseKind::Door;
                case Content::fourCC("FLOR"):
                    return sdkgame::BaseKind::Flora;
                case Content::fourCC("FURN"):
                    return sdkgame::BaseKind::Furniture;
                case Content::fourCC("LIGH"):
                    return sdkgame::BaseKind::Light;
                case Content::fourCC("MSTT"):
                    return sdkgame::BaseKind::MovableStatic;
                case Content::fourCC("TREE"):
                    return sdkgame::BaseKind::Tree;
                case Content::fourCC("TACT"):
                    return sdkgame::BaseKind::Activator;
                case Content::fourCC("ALCH"):
                case Content::fourCC("AMMO"):
                case Content::fourCC("APPA"):
                case Content::fourCC("BOOK"):
                case Content::fourCC("INGR"):
                case Content::fourCC("KEYM"):
                case Content::fourCC("SCRL"):
                case Content::fourCC("SLGM"):
                    return sdkgame::BaseKind::Item;
                default:
                    return sdkgame::BaseKind::Other;
            }
        }

        // The base file name of a plugin without extension.
        std::string stemOf(const std::filesystem::path& p)
        {
            return p.stem().string();
        }

        constexpr std::uint32_t kRecordStartsDead = 0x200; ///< the ACHR record flag (task 0330)

        bool finite(float v)
        {
            return std::isfinite(v);
        }
    }

    // ---- DataFolderFiles --------------------------------------------------------------------------------------------

    bool DataFolderFiles::read(std::string_view path, std::string& bytes) const
    {
        namespace fs = std::filesystem;
        std::string native(path);
        std::replace(native.begin(), native.end(), '\\', '/');
        // Loose file first (a mod may ship its tables loose; the file system is case-insensitive on Windows only, so
        // try the name as given).
        {
            std::error_code ec;
            const fs::path loose = mData / fs::path(native);
            if (fs::is_regular_file(loose, ec))
            {
                std::ifstream in(loose, std::ios::binary);
                if (in)
                {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    bytes = ss.str();
                    return true;
                }
            }
        }
        // The plugin's base name is the part before the last '_' of the file name.
        const std::size_t slash = native.find_last_of('/');
        const std::string file = slash == std::string::npos ? native : native.substr(slash + 1);
        const std::size_t underscore = file.find_last_of('_');
        const std::string stem = lowerAscii(underscore == std::string::npos ? file : file.substr(0, underscore));

        std::vector<fs::path> candidates;
        candidates.push_back(mData / "Skyrim - Interface.bsa");
        std::error_code ec;
        std::vector<fs::path> ownArchives;
        for (fs::directory_iterator it(mData, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string name = lowerAscii(it->path().filename().string());
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bsa") == 0 && name.rfind(stem, 0) == 0
                && name != "skyrim - interface.bsa")
                ownArchives.push_back(it->path());
        }
        std::sort(ownArchives.begin(), ownArchives.end());
        candidates.insert(candidates.end(), ownArchives.begin(), ownArchives.end());

        std::string backslashed(path);
        std::replace(backslashed.begin(), backslashed.end(), '/', '\\');
        for (const fs::path& archive : candidates)
        {
            std::string err;
            if (std::filesystem::exists(archive, ec) && readArchiveEntry(archive, backslashed, bytes, err))
                return true;
        }
        return false;
    }

    bool VfsFileSource::read(std::string_view path, std::string& bytes) const
    {
        std::string unified(path);
        std::replace(unified.begin(), unified.end(), '\\', '/');
        const std::vector<std::uint8_t> data = mVfs.readFile(unified);
        if (data.empty())
            return false;
        bytes.assign(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    }

    bool VfsFileSource::exists(std::string_view path) const
    {
        std::string unified(path);
        std::replace(unified.begin(), unified.end(), '\\', '/');
        return mVfs.hasFile(unified);
    }

    // ---- construction ---------------------------------------------------------------------------------------------

    /// The dialogue and quest records of a load order (task 0141). Built from the plugin files only, so a worker can
    /// build it while the main thread goes on (the records of the five masters take several seconds).
    struct SkyrimWorldView::DialogueData
    {
        DialogueIndex mDialogue;
        QuestIndex mQuests;
        PackageIndex mPackages; ///< task 0135 stage S5
        LocationIndex mLocations; ///< task 0156
        QuestScriptIndex mQuestScripts; ///< task 0156
        PlacedRefIndex mPlacedRefs; ///< task 0160
        StoryIndex mStory; ///< task 0187: the story manager's nodes
        SceneIndex mScenes; ///< task 0194
        map::MarkerIndex mMapMarkers; ///< task 0201: empty unless the pack asked for them
        menus::LoadingScreenIndex mLoadingScreens; ///< task 0207: empty unless the pack asked for them
        QuestVmadIndex mQuestVmads; ///< task 0176: scripts, fragments and alias scripts of every quest
        std::uint64_t mPlacedRefsMillis = 0; ///< the part of the build that read references (task 0160)
    };

    struct SkyrimWorldView::DialogueState
    {
        std::unique_ptr<DialogueData> mData;
        /// String tables by (plugin index, kind); an empty table is one that could not be read (tried once).
        std::map<std::pair<std::uint32_t, int>, std::unique_ptr<StringTable>> mTables;
        std::unique_ptr<DialogueRuntime> mRuntime;
        DialogueTextResolver mResolver; ///< task 0185: the runtime's text resolver, kept for the journal
        std::unique_ptr<QuestVariables> mQuestVariables; ///< task 0156: reads scripts/<name>.pex through the VFS
        std::uint64_t mTablesMissing = 0;
        // Task 0135 stage S5: the package conditions are evaluated against these (not the conversation's own).
        std::unique_ptr<SkyrimConditionWorld> mPackageWorld;
        QuestState mPackageQuests;
        // Task 0187: the alias filler and the story manager, over a conditions world of their own.
        std::unique_ptr<SkyrimConditionWorld> mQuestWorld;
        conditions::Evaluator mQuestEvaluator;
        StoryEvent mStoryEvent;
        std::unique_ptr<QuestAliasFiller> mAliasFiller;
        std::vector<conditions::ExtraFunction> mQuestExtras; ///< what mQuestEvaluator may call besides the table (0230)
        std::set<FormId> mStoryRan; ///< task 0194: quests an ADIA event started
        std::unique_ptr<StoryManager> mStoryManager;
        conditions::Evaluator mPackageEvaluator;
        std::map<FormId, std::vector<const PackageRecord*>> mPackageLists; ///< by NPC base: resolved once
        /// Task 0271: the packages quest aliases give a unique actor (ALPC), by NPC base; built on first use.
        struct AliasPackages
        {
            FormId mQuest = kNullFormId;
            std::vector<FormId> mPackages;
        };
        std::map<FormId, std::vector<AliasPackages>> mAliasPackages;
        std::map<FormId, std::vector<AliasPackages>> mAliasPackagesOfReference; ///< task 0274: ALFR aliases
        /// Task 0305: aliases with packages that no data fills (a script forces them), highest quest priority first
        struct DynamicAliasPackages
        {
            FormId mQuest = kNullFormId;
            std::uint32_t mAlias = 0;
            std::uint8_t mPriority = 0;
            std::vector<FormId> mPackages;
            bool mScriptOnly = false; ///< task 0314: no fill data and no conditions: only a script's ForceRefTo fills it
        };
        std::vector<DynamicAliasPackages> mDynamicAliasPackages;
        mutable std::map<FormId, FormId> mPackageOwner; ///< task 0274: package -> the quest whose alias lists it
        bool mAliasPackagesBuilt = false;
        bool mDynamicAliasPackagesSorted = false; ///< task 0305
        SkyrimConditionWorld::LiveQuestVariable mLiveQuestVariable; ///< task 0271
    };

    namespace
    {
        struct PluginRef
        {
            std::filesystem::path mPath;
            std::string mName;
        };
    }

    SkyrimWorldView::SkyrimWorldView() = default;

    SkyrimWorldView::SkyrimWorldView(Options options, const FileSource* files)
        : mOptions(std::move(options))
        , mFiles(files)
    {
        mOptions.mLimits.mSkipReferences = true; // the index never holds a reference
    }

    SkyrimWorldView::~SkyrimWorldView()
    {
        if (mCombatThread.joinable())
            mCombatThread.join();
        cancelAndJoin();
        if (mDialogueThread.joinable())
            mDialogueThread.join();
    }

    bool SkyrimWorldView::ensureActorIndex()
    {
        if (mActorIndex != nullptr)
            return true;
        {
            auto index = std::make_unique<ActorIndex>();
            const ActorLimits limits;
            for (const PluginData& plugin : mPlugins)
            {
                Content::Error openError;
                const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(plugin.mPath, openError);
                if (file == nullptr)
                {
                    noteError(openError);
                    continue;
                }
                ActorRecords records;
                (void)parseActorRecords(*file, mOrder, limits, records);
                for (ActorNpc& npc : records.mNpcs)
                    npc.mPlugin = static_cast<std::uint32_t>(&plugin - mPlugins.data());
                index->add(records);
                index->addLeveledLists(plugin.mParsed.mLeveledLists);
            }
            mActorIndex = std::move(index);
        }
        return true;
    }

    bool SkyrimWorldView::combatReady()
    {
        const int state = mCombatState.load(std::memory_order_acquire);
        if (state == 3)
            return true;
        if (state == 0)
        {
            // The plugin list is final only when the world index is built: a worker started earlier would read a list
            // that is still growing.
            if (mBuilding.load(std::memory_order_acquire) || mFailed.load(std::memory_order_acquire))
                return false;
            mCombatState.store(1, std::memory_order_release);
            const bool wantActors = mActorIndex == nullptr;
            mCombatThread = std::thread([this, wantActors]() {
                const auto started = std::chrono::steady_clock::now();
                auto combat = std::make_unique<CombatIndex>();
                std::unique_ptr<ActorIndex> actors;
                if (wantActors)
                    actors = std::make_unique<ActorIndex>();
                std::vector<Content::Error> errors;
                const ActorLimits limits;
                try
                {
                    for (const PluginData& plugin : mPlugins)
                    {
                        Content::Error openError;
                        const std::unique_ptr<Content::Plugin> file
                            = Content::Plugin::openFile(plugin.mPath, openError);
                        if (file == nullptr)
                        {
                            errors.push_back(openError);
                            continue;
                        }
                        CombatRecords records;
                        (void)parseCombatRecords(*file, mOrder, ExteriorLimits(), records);
                        combat->add(records);
                        if (actors != nullptr)
                        {
                            ActorRecords actorRecords;
                            (void)parseActorRecords(*file, mOrder, limits, actorRecords);
                            for (ActorNpc& npc : actorRecords.mNpcs)
                                npc.mPlugin = static_cast<std::uint32_t>(&plugin - mPlugins.data());
                            actors->add(actorRecords);
                            actors->addLeveledLists(plugin.mParsed.mLeveledLists);
                        }
                    }
                }
                catch (...)
                {
                    // A read that threw leaves what it had: the combat answers are then partial, never undefined.
                }
                mCombatBuilt = std::move(combat);
                mActorBuilt = std::move(actors);
                mCombatErrors = std::move(errors);
                mCombatMillis = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                        .count());
                mCombatState.store(2, std::memory_order_release);
            });
            return false;
        }
        if (state == 1)
            return false;
        // 2: adopt on the calling (main) thread. Another path may have built the actor index meanwhile: keep that one.
        mCombatThread.join();
        mCombatIndex = std::move(mCombatBuilt);
        if (mActorIndex == nullptr && mActorBuilt != nullptr)
            mActorIndex = std::move(mActorBuilt);
        mActorBuilt.reset();
        for (const Content::Error& error : mCombatErrors)
            noteError(error);
        mCombatErrors.clear();
        if (mOptions.mNotice)
            mOptions.mNotice("combat records read on a worker in " + std::to_string(mCombatMillis)
                + " ms (the main thread did not wait)");
        mCombatState.store(3, std::memory_order_release);
        return true;
    }

    namespace
    {
        bool equalsLower(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }
    }

    openeden::sdk::game::ActorRelation SkyrimWorldView::factionRelation(
        const FighterProfile& a, const FighterProfile& b)
    {
        if (!mActorIndex)
            return openeden::sdk::game::ActorRelation::Neutral;
        std::vector<FactionMembership> ma;
        std::vector<FactionMembership> mb;
        ma.reserve(a.mFactions.size());
        mb.reserve(b.mFactions.size());
        for (const FormId f : a.mFactions)
            ma.push_back({ f, 0 });
        for (const FormId f : b.mFactions)
            mb.push_back({ f, 0 });
        return relationOfFactions(*mActorIndex, ma, mb);
    }

    CombatSettings SkyrimWorldView::settings()
    {
        if (mCombatIndex == nullptr)
            return {};
        return mCombatIndex->settings().combatSettings();
    }

    const SettingsTable* SkyrimWorldView::gameSettings()
    {
        return combatReady() && mCombatIndex != nullptr ? &mCombatIndex->settings() : nullptr;
    }

    BlockSettings SkyrimWorldView::blockSettings()
    {
        if (mCombatIndex == nullptr)
            return {};
        return mCombatIndex->settings().blockSettings();
    }

    const AmmoRecord* SkyrimWorldView::ammoByEditorId(std::string_view editorId)
    {
        if (mCombatIndex == nullptr)
            return nullptr;
        auto lower = [](std::string_view text) {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        };
        const std::string wanted = lower(editorId);
        for (const FormId id : mCombatIndex->ammoIds())
        {
            const AmmoRecord* ammo = mCombatIndex->ammo(id);
            if (ammo != nullptr && lower(ammo->mCommon.mEditorId) == wanted)
                return ammo;
        }
        return nullptr;
    }

    const ProjectileRecord* SkyrimWorldView::projectileOf(const AmmoRecord& ammo)
    {
        return mCombatIndex == nullptr || ammo.mProjectile == 0 ? nullptr : mCombatIndex->projectile(ammo.mProjectile);
    }

    ProjectileSettings SkyrimWorldView::projectileSettings()
    {
        return mCombatIndex == nullptr ? ProjectileSettings{} : mCombatIndex->settings().projectileSettings();
    }

    DualCastSettings SkyrimWorldView::dualCastSettings()
    {
        return mCombatIndex == nullptr ? DualCastSettings{} : dualCastSettingsFrom(mCombatIndex->settings());
    }

    const SpellRecord* SkyrimWorldView::spellByEditorId(std::string_view editorId)
    {
        return mCombatIndex == nullptr ? nullptr : mCombatIndex->spellByEditorId(std::string(editorId));
    }

    const ImageModifierRecord* SkyrimWorldView::imageModifierByEditorId(std::string_view editorId)
    {
        return mCombatIndex == nullptr ? nullptr : mCombatIndex->imageModifierByEditorId(std::string(editorId));
    }

    const ShoutRecord* SkyrimWorldView::shoutById(FormId id)
    {
        return mCombatIndex == nullptr ? nullptr : mCombatIndex->shouts().shout(id);
    }

    const ShoutRecord* SkyrimWorldView::shoutByEditorId(std::string_view editorId)
    {
        return mCombatIndex == nullptr ? nullptr : mCombatIndex->shouts().shoutByEditorId(editorId);
    }

    shouts::Settings SkyrimWorldView::shoutSettings()
    {
        shouts::Settings settings;
        if (mCombatIndex != nullptr)
        {
            const MagicSettings magic = mCombatIndex->settings().magicSettings();
            settings.mMinimumHold = magic.mShoutTime1;
            settings.mWordTime = magic.mShoutTime2;
        }
        return settings;
    }

    const ShoutIndex* SkyrimWorldView::shoutIndex() const
    {
        return mCombatState.load(std::memory_order_acquire) != 3 || mCombatIndex == nullptr ? nullptr
                                                                                            : &mCombatIndex->shouts();
    }

    const ProjectileRecord* SkyrimWorldView::projectileById(FormId id)
    {
        return mCombatIndex == nullptr || id == kNullFormId ? nullptr : mCombatIndex->projectile(id);
    }

    const WeaponRecord* SkyrimWorldView::weaponById(FormId id)
    {
        return mCombatIndex == nullptr || id == kNullFormId ? nullptr : mCombatIndex->weapon(id);
    }

    const AmmoRecord* SkyrimWorldView::ammoById(FormId id)
    {
        return mCombatIndex == nullptr || id == kNullFormId ? nullptr : mCombatIndex->ammo(id);
    }

    const ExplosionRecord* SkyrimWorldView::explosionById(FormId id)
    {
        return mCombatIndex == nullptr || id == kNullFormId ? nullptr : mCombatIndex->explosion(id);
    }

    std::string_view SkyrimWorldView::artModelById(FormId id)
    {
        const ArtRecord* art = mCombatIndex == nullptr || id == kNullFormId ? nullptr : mCombatIndex->art(id);
        return art == nullptr ? std::string_view() : std::string_view(art->mModel);
    }

    MagicSettings SkyrimWorldView::magicSettings()
    {
        return mCombatIndex == nullptr ? MagicSettings{} : mCombatIndex->settings().magicSettings();
    }

    const WeaponRecord* SkyrimWorldView::weaponByEditorId(std::string_view editorId)
    {
        if (mCombatIndex == nullptr)
            return nullptr;
        auto lower = [](std::string_view text) {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        };
        const std::string wanted = lower(editorId);
        for (const FormId id : mCombatIndex->weaponIds())
        {
            const WeaponRecord* weapon = mCombatIndex->weapon(id);
            if (weapon != nullptr && lower(weapon->mCommon.mEditorId) == wanted)
                return weapon;
        }
        return nullptr;
    }

    bool SkyrimWorldView::raceIsChild(FormId race)
    {
        // RACE DATA +32 bit 2 (0x4): the Child flag. VERIFIED by the data: every *RaceChild race has it, no adult race
        // does.
        const RaceRecord* record = mActorIndex ? mActorIndex->race(race) : nullptr;
        return record != nullptr && (record->mFlags & 0x4u) != 0;
    }

    const PerkLibrary* SkyrimWorldView::perkLibrary()
    {
        return mCombatIndex.get();
    }

    const SoundLibrary* SkyrimWorldView::soundLibrary()
    {
        return mCombatIndex == nullptr ? nullptr : &mCombatIndex->sounds();
    }

    bool SkyrimWorldView::soundFileExists(const std::string& vfsPath)
    {
        return mFiles == nullptr || mFiles->exists(vfsPath);
    }

    FormId SkyrimWorldView::perkByEditorId(std::string_view editorId)
    {
        return mCombatIndex == nullptr ? kNullFormId : mCombatIndex->perkByEditorId(std::string(editorId));
    }

    bool SkyrimWorldView::progressionInputs(
        const progression::SkillTable*& skills, const PerkLibrary*& perks, progression::Settings& settings)
    {
        if (!combatReady() || mCombatIndex == nullptr)
            return false;
        skills = &mCombatIndex->skillTable();
        perks = mCombatIndex.get();
        settings = progression::settingsFrom(mCombatIndex->settings());
        return true;
    }

    void SkyrimWorldView::setProgressionPlayer(
        std::uint16_t level, const std::array<std::uint8_t, progression::kSkillCount>& skills)
    {
        mHaveProgressionPlayer = true;
        mProgressionLevel = level;
        mProgressionSkills = skills;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
        {
            PlayerStandIn player = mDialogue->mRuntime->player();
            player.mLevel = level;
            player.mSkills = skills;
            mDialogue->mRuntime->setPlayer(player);
            if (mDialogue->mQuestWorld != nullptr)
                mDialogue->mQuestWorld->setPlayerLevel(level); // task 0305
        }
    }

    FighterProfile SkyrimWorldView::playerFighter()
    {
        // The player has no records: a level 1 stand-in with skills at kStandInSkill, no armour, a Nord's unarmed hit.
        FighterProfile out;
        out.mHealthMax = 100.f;
        out.mStaminaMax = 100.f;
        out.mMagickaMax = 100.f; // task 0186: the HUD's old stand-in, now a real value
        out.mHealthRegenPercent = 0.7f;
        out.mStaminaRegenPercent = 5.f;
        out.mMagickaRegenPercent = 3.f; // a Nord's (RACE DATA +88)
        out.mMitigationPercent = 0.f;
        out.mUnarmedDamage = 4.f;
        out.mUnarmedReach = 96.f;
        if (mCombatIndex != nullptr) // task 0193: a Nord's body and bare hands
        {
            out.mBodyMaterial = mCombatIndex->sounds().materialByEditorId(kPlayerBodyMaterial);
            out.mUnarmedImpactSet = mCombatIndex->sounds().impactSetByEditorId(kPlayerUnarmedImpactSet);
        }
        out.mSkills.fill(15);
        return out;
    }

    bool SkyrimWorldView::fighter(openeden::sdk::game::WorldId base, FighterProfile& out)
    {
        return fighterFor(base, 0, out);
    }

    FormId SkyrimWorldView::npcByEditorId(std::string_view editorId)
    {
        if (!mNpcByEditorIdBuilt)
        {
            mNpcByEditorIdBuilt = true;
            for (const PluginData& plugin : mPlugins)
                for (const NpcRecord& npc : plugin.mParsed.mNpcs)
                {
                    std::string key = npc.mCommon.mEditorId;
                    for (char& c : key)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    mNpcByEditorId[key] = npc.mCommon.mId;
                }
        }
        std::string key(editorId);
        for (char& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto it = mNpcByEditorId.find(key);
        return it == mNpcByEditorId.end() ? kNullFormId : it->second;
    }

    double SkyrimWorldView::hoursToRespawnCell() const
    {
        if (mCombatIndex == nullptr)
            return 240.0;
        return static_cast<double>(mCombatIndex->settings().get("iHoursToRespawnCell", 240.f));
    }

    bool SkyrimWorldView::fighterFor(openeden::sdk::game::WorldId base, std::uint32_t variation, FighterProfile& out)
    {
        out = {};
        // Task 0216: with `creatures` on the placement's own number picks a leveled template, as in describeActor.
        const std::uint32_t pick = mCreatures ? variation : 0;
        if (mCombatIndex == nullptr || mActorIndex == nullptr)
            return false; // not read yet: combatReady() starts that and says when
        const FormId npc = static_cast<FormId>(base);
        openeden::sdk::game::ActorStats stats;
        std::string error;
        if (!computeActorStats(*mActorIndex, npc, pick, mStandInPlayerLevel, stats, error))
            return false;
        const CombatSettings combat = mCombatIndex->settings().combatSettings();
        ArmourResult armour;
        std::string armourError;
        if (computeActorArmour(*mActorIndex, combat, npc, pick, mStandInPlayerLevel, armour, armourError))
            out.mMitigationPercent = armour.mMitigationPercent;
        out.mHealthMax = stats.health;
        out.mStaminaMax = stats.stamina;
        out.mMagickaMax = stats.magicka;
        out.mSkills = stats.skills;
        ActorIndex::Resolved resolved;
        out.mBase = npc;
        out.mLevel = stats.level;
        out.mFemale = stats.female;
        {
            std::string pieceError;
            (void)wornArmour(*mActorIndex, npc, pick, mStandInPlayerLevel, out.mPieces, pieceError);
        }
        if (mActorIndex->resolve(npc, mStandInPlayerLevel, pick, resolved) == ActorIndex::ResolveStatus::Ok)
        {
            // Task 0179: what the perk conditions ask about. PRKR follows the template's "spell list" use flag
            // (RECALLED-VERIFY: the flag that covers it).
            out.mRace = resolved.mRace;
            if (mCreatures)
            {
                // The creature on screen is resolved at the appearance's level with the same number (describeActor),
                // not at the stand-in player's, so a leveled stand-in fights as the creature it shows.
                ActorIndex::Resolved shown;
                if (mActorIndex->resolve(npc, kAppearanceLevel, pick, shown) == ActorIndex::ResolveStatus::Ok)
                    if (const RaceRecord* creatureRace = mActorIndex->race(shown.mRace))
                    {
                        out.mCreature
                            = creatureProfileFor(creatureRace->skeleton(out.mFemale ? Sex::Female : Sex::Male));
                        if (out.mCreature == nullptr)
                            out.mCreature = creatureProfileFor(creatureRace->skeleton(Sex::Male));
                        if (out.mCreature == nullptr && mDragons)
                            out.mCreature = dragons::dragonProfileFor(creatureRace->skeleton(Sex::Male));
                    }
            }
            if (const ActorNpc* source = mActorIndex->npc(resolved.mSpellsFrom))
            {
                out.mPerks = source->mPerks;
                out.mSpells = source->mSpells;
            }
            if (const ActorNpc* own = mActorIndex->npc(npc)) // task 0254
            {
                out.mUnique = (own->mFlags & NpcFlags::Unique) != 0;
                out.mEssential = (own->mFlags & NpcFlags::Essential) != 0;
            }
            if (const ActorNpc* keywords = mActorIndex->npc(resolved.mKeywordsFrom))
                out.mKeywords = keywords->mKeywords;
            if (const RaceRecord* actorRace = mActorIndex->race(resolved.mRace))
            {
                out.mKeywords.insert(out.mKeywords.end(), actorRace->mKeywords.begin(), actorRace->mKeywords.end());
                out.mSpells.insert(out.mSpells.end(), actorRace->mSpells.begin(), actorRace->mSpells.end());
            }
            // Task 0204: the crime faction of the NPC (CRIF), from the NPC that owns the faction list after templates.
            if (const ActorNpc* crimeSource = mActorIndex->npc(resolved.mFactionsFrom))
                out.mAi.mCrimeFaction = crimeSource->mCrimeFaction;
            if (out.mAi.mCrimeFaction == kNullFormId)
                if (const ActorNpc* own = mActorIndex->npc(npc))
                    out.mAi.mCrimeFaction = own->mCrimeFaction;
            for (const FactionMembership& membership : resolved.mFactions)
            {
                out.mFactions.push_back(membership.mFaction);
                // Task 0197: a guard is a member of the game's own guard faction.
                if (const FactionRecord* faction = mActorIndex->faction(membership.mFaction))
                    if (equalsLower(faction->mCommon.mEditorId, Factions::kGuardFactionEditorId))
                        out.mAi.mGuard = true;
            }
            // Task 0190: what the combat AI reads: AIDT (from the NPC that owns the AI data after templates), the
            // combat style it names (ZNAM; the defaults of DefaultCombatstyle when none) and whether its relation to
            // the player makes it hostile on sight.
            if (const ActorNpc* aiOwner = mActorIndex->npc(resolved.mAiFrom))
            {
                out.mAi.mKnown = true;
                if (aiOwner->mAi)
                {
                    out.mAi.mAggression = aiOwner->mAi->mAggression;
                    out.mAi.mConfidence = aiOwner->mAi->mConfidence;
                    out.mAi.mAssistance = aiOwner->mAi->mAssistance; // task 0197
                }
                if (aiOwner->mCombatStyle != kNullFormId)
                    if (const CombatStyleRecord* style = mCombatIndex->style(aiOwner->mCombatStyle))
                        out.mAi.mStyle = *style;
                openeden::sdk::game::ActorDisposition disposition;
                std::string dispositionError;
                MechanicsOptions options;
                options.mPlayerLevel = mStandInPlayerLevel;
                options.mHostileFactions = mHostileFactions; // task 0316
                if (computeActorDisposition(*mActorIndex, npc, pick, options, disposition, dispositionError))
                {
                    out.mAi.mHostileOnSight = disposition.hostileOnSight;
                    out.mAi.mRelationToPlayer = static_cast<std::uint8_t>(disposition.relationToPlayer); // task 0197
                }
            }
            if (const RaceCombatRecord* race = mCombatIndex->race(resolved.mRace))
            {
                out.mHealthRegenPercent = race->mHealthRegen;
                out.mStaminaRegenPercent = race->mStaminaRegen;
                out.mMagickaRegenPercent = race->mMagickaRegen;
                out.mUnarmedDamage = race->mUnarmedDamage;
                out.mUnarmedReach = race->mUnarmedReach;
                out.mBodyMaterial = race->mBodyMaterial; // task 0193
                out.mUnarmedImpactSet = race->mUnarmedImpactSet;
                out.mAttackSpells = race->mAttackSpells; // task 0240
            }
        }
        return true;
    }

    // Task 0214: the weapon, shield and inventory armour of an NPC (leveled lists resolved as the armour's are).
    bool SkyrimWorldView::fighterGear(
        openeden::sdk::game::WorldId base, const std::vector<ArmourPiece>& worn, FighterGear& out)
    {
        out = {};
        if (mCombatIndex == nullptr || mActorIndex == nullptr)
            return false;
        return resolveFighterGear(
            *mActorIndex, *mCombatIndex, static_cast<FormId>(base), 0, kAppearanceLevel, worn, out);
    }

    bool SkyrimWorldView::describeActor(openeden::sdk::game::WorldId base, std::uint32_t variation,
        openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        out = {};
        if (!awaitIndex())
        {
            error = "the world index is not available";
            return false;
        }
        if (!ensureActorIndex())
        {
            error = "the actor records are not available";
            return false;
        }
        startDialoguePrefetch(); // task 0141: the conversation records are read while the cell loads
        FaceFileProbe probe;
        if (mFiles != nullptr)
        {
            probe.mOrder = &mOrder;
            const FileSource* files = mFiles;
            probe.mExists = [files](const std::string& path) { return files->exists(path); };
        }
        AppearanceLimits limits;
        limits.mCreatures = mCreatures; // task 0212
        limits.mDragons = mDragons; // task 0253
        limits.mHeldGear = mHeldGear; // task 0220
        return makeActorAppearance(*mActorIndex, static_cast<FormId>(base), variation, out, error, limits, &probe);
    }

    // ---- the player's body (task 0222) -----------------------------------------------------------------------------

    void SkyrimWorldView::setPlayerEquipment(std::vector<FormId> armour, FormId weapon)
    {
        std::sort(armour.begin(), armour.end());
        armour.erase(std::unique(armour.begin(), armour.end()), armour.end());
        if (armour == mPlayerArmour && weapon == mPlayerWeapon)
            return;
        mPlayerArmour = std::move(armour);
        mPlayerWeapon = weapon;
        ++mPlayerBodyRevision;
    }

    void SkyrimWorldView::setBeastForm(bool on)
    {
        if (on == mBeastForm)
            return;
        mBeastForm = on;
        ++mPlayerBodyRevision;
    }

    void SkyrimWorldView::setVampireLook(bool on)
    {
        if (on == mVampireLook)
            return;
        mVampireLook = on;
        ++mPlayerBodyRevision;
    }

    bool SkyrimWorldView::beastNumbers(std::uint32_t playerLevel, werewolf::BeastNumbers& out)
    {
        out = {};
        if (mCombatIndex == nullptr)
        {
            (void)combatReady();
            if (mCombatIndex == nullptr)
                return false;
        }
        const RaceCombatRecord* race = mCombatIndex->race(werewolf::kBeastRace);
        if (race == nullptr)
            return false;
        out.mUnarmedDamage = race->mUnarmedDamage;
        out.mUnarmedReach = race->mUnarmedReach;
        out.mBodyMaterial = race->mBodyMaterial;
        out.mUnarmedImpactSet = race->mUnarmedImpactSet;
        // the level ability's Fortify Unarmed Damage (PlayerWerewolfChangeScript.StartTracking adds it)
        if (const SpellRecord* ability = mCombatIndex->spell(werewolf::levelAbility(playerLevel)))
            for (const SpellEffect& effect : ability->mEffects)
                if (effect.mEffect == werewolf::kFortifyUnarmedEffect && std::isfinite(effect.mMagnitude))
                    out.mUnarmedDamage += effect.mMagnitude;
        return true;
    }

    bool SkyrimWorldView::baseIsPerson(openeden::sdk::game::WorldId base)
    {
        if (mActorIndex == nullptr)
            return false;
        ActorIndex::Resolved resolved;
        if (mActorIndex->resolve(static_cast<FormId>(base), 1, 0, resolved) != ActorIndex::ResolveStatus::Ok)
            return false;
        const RaceRecord* race = mActorIndex->race(resolved.mRace);
        if (race == nullptr)
            return false;
        std::string path = race->skeleton(Sex::Male);
        for (char& c : path)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return path.find("actors/character/") != std::string::npos;
    }

    void SkyrimWorldView::setPlayerLook(
        FormId race, bool female, std::vector<std::pair<std::uint32_t, FormId>> headChoice)
    {
        if (mPlayerLookSet && race == mPlayerRace && female == mPlayerFemale && headChoice == mPlayerHeadChoice)
            return;
        mPlayerLookSet = true;
        mPlayerRace = race;
        mPlayerFemale = female;
        mPlayerHeadChoice = std::move(headChoice);
        ++mPlayerBodyRevision;
    }

    bool SkyrimWorldView::characterAccess(character::Access& out)
    {
        if (mActorIndex == nullptr)
        {
            (void)combatReady(); // adopts the index once the worker has read it
            if (mActorIndex == nullptr)
                return false;
        }
        out.mIndex = mActorIndex.get();
        // The names and descriptions of the records come from the plugins' own string tables, read when first needed
        // (not through the dialogue data: that is read a good while after the start).
        out.mText = [this](const LString& text, std::uint32_t plugin, StringTableKind kind) -> std::string {
            if (!text.mPresent)
                return std::string();
            if (!text.mLocalized)
                return text.mText;
            if (mFiles == nullptr || plugin >= mPlugins.size())
                return std::string();
            auto& slot = mCharacterTables[{ plugin, static_cast<int>(kind) }];
            if (slot == nullptr)
            {
                std::string bytes;
                const std::string path = stringTablePath(stemOf(mPlugins[plugin].mPath), mOptions.mLanguage, kind);
                auto table = std::make_unique<StringTable>();
                Content::Error tableError;
                if (mFiles->read(path, bytes)
                    && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, kind,
                        mPlugins[plugin].mName, *table, tableError))
                    slot = std::move(table);
                else
                    slot = std::make_unique<StringTable>(); // empty: not tried again
            }
            const std::string* found = slot->find(text.mId);
            return found != nullptr ? *found : std::string();
        };
        return true;
    }

    void SkyrimWorldView::setPlayerName(std::string name)
    {
        if (name == mPlayerName)
            return;
        mPlayerName = std::move(name);
        ++mPlayerNameRevision;
    }

    bool SkyrimWorldView::playerIdentity(openeden::sdk::game::PlayerIdentityState& out)
    {
        out = {};
        if (mPlayerName.empty())
            return true; // revision 0: no name yet
        out.revision = mPlayerNameRevision;
        out.name = mPlayerName;
        return true;
    }

    bool SkyrimWorldView::playerBody(openeden::sdk::game::PlayerBodyState& out)
    {
        out = {};
        if (!mPlayerBodyOn)
            return false;
        // Not ready (revision 0) until the world index is built and the actor records are in: a worker reads them
        // (the combat index's), the main thread adopts them here, so this call never waits.
        if (mActorIndex == nullptr)
        {
            (void)combatReady();
            return true;
        }
        out.revision = mPlayerBodyRevision + 1; // never 0
        out.firstPerson = mFirstPersonBodyOn && !mBeastForm; // task 0245 (task 0259: a werewolf is third person only)
        return true;
    }

    bool SkyrimWorldView::describeFirstPersonBody(openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        out = {};
        if (!mPlayerBodyOn || !mFirstPersonBodyOn)
        {
            error = "the first-person body is off";
            return false;
        }
        if (mActorIndex == nullptr)
        {
            error = "the actor records are not read yet";
            return false;
        }
        PlayerBodyRequest request;
        request.mFemale = mStandInFemale;
        if (mPlayerLookSet)
        {
            request.mRace = mPlayerRace;
            request.mFemale = mPlayerFemale;
        }
        request.mWornArmour = mPlayerArmour;
        request.mWeapon = mPlayerWeapon;
        AppearanceLimits limits;
        limits.mHeldGear = true;
        limits.mHeldBow = true; // task 0249: the arms hold a bow in the left hand
        return makeFirstPersonAppearance(*mActorIndex, request, out, error, limits);
    }

    bool SkyrimWorldView::describePlayerBody(openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        out = {};
        if (!mPlayerBodyOn)
        {
            error = "the player body is off";
            return false;
        }
        if (mActorIndex == nullptr)
        {
            error = "the actor records are not read yet";
            return false;
        }
        PlayerBodyRequest request;
        request.mFemale = mStandInFemale;
        if (mBeastForm)
        {
            // Task 0259: the beast's body: its race and skin, no clothes, no gear (it has none), the werewolf's clips
            request.mRace = werewolf::kBeastRace;
            request.mFemale = false;
            request.mStartOutfit = kNullFormId;
            AppearanceLimits beast;
            beast.mCreatures = true;
            if (!makePlayerAppearance(*mActorIndex, request, out, error, beast))
                return false;
            const CreatureProfile& row = werewolf::bareProfile();
            out.animationProject = std::string(row.mProject);
            out.idleClip = std::string(row.mIdle);
            out.walkClip = std::string(row.mWalk);
            out.runClip = std::string(row.mRun);
            out.walkSpeed = row.mWalkSpeed;
            out.runSpeed = row.mRunSpeed;
            out.walkStride = row.mWalkStride;
            out.runStride = row.mRunStride;
            return true;
        }
        if (mPlayerLookSet)
        {
            request.mRace = mPlayerRace;
            request.mFemale = mPlayerFemale;
            request.mHeadChoice = mPlayerHeadChoice;
        }
        request.mWornArmour = mPlayerArmour;
        request.mWeapon = mPlayerWeapon;
        AppearanceLimits limits;
        limits.mHeldGear = true; // the player's own weapon and shield are always shown with his body
        if (mVampireLook)
        {
            // Task 0279: VampireChange's SetRace: the vampire race of his own (its skin); a race the body cannot be
            // made from falls back to his own, so the player is never left undrawn.
            PlayerBodyRequest vampire = request;
            if (vampire.mRace == kNullFormId)
                vampire.mRace = vampirism::kDefaultPlayerRace;
            vampire.mRace = vampirism::vampireRaceOf(vampire.mRace);
            if (makePlayerAppearance(*mActorIndex, vampire, out, error, limits))
                return true;
            out = {};
        }
        return makePlayerAppearance(*mActorIndex, request, out, error, limits);
    }

    // ---- actor numbers and relations (task 0154, stage S7) ---------------------------------------------------------

    bool SkyrimWorldView::actorStats(openeden::sdk::game::WorldId base, std::uint32_t variation,
        std::uint16_t playerLevel, openeden::sdk::game::ActorStats& out, std::string& error)
    {
        out = {};
        if (!awaitIndex() || !ensureActorIndex())
        {
            error = "the actor records are not available";
            return false;
        }
        return computeActorStats(*mActorIndex, static_cast<FormId>(base), variation,
            playerLevel != 0 ? playerLevel : mStandInPlayerLevel, out, error);
    }

    bool SkyrimWorldView::actorLevelOf(FormId base, std::int32_t& level)
    {
        openeden::sdk::game::ActorStats stats;
        std::string error;
        if (!actorStats(base, 0, 0, stats, error))
            return false;
        level = stats.level;
        return true;
    }

    bool SkyrimWorldView::actorRaceOf(FormId base, FormId& race)
    {
        if (!awaitIndex() || !ensureActorIndex())
            return false;
        ActorIndex::Resolved resolved;
        if (mActorIndex->resolve(base, mStandInPlayerLevel, 0, resolved) != ActorIndex::ResolveStatus::Ok
            || resolved.mRace == kNullFormId)
            return false;
        race = resolved.mRace;
        return true;
    }

    bool SkyrimWorldView::actorDisposition(openeden::sdk::game::WorldId base, std::uint32_t variation,
        openeden::sdk::game::ActorDisposition& out, std::string& error)
    {
        out = {};
        if (!awaitIndex() || !ensureActorIndex())
        {
            error = "the actor records are not available";
            return false;
        }
        MechanicsOptions options;
        options.mPlayerLevel = mStandInPlayerLevel;
        options.mHostileFactions = mHostileFactions; // task 0316
        return computeActorDisposition(*mActorIndex, static_cast<FormId>(base), variation, options, out, error);
    }

    openeden::sdk::game::ActorRelation SkyrimWorldView::relationBetween(openeden::sdk::game::WorldId baseA,
        std::uint32_t variationA, openeden::sdk::game::WorldId baseB, std::uint32_t variationB)
    {
        if (!awaitIndex() || !ensureActorIndex())
            return openeden::sdk::game::ActorRelation::Neutral;
        return relationBetweenActors(
            *mActorIndex, static_cast<FormId>(baseA), variationA, static_cast<FormId>(baseB), variationB);
    }

    // ---- conversations (task 0141, M-S4 slice 1) -----------------------------------------------------------------

    std::unique_ptr<SkyrimWorldView::DialogueData> SkyrimWorldView::buildDialogueData(
        const std::vector<std::pair<std::filesystem::path, std::string>>& plugins, const LoadOrder& order,
        bool mapMarkers, bool loadingScreens)
    {
        auto data = std::make_unique<DialogueData>();
        const DialogueLimits dialogueLimits;
        const QuestLimits questLimits;
        const PackageLimits packageLimits;
        const LocationLimits locationLimits;
        const QuestScriptLimits questScriptLimits;
        const PlacedRefLimits placedLimits;
        std::chrono::steady_clock::duration placedTime{};
        for (const auto& [path, name] : plugins)
        {
            Content::Error openError;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(path, openError);
            if (file == nullptr)
            {
                // Keep the plugin numbering of the index: an empty plugin takes the slot.
                data->mDialogue.add(DialogueRecords(), FormIdResolver(Content::Header(), name, order));
                data->mQuests.add(QuestRecords(), FormIdResolver(Content::Header(), name, order));
                data->mPackages.add(PackageRecords(), FormIdResolver(Content::Header(), name, order));
                data->mLocations.add(LocationRecords());
                data->mMapMarkers.add(map::MarkerRecords());
                data->mLoadingScreens.add(
                    menus::LoadingScreenRecords(), FormIdResolver(Content::Header(), name, order));
                data->mQuestScripts.add(QuestScriptRecords());
                data->mQuestVmads.add(QuestVmadRecords());
                data->mStory.add(StoryRecords(), FormIdResolver(Content::Header(), name, order));
                data->mScenes.add(SceneRecords(), FormIdResolver(Content::Header(), name, order));
                continue;
            }
            DialogueRecords dialogue;
            (void)parseDialogueRecords(*file, order, dialogueLimits, dialogue);
            data->mDialogue.add(std::move(dialogue), FormIdResolver(file->header(), name, order));
            QuestRecords quests;
            (void)parseQuestRecords(*file, order, questLimits, quests);
            data->mQuests.add(std::move(quests), FormIdResolver(file->header(), name, order));
            StoryRecords story;
            (void)parseStoryRecords(*file, order, StoryLimits(), story);
            data->mStory.add(std::move(story), FormIdResolver(file->header(), name, order));
            SceneRecords scenes;
            (void)parseSceneRecords(*file, order, SceneLimits(), scenes);
            data->mScenes.add(std::move(scenes), FormIdResolver(file->header(), name, order));
            PackageRecords packages;
            (void)parsePackageRecords(*file, order, packageLimits, packages);
            data->mPackages.add(std::move(packages), FormIdResolver(file->header(), name, order));
            LocationRecords locations;
            (void)parseLocationRecords(*file, order, locationLimits, locations);
            data->mLocations.add(std::move(locations));
            {
                QuestVmadRecords vmads;
                (void)parseQuestVmads(*file, order, vmads);
                data->mQuestVmads.add(std::move(vmads));
            }
            QuestScriptRecords questScripts;
            (void)parseQuestScripts(*file, order, questScriptLimits, questScripts);
            data->mQuestScripts.add(std::move(questScripts));
            const auto placedStart = std::chrono::steady_clock::now();
            PlacedRefRecords placed;
            (void)parsePlacedRefs(*file, order, placedLimits, placed);
            data->mPlacedRefs.add(std::move(placed));
            {
                map::MarkerRecords markers; // task 0201
                if (mapMarkers)
                    (void)map::parseMarkers(*file, order, map::MarkerLimits(), markers);
                data->mMapMarkers.add(std::move(markers));
            }
            {
                menus::LoadingScreenRecords screens; // task 0207
                if (loadingScreens)
                    (void)menus::parseLoadingScreens(*file, order, menus::LoadingScreenLimits(), screens);
                data->mLoadingScreens.add(std::move(screens), FormIdResolver(file->header(), name, order));
            }
            placedTime += std::chrono::steady_clock::now() - placedStart;
        }
        const auto finishStart = std::chrono::steady_clock::now();
        data->mPlacedRefs.finish();
        data->mMapMarkers.finish();
        placedTime += std::chrono::steady_clock::now() - finishStart;
        data->mPlacedRefsMillis
            = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(placedTime).count());
        return data;
    }

    void SkyrimWorldView::startDialoguePrefetch()
    {
        if (mDialogue != nullptr || mDialogueThread.joinable())
            return;
        std::vector<std::pair<std::filesystem::path, std::string>> plugins;
        for (const PluginData& plugin : mPlugins)
            plugins.emplace_back(plugin.mPath, plugin.mName);
        const bool mapMarkers = mWantMapMarkers;
        const bool loadingScreens = mWantLoadingScreens;
        mDialogueThread
            = std::thread([this, plugins = std::move(plugins), order = mOrder, mapMarkers, loadingScreens]() {
                  std::unique_ptr<DialogueData> data = buildDialogueData(plugins, order, mapMarkers, loadingScreens);
                  mPrefetched = std::move(data);
                  mDialogueReady.store(true);
              });
    }

    bool SkyrimWorldView::ensureDialogue()
    {
        if (mDialogue != nullptr)
            return mDialogue->mRuntime != nullptr;
        if (!ensureActorIndex())
            return false;
        auto state = std::make_unique<DialogueState>();
        if (mDialogueThread.joinable())
        {
            if (!mDialogueReady.load())
                return false; // still being read: the caller may ask again; nothing blocks the main thread
            mDialogueThread.join();
            state->mData = std::move(mPrefetched);
        }
        else
        {
            std::vector<std::pair<std::filesystem::path, std::string>> plugins;
            for (const PluginData& plugin : mPlugins)
                plugins.emplace_back(plugin.mPath, plugin.mName);
            state->mData = buildDialogueData(plugins, mOrder, mWantMapMarkers, mWantLoadingScreens);
        }
        DialogueState* raw = state.get();
        raw->mResolver = [this, raw](const LString& text, std::uint32_t plugin, StringTableKind kind) -> std::string {
            if (!text.mPresent)
                return std::string();
            if (!text.mLocalized)
                return text.mText;
            if (mFiles == nullptr || plugin >= mPlugins.size())
                return std::string();
            auto& slot = raw->mTables[{ plugin, static_cast<int>(kind) }];
            if (slot == nullptr)
            {
                std::string bytes;
                const std::string path = stringTablePath(stemOf(mPlugins[plugin].mPath), mOptions.mLanguage, kind);
                auto table = std::make_unique<StringTable>();
                Content::Error tableError;
                if (mFiles->read(path, bytes)
                    && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, kind,
                        mPlugins[plugin].mName, *table, tableError))
                    slot = std::move(table);
                else
                {
                    ++raw->mTablesMissing;
                    slot = std::make_unique<StringTable>(); // empty: not tried again
                }
            }
            const std::string* found = slot->find(text.mId);
            return found != nullptr ? *found : std::string();
        };
        raw->mRuntime = std::make_unique<DialogueRuntime>(*mActorIndex, raw->mData->mDialogue, raw->mData->mQuests,
            QuestState::initial(raw->mData->mQuests), raw->mResolver);
        {
            // Task 0228: the text tags of conversations; the alias names are the journal's own function.
            DialogueTagSources tags;
            tags.mAliasName = [this](FormId quest, std::string_view alias) { return aliasDisplayName(quest, alias); };
            tags.mAliasFemale = [this](FormId quest, std::string_view alias) { return aliasFemale(quest, alias); };
            raw->mRuntime->setTagSources(std::move(tags));
        }
        if (mFiles != nullptr)
        {
            // Task 0143: a voice line is named only when its .fuz exists (loose, in a mod or in "Skyrim -
            // Voices_*.bsa", whichever the session's VFS holds).
            const FileSource* files = mFiles;
            raw->mRuntime->setVoiceProbe([files](const std::string& path) { return files->exists("sound/" + path); });
        }
        {
            // Task 0155: the stand-in player of the speech checks, and the reading of fragment scripts that names a
            // service line (barter, training ...).
            PlayerStandIn player;
            player.mLevel = mStandInPlayerLevel;
            player.mSkills[*skillSlotOfActorValue(kAvSpeechcraft)] = mStandInSpeech;
            player.mGold = mStandInGold;
            player.mFemale = mStandInFemale;
            if (mHaveProgressionPlayer) // task 0200
            {
                player.mLevel = mProgressionLevel;
                player.mSkills = mProgressionSkills;
            }
            raw->mRuntime->setPlayer(player);
            if (mGoldSource)
                raw->mRuntime->setGoldSource(mGoldSource); // task 0184
            if (mWornSource)
                raw->mRuntime->setPlayerWornSource(mWornSource); // task 0270
            if (mItemSource)
                raw->mRuntime->setPlayerItemSource(mItemSource);
            // Task 0156: locations, and the quest scripts' values before any script ran (the .pex through the VFS).
            raw->mRuntime->setLocations(&raw->mData->mLocations);
            raw->mRuntime->setKeywordData(&mKeywordData); // task 0217
            if (mFiles != nullptr)
            {
                const FileSource* files = mFiles;
                raw->mQuestVariables = std::make_unique<QuestVariables>(
                    raw->mData->mQuestScripts, [files](const std::string& script, std::vector<std::uint8_t>& bytes) {
                        std::string text;
                        if (!files->read("scripts/" + ScriptPapyrus::Pex::lowered(script) + ".pex", text))
                            return false;
                        bytes.assign(reinterpret_cast<const std::uint8_t*>(text.data()),
                            reinterpret_cast<const std::uint8_t*>(text.data()) + text.size());
                        return true;
                    });
                raw->mRuntime->setQuestVariables(raw->mQuestVariables.get());
            }
            if (mFiles != nullptr)
            {
                const FileSource* files = mFiles;
                raw->mRuntime->setScriptProbe([files](const std::string& script) {
                    std::vector<std::string> names;
                    std::string bytes;
                    std::string path = "scripts/" + ScriptPapyrus::Pex::lowered(script) + ".pex";
                    if (!files->read(path, bytes))
                        return names;
                    const ScriptPapyrus::Pex::ParseResult parsed = ScriptPapyrus::Pex::parse(
                        { reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, path);
                    if (!parsed.mOk)
                        return names;
                    names.reserve(parsed.mFile.mStrings.size());
                    for (const std::string& text : parsed.mFile.mStrings)
                        names.push_back(ScriptPapyrus::Pex::lowered(text));
                    return names;
                });
            }
        }
        raw->mPackageQuests = QuestState::initial(raw->mData->mQuests);
        raw->mPackageWorld = std::make_unique<SkyrimConditionWorld>(*mActorIndex, raw->mData->mDialogue);
        raw->mPackageWorld->setQuests(&raw->mData->mQuests, &raw->mPackageQuests);
        if (mDeadLookup)
            raw->mPackageWorld->setDeadLookup(mDeadLookup); // task 0337: a package asks GetDead of a killed alias
        // task 0337: a package condition run on the actor's LINKED REFERENCE (Eltrys is linked to Weylin)
        raw->mPackageWorld->setLinkedRefLookup(
            [this](FormId reference, FormId keyword, FormId& out) { return linkedRef(reference, keyword, out); });
        // Task 0160: other references, packages, and the package brain's current package for a conversation
        mPlacedRefsMillis.store(raw->mData->mPlacedRefsMillis);
        raw->mRuntime->setPlacedRefs(&raw->mData->mPlacedRefs);
        raw->mRuntime->setPackages(&raw->mData->mPackages);
        if (mCurrentPackageLookup)
            raw->mRuntime->setCurrentPackageLookup(mCurrentPackageLookup);
        if (mVendorServicesLookup)
            raw->mRuntime->setVendorServicesLookup(mVendorServicesLookup); // task 0189
        if (mDeadLookup)
            raw->mRuntime->setDeadLookup(mDeadLookup); // task 0326
        raw->mRuntime->setLinkedRefLookup(
            [this](FormId reference, FormId keyword, FormId& out) { return linkedRef(reference, keyword, out); });
        raw->mRuntime->setFollowerOverlay(mFollowerOverlay); // task 0239
        if (mLiveActorPlace)
        {
            auto target = [this](std::uint32_t reference, double& x, double& y, double& z) {
                return locate(reference, x, y, z);
            };
            raw->mRuntime->setLivePlaces(mLiveActorPlace, target);
        }
        if (mCrimeSourceSet)
            raw->mRuntime->setCrimeSource(mCrimeSource); // task 0258
        raw->mRuntime->setOpenServices(mOpenServices); // task 0219
        raw->mRuntime->setBlockingBranches(mBlockingBranches); // task 0298
        raw->mRuntime->setQuestGreetings(mQuestGreetings); // task 0304
        raw->mRuntime->setWalkAwayTopics(mWalkAwayTopics); // task 0315
        raw->mRuntime->setWalkAwayInfos(mWalkAwayInfos); // task 0326
        raw->mRuntime->setTrace(mDialogueTrace);
        raw->mRuntime->setFragmentFlush(mFragmentFlush);
        raw->mRuntime->setRankMinusOneMembers(mRankMinusOneMembers); // task 0261
        // Task 0187: aliases and the story manager
        if (mInfoFragmentSink)
            raw->mRuntime->setFragmentSink(mInfoFragmentSink);
        {
            raw->mQuestWorld = std::make_unique<SkyrimConditionWorld>(*mActorIndex, raw->mData->mDialogue);
            SkyrimConditionWorld* world = raw->mQuestWorld.get();
            world->setPlayerLevel(mHaveProgressionPlayer ? mProgressionLevel : mStandInPlayerLevel); // task 0305
            world->setActorEditorLocation(mActorEditorLocation); // task 0342
            world->setCreatedActorBase([this](FormId reference) { return createdActorBase(reference); }); // task 0276
            world->setQuests(&raw->mData->mQuests, &raw->mRuntime->questsMutable());
            world->setLocations(&raw->mData->mLocations);
            world->setKeywordData(&mKeywordData); // task 0217
            world->setPlacedRefs(&raw->mData->mPlacedRefs);
            world->setQuestVariables(raw->mQuestVariables.get());
            world->setDetectsPlayerInCell(mDetectsPlayer); // task 0335
            if (mPlayerPlaceKnown && (mLiveActorPlace || mDetectsPlayer)) // tasks 0327, 0335
                world->setPlayerPlace(true, mPlayerInterior, mPlayerCell, mPlayerWorld);
            if (mLiveActorPlace) // task 0327
                world->setLivePlaces(mLiveActorPlace, [this](std::uint32_t reference, double& x, double& y, double& z) {
                    return locate(reference, x, y, z);
                });
            raw->mQuestExtras = storyAndLocationExtraFunctions(); // task 0211
            raw->mQuestExtras.insert(
                raw->mQuestExtras.end(), sceneExtraFunctions().begin(), sceneExtraFunctions().end());
            for (const conditions::ExtraFunction& extra : aliasLocationExtraFunctions())
                if (extra.mIndex != 567 || mAnswerEditorLocAlias) // task 0230: the measurement switch
                    raw->mQuestExtras.push_back(extra);
            raw->mQuestEvaluator.setExtraFunctions(raw->mQuestExtras);
            raw->mAliasFiller = std::make_unique<QuestAliasFiller>(raw->mData->mQuests, raw->mData->mPlacedRefs,
                [this, raw, world](const QuestRecord& quest, const QuestAlias& alias, FormId reference, FormId base) {
                    DialogueSpeaker speaker;
                    speaker.mBase = base;
                    speaker.mReference = reference;
                    if (const PlacedEntry* entry = raw->mData->mPlacedRefs.find(reference))
                        fillSpeakerCell(speaker, entry->mCell);
                    world->setStoryEvent(nullptr);
                    world->setSubjectLocation(kNullFormId);
                    world->setSpeaker(speaker);
                    world->setQuestContext(quest.mCommon.mId);
                    world->setPlugin(&raw->mData->mQuests.resolverOf(quest));
                    return raw->mQuestEvaluator.evaluate(alias.mConditions, *world);
                });
            // Task 0211: location aliases, the player place and the facts about cells (radiant quests)
            {
                QuestAliasFiller::Environment env;
                env.mLocations = &raw->mData->mLocations;
                env.mCreatedBase = [this](FormId reference) { return createdActorBase(reference); }; // task 0276
                env.mLocationMatcher
                    = [raw, world](const QuestRecord& quest, const QuestAlias& alias, FormId location) {
                          world->setStoryEvent(nullptr);
                          world->setSpeaker(DialogueSpeaker());
                          world->setQuestContext(quest.mCommon.mId);
                          world->setPlugin(&raw->mData->mQuests.resolverOf(quest));
                          world->setSubjectLocation(location);
                          const bool ok = raw->mQuestEvaluator.evaluate(alias.mConditions, *world);
                          world->setSubjectLocation(kNullFormId);
                          return ok;
                      };
                // task 0226: the location of a reference that no location lists is the location of its cell
                const auto referenceLocation = [this, raw](FormId reference) {
                    const PlacedEntry* entry = raw->mData->mPlacedRefs.find(reference);
                    return entry != nullptr ? locationOfCell(entry->mCell) : kNullFormId;
                };
                env.mReferenceLocation = referenceLocation;
                world->setReferenceLocation(referenceLocation);
                env.mSetAliasView
                    = [world](const std::function<FormId(std::uint32_t)>& view) { world->setAliasView(view); };
                env.mPlayer = [this] { return mPlayerSite ? mPlayerSite() : QuestAliasFiller::PlayerSite(); };
                env.mCell = [this](FormId cell) {
                    QuestAliasFiller::CellFacts facts;
                    const auto it = mCells.find(cell);
                    if (it == mCells.end())
                        return facts;
                    facts.mKnown = true;
                    facts.mInterior = it->second.mInfo.interior;
                    facts.mWorld = facts.mInterior ? kNullFormId : static_cast<FormId>(it->second.mInfo.world);
                    const QuestAliasFiller::PlayerSite site
                        = mPlayerSite ? mPlayerSite() : QuestAliasFiller::PlayerSite();
                    if (site.mKnown)
                    {
                        if (facts.mInterior)
                            facts.mLoaded = site.mInterior && site.mCell == cell;
                        else if (!site.mInterior && site.mWorld == facts.mWorld && it->second.mInfo.hasGrid)
                        {
                            const auto gx
                                = static_cast<std::int32_t>(std::floor(site.mX / QuestAliasFiller::kCellSize));
                            const auto gy
                                = static_cast<std::int32_t>(std::floor(site.mY / QuestAliasFiller::kCellSize));
                            facts.mLoaded = std::abs(it->second.mInfo.gridX - gx) <= kLoadedRing
                                && std::abs(it->second.mInfo.gridY - gy) <= kLoadedRing;
                        }
                    }
                    return facts;
                };
                raw->mAliasFiller->setEnvironment(std::move(env));
            }
            raw->mStoryManager = std::make_unique<StoryManager>(raw->mData->mStory, raw->mData->mQuests);
            raw->mStoryManager->setTrace(mStoryTrace); // task 0217
            raw->mAliasFiller->setTrace(mStoryTrace);
        }
        mDialogue = std::move(state);
        return true;
    }

    void SkyrimWorldView::setPlayerSite(std::function<QuestAliasFiller::PlayerSite()> site)
    {
        mPlayerSite = std::move(site);
    }

    void SkyrimWorldView::setPlayerPlace(bool interior, FormId cell, FormId world)
    {
        mPlayerPlaceKnown = true;
        mPlayerInterior = interior;
        mPlayerCell = cell;
        mPlayerWorld = world;
        if ((mLiveActorPlace || mDetectsPlayer) && mDialogue != nullptr && mDialogue->mQuestWorld != nullptr)
            mDialogue->mQuestWorld->setPlayerPlace(true, interior, cell, world);
    }

    void SkyrimWorldView::setPlayerPosition(double x, double y, double z)
    {
        if (mDialogue != nullptr && mDialogue->mQuestWorld != nullptr)
            mDialogue->mQuestWorld->setPlayerPosition(x, y, z);
    }

    bool SkyrimWorldView::locationWithin(FormId location, FormId ancestor) const
    {
        if (location == kNullFormId || ancestor == kNullFormId)
            return false;
        if (location == ancestor)
            return true;
        return mDialogue != nullptr && mDialogue->mData != nullptr
            && mDialogue->mData->mLocations.isWithin(location, ancestor);
    }

    FormId SkyrimWorldView::locationOfCell(FormId cell) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return kNullFormId;
        const auto it = mCells.find(cell);
        if (it == mCells.end())
            return kNullFormId;
        DialogueSpeaker speaker;
        fillSpeakerCell(speaker, cell);
        PlaceQuery place;
        place.mInterior = it->second.mInfo.interior;
        place.mCellLocation = speaker.mLocationKnown ? speaker.mLocation : kNullFormId;
        place.mWorld = static_cast<FormId>(it->second.mInfo.world);
        place.mX = it->second.mInfo.gridX;
        place.mY = it->second.mInfo.gridY;
        return locationOfPlace(place, mDialogue->mData->mLocations);
    }

    FormId SkyrimWorldView::locationOfGridSquare(FormId world, std::int32_t x, std::int32_t y) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return kNullFormId;
        PlaceQuery place;
        place.mWorld = world;
        place.mX = x;
        place.mY = y;
        const auto cell = mCellByGrid.find({ world, x, y });
        if (cell != mCellByGrid.end())
        {
            DialogueSpeaker speaker;
            fillSpeakerCell(speaker, cell->second);
            place.mCellLocation = speaker.mLocationKnown ? speaker.mLocation : kNullFormId;
        }
        return locationOfPlace(place, mDialogue->mData->mLocations);
    }

    bool SkyrimWorldView::globalValue(FormId global, float& value) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return false;
        const GlobalValue* g = mDialogue->mData->mDialogue.global(global);
        if (g == nullptr)
            return false;
        value = g->mValue;
        return true;
    }

    FormId SkyrimWorldView::worldOfInterior(std::string_view editorId) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return kNullFormId;
        const auto it = mCellByEditorId.find(lowerAscii(editorId));
        if (it == mCellByEditorId.end())
            return kNullFormId;
        const LocationIndex& locations = mDialogue->mData->mLocations;
        FormId location = locationOfCell(it->second);
        // the chain up from the cell's location: the first location that lists a grid square
        std::vector<FormId> chain;
        for (int step = 0; step < 64 && location != kNullFormId; ++step) // the same bound as LocationIndex::isWithin
        {
            chain.push_back(location);
            FormId world = kNullFormId;
            std::int32_t x = 0, y = 0;
            if (locations.gridOf(location, world, x, y) && world != kNullFormId)
                return world;
            const LocationRecord* record = locations.location(location);
            location = record != nullptr ? record->mParent : kNullFormId;
        }
        // a city's own record may list no cells (a later plugin's record replaces it whole): then a location inside it
        // does
        const std::vector<FormId> all = locations.ids();
        for (const FormId ancestor : chain)
            for (const FormId other : all)
            {
                FormId world = kNullFormId;
                std::int32_t x = 0, y = 0;
                if (other != ancestor && locations.isWithin(other, ancestor) && locations.gridOf(other, world, x, y)
                    && world != kNullFormId)
                    return world;
            }
        return kNullFormId;
    }

    void SkyrimWorldView::setInfoFragmentSink(InfoFragmentSink sink)
    {
        mInfoFragmentSink = std::move(sink);
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setFragmentSink(mInfoFragmentSink);
    }

    bool SkyrimWorldView::isScene(FormId form) const
    {
        return mDialogue != nullptr && mDialogue->mData != nullptr && mDialogue->mData->mScenes.scene(form) != nullptr;
    }

    bool SkyrimWorldView::isScriptedInfo(FormId info) const
    {
        return mDialogue != nullptr && mDialogue->mData != nullptr
            && mDialogue->mData->mDialogue.scriptedInfo(info) != nullptr;
    }

    FormId SkyrimWorldView::questOfInfo(FormId id) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return kNullFormId;
        const DialogInfo* info = mDialogue->mData->mDialogue.scriptedInfo(id);
        if (info == nullptr)
            return kNullFormId;
        if (info->mQuest != kNullFormId)
            return info->mQuest;
        const DialogTopic* topic = mDialogue->mData->mDialogue.topic(info->mTopic);
        return topic != nullptr ? topic->mQuest : kNullFormId;
    }

    FormId SkyrimWorldView::questOfScene(FormId scene) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return kNullFormId;
        const SceneRecord* record = mDialogue->mData->mScenes.scene(scene);
        return record != nullptr ? record->mQuest : kNullFormId;
    }

    bool SkyrimWorldView::cellLocation(FormId cell, FormId& location, bool& interior) const
    {
        DialogueSpeaker speaker;
        fillSpeakerCell(speaker, cell);
        if (!speaker.mLocationKnown)
            return false;
        location = speaker.mLocation;
        interior = speaker.mInterior.value_or(false);
        return true;
    }

    bool SkyrimWorldView::cellAccessOf(FormId cell, crime::CellAccess& out) const
    {
        out = {};
        if (!awaitIndex())
            return false;
        const auto it = mCells.find(cell);
        if (it == mCells.end())
            return false;
        out.mKnown = true;
        out.mInterior = it->second.mInfo.interior;
        // The latest plugin that says something wins: a flags word of 0 or no XOWN is "not said" in an override.
        bool flagsSeen = false;
        for (auto s = it->second.mSources.rbegin(); s != it->second.mSources.rend(); ++s)
        {
            const CellRecord& record = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
            if (!flagsSeen && record.mFlags != 0)
            {
                out.mPublic = (record.mFlags & CellFlags::PublicArea) != 0;
                flagsSeen = true;
            }
            if (out.mOwner == kNullFormId && record.mOwner != kNullFormId)
                out.mOwner = record.mOwner;
        }
        return true;
    }

    bool SkyrimWorldView::cellAccessByName(std::string_view editorId, crime::CellAccess& out) const
    {
        out = {};
        if (!awaitIndex())
            return false;
        const auto it = mCellByEditorId.find(lowerAscii(editorId));
        return it != mCellByEditorId.end() && cellAccessOf(it->second, out);
    }

    // ---- AI packages (task 0135, stage S5) -------------------------------------------------------------------------

    bool SkyrimWorldView::ready() const
    {
        // ensureDialogue() joins the finished worker and builds the runtime: cheap, and it never waits for a running
        // one.
        SkyrimWorldView* self = const_cast<SkyrimWorldView*>(this);
        if (self->mDialogue == nullptr)
            self->startDialoguePrefetch(); // without a worker ensureDialogue() would read everything on this thread
        return self->ensureDialogue();
    }

    std::vector<AliasPackageGroup> SkyrimWorldView::uniqueActorAliasGroups(FormId base) const
    {
        std::vector<AliasPackageGroup> groups;
        const auto found = mDialogue->mAliasPackages.find(base);
        if (found == mDialogue->mAliasPackages.end())
            return groups;
        // task 0298: a quest of higher priority is tried first (the Civil War's CW00A before CW)
        std::vector<std::size_t> ordered(found->second.size());
        for (std::size_t i = 0; i < ordered.size(); ++i)
            ordered[i] = i;
        const auto priorityOf = [this](FormId quest) -> std::uint8_t {
            const QuestRecord* record = mDialogue->mData->mQuests.quest(quest);
            return record != nullptr ? record->mPriority : std::uint8_t(0);
        };
        if (mBlockingBranches) // the setting `civil war` (earlier slices keep their order)
        {
            std::stable_sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
                const FormId qa = found->second[a].mQuest;
                const FormId qb = found->second[b].mQuest;
                return questPriorityBefore(priorityOf(qa), qa, priorityOf(qb), qb);
            });
        }
        for (const std::size_t at : ordered)
        {
            const auto& entry = found->second[at];
            if (!mDialogue->mPackageQuests.running(entry.mQuest))
                continue;
            AliasPackageGroup group{ entry.mQuest, priorityOf(entry.mQuest), false, {} };
            for (const FormId id : entry.mPackages)
                if (const PackageRecord* p = mDialogue->mData->mPackages.package(id))
                    group.mPackages.push_back(p);
            groups.push_back(std::move(group));
        }
        return groups;
    }

    bool SkyrimWorldView::packagesFor(FormId base, std::vector<const PackageRecord*>& out) const
    {
        out.clear();
        if (mDialogue == nullptr || mDialogue->mData == nullptr || mActorIndex == nullptr)
            return false;
        const PackageIndex& index = mDialogue->mData->mPackages;
        const auto cached = mDialogue->mPackageLists.find(base);
        if (cached != mDialogue->mPackageLists.end())
            out = cached->second;
        else
        {
            ActorIndex::Resolved resolved;
            if (mActorIndex->resolve(base, 1, 0, resolved) != ActorIndex::ResolveStatus::Ok)
                return false;
            std::vector<const PackageRecord*> list;
            for (const FormId id : resolved.mPackages)
                if (const PackageRecord* p = index.package(id))
                    list.push_back(p);
            // Then the default package list (an FLST) of the record that supplies the packages.
            if (const ActorNpc* holder = mActorIndex->npc(resolved.mPackagesFrom))
                if (const FormListRecord* flst = mDialogue->mData->mDialogue.formList(holder->mDefaultPackageList))
                    for (const FormId id : flst->mEntries)
                        if (const PackageRecord* p = index.package(id))
                            list.push_back(p);
            out = list;
            mDialogue->mPackageLists.emplace(base, std::move(list));
        }
        // Task 0271: the packages a running quest's alias gives this actor (ALPC) come first, in the alias's order.
        // Which quests run is read now; a quest that starts or stops later changes the list when the actor is resolved
        // again.
        if (!mAliasPackagesOn)
            return true;
        if (!mDialogue->mAliasPackagesBuilt)
        {
            mDialogue->mAliasPackagesBuilt = true;
            for (const FormId questId : mDialogue->mData->mQuests.questIds())
                if (const QuestRecord* quest = mDialogue->mData->mQuests.quest(questId))
                    for (const QuestAlias& alias : quest->mAliases)
                    {
                        if (alias.mFill == AliasFillKind::UniqueActor && alias.mActor != kNullFormId
                            && !alias.mPackages.empty())
                            mDialogue->mAliasPackages[alias.mActor].push_back({ questId, alias.mPackages });
                        // task 0274: an alias filled with a specific reference (the cart horses) gives ITS packages
                        // to that placement only
                        else if (alias.mFill == AliasFillKind::SpecificReference && alias.mReference != kNullFormId
                            && !alias.mPackages.empty())
                            mDialogue->mAliasPackagesOfReference[alias.mReference].push_back(
                                { questId, alias.mPackages });
                        else if (!alias.mPackages.empty() && !alias.mIsLocation)
                            mDialogue->mDynamicAliasPackages.push_back(
                                { questId, alias.mId, quest->mPriority, alias.mPackages,
                                    alias.mFill == AliasFillKind::FindMatching && alias.mConditions.empty() });
                    }
        }
        if (!mDialogue->mDynamicAliasPackagesSorted)
        {
            mDialogue->mDynamicAliasPackagesSorted = true;
            std::stable_sort(mDialogue->mDynamicAliasPackages.begin(), mDialogue->mDynamicAliasPackages.end(),
                [](const DialogueState::DynamicAliasPackages& a, const DialogueState::DynamicAliasPackages& b) {
                    return a.mPriority != b.mPriority ? a.mPriority > b.mPriority : a.mQuest < b.mQuest;
                });
        }
        // Task 0341: the groups are found by one helper, so that packagesForReference can order them with the others
        std::vector<AliasPackageGroup> uniqueGroups = uniqueActorAliasGroups(base);
        if (!uniqueGroups.empty())
        {
            std::vector<const PackageRecord*> front;
            for (const AliasPackageGroup& group : uniqueGroups)
                front.insert(front.end(), group.mPackages.begin(), group.mPackages.end());
            out.insert(out.begin(), front.begin(), front.end());
        }
        if (mPackageTraceLog && base == mPackageTraceBase)
            for (const PackageRecord* p : out)
                mPackageTraceLog("PKTRACE base " + std::to_string(base) + " has package " + p->mCommon.mEditorId);
        return true;
    }

    bool SkyrimWorldView::packagesForReference(
        FormId reference, FormId base, std::vector<const PackageRecord*>& out) const
    {
        if (!packagesFor(base, out))
            return false;
        if (!mAliasPackagesOn || mDialogue == nullptr || mDialogue->mData == nullptr)
            return true;
        // Task 0305: the packages of the running quests' aliases that a script forced this reference into, and (task
        // 0274) those of an alias filled with this placement itself. Task 0334: ONE order for both kinds, the quest of
        // the higher priority first (Serana's follow packages, an alias of DLC1NPCMentalModel at priority 80, before
        // DLC1VQ02's SeranaAbandoned, an external alias at 70); at the same priority a forced alias first, as before.
        std::vector<AliasPackageGroup> groups;
        if (mAliasLookup)
            for (const auto& entry : mDialogue->mDynamicAliasPackages)
            {
                if (!mDialogue->mPackageQuests.running(entry.mQuest)
                    || (entry.mScriptOnly && mForcedAliasLookup ? mForcedAliasLookup : mAliasLookup)(
                           entry.mQuest, entry.mAlias)
                        != reference)
                    continue;
                AliasPackageGroup group{ entry.mQuest, entry.mPriority, true, {} };
                for (const FormId id : entry.mPackages)
                    if (const PackageRecord* p = mDialogue->mData->mPackages.package(id))
                        group.mPackages.push_back(p);
                groups.push_back(std::move(group));
            }
        const auto found = mDialogue->mAliasPackagesOfReference.find(reference);
        const std::size_t forcedGroups = groups.size();
        const PackageIndex& index = mDialogue->mData->mPackages;
        if (found != mDialogue->mAliasPackagesOfReference.end())
            for (const auto& entry : found->second)
            {
                if (!mDialogue->mPackageQuests.running(entry.mQuest))
                    continue;
                const QuestRecord* quest = mDialogue->mData->mQuests.quest(entry.mQuest);
                AliasPackageGroup group{ entry.mQuest, quest != nullptr ? quest->mPriority : std::uint8_t(0), false, {} };
                for (const FormId id : entry.mPackages)
                    if (const PackageRecord* p = index.package(id))
                        group.mPackages.push_back(p);
                groups.push_back(std::move(group));
            }
        if (mPackageTraceLog && base == mPackageTraceBase)
            mPackageTraceLog("PKTRACE ref " + std::to_string(reference) + " base list " + std::to_string(out.size())
                + (found == mDialogue->mAliasPackagesOfReference.end() ? ", no alias entry" : ", alias entry")
                + " (" + std::to_string(mDialogue->mAliasPackagesOfReference.size()) + " references have one)");
        if (mAliasPriorityOrder)
        {
            // Task 0341: the unique-actor aliases join the one order (see orderWithUniqueActorGroups)
            std::vector<AliasPackageGroup> unique = uniqueActorAliasGroups(base);
            std::size_t frontCount = 0;
            for (const AliasPackageGroup& group : unique)
                frontCount += group.mPackages.size();
            if (!unique.empty() && !groups.empty())
            {
                out = orderWithUniqueActorGroups(out, frontCount, unique, groups);
                if (mPackageTraceLog && base == mPackageTraceBase)
                    for (const PackageRecord* p : out)
                        mPackageTraceLog("PKTRACE ref " + std::to_string(reference) + " ordered " + p->mCommon.mEditorId);
                return true;
            }
        }
        if (groups.empty())
            return true;
        const std::vector<const PackageRecord*> ordered = orderAliasPackageGroups(groups);
        out.insert(out.begin(), ordered.begin(), ordered.end());
        if (mPackageTraceLog && base == mPackageTraceBase)
            for (std::size_t i = 0; i < groups.size(); ++i)
                for (const PackageRecord* p : groups[i].mPackages)
                    mPackageTraceLog("PKTRACE ref " + std::to_string(reference) + " has "
                        + (i < forcedGroups ? "forced alias package " : "alias package ") + p->mCommon.mEditorId
                        + " (quest priority " + std::to_string(groups[i].mPriority) + ")");
        return true;
    }

    FormId SkyrimWorldView::aliasPackageQuest(FormId package) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr || mDialogue->mData->mQuests.questIds().empty())
            return kNullFormId;
        if (const auto known = mDialogue->mPackageOwner.find(package); known != mDialogue->mPackageOwner.end())
            return known->second;
        FormId owner = kNullFormId;
        for (const FormId questId : mDialogue->mData->mQuests.questIds())
            if (const QuestRecord* quest = mDialogue->mData->mQuests.quest(questId))
                for (const QuestAlias& alias : quest->mAliases)
                    if (owner == kNullFormId
                        && std::find(alias.mPackages.begin(), alias.mPackages.end(), package) != alias.mPackages.end())
                        owner = questId;
        mDialogue->mPackageOwner[package] = owner;
        return owner;
    }

    std::uint64_t SkyrimWorldView::packageListRevision() const
    {
        if (!mAliasPackagesOn || mDialogue == nullptr)
            return 0;
        const std::uint64_t forced = mAliasRevision ? static_cast<std::uint64_t>(mAliasRevision()) << 32 : 0;
        return mDialogue->mPackageQuests.revision() + forced;
    }

    void SkyrimWorldView::packageChosen(
        FormId reference, FormId base, const PackageRecord* package, const std::string& note) const
    {
        if (mPackageTraceLog && base == mPackageTraceBase)
            mPackageTraceLog("PKCHOSEN ref " + std::to_string(reference) + " "
                + (package != nullptr ? package->mCommon.mEditorId : std::string("none")) + " " + note);
    }

    PackageBehavior SkyrimWorldView::behaviorOf(const PackageRecord& package) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return PackageBehavior::Stand;
        return mDialogue->mData->mPackages.behaviorOf(package);
    }

    bool SkyrimWorldView::conditionsPass(const PackageRecord& package, FormId actorBase, FormId actorReference)
    {
        if (!package.mConditionsOk)
            return false;
        if (package.mConditions.empty())
            return true;
        if (mDialogue == nullptr || mDialogue->mData == nullptr || mDialogue->mPackageWorld == nullptr)
            return false;
        DialogueSpeaker speaker;
        speaker.mBase = actorBase;
        speaker.mReference = actorReference;
        SkyrimConditionWorld& world = *mDialogue->mPackageWorld;
        // Task 0160 (decision 3 of task 0156): the package conditions may see the actor's cell and location, the quest
        // scripts' values before any script ran and the other placed references; setPackageConditionsWired(false) is
        // the world of before
        if (mPackageWired)
        {
            if (const PlacedEntry* ref = mDialogue->mData->mPlacedRefs.find(actorReference))
                fillSpeakerCell(speaker, ref->mCell);
            double liveX = 0, liveY = 0, liveZ = 0;
            if (mMovedActorsOutdoors && mLiveActorPlace // task 0329: an actor a script moved to the player's exterior
                && dragonborn::movedActorStandsOutdoors(speaker.mInterior.value_or(false),
                    actorReference != kNullFormId && mLiveActorPlace(actorReference, liveX, liveY, liveZ),
                    mPlayerPlaceKnown && !mPlayerInterior))
            {
                speaker.mInterior = false;
                speaker.mLocation = locationOfGridSquare(
                    mPlayerWorld, dragonborn::gridSquareOf(liveX), dragonborn::gridSquareOf(liveY));
            }
            world.setLocations(&mDialogue->mData->mLocations);
            world.setQuestVariables(mDialogue->mQuestVariables.get());
            world.setPlacedRefs(&mDialogue->mData->mPlacedRefs);
        }
        else
        {
            world.setLocations(nullptr);
            world.setQuestVariables(nullptr);
            world.setPlacedRefs(nullptr);
        }
        world.setSpeaker(speaker);
        world.setTalkedLookup(mAliasPackagesOn ? SkyrimConditionWorld::TalkedLookup([this](FormId reference) {
            return mTalkedTo.count(reference) != 0;
        })
                                               : SkyrimConditionWorld::TalkedLookup()); // task 0324
        world.setLoadedLookup(mLoadedLookup); // task 0330
        world.setFollowerOverlay(mAmbushOn ? mFollowerOverlay : nullptr); // task 0302: Variable01 of an ambusher
        // Task 0310: the player's level, for a package whose condition reads it (DA03StartGuardStartQuestGreet:
        // GetLevel of the player at least 10); only with `quest alias packages`, like the other player facts here
        if (mAliasPackagesOn)
            world.setPlayerLevel(mHaveProgressionPlayer ? mProgressionLevel : mStandInPlayerLevel);
        // Task 0271: only with `quest alias packages` (else the package conditions are what they were)
        world.setPlayerPlace(mAliasPackagesOn && mPlayerPlaceKnown, mPlayerInterior, mPlayerCell, mPlayerWorld);
        world.setLiveQuestVariable(
            mAliasPackagesOn ? mDialogue->mLiveQuestVariable : SkyrimConditionWorld::LiveQuestVariable());
        world.setPlugin(&mDialogue->mData->mPackages.resolverOf(package));
        // Task 0305: a package a quest's alias gives reads that quest's aliases (GetInCurrentLocAlias of DLC1VQ00's
        // Forcegreet) and the location the player stands in; every other package sees neither
        FormId ownerQuest = kNullFormId;
        if (mAliasPackagesOn && mAliasLookup)
            ownerQuest = aliasPackageQuest(package.mCommon.mId);
        if (ownerQuest != kNullFormId)
        {
            world.setQuestContext(ownerQuest);
            world.setAliasView([this, ownerQuest](std::uint32_t alias) { return mAliasLookup(ownerQuest, alias); });
            FormId at = kNullFormId;
            if (mPlayerSite)
            {
                const QuestAliasFiller::PlayerSite site = mPlayerSite();
                if (site.mKnown)
                {
                    if (site.mInterior)
                        at = locationOfCell(site.mCell);
                    else
                        at = locationOfGridSquare(site.mWorld,
                            static_cast<std::int32_t>(std::floor(site.mX / QuestAliasFiller::kCellSize)),
                            static_cast<std::int32_t>(std::floor(site.mY / QuestAliasFiller::kCellSize)));
                }
            }
            world.setPlayerLocation(at);
            if (mPackageTraceLog && actorBase == mPackageTraceBase)
                mPackageTraceLog("PKTRACE player location " + std::to_string(at) + ", alias 7 holds "
                    + std::to_string(mAliasLookup(ownerQuest, 7)) + " in quest " + std::to_string(ownerQuest));
        }
        else
        {
            world.setAliasView({});
            world.setPlayerLocation(kNullFormId);
        }
        const bool pass = mDialogue->mPackageEvaluator.evaluate(package.mConditions, world);
        if (mPackageTraceLog && actorBase == mPackageTraceBase)
        {
            std::string line = "PKTRACE " + package.mCommon.mEditorId + (pass ? " passes" : " fails");
            if (!pass)
                for (const survey::Ctda& c : package.mConditions) // each alone (the OR chains are not honoured here)
                {
                    const std::vector<survey::Ctda> one{ c };
                    const bool alone = mDialogue->mPackageEvaluator.evaluate(one, world);
                    line += " [fn" + std::to_string(c.mFunction) + " op" + std::to_string(c.mCompare) + " "
                        + std::to_string(c.mComparison) + (alone ? " ok]" : " no]");
                }
            mPackageTraceLog(line);
        }
        if (ownerQuest != kNullFormId)
        {
            world.setAliasView({});
            world.setPlayerLocation(kNullFormId);
        }
        return pass;
    }

    PackageActivity SkyrimWorldView::activityOf(const PackageRecord& package) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return PackageActivity::None;
        return mDialogue->mData->mPackages.activityOf(package);
    }

    bool SkyrimWorldView::targetOf(const PackageRecord& package, PackageTarget& out) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return false;
        return mDialogue->mData->mPackages.targetOf(package, out);
    }

    bool SkyrimWorldView::linkedRef(FormId reference, FormId keyword, FormId& out) const
    {
        const auto it = mRefLinks.find(reference);
        if (it == mRefLinks.end())
        {
            if (mPackageTraceLog && keyword == kNullFormId)
                mPackageTraceLog("PKTRACE link of " + std::to_string(reference) + ": reference not materialised");
            return false;
        }
        for (const RefLink& link : it->second)
        {
            if (link.mKeyword == keyword && link.mRef != kNullFormId && link.mRef != kUnresolvedFormId)
            {
                out = link.mRef;
                return true;
            }
        }
        if (mPackageTraceLog && keyword == kNullFormId)
        {
            std::string line = "PKTRACE link of " + std::to_string(reference) + " has no plain link; links:";
            for (const RefLink& link : it->second)
                line += " " + std::to_string(link.mKeyword) + "->" + std::to_string(link.mRef);
            mPackageTraceLog(line);
        }
        return false;
    }

    bool SkyrimWorldView::referenceRotation(FormId reference, double (&xyz)[3]) const
    {
        const auto it = mRefRotations.find(reference);
        if (it == mRefRotations.end())
            return false;
        for (int i = 0; i < 3; ++i)
            xyz[i] = static_cast<double>(it->second[static_cast<std::size_t>(i)]);
        return true;
    }

    float SkyrimWorldView::patrolIdle(FormId reference) const
    {
        const auto it = mPatrolIdles.find(reference);
        if (mPackageTraceLog)
            mPackageTraceLog("PKTRACE patrol idle of " + std::to_string(reference) + " = "
                + (it == mPatrolIdles.end() ? std::string("none") : std::to_string(it->second)));
        return it == mPatrolIdles.end() ? -1.f : it->second;
    }

    void SkyrimWorldView::ensureMotion()
    {
        if (mMotionTried)
            return;
        mMotionTried = true;
        if (mFiles == nullptr)
            return;
        // The male project's clips and motion: the furniture clips of a female actor have the same names and (measured
        // on the chair and bed clips) the same displacement to within a unit, so one table serves both.
        std::string project, bound;
        if (mFiles->read("meshes\\animationdata\\defaultmale.txt", project)
            && mFiles->read("meshes\\animationdata\\boundanims\\anims_defaultmale.txt", bound))
            mMotion.add(project, bound);
        // Task 0302: the draugr's own clips (the alcove idles and wakes); their names are not in the male project
        std::string draugrProject, draugrBound;
        if (mAmbushOn && mFiles->read("meshes\\animationdata\\draugrproject.txt", draugrProject)
            && mFiles->read("meshes\\animationdata\\boundanims\\anims_draugrproject.txt", draugrBound))
            mMotion.add(draugrProject, draugrBound);
    }

    bool SkyrimWorldView::locate(FormId reference, double& x, double& y, double& z) const
    {
        const auto it = mPlacements.find(reference);
        if (it == mPlacements.end())
            return false;
        x = it->second[0];
        y = it->second[1];
        z = it->second[2];
        return true;
    }

    bool SkyrimWorldView::targetIsElsewhere(FormId reference) const
    {
        if (!mPlayerPlaceKnown)
            return false;
        const PlacedRefIndex* refs = placedRefs();
        if (refs == nullptr)
            return false;
        const PlacedEntry* entry = refs->find(reference);
        if (entry == nullptr || entry->mCell == kNullFormId)
            return false;
        if (mPlayerInterior)
            return entry->mCell != mPlayerCell; // an interior holds nothing but its own cell's references
        FormId location = kNullFormId;
        bool interior = false;
        return cellLocation(entry->mCell, location, interior) && interior; // outside, only an interior is elsewhere
    }

    const std::vector<FurnitureMarker>& SkyrimWorldView::markersOf(const std::string& model)
    {
        std::string key(model);
        std::transform(key.begin(), key.end(), key.begin(),
            [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
        const auto cached = mMarkerCache.find(key);
        if (cached != mMarkerCache.end())
            return cached->second;
        std::vector<FurnitureMarker>& markers = mMarkerCache[key];
        if (mFiles == nullptr || mMarkerCache.size() > 8192)
            return markers;
        std::string bytes;
        if (!mFiles->read("meshes\\" + key, bytes))
            return markers;
        std::string error;
        if (!readFurnitureMarkers(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()), markers,
                error))
            markers.clear();
        return markers;
    }

    bool SkyrimWorldView::furnitureIsBed(FormId base)
    {
        if (base == kNullFormId || base == kUnresolvedFormId)
            return false;
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return false;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        if (m.mModel.empty())
            return false;
        for (const FurnitureMarker& marker : markersOf(m.mModel))
            if (marker.mType == MarkerType::Sleep)
                return true;
        return false;
    }

    void SkyrimWorldView::slotsNear(FormId actorReference, double x, double y, double z, double radius,
        sdkgame::FurniturePose pose, double fromX, double fromY, double fromZ, std::vector<FurnitureSlot>& out)
    {
        out.clear();
        const auto cellIt = mActorCell.find(actorReference);
        if (cellIt == mActorCell.end())
            return;
        const auto listIt = mFurnitureByCell.find(cellIt->second);
        if (listIt == mFurnitureByCell.end())
            return;
        const std::uint16_t wanted = pose == sdkgame::FurniturePose::Sleep ? MarkerType::Sleep : MarkerType::Sit;
        const double approach = pose == sdkgame::FurniturePose::Sleep ? kSleepApproachDistance : kSitApproachDistance;
        struct Scored
        {
            double mScore;
            FurnitureSlot mSlot;
        };
        std::vector<Scored> found;
        for (const FurnitureRef& ref : listIt->second)
        {
            if (std::hypot(ref.mX - x, ref.mY - y) > radius)
                continue;
            std::string lowerModel(ref.mModel);
            std::transform(lowerModel.begin(), lowerModel.end(), lowerModel.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            // Jarls' thrones are not seats for passers-by, and a counter's lean marker is a sit marker in the data
            // (type 1, 32 units up) that people lean on, not sit on.
            if (pose == sdkgame::FurniturePose::Sit
                && (lowerModel.find("throne") != std::string::npos
                    || lowerModel.find("leanmarker") != std::string::npos))
                continue;
            const std::vector<FurnitureMarker>& markers = markersOf(ref.mModel);
            for (std::size_t i = 0; i < markers.size(); ++i)
            {
                const FurnitureMarker& m = markers[i];
                if (m.mType != wanted)
                    continue;
                const FurniturePlacement placement{ ref.mX, ref.mY, ref.mZ, ref.mRotZ, ref.mScale };
                const SeatPoint seat = markerInWorld(placement, m);
                const std::vector<EntryPoint> entries = entryPoints(placement, seat, m.mEntry, approach);
                const EntryPoint* best = nullptr;
                double bestD = 0;
                for (const EntryPoint& e : entries)
                {
                    const double d = std::hypot(e.mX - fromX, e.mY - fromY) + 2.0 * std::abs(e.mZ - fromZ);
                    if (best == nullptr || d < bestD)
                    {
                        best = &e;
                        bestD = d;
                    }
                }
                if (best == nullptr)
                    continue;
                FurnitureSlot slot;
                slot.mKey = (static_cast<std::uint64_t>(ref.mId) << 8) | static_cast<std::uint64_t>(i & 0xFF);
                slot.mPose = pose;
                slot.mX = seat.mX;
                slot.mY = seat.mY;
                slot.mZ = seat.mZ;
                slot.mHeading = seat.mHeading;
                slot.mEntryX = best->mX;
                slot.mEntryY = best->mY;
                slot.mEntryZ = best->mZ;
                ensureMotion();
                if (pose == sdkgame::FurniturePose::Sit)
                {
                    slot.mClip = "chair_idlebasevar1";
                    slot.mClipPerSex = true;
                    // The sit-down and get-up clips of the side the actor comes from (a stool's only side, behind, has
                    // none: the actor is placed on it, as before).
                    const char* side = best->mSide == Side::Front ? "front"
                        : best->mSide == Side::Left               ? "left"
                        : best->mSide == Side::Right              ? "right"
                                                                  : nullptr;
                    if (side != nullptr)
                    {
                        const std::string enter
                            = std::string("chair_") + side + "enter" + (best->mSide == Side::Front ? "var1" : "");
                        const std::string exitName = std::string("chair_") + side + "exit";
                        const ClipMotion* enterMotion = mMotion.find(enter);
                        const ClipMotion* exitMotion = mMotion.find(exitName);
                        if (enterMotion != nullptr && exitMotion != nullptr)
                        {
                            slot.mEnterClip = enter;
                            slot.mExitClip = exitName;
                            slot.mEnterMotion = enterMotion;
                            slot.mExitMotion = exitMotion;
                        }
                    }
                }
                else
                {
                    const bool roll = lowerModel.find("bedroll") != std::string::npos;
                    const char* side = best->mSide == Side::Right
                        ? "right"
                        : (best->mSide == Side::Front && roll ? "front" : "left");
                    slot.mClip = std::string(roll ? "bedroll_" : "bed_") + side + "sleep";
                    // Task 0302 (draugr ambushes, `ambush`): a creature's alcove marker is not a bed. The draugr lies in it
                    // with the draugr project's own idle and gets up with its exit clip (a draugr set of the alcove names
                    // IdleSleepLayEnterInstant: no enter clip, it is placed lying). RECALLED-VERIFY: that the Bg marker is
                    // the lying one; only the file names and that set tell.
                    if (mAmbushOn && lowerModel.find("creaturealcovebgmarker") != std::string::npos)
                    {
                        slot.mClip = "specialidle_alcovelayingidle";
                        // the project names the clip AlcoveLayingExit; its file is SpecialIdle_AlcoveLayingExit
                        if (const ClipMotion* exitMotion = mMotion.find("alcovelayingexit"))
                        {
                            slot.mExitClip = "specialidle_alcovelayingexit";
                            slot.mExitMotion = exitMotion;
                            slot.mQuickExitClip = slot.mExitClip; // the same get-up for a conversation or a fight
                            slot.mQuickExitMotion = exitMotion;
                        }
                        found.push_back({ std::hypot(seat.mX - fromX, seat.mY - fromY)
                                + 2.0 * std::abs(seat.mZ - fromZ),
                            std::move(slot) });
                        continue;
                    }
                    const std::string enter = std::string(roll ? "bedroll_" : "bed_") + side + "enter";
                    const std::string exitName = std::string(roll ? "bedroll_" : "bed_") + side + "exit";
                    const ClipMotion* enterMotion = mMotion.find(enter);
                    const ClipMotion* exitMotion = mMotion.find(exitName);
                    if (enterMotion != nullptr && exitMotion != nullptr)
                    {
                        slot.mEnterClip = enter;
                        slot.mExitClip = exitName;
                        slot.mEnterMotion = enterMotion;
                        slot.mExitMotion = exitMotion;
                        const std::string quick = std::string(roll ? "bedroll_" : "bed_") + side + "quickexit";
                        if (const ClipMotion* quickMotion = mMotion.find(quick))
                        {
                            slot.mQuickExitClip = quick;
                            slot.mQuickExitMotion = quickMotion;
                        }
                    }
                }
                found.push_back({ std::hypot(seat.mX - fromX, seat.mY - fromY) + 2.0 * std::abs(seat.mZ - fromZ),
                    std::move(slot) });
            }
        }
        std::sort(found.begin(), found.end(), [](const Scored& a, const Scored& b) {
            return a.mScore != b.mScore ? a.mScore < b.mScore : a.mSlot.mKey < b.mSlot.mKey;
        });
        for (Scored& s : found)
            out.push_back(std::move(s.mSlot));
    }

    void SkyrimWorldView::fillSpeakerCell(DialogueSpeaker& speaker, FormId cell) const
    {
        if (cell == kNullFormId)
            return;
        const auto it = mCells.find(cell);
        if (it == mCells.end())
            return;
        speaker.mCell = cell;
        speaker.mInterior = it->second.mInfo.interior;
        speaker.mLocationKnown = true;
        for (auto s = it->second.mSources.rbegin(); s != it->second.mSources.rend(); ++s)
        {
            const FormId location = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex].mLocation;
            if (location != kNullFormId && location != kUnresolvedFormId)
            {
                speaker.mLocation = location;
                break;
            }
        }
    }

    void SkyrimWorldView::setVendorServicesLookup(std::function<bool(FormId speakerBase)> lookup)
    {
        mVendorServicesLookup = std::move(lookup);
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setVendorServicesLookup(mVendorServicesLookup);
    }

    void SkyrimWorldView::setDeadLookup(std::function<bool(FormId)> lookup)
    {
        mDeadLookup = std::move(lookup);
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setDeadLookup(mDeadLookup);
        if (mDialogue != nullptr && mDialogue->mPackageWorld != nullptr)
            mDialogue->mPackageWorld->setDeadLookup(mDeadLookup); // task 0337
    }

    void SkyrimWorldView::setLiveActorPlace(std::function<bool(FormId, double&, double&, double&)> lookup)
    {
        mLiveActorPlace = std::move(lookup);
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
        {
            auto target = [this](std::uint32_t reference, double& x, double& y, double& z) {
                return locate(reference, x, y, z);
            };
            mDialogue->mRuntime->setLivePlaces(mLiveActorPlace, target);
            if (mDialogue->mQuestWorld != nullptr) // task 0327: a scene's phase asks where an actor stands now
                mDialogue->mQuestWorld->setLivePlaces(mLiveActorPlace, target);
        }
    }

    void SkyrimWorldView::setFollowerOverlay(const FollowerOverlay* overlay)
    {
        mFollowerOverlay = overlay;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setFollowerOverlay(overlay);
    }

    void SkyrimWorldView::setRankMinusOneMembers(std::set<FormId> factions)
    {
        mRankMinusOneMembers = factions;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setRankMinusOneMembers(std::move(factions));
    }

    void SkyrimWorldView::setOpenServices(bool on)
    {
        mOpenServices = on;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setOpenServices(on);
    }

    void SkyrimWorldView::setBlockingBranches(bool on)
    {
        mBlockingBranches = on;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setBlockingBranches(on);
    }

    void SkyrimWorldView::setFragmentFlush(std::function<void()> flush)
    {
        mFragmentFlush = flush;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setFragmentFlush(std::move(flush));
    }

    void SkyrimWorldView::setDialogueTrace(std::function<void(const std::string&)> trace)
    {
        mDialogueTrace = trace;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setTrace(std::move(trace));
    }

    void SkyrimWorldView::setWalkAwayTopics(bool on)
    {
        mWalkAwayTopics = on;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setWalkAwayTopics(on);
    }

    void SkyrimWorldView::setWalkAwayInfos(bool on)
    {
        mWalkAwayInfos = on;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setWalkAwayInfos(on);
    }

    void SkyrimWorldView::setQuestGreetings(bool on)
    {
        mQuestGreetings = on;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setQuestGreetings(on);
    }

    barter::VendorLookup SkyrimWorldView::trainingOf(FormId npcBase, ClassTraining& out)
    {
        out = ClassTraining();
        if (!awaitIndex() || !ensureActorIndex())
            return barter::VendorLookup::Pending;
        ActorIndex::Resolved resolved;
        if (mActorIndex->resolve(npcBase, mStandInPlayerLevel, static_cast<std::uint32_t>(npcBase), resolved)
            != ActorIndex::ResolveStatus::Ok)
            return barter::VendorLookup::NotVendor;
        const ClassRecord* record = mActorIndex->classRecord(resolved.mClass);
        if (record == nullptr)
            return barter::VendorLookup::NotVendor;
        out.mHasData = record->mHasData;
        out.mSkill = record->mTrainedSkill;
        out.mMaxLevel = record->mMaxTrainingLevel;
        return barter::VendorLookup::Ready;
    }

    barter::VendorLookup SkyrimWorldView::rentBedOf(FormId npcBase, FormId& bed)
    {
        bed = kNullFormId;
        if (!awaitIndex())
            return barter::VendorLookup::Pending;
        const std::vector<vmad::Script>* scripts = baseScriptsOf(npcBase);
        if (scripts == nullptr)
            return barter::VendorLookup::NotVendor;
        for (const vmad::Script& script : *scripts)
        {
            if (script.mName.size() != 14)
                continue;
            std::string lower = script.mName;
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower != "rentroomscript")
                continue;
            for (const vmad::Property& property : script.mProperties)
            {
                std::string name = property.mName;
                for (char& c : name)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (name == "bed" && !property.mArray && property.mValue.mKind == vmad::ValueKind::Object
                    && property.mValue.mForm != kNullFormId)
                {
                    bed = property.mValue.mForm;
                    return barter::VendorLookup::Ready;
                }
            }
        }
        return barter::VendorLookup::NotVendor;
    }

    void SkyrimWorldView::setCurrentPackageLookup(std::function<std::optional<FormId>(FormId reference)> lookup)
    {
        mCurrentPackageLookup = std::move(lookup);
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setCurrentPackageLookup(mCurrentPackageLookup);
    }

    bool SkyrimWorldView::mapAccess(MapAccess& out)
    {
        if (!ready() || mDialogue == nullptr || mDialogue->mData == nullptr)
            return false;
        const DialogueData* data = mDialogue->mData.get();
        out.mMarkers = &data->mMapMarkers;
        out.mPlaced = &data->mPlacedRefs;
        out.mInteriorWorld = [this](std::string_view editorId) { return worldOfInterior(editorId); };
        out.mCellWorld = [this](FormId cell, FormId& world, bool& interior) {
            const auto it = mCells.find(cell);
            if (it == mCells.end())
                return false;
            interior = it->second.mInfo.interior;
            world = static_cast<FormId>(it->second.mInfo.world);
            return true;
        };
        return true;
    }

    FormId SkyrimWorldView::playerLocationNow() const
    {
        if (!mPlayerSite)
            return kNullFormId;
        const QuestAliasFiller::PlayerSite site = mPlayerSite();
        if (!site.mKnown)
            return kNullFormId;
        if (site.mInterior)
            return locationOfCell(site.mCell);
        return locationOfGridSquare(site.mWorld, static_cast<std::int32_t>(std::floor(site.mX / QuestAliasFiller::kCellSize)),
            static_cast<std::int32_t>(std::floor(site.mY / QuestAliasFiller::kCellSize)));
    }

    FormId SkyrimWorldView::worldOfCell(FormId cell)
    {
        if (cell == kNullFormId || !awaitIndex())
            return kNullFormId;
        const auto it = mCells.find(cell);
        if (it == mCells.end() || it->second.mInfo.interior)
            return kNullFormId;
        return static_cast<FormId>(it->second.mInfo.world);
    }

    bool SkyrimWorldView::loadingAccess(menus::Access& out)
    {
        if (!ready() || mDialogue == nullptr || mDialogue->mData == nullptr || mDialogue->mQuestWorld == nullptr)
            return false;
        DialogueState* state = mDialogue.get();
        out.mScreens = &state->mData->mLoadingScreens;
        out.mText = state->mResolver;
        out.mEvaluate = [state](const std::vector<survey::Ctda>& list, const FormIdResolver& resolver) {
            SkyrimConditionWorld* world = state->mQuestWorld.get();
            world->setStoryEvent(nullptr);
            world->setSpeaker(DialogueSpeaker());
            world->setQuestContext(kNullFormId);
            world->setPlugin(&resolver);
            return state->mQuestEvaluator.evaluate(list, *world);
        };
        return true;
    }

    void SkyrimWorldView::setStoryTrace(StoryManager::Trace trace)
    {
        mStoryTrace = std::move(trace);
        if (mDialogue != nullptr && mDialogue->mAliasFiller != nullptr)
            mDialogue->mAliasFiller->setTrace(mStoryTrace);
        if (mDialogue != nullptr && mDialogue->mStoryManager != nullptr)
            mDialogue->mStoryManager->setTrace(mStoryTrace);
    }

    StoryState SkyrimWorldView::storyState() const
    {
        StoryState out;
        if (mDialogue != nullptr && mDialogue->mStoryManager != nullptr)
        {
            out.mManager = mDialogue->mStoryManager->saveState();
            out.mRan.assign(mDialogue->mStoryRan.begin(), mDialogue->mStoryRan.end());
        }
        out.mKeywordData = mKeywordData.entries();
        return out;
    }

    bool SkyrimWorldView::restoreStoryState(const StoryState& state)
    {
        if (mDialogue == nullptr || mDialogue->mStoryManager == nullptr)
            return false;
        mDialogue->mStoryManager->restoreState(state.mManager);
        mDialogue->mStoryRan = std::set<FormId>(state.mRan.begin(), state.mRan.end());
        mKeywordData.clear();
        for (const auto& [location, keyword, value] : state.mKeywordData)
            (void)mKeywordData.set(location, keyword, value);
        return true;
    }

    const QuestAlias* SkyrimWorldView::findAliasByName(FormId quest, std::string_view aliasName) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return nullptr;
        const QuestRecord* record = mDialogue->mData->mQuests.quest(quest);
        if (record == nullptr)
            return nullptr;
        for (const QuestAlias& a : record->mAliases)
        {
            if (a.mName.size() != aliasName.size())
                continue;
            bool same = true;
            for (std::size_t i = 0; i < aliasName.size() && same; ++i)
                same = std::tolower(static_cast<unsigned char>(a.mName[i]))
                    == std::tolower(static_cast<unsigned char>(aliasName[i]));
            if (same)
                return &a;
        }
        return nullptr;
    }

    // Task 0217 (the journal) and task 0228 (conversations): the display name of what fills an alias.
    std::string SkyrimWorldView::aliasDisplayName(FormId quest, std::string_view aliasName)
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr || mDialogue->mRuntime == nullptr)
            return {};
        DialogueData* data = mDialogue->mData.get();
        DialogueState* state = mDialogue.get();
        const QuestAlias* alias = findAliasByName(quest, aliasName);
        if (alias == nullptr)
            return {};
        const AliasFill fill = state->mRuntime->quests().aliasFill(quest, alias->mId);
        if (!fill.mKnown)
            return {};
        if (alias->mIsLocation)
        {
            const LocationRecord* location = data->mLocations.location(fill.mReference);
            return location != nullptr && state->mResolver
                ? state->mResolver(location->mName, location->mPlugin, StringTableKind::Strings)
                : std::string();
        }
        FormId base = fill.mActor;
        if (fill.mReference != kNullFormId)
        {
            const PlacedEntry* entry = data->mPlacedRefs.find(fill.mReference);
            if (entry != nullptr)
                base = entry->mBase;
        }
        return base != kNullFormId ? displayNameOf(static_cast<sdkgame::WorldId>(base)) : std::string();
    }

    // Task 0228: 1 when the NPC of the alias is female, 0 male, -1 when nothing fills it or it is no one.
    int SkyrimWorldView::aliasFemale(FormId quest, std::string_view aliasName)
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr || mDialogue->mRuntime == nullptr
            || mActorIndex == nullptr)
            return -1;
        const QuestAlias* alias = findAliasByName(quest, aliasName);
        if (alias == nullptr || alias->mIsLocation)
            return -1;
        const AliasFill fill = mDialogue->mRuntime->quests().aliasFill(quest, alias->mId);
        if (!fill.mKnown)
            return -1;
        FormId base = fill.mActor;
        if (fill.mReference != kNullFormId)
        {
            const PlacedEntry* entry = mDialogue->mData->mPlacedRefs.find(fill.mReference);
            if (entry != nullptr)
                base = entry->mBase;
        }
        const ActorNpc* npc = base != kNullFormId ? mActorIndex->npc(base) : nullptr;
        return npc == nullptr ? -1 : (npc->female() ? 1 : 0);
    }

    bool SkyrimWorldView::papyrusAccess(PapyrusAccess& out)
    {
        if (!ready() || mDialogue == nullptr || mDialogue->mData == nullptr || mDialogue->mRuntime == nullptr)
            return false;
        DialogueData* data = mDialogue->mData.get();
        out.mQuests = &data->mQuests;
        out.mState = &mDialogue->mRuntime->questsMutable();
        out.mVmads = [data](FormId quest) { return data->mQuestVmads.find(quest); };
        out.mText = mDialogue->mResolver;
        DialogueState* state = mDialogue.get();
        out.mAliases = state->mAliasFiller.get();
        // Task 0194: scenes
        out.mScenes = &data->mScenes;
        out.mLocations = &data->mLocations; // task 0217
        out.mPackage = [data](FormId id) { return data->mPackages.package(id); };
        out.mBaseOf = [data](FormId reference) {
            const PlacedEntry* entry = data->mPlacedRefs.find(reference);
            return entry != nullptr ? entry->mBase : kNullFormId;
        };
        // Task 0217: the journal names what an alias holds
        out.mAliasName
            = [this](FormId quest, std::string_view aliasName) { return aliasDisplayName(quest, aliasName); };
        out.mSetSceneLookup = [state](std::function<bool(FormId, std::uint32_t)> lookup) {
            state->mQuestWorld->setSceneActionLookup(std::move(lookup));
        };
        out.mEvaluate = [this, state, data](const std::vector<survey::Ctda>& list, FormId scene, FormId quest) {
            SkyrimConditionWorld* world = state->mQuestWorld.get();
            world->setLoadedLookup(mLoadedLookup); // task 0330: a phase waits for HasLoaded3D of its actors
            const SceneRecord* record = data->mScenes.scene(scene);
            // Task 0328: a scene's phase waits for "the player alias is in this cell" (MQ105EinarthTeachScene's phase 7:
            // GetInCell(HighHrothgar) on the Player alias): the quest world is told where the player stands, as the
            // package worlds are (only with `quest alias packages`, like the other player facts)
            world->setPlayerPlace(mAliasPackagesOn && mPlayerPlaceKnown, mPlayerInterior, mPlayerCell, mPlayerWorld);
            world->setStoryEvent(nullptr);
            world->setSpeaker(DialogueSpeaker());
            world->setQuestContext(quest);
            if (record != nullptr)
                world->setPlugin(&data->mScenes.resolverOf(*record));
            return state->mQuestEvaluator.evaluate(list, *world);
        };
        out.mLine = [this, state, data](FormId topic, FormId reference, std::uint32_t pick, SceneLine& line) {
            const PlacedEntry* entry = data->mPlacedRefs.find(reference);
            const FormId createdBase = entry == nullptr ? createdActorBase(reference) : kNullFormId;
            if (entry == nullptr && createdBase == kNullFormId)
                return false;
            DialogueSpeaker speaker;
            speaker.mReference = reference;
            speaker.mBase = entry != nullptr ? entry->mBase : createdBase;
            if (entry != nullptr)
            {
                fillSpeakerCell(speaker, entry->mCell);
                speaker.mCell = entry->mCell;
            }
            sdkgame::DialogueTurn turn;
            if (!state->mRuntime->sceneLine(topic, speaker, pick, turn))
                return false;
            line.mText = turn.text;
            line.mVoice = turn.voice;
            return true;
        };
        out.mBark = [this, state, data](std::string_view subtype, FormId reference, std::uint32_t pick,
                        bool speakerIsVictim, SceneLine& line) {
            const PlacedEntry* entry = data->mPlacedRefs.find(reference);
            if (entry == nullptr)
                return false;
            DialogueSpeaker speaker;
            speaker.mReference = reference;
            speaker.mBase = entry->mBase;
            fillSpeakerCell(speaker, entry->mCell);
            speaker.mCell = entry->mCell;
            sdkgame::DialogueTurn turn;
            if (!state->mRuntime->barkLine(subtype, speaker, pick, turn, speakerIsVictim))
                return false;
            line.mText = turn.text;
            line.mVoice = turn.voice;
            return true;
        };
        out.mSetStance
            = [state](bool sneaking, std::uint8_t drawState) { state->mRuntime->setPlayerStance(sneaking, drawState); };
        out.mQuestConditions = [this, state, data](const std::vector<survey::Ctda>& list, FormId quest) {
            const QuestRecord* record = data->mQuests.quest(quest);
            if (record == nullptr)
                return true;
            return state->mRuntime->evaluateQuestConditions(
                list, quest, data->mQuests.resolverOf(*record), playerLocationNow()); // task 0312
        };
        out.mSetLiveQuestVariable = [state](SkyrimConditionWorld::LiveQuestVariable source) {
            state->mLiveQuestVariable = source; // task 0271: the package world reads it with `quest alias packages`
            state->mRuntime->setLiveQuestVariable(source);
            state->mQuestWorld->setLiveQuestVariable(std::move(source)); // task 0261: the story and alias worlds too
        };
        out.mStory = [state](const StoryEvent& event) {
            state->mStoryEvent = event;
            SkyrimConditionWorld* world = state->mQuestWorld.get();
            world->setStoryEvent(&state->mStoryEvent);
            const QuestState& quests = state->mRuntime->quests();
            std::vector<FormId> chosen = state->mStoryManager->raise(
                event,
                [state, world](const std::vector<survey::Ctda>& list, const FormIdResolver& resolver) {
                    // Task 0217: the alias filler's dry run (the quest start gate below) leaves a candidate speaker and
                    // subject location in this world; the story manager's subject is the player
                    world->setSpeaker(DialogueSpeaker());
                    world->setSubjectLocation(kNullFormId);
                    world->setPlugin(&resolver);
                    return state->mQuestEvaluator.evaluate(list, *world);
                },
                [&quests, state, world, &event](FormId quest) {
                    if (!quests.exists(quest) || quests.running(quest) || quests.completed(quest))
                        return false;
                    // Task 0194: an ambient (ADIA) quest runs once a session (the hours until reset, RNAM, are not
                    // honoured: decision for toddhow)
                    if (event.mType == "ADIA" && state->mStoryRan.count(quest) != 0)
                        return false;
                    // Task 0211: for every event, a quest starts only when its required aliases of an answered kind
                    // fill (the event members, a find-matching reference or location, a reference in a location) Task
                    // 0217: the dry run of the alias filler evaluates conditions on candidates and so clears the
                    // world's event; without putting it back every node after the first quest tried saw no event (all
                    // the fn_576 conditions failed) and the walk could never go past it.
                    const StoryEvent* const walking = world->currentStoryEvent();
                    const bool fills = state->mAliasFiller->canStart(quest, quests, &event);
                    world->setStoryEvent(walking);
                    return fills;
                });
            if (event.mType == "ADIA")
                for (const FormId quest : chosen)
                    state->mStoryRan.insert(quest);
            world->setStoryEvent(nullptr);
            return chosen;
        };
        return true;
    }

    std::vector<SkyrimWorldView::ReferenceScripts> SkyrimWorldView::scriptsOfCell(FormId cell)
    {
        std::string error;
        if (!awaitIndex())
            return {};
        const std::shared_ptr<const Materialised> content = materialise(cell, error);
        if (content == nullptr)
            return {};
        std::vector<ReferenceScripts> result = content->mScripts;
        // Task 0292: the world's persistent placements that stand in this exterior square are part of its content
        // (loadCell adds them) and so are their scripts: the Winterhold bridge's trigger box (0x50785) is persistent.
        const auto entryIt = mCells.find(cell);
        if (entryIt == mCells.end())
            return result;
        const CellEntry& entry = entryIt->second;
        if (entry.mInfo.interior || !entry.mInfo.hasGrid || entry.mWorldPersistent)
            return result;
        const auto world = mWorlds.find(entry.mInfo.world);
        if (world == mWorlds.end() || world->second.mPersistentCell == kNullFormId
            || world->second.mPersistentCell == cell)
            return result;
        const std::shared_ptr<const Materialised> persistent = materialise(world->second.mPersistentCell, error);
        if (persistent == nullptr)
            return result;
        std::set<FormId> own;
        for (const sdkgame::PlacedObject& placed : content->mObjects)
            own.insert(static_cast<FormId>(placed.id));
        for (const ReferenceScripts& s : persistent->mScripts)
        {
            if (own.count(s.mReference) != 0 || !std::isfinite(s.mPosition.x) || !std::isfinite(s.mPosition.y))
                continue;
            // scripts without a primitive carry no position: look the placement up among the persistent objects
            double px = s.mPosition.x;
            double py = s.mPosition.y;
            if (!s.mHasPrimitive)
            {
                bool found = false;
                for (const sdkgame::PlacedObject& o : persistent->mObjects)
                    if (static_cast<FormId>(o.id) == s.mReference)
                    {
                        px = o.position.x;
                        py = o.position.y;
                        found = true;
                        break;
                    }
                if (!found)
                    continue;
            }
            if (static_cast<std::int32_t>(std::floor(px / kCellSize)) == entry.mInfo.gridX
                && static_cast<std::int32_t>(std::floor(py / kCellSize)) == entry.mInfo.gridY)
            {
                ReferenceScripts copy = s;
                copy.mCell = cell; // it belongs to the square that holds it now
                result.push_back(std::move(copy));
            }
        }
        return result;
    }

    const std::vector<vmad::Script>* SkyrimWorldView::baseScriptsOf(FormId base) const
    {
        const auto it = mBases.find(base);
        if (it == mBases.end())
            return nullptr;
        const ParsedPlugin& p = mPlugins[it->second.mPlugin].mParsed;
        const std::vector<vmad::Script>* scripts = nullptr;
        switch (it->second.mTable)
        {
            case BaseTable::Model:
                scripts = &p.mModels[it->second.mIndex].mScripts;
                break;
            case BaseTable::Npc:
                scripts = &p.mNpcs[it->second.mIndex].mScripts;
                break;
            case BaseTable::Item:
                scripts = &p.mItems[it->second.mIndex].mScripts;
                break;
            default:
                break;
        }
        // Task 0308: an NPC_ whose template use flags include Script runs its template's scripts (Mirmulnir,
        // MQ104Dragon, takes dragonActorSCRIPT from its leveled template); only when the actor index is built
        if (mHelloGreetings && it->second.mTable == BaseTable::Npc && mActorIndex != nullptr)
        {
            constexpr std::uint16_t kScriptTemplateLevel = 30; // ours: any entry of such a list carries the same script
            const FormId source = mActorIndex->scriptSource(base, kScriptTemplateLevel, 0);
            if (source != base)
                return baseScriptsOf(source);
        }
        return scripts != nullptr && !scripts->empty() ? scripts : nullptr;
    }

    bool SkyrimWorldView::formHasKeyword(FormId form, FormId keyword) const
    {
        if (keyword == kNullFormId)
            return false;
        FormId base = form;
        if (const PlacedRefIndex* refs = placedRefs())
            if (const PlacedEntry* entry = refs->find(form))
                base = entry->mBase;
        const auto it = mBases.find(base);
        if (it == mBases.end())
            return false;
        const ParsedPlugin& p = mPlugins[it->second.mPlugin].mParsed;
        const auto has = [keyword](const std::vector<FormId>& list) {
            return std::find(list.begin(), list.end(), keyword) != list.end();
        };
        switch (it->second.mTable)
        {
            case BaseTable::Model:
                return has(p.mModels[it->second.mIndex].mKeywords);
            case BaseTable::Item:
                return has(p.mItems[it->second.mIndex].mKeywords);
            case BaseTable::Npc:
            {
                if (mActorIndex == nullptr)
                    return false;
                const ActorNpc* npc = mActorIndex->npc(base);
                if (npc == nullptr)
                    return false;
                if (has(npc->mKeywords))
                    return true;
                const RaceRecord* race = mActorIndex->race(npc->mRace);
                return race != nullptr && has(race->mKeywords);
            }
            default:
                return false;
        }
    }

    void SkyrimWorldView::setPackageQuestStage(FormId quest, std::uint32_t stage)
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return;
        mDialogue->mPackageQuests.setStage(mDialogue->mData->mQuests, quest, stage);
        if (mAliasPackagesOn)
            ++mPackagesRevision; // task 0277: a running quest may now give (or stop giving) an actor packages
    }

    void SkyrimWorldView::startPackageQuest(FormId quest)
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr || !mDialogue->mData->mQuests.quest(quest))
            return;
        mDialogue->mPackageQuests.start(quest);
        if (mAliasPackagesOn)
            ++mPackagesRevision;
    }

    const PlacedRefIndex* SkyrimWorldView::placedRefs() const
    {
        return mDialogue != nullptr && mDialogue->mData != nullptr ? &mDialogue->mData->mPlacedRefs : nullptr;
    }

    bool SkyrimWorldView::beginConversation(sdkgame::WorldId base, sdkgame::WorldId reference, sdkgame::WorldId cell,
        sdkgame::DialogueTurn& out, std::string& error)
    {
        out = {};
        if (!awaitIndex() || !ensureDialogue())
        {
            error = "the dialogue records are not available";
            return false;
        }
        DialogueSpeaker speaker;
        speaker.mBase = static_cast<FormId>(base);
        speaker.mReference = static_cast<FormId>(reference);
        speaker.mCell = static_cast<FormId>(cell);
        // task 0338: a talking activator (Azura's altar) speaks with the voice type of its record; the lookup is read when
        // a speaker is set
        mDialogue->mRuntime->setActivatorVoiceLookup([this](FormId b) { return talkingActivatorVoice(b); });
        if (mAliasPackagesOn && speaker.mReference != kNullFormId) // task 0324: the greeting package of Lydia ends
            mTalkedTo.insert(speaker.mReference);
        // Task 0156: what the cell says about interior and location
        fillSpeakerCell(speaker, speaker.mCell);
        // Task 0230: the running quests' aliases that need the speaker's editor location (Steward, Innkeeper, Jarl)
        // take him now when they are still empty, so their dialogue lines can be offered
        if (mDialogue->mAliasFiller != nullptr)
            (void)mDialogue->mAliasFiller->fillForSpeaker(
                mDialogue->mRuntime->questsMutable(), speaker.mReference, speaker.mBase);
        // Task 0258: the guard whose offer is the arrest conversation opens the forced greeting
        if (mArrest.mWants && mArrest.mWants(speaker.mReference))
        {
            if (mDialogue->mRuntime->beginForced(speaker, "PFGT", out, error))
            {
                mForcedReference = speaker.mReference;
                if (mArrest.mBegan)
                    mArrest.mBegan(speaker.mReference);
                return true;
            }
            if (mArrest.mFailed)
                mArrest.mFailed(speaker.mReference);
            return false;
        }
        // Task 0264: a ForceGreet package opens the conversation with the topic of its data
        if (mForcedGreet.mTopic)
        {
            const FormId topic = mForcedGreet.mTopic(speaker.mReference);
            if (topic == kForceGreetHelloTopic) // task 0308: the actor's own greeting (a Blocking branch's start, else Hello)
            {
                mForcedGreetReference = speaker.mReference;
                if (mForcedGreet.mBegan)
                    mForcedGreet.mBegan(speaker.mReference);
                return mDialogue->mRuntime->begin(speaker, out, error);
            }
            if (topic != kNullFormId)
            {
                if (mDialogue->mRuntime->beginForcedTopic(speaker, topic, out, error))
                {
                    mForcedGreetReference = speaker.mReference;
                    if (mForcedGreet.mBegan)
                        mForcedGreet.mBegan(speaker.mReference);
                    return true;
                }
                const bool plain = mForcedGreet.mPlainWhenNone && mForcedGreet.mPlainWhenNone(speaker.mReference);
                if (mForcedGreet.mEnded)
                    mForcedGreet.mEnded(speaker.mReference);
                if (!plain)
                    return false;
            }
        }
        return mDialogue->mRuntime->begin(speaker, out, error);
    }

    void SkyrimWorldView::setForcedGreeting(ForcedGreetHooks hooks)
    {
        mForcedGreet = std::move(hooks);
    }

    bool SkyrimWorldView::forceGreetTopic(const PackageRecord& package, FormId& out) const
    {
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return false;
        if (!mDialogue->mData->mPackages.forceGreetTopic(package, out))
            return false;
        return out != kForceGreetHelloTopic || mHelloGreetings; // task 0308
    }

    void SkyrimWorldView::setArrestDialogue(ArrestDialogueHooks hooks)
    {
        mArrest = std::move(hooks);
    }

    void SkyrimWorldView::setCrimeConditionSource(CrimeConditionSource source)
    {
        mCrimeSource = std::move(source);
        mCrimeSourceSet = true;
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->setCrimeSource(mCrimeSource);
    }

    bool SkyrimWorldView::forcedGreetingAvailable(FormId base, FormId reference, std::string* why)
    {
        if (mDialogue == nullptr || mDialogue->mRuntime == nullptr || mDialogue->mData == nullptr)
        {
            if (why != nullptr)
                *why = " the dialogue data is not built yet";
            return false;
        }
        DialogueSpeaker speaker;
        speaker.mBase = base;
        speaker.mReference = reference;
        if (const PlacedEntry* entry = mDialogue->mData->mPlacedRefs.find(reference))
        {
            speaker.mCell = entry->mCell;
            fillSpeakerCell(speaker, entry->mCell);
        }
        return mDialogue->mRuntime->forcedGreetingAvailable(speaker, "PFGT", why);
    }

    bool SkyrimWorldView::chooseTopic(std::uint32_t topicId, sdkgame::DialogueTurn& out, std::string& error)
    {
        out = {};
        if (mDialogue == nullptr || mDialogue->mRuntime == nullptr)
        {
            error = "no conversation is open";
            return false;
        }
        return mDialogue->mRuntime->choose(topicId, out, error);
    }

    void SkyrimWorldView::endConversation()
    {
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->end();
        if (mForcedGreetReference != kNullFormId)
        {
            const FormId reference = mForcedGreetReference;
            mForcedGreetReference = kNullFormId;
            if (mForcedGreet.mEnded)
                mForcedGreet.mEnded(reference);
        }
        if (mForcedReference != kNullFormId)
        {
            const FormId reference = mForcedReference;
            mForcedReference = kNullFormId;
            if (mArrest.mEnded)
                mArrest.mEnded(reference);
        }
    }

    bool SkyrimWorldView::ensureWeather()
    {
        if (!awaitIndex())
            return false;
        if (mWeatherTable != nullptr)
            return true;
        auto index = std::make_unique<WeatherIndex>();
        const WeatherLimits limits;
        for (const PluginData& plugin : mPlugins)
        {
            Content::Error openError;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(plugin.mPath, openError);
            if (file == nullptr)
            {
                noteError(openError);
                continue;
            }
            WeatherRecords records;
            (void)parseWeatherRecords(*file, mOrder, limits, records);
            index->add(records);
        }
        mWeatherIndex = std::move(index);
        TextureProbe probe;
        SoundProbe soundProbe;
        if (mFiles != nullptr)
        {
            const FileSource* files = mFiles;
            probe = [files](const std::string& path) { return files->exists("textures\\" + path); };
            // Task 0127 stage W-S4: a weather sound is named only when its file is in the session's VFS (the sound
            // archive of the game, or a loose file). The path is already the VFS's own ("sound/fx/...").
            soundProbe = [files](const std::string& path) { return files->exists(path); };
        }
        mWeatherProbe = probe;
        mWeatherTable = std::make_unique<WeatherTable>(*mWeatherIndex, probe, soundProbe);
        return true;
    }

    bool SkyrimWorldView::describeWeathers(
        std::vector<openeden::sdk::game::WeatherDescription>& out, std::string& error)
    {
        out.clear();
        if (!ensureWeather())
        {
            error = "the world index is not available";
            return false;
        }
        out = mWeatherTable->weathers();
        if (out.empty())
        {
            error = "the plugins hold no weather";
            return false;
        }
        return true;
    }

    bool SkyrimWorldView::describeTimeOfDay(openeden::sdk::game::TimeOfDayDescription& out)
    {
        return ensureWeather() && game_skyrimse::describeTimeOfDay(*mWeatherIndex, out, mWeatherProbe);
    }

    bool SkyrimWorldView::weatherListForCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::CellWeatherList& out)
    {
        out = {};
        return ensureWeather() && mWeatherTable->listForCell(static_cast<FormId>(cell), out);
    }

    music::MusicRuntime* SkyrimWorldView::musicRuntime()
    {
        if (mMusic == nullptr)
        {
            if (!awaitIndex() || mPlugins.empty())
                return nullptr;
            std::vector<std::filesystem::path> paths;
            paths.reserve(mPlugins.size());
            for (const PluginData& plugin : mPlugins)
                paths.push_back(plugin.mPath);
            mMusic = std::make_unique<music::MusicRuntime>(std::move(paths), mOrder);
            mMusic->start();
        }
        return mMusic.get();
    }

    bool SkyrimWorldView::musicFileExists(const std::string& vfsPath)
    {
        return mFiles != nullptr && mFiles->exists(vfsPath);
    }

    // ---- the worker -------------------------------------------------------------------------------------------------

    void SkyrimWorldView::buildTerrainIndex() const
    {
        auto index = std::make_unique<SkyrimTerrainIndex>();
        for (const PluginData& plugin : mPlugins)
        {
            std::string error;
            if (!index->addPluginFile(plugin.mPath, mOrder, error) && mOptions.mNotice)
                mOptions.mNotice("terrain index: " + error);
        }
        mTerrain = std::move(index);
    }

    const SkyrimTerrainIndex* SkyrimWorldView::terrainIndex() const
    {
        if (!awaitIndex())
            return nullptr;
        if (mTerrainState.load(std::memory_order_acquire) == 1)
        {
            // The worker is on it (it started before the cell index was published): wait, it was going to be needed.
            std::unique_lock<std::mutex> lock(mBuildMutex);
            mBuildCv.wait(lock, [this] { return mTerrainState.load(std::memory_order_acquire) != 1; });
        }
        if (!mTerrain)
            buildTerrainIndex();
        return mTerrain.get();
    }

    bool SkyrimWorldView::awaitIndex() const
    {
        if (!mBuilding.load(std::memory_order_acquire))
            return !mFailed.load(std::memory_order_acquire); // a cancelled or failed build is never read
        if (std::this_thread::get_id() == mWorkerId.load())
            return true; // the worker is the only user while it builds
        const auto started = std::chrono::steady_clock::now();
        bool done;
        {
            std::unique_lock<std::mutex> lock(mBuildMutex);
            done = mBuildCv.wait_for(lock, mOptions.mIndexWait, [this] { return !mBuilding.load(); });
        }
        const auto waited = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        mWaits.fetch_add(1);
        std::uint64_t longest = mLongestWaitMillis.load();
        while (waited > longest && !mLongestWaitMillis.compare_exchange_weak(longest, waited))
        {
        }
        if (done && mFailed.load(std::memory_order_acquire))
            return false;
        if (done)
        {
            // Said once: the first wait, with how long the caller was blocked.
            bool expected = false;
            if (mOptions.mNotice && mWaitReported.compare_exchange_strong(expected, true))
                mOptions.mNotice("a query waited " + std::to_string(waited) + " ms for the world index to finish");
            return true;
        }
        bool expected = false;
        if (mOptions.mNotice && mTimeoutReported.compare_exchange_strong(expected, true))
            mOptions.mNotice("the world index was not ready after " + std::to_string(waited)
                + " ms; queries answer 'nothing' until it is");
        return false;
    }

    bool SkyrimWorldView::startLoad(
        std::vector<std::filesystem::path> paths, std::function<void(const LoadReport&)> onBuilt)
    {
        if (mBuilding.load() || !mPlugins.empty())
            return false;
        if (mWorker.joinable())
            mWorker.join(); // a finished earlier load whose thread is only waiting to be joined
        mCancel.store(false);
        mFailed.store(false);
        mBuilding.store(true, std::memory_order_release);
        mWorker = std::thread([this, pathList = std::move(paths), whenBuilt = std::move(onBuilt)]() {
            mWorkerId.store(std::this_thread::get_id());
            LoadReport report;
            report.mGiven = pathList.size();
            const auto started = std::chrono::steady_clock::now();
            try
            {
                for (const std::filesystem::path& path : pathList)
                {
                    if (mCancel.load())
                    {
                        report.mAborted = true;
                        break;
                    }
                    std::string error;
                    if (addPluginUnchecked(path, error))
                        ++report.mTaken;
                    else
                        report.mProblems.push_back(error);
                }
                if (!report.mAborted)
                    finish();
                report.mElapsed
                    = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
                if (report.mAborted)
                    mFailed.store(true); // no finish(): the lookups were never built
                if (whenBuilt)
                    whenBuilt(report);
            }
            catch (const std::exception& e)
            {
                // The index may be incomplete: queries answer "nothing" from now on (a session error would need a
                // channel to the lifecycle that the pack does not have from a worker).
                mFailed.store(true);
                if (mOptions.mNotice)
                    mOptions.mNotice(std::string("the world index build failed: ") + e.what());
            }
            catch (...)
            {
                mFailed.store(true);
                if (mOptions.mNotice)
                    mOptions.mNotice("the world index build failed with an unknown error");
            }
            mWorkerId.store(std::thread::id{}); // a finished thread's id may be handed to a new thread
            // Task 0125: the terrain index (LAND and LTEX of every plugin, about 2.5 s on the five masters) is built
            // here, after the cell index is published, so that the first exterior door finds it done instead of
            // building it on the main thread.
            const bool buildTerrain = !mFailed.load() && !mCancel.load();
            if (buildTerrain)
                mTerrainState.store(1, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(mBuildMutex);
                mBuilding.store(false, std::memory_order_release);
            }
            mBuildCv.notify_all();
            if (buildTerrain)
            {
                try
                {
                    buildTerrainIndex();
                    mTerrainFromWorker.store(true, std::memory_order_release);
                }
                catch (...)
                {
                    mTerrain.reset(); // terrainIndex() builds it on demand, as before
                }
                {
                    std::lock_guard<std::mutex> lock(mBuildMutex);
                    mTerrainState.store(2, std::memory_order_release);
                }
                mBuildCv.notify_all();
            }
        });
        return true;
    }

    void SkyrimWorldView::cancelAndJoin()
    {
        if (std::this_thread::get_id() == mWorkerId.load() && mBuilding.load())
            return; // never join oneself
        mCancel.store(true);
        if (mWorker.joinable())
            mWorker.join();
    }

    bool SkyrimWorldView::configure(Options options, const FileSource* files)
    {
        if (mBuilding.load() || !mPlugins.empty())
            return false;
        mOptions = std::move(options);
        mOptions.mLimits.mSkipReferences = true;
        mFiles = files;
        return true;
    }

    void SkyrimWorldView::noteError(Content::Error error)
    {
        ++mStats.mRecordErrors;
        if (mErrors.size() < kMaxErrorsKept)
            mErrors.push_back(std::move(error));
    }

    // ---- indexing ---------------------------------------------------------------------------------------------------

    bool SkyrimWorldView::addPlugin(const std::filesystem::path& path, std::string& error)
    {
        if (!awaitIndex())
        {
            error = path.filename().string() + ": the world index is being built";
            return false;
        }
        return addPluginUnchecked(path, error);
    }

    bool SkyrimWorldView::addPluginUnchecked(const std::filesystem::path& path, std::string& error)
    {
        mFinished = false;
        mOptions.mLimits.mSkipReferences = true;
        Content::Error openError;
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, openError);
        if (plugin == nullptr)
        {
            ++mStats.mPluginsRefused;
            error = openError.describe();
            return false;
        }
        if (!isSkyrimSeHeader(plugin->header()))
        {
            ++mStats.mPluginsRefused;
            error = plugin->name() + ": this plugin is not a Skyrim Special Edition one";
            return false;
        }
        std::string orderError;
        if (!mOrder.add(plugin->name(), plugin->header().isLight(), orderError))
        {
            ++mStats.mPluginsRefused;
            error = plugin->name() + ": " + orderError;
            return false;
        }
        PluginData data;
        data.mPath = path;
        data.mName = plugin->name();
        (void)parseRecords(*plugin, mOrder, mOptions.mLimits, data.mParsed);
        mPlugins.push_back(std::move(data));
        ++mStats.mPlugins;
        indexPlugin(static_cast<std::uint32_t>(mPlugins.size() - 1));
        return true;
    }

    std::size_t SkyrimWorldView::load(
        const std::vector<std::filesystem::path>& paths, std::vector<std::string>& problems)
    {
        std::size_t taken = 0;
        for (const std::filesystem::path& path : paths)
        {
            std::string error;
            if (addPlugin(path, error))
                ++taken;
            else
                problems.push_back(error);
        }
        finish();
        return taken;
    }

    void SkyrimWorldView::resolveNames(PluginData& plugin, const StringTable* table)
    {
        ParsedPlugin& p = plugin.mParsed;
        const auto resolve = [&](LString& s) {
            if (table != nullptr && s.mLocalized)
                (void)lookupLString(s, *table);
        };
        for (WorldRecord& w : p.mWorlds)
            resolve(w.mName);
        for (CellRecord& c : p.mCells)
            resolve(c.mName);
        for (NpcRecord& n : p.mNpcs)
            resolve(n.mName);
        for (ModelRecord& m : p.mModels)
        {
            resolve(m.mName);
            if (m.mHarvest)
                resolve(m.mHarvest->mActivateText); // task 0221
            resolve(m.mActivateText); // task 0224
        }
        for (ItemRecord& i : p.mItems)
            resolve(i.mName);
    }

    void SkyrimWorldView::indexPlugin(std::uint32_t pluginIndex)
    {
        PluginData& plugin = mPlugins[pluginIndex];
        ParsedPlugin& p = plugin.mParsed;

        mStats.mUnresolvedFormIds += p.mUnresolvedFormIds;
        for (const Content::Error& e : p.mErrors)
            noteError(e);
        if (p.mWalkError)
            noteError(*p.mWalkError);

        // Names of a localised plugin come from its string tables.
        StringTable table;
        bool haveTable = false;
        if (p.mHeader.isLocalized())
        {
            if (mFiles != nullptr)
            {
                std::string bytes;
                const std::string tablePath
                    = stringTablePath(stemOf(plugin.mPath), mOptions.mLanguage, StringTableKind::Strings);
                if (mFiles->read(tablePath, bytes))
                {
                    Content::Error tableError;
                    haveTable = StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                        StringTableKind::Strings, plugin.mName, table, tableError);
                    if (!haveTable)
                        noteError(tableError);
                }
            }
            if (!haveTable)
                ++mStats.mStringTablesMissing;
        }
        resolveNames(plugin, haveTable ? &table : nullptr);

        for (std::uint32_t i = 0; i < p.mWorlds.size(); ++i)
        {
            const WorldRecord& w = p.mWorlds[i];
            if (w.mCommon.mId == kNullFormId || w.mCommon.mId == kUnresolvedFormId)
                continue;
            WorldEntry& entry = mWorlds[w.mCommon.mId];
            entry.mInfo.id = w.mCommon.mId;
            if (!w.mCommon.mEditorId.empty())
                entry.mInfo.editorId = w.mCommon.mEditorId;
            if (!w.mName.mText.empty())
                entry.mInfo.name = w.mName.mText;
        }
        for (std::uint32_t i = 0; i < p.mCells.size(); ++i)
        {
            const CellRecord& c = p.mCells[i];
            if (c.mCommon.mId == kNullFormId || c.mCommon.mId == kUnresolvedFormId)
                continue;
            mCells[c.mCommon.mId].mSources.push_back({ pluginIndex, i });
        }
        for (const auto& [ref, cell] : p.mTeleportRefCells)
        {
            if (ref == kNullFormId || ref == kUnresolvedFormId || cell == kNullFormId || cell == kUnresolvedFormId)
                continue;
            mRefCells[ref] = cell; // later plugins win
            ++mStats.mTeleportDoorsIndexed;
        }
        for (std::uint32_t i = 0; i < p.mStatics.size(); ++i)
            mBases[p.mStatics[i].mCommon.mId] = { pluginIndex, BaseTable::Static, i };
        for (std::uint32_t i = 0; i < p.mModels.size(); ++i)
            mBases[p.mModels[i].mCommon.mId] = { pluginIndex, BaseTable::Model, i };
        for (std::uint32_t i = 0; i < p.mNpcs.size(); ++i)
            mBases[p.mNpcs[i].mCommon.mId] = { pluginIndex, BaseTable::Npc, i };
        for (std::uint32_t i = 0; i < p.mItems.size(); ++i)
            mBases[p.mItems[i].mCommon.mId] = { pluginIndex, BaseTable::Item, i };
        for (std::uint32_t i = 0; i < p.mLeveledLists.size(); ++i)
            mBases[p.mLeveledLists[i].mCommon.mId] = { pluginIndex, BaseTable::Leveled, i };
    }

    void SkyrimWorldView::finish()
    {
        mCellOrder.clear();
        mWorldOrder.clear();
        mCellByEditorId.clear();
        mCellByGrid.clear();
        mWorldByEditorId.clear();

        for (auto& [id, world] : mWorlds)
        {
            mWorldOrder.push_back(id);
            if (!world.mInfo.editorId.empty())
                mWorldByEditorId[lowerAscii(world.mInfo.editorId)] = id;
            world.mPersistentCell = kNullFormId;
        }

        // The winner's fields, with earlier plugins filling what the winner lacks.
        for (auto it = mCells.begin(); it != mCells.end();)
        {
            CellEntry& entry = it->second;
            const CellRecord& winner
                = mPlugins[entry.mSources.back().mPlugin].mParsed.mCells[entry.mSources.back().mIndex];
            if (winner.mCommon.deleted())
            {
                it = mCells.erase(it);
                continue;
            }
            sdkgame::WorldCellInfo info;
            info.id = it->first;
            bool haveFlags = false;
            for (auto s = entry.mSources.rbegin(); s != entry.mSources.rend(); ++s)
            {
                const CellRecord& c = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
                if (info.editorId.empty())
                    info.editorId = c.mCommon.mEditorId;
                if (info.name.empty())
                    info.name = c.mName.mText;
                if (!info.hasGrid && c.mGrid)
                {
                    info.hasGrid = true;
                    info.gridX = c.mGrid->x;
                    info.gridY = c.mGrid->y;
                }
                if (info.world == sdkgame::kNoWorldId && c.mWorld != kNullFormId)
                    info.world = c.mWorld;
                if (!haveFlags)
                {
                    info.interior = c.interior() || c.mInteriorBlock >= 0;
                    entry.mWorldPersistent = c.mWorldPersistentCell;
                    haveFlags = true;
                }
            }
            if (info.interior)
                info.world = sdkgame::kNoWorldId;
            entry.mInfo = std::move(info);
            for (auto s = entry.mSources.rbegin(); s != entry.mSources.rend(); ++s)
            {
                const CellRecord& c = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
                if (c.mLighting)
                {
                    entry.mLighting = c.mLighting;
                    break;
                }
            }
            for (auto s = entry.mSources.rbegin(); s != entry.mSources.rend(); ++s)
            {
                const CellRecord& c = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
                if (c.mLightingTemplate != kNullFormId)
                {
                    entry.mLightingTemplate = c.mLightingTemplate;
                    break;
                }
            }
            if (entry.mInfo.interior)
                for (auto s = entry.mSources.rbegin(); s != entry.mSources.rend(); ++s)
                {
                    const CellRecord& c = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
                    if (c.mWaterHeight || (c.mFlags & CellFlags::HasWater) != 0)
                    {
                        entry.mInteriorWater = c.interiorWaterHeight(); // the latest source that speaks of water wins
                        break;
                    }
                }
            if (entry.mWorldPersistent && entry.mInfo.world != sdkgame::kNoWorldId)
            {
                const auto w = mWorlds.find(entry.mInfo.world);
                if (w != mWorlds.end())
                    w->second.mPersistentCell = it->first;
            }
            ++it;
        }

        for (const auto& [id, entry] : mCells)
        {
            mCellOrder.push_back(id);
            // Two cells can share an editor id or a grid square (two plugins, two form ids). mCells is an
            // unordered_map, so the visiting order is the library's: the cell of the LATER plugin (the larger form id:
            // the load order index is its top byte) wins, whatever the order here.
            if (!entry.mInfo.editorId.empty())
            {
                const auto [pos, inserted] = mCellByEditorId.try_emplace(lowerAscii(entry.mInfo.editorId), id);
                if (!inserted && id > pos->second)
                    pos->second = id;
            }
            if (!entry.mInfo.interior && entry.mInfo.hasGrid)
            {
                const auto [pos, inserted] = mCellByGrid.try_emplace(
                    std::make_tuple(entry.mInfo.world, entry.mInfo.gridX, entry.mInfo.gridY), id);
                if (!inserted && id > pos->second)
                    pos->second = id;
            }
        }
        std::sort(mCellOrder.begin(), mCellOrder.end(), [this](FormId a, FormId b) {
            const sdkgame::WorldCellInfo& x = mCells.at(a).mInfo;
            const sdkgame::WorldCellInfo& y = mCells.at(b).mInfo;
            if (x.interior != y.interior)
                return x.interior; // interior cells first
            if (x.interior)
            {
                const std::string ex = lowerAscii(x.editorId);
                const std::string ey = lowerAscii(y.editorId);
                if (ex != ey)
                    return ex < ey;
                return a < b;
            }
            if (x.world != y.world)
                return x.world < y.world;
            if (x.hasGrid != y.hasGrid)
                return !x.hasGrid; // a world's persistent cell (no grid) before its squares
            if (x.gridY != y.gridY)
                return x.gridY < y.gridY;
            if (x.gridX != y.gridX)
                return x.gridX < y.gridX;
            return a < b;
        });

        mStats.mCells = mCells.size();
        mStats.mWorlds = mWorlds.size();
        mStats.mBaseForms = mBases.size();
        std::uint64_t inIndex = 0;
        for (const PluginData& plugin : mPlugins)
            for (const CellRecord& c : plugin.mParsed.mCells)
                inIndex += c.refCount();
        mStats.mReferencesInIndex = inIndex;
        mFinished = true;
    }

    // ---- queries ----------------------------------------------------------------------------------------------------

    const SkyrimWorldView::Stats& SkyrimWorldView::stats() const
    {
        static const Stats sEmpty;
        return awaitIndex() ? mStats : sEmpty;
    }

    const std::vector<Content::Error>& SkyrimWorldView::errors() const
    {
        static const std::vector<Content::Error> sEmpty;
        return awaitIndex() ? mErrors : sEmpty;
    }

    const RefCellIndex& SkyrimWorldView::teleportDoorCells() const
    {
        static const RefCellIndex sEmpty;
        return awaitIndex() ? mRefCells : sEmpty;
    }

    std::size_t SkyrimWorldView::cellCount() const
    {
        return awaitIndex() ? mCellOrder.size() : 0;
    }

    std::size_t SkyrimWorldView::worldCount() const
    {
        return awaitIndex() ? mWorldOrder.size() : 0;
    }

    std::size_t SkyrimWorldView::listCells(
        std::size_t first, std::size_t maxCount, std::vector<sdkgame::WorldCellInfo>& out) const
    {
        std::size_t appended = 0;
        if (!awaitIndex())
            return 0;
        for (std::size_t i = first; i < mCellOrder.size() && appended < maxCount; ++i, ++appended)
            out.push_back(mCells.at(mCellOrder[i]).mInfo);
        return appended;
    }

    std::size_t SkyrimWorldView::listWorlds(std::vector<sdkgame::WorldInfo>& out) const
    {
        if (!awaitIndex())
            return 0;
        for (const FormId id : mWorldOrder)
            out.push_back(mWorlds.at(id).mInfo);
        return mWorldOrder.size();
    }

    std::optional<sdkgame::WorldCellInfo> SkyrimWorldView::findCellByEditorId(std::string_view editorId) const
    {
        if (!awaitIndex())
            return std::nullopt;
        const auto it = mCellByEditorId.find(lowerAscii(editorId));
        if (it == mCellByEditorId.end())
            return std::nullopt;
        return mCells.at(it->second).mInfo;
    }

    std::optional<sdkgame::WorldCellInfo> SkyrimWorldView::findCellByGrid(
        sdkgame::WorldId world, std::int32_t x, std::int32_t y) const
    {
        if (!awaitIndex())
            return std::nullopt;
        const auto it = mCellByGrid.find({ world, x, y });
        if (it == mCellByGrid.end())
            return std::nullopt;
        return mCells.at(it->second).mInfo;
    }

    std::optional<sdkgame::WorldInfo> SkyrimWorldView::findWorldByEditorId(std::string_view editorId) const
    {
        if (!awaitIndex())
            return std::nullopt;
        const auto it = mWorldByEditorId.find(lowerAscii(editorId));
        if (it == mWorldByEditorId.end())
            return std::nullopt;
        return mWorlds.at(it->second).mInfo;
    }

    // ---- cells ------------------------------------------------------------------------------------------------------

    bool SkyrimWorldView::readCellRefs(
        const CellSource& source, const CellEntry&, std::vector<PlacedRef>& out, std::string& error)
    {
        const PluginData& plugin = mPlugins[source.mPlugin];
        const CellRecord& cell = plugin.mParsed.mCells[source.mIndex];
        if (!cell.mHasChildren)
            return true; // a cell without a children group has no references in this plugin

        // The file is read again from disk: header record, the cell record, the cell-children group.
        std::error_code ec;
        const std::uint64_t fileSize = std::filesystem::file_size(plugin.mPath, ec);
        if (ec)
        {
            error = plugin.mName + ": cannot read the file: " + ec.message();
            return false;
        }
        const std::uint64_t headerEnd = plugin.mParsed.mHeader.mFirstRecordOffset;
        const auto fits
            = [&](std::uint64_t offset, std::uint64_t size) { return offset <= fileSize && size <= fileSize - offset; };
        if (!fits(0, headerEnd) || !fits(cell.mCommon.mOffset, cell.mRecordSize)
            || !fits(cell.mChildrenOffset, cell.mChildrenSize))
        {
            error = plugin.mName
                + ": a cell's place in the file lies outside it (the file changed since it was indexed?)";
            return false;
        }
        const std::uint64_t total = headerEnd + cell.mRecordSize + cell.mChildrenSize;
        if (total > mOptions.mMaxCellBytes)
        {
            error = plugin.mName + ": a cell needs " + std::to_string(total) + " bytes, over the limit of "
                + std::to_string(mOptions.mMaxCellBytes);
            return false;
        }
        std::string bytes;
        bytes.resize(static_cast<std::size_t>(total));
        std::ifstream in(plugin.mPath, std::ios::binary);
        if (!in)
        {
            error = plugin.mName + ": cannot open the file";
            return false;
        }
        const auto readAt = [&](std::uint64_t offset, std::uint64_t size, std::size_t dest) {
            in.clear();
            in.seekg(static_cast<std::streamoff>(offset));
            in.read(bytes.data() + dest, static_cast<std::streamsize>(size));
            return in.gcount() == static_cast<std::streamsize>(size);
        };
        if (!readAt(0, headerEnd, 0) || !readAt(cell.mCommon.mOffset, cell.mRecordSize, headerEnd)
            || !readAt(cell.mChildrenOffset, cell.mChildrenSize, headerEnd + cell.mRecordSize))
        {
            error = plugin.mName + ": a read was cut short";
            return false;
        }
        mStats.mBytesRead += total;
        ++mStats.mCellReads;

        std::unique_ptr<Content::Plugin> mini;
        Content::Error openError;
        mini = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes, std::ios::binary), plugin.mName, openError);
        if (mini == nullptr)
        {
            error = openError.describe();
            return false;
        }
        ParseLimits limits = mOptions.mLimits;
        limits.mSkipReferences = false;
        ParsedPlugin parsed;
        (void)parseRecords(*mini, mOrder, limits, parsed);
        for (const Content::Error& e : parsed.mErrors)
            noteError(e);
        if (parsed.mWalkError)
            noteError(*parsed.mWalkError);
        if (parsed.mCells.empty())
        {
            error = plugin.mName + ": the cell record could not be read again";
            return false;
        }
        CellRecord& c = parsed.mCells.front();
        for (auto* list : { &c.mPersistent, &c.mTemporary, &c.mVisibleDistant, &c.mOther })
            for (PlacedRef& r : *list)
                out.push_back(std::move(r));
        return true;
    }

    FormId SkyrimWorldView::formFromFile(std::string_view lowerFile, std::uint32_t local) const
    {
        if (mBuilding.load(std::memory_order_acquire) || mFailed.load(std::memory_order_acquire))
            return 0;
        const std::optional<FormId> prefix = mOrder.prefixOf(lowerFile);
        if (!prefix)
            return 0;
        // a light plugin has 12 bits of local id, a normal one 24 (the master index of the stored id is not asked)
        return *prefix | (local & (mOrder.isLight(lowerFile) ? 0xFFFu : 0xFFFFFFu));
    }

    bool SkyrimWorldView::inventoryInputs(
        std::vector<std::filesystem::path>& paths, LoadOrder& order, std::vector<LeveledList>& leveled) const
    {
        if (mBuilding.load(std::memory_order_acquire) || mFailed.load(std::memory_order_acquire))
            return false;
        for (const PluginData& plugin : mPlugins)
        {
            paths.push_back(plugin.mPath);
            leveled.insert(leveled.end(), plugin.mParsed.mLeveledLists.begin(), plugin.mParsed.mLeveledLists.end());
        }
        order = mOrder;
        return true;
    }

    bool SkyrimWorldView::lookupWeapon(FormId id, const WeaponRecord*& out)
    {
        out = nullptr;
        if (mCombatIndex == nullptr)
            return false;
        out = mCombatIndex->weapon(id);
        return true;
    }

    bool SkyrimWorldView::lookupAmmo(FormId id, const AmmoRecord*& out)
    {
        out = nullptr;
        if (mCombatIndex == nullptr)
            return false;
        out = mCombatIndex->ammo(id);
        return true;
    }

    bool SkyrimWorldView::armourPieceById(FormId id, ArmourPiece& out)
    {
        if (!awaitIndex() || !ensureActorIndex())
            return false;
        const ArmorExtra* armor = mActorIndex->armor(id);
        if (armor == nullptr)
            return false;
        out = { static_cast<float>(armor->mRating) / 100.f, armor->mSlotMask, armor->mArmorType, id, armor->mKeywords };
        return true;
    }

    float SkyrimWorldView::healthOfEffect(FormId effect, float magnitude, std::uint32_t duration)
    {
        constexpr std::int32_t kHealthValue = 24; // the Health actor value (RECALLED-VERIFY)
        constexpr std::uint32_t kHostile = 0x1, kDetrimental = 0x4; // MGEF DATA flags (RECALLED-VERIFY)
        if (mCombatIndex == nullptr || duration != 0 || !(magnitude > 0.f))
            return 0.f;
        const MagicEffectRecord* record = mCombatIndex->magicEffect(effect);
        if (record == nullptr || !record->mHasData || record->mArchetype != 0 || record->mPrimaryValue != kHealthValue
            || (record->mFlags & (kHostile | kDetrimental)) != 0)
            return 0.f;
        return magnitude;
    }

    bool SkyrimWorldView::magicEffectData(
        FormId effect, std::uint32_t& flags, float& baseCost, std::uint32_t& archetype, std::int32_t& primaryValue)
    {
        if (mCombatIndex == nullptr)
            return false;
        const MagicEffectRecord* record = mCombatIndex->magicEffect(effect);
        if (record == nullptr || !record->mHasData)
            return false;
        flags = record->mFlags;
        baseCost = record->mBaseCost;
        archetype = record->mArchetype;
        primaryValue = record->mPrimaryValue;
        return true;
    }

    bool SkyrimWorldView::npcInventory(FormId base, std::vector<InventoryEntry>& out)
    {
        if (!awaitIndex() || !ensureActorIndex())
            return false;
        ActorIndex::Resolved resolved;
        if (mActorIndex->resolve(base, mStandInPlayerLevel, static_cast<std::uint32_t>(base), resolved)
            != ActorIndex::ResolveStatus::Ok)
            return false;
        out = resolved.mInventory;
        if (const OutfitRecord* outfit = mActorIndex->outfit(resolved.mDefaultOutfit))
            for (const FormId item : outfit->mItems)
                out.push_back({ item, 1 });
        return true;
    }

    barter::VendorLookup SkyrimWorldView::vendorOf(FormId npcBase, barter::VendorInfo& out)
    {
        out = barter::VendorInfo();
        if (!awaitIndex() || !ensureActorIndex())
            return barter::VendorLookup::Pending;
        if (mDialogue == nullptr || mDialogue->mData == nullptr)
            return barter::VendorLookup::Pending; // the placed references and form lists come with the conversation
        ActorIndex::Resolved resolved;
        if (mActorIndex->resolve(npcBase, mStandInPlayerLevel, static_cast<std::uint32_t>(npcBase), resolved)
            != ActorIndex::ResolveStatus::Ok)
            return barter::VendorLookup::NotVendor;
        for (const FactionMembership& membership : resolved.mFactions)
        {
            const FactionRecord* faction = mActorIndex->faction(membership.mFaction);
            if (faction == nullptr || !faction->mVendor.isVendor())
                continue;
            const PlacedEntry* chest = mDialogue->mData->mPlacedRefs.find(faction->mVendor.mChest);
            if (chest == nullptr || chest->mBase == kNullFormId || chest->mDeleted)
                continue; // a faction whose chest the data does not place is no vendor
            out.mFaction = membership.mFaction;
            out.mChestRef = faction->mVendor.mChest;
            out.mChestBase = chest->mBase;
            out.mListIsExclusion = faction->mVendor.mListIsExclusion;
            out.mBuysStolen = faction->mVendor.mBuysStolen;
            out.mStartHour = faction->mVendor.mStartHour;
            out.mEndHour = faction->mVendor.mEndHour;
            if (const FormListRecord* list = mDialogue->mData->mDialogue.formList(faction->mVendor.mList))
                out.mKeywords = list->mEntries;
            return barter::VendorLookup::Ready;
        }
        return barter::VendorLookup::NotVendor;
    }

    std::string SkyrimWorldView::displayNameOf(sdkgame::WorldId base)
    {
        if (!awaitIndex())
            return {};
        const auto it = mBases.find(base);
        if (it == mBases.end())
            return {};
        const ParsedPlugin& p = mPlugins[it->second.mPlugin].mParsed;
        const LString* name = nullptr;
        switch (it->second.mTable)
        {
            case BaseTable::Npc:
                name = &p.mNpcs[it->second.mIndex].mName;
                break;
            case BaseTable::Model:
            {
                const ModelRecord& m = p.mModels[it->second.mIndex];
                name = &m.mName;
                // Task 0221: with the harvest on, a plant's prompt is its verb and name ("Harvest Canis Root").
                if (mHarvestPrompts && m.mHarvest && m.mHarvest->mIngredient != kNullFormId)
                    return harvest::promptFor(
                        m.mHarvest->mActivateText.mPresent ? m.mHarvest->mActivateText.mText : std::string(),
                        name->mPresent ? name->mText : m.mCommon.mEditorId);
                break;
            }
            case BaseTable::Item:
                name = &p.mItems[it->second.mIndex].mName;
                break;
            default:
                return {};
        }
        // A localised name that could not be looked up has no text: the core then shows the editor id.
        return name->mPresent ? name->mText : std::string();
    }

    FormId SkyrimWorldView::summonBase(FormId associated, std::uint16_t level, std::uint32_t pick)
    {
        if (associated == kNullFormId || associated == kUnresolvedFormId || !awaitIndex() || !ensureActorIndex())
            return kNullFormId;
        FormId current = associated;
        for (int depth = 0; depth < 4; ++depth)
        {
            if (mActorIndex->npc(current) != nullptr)
                return current;
            const LeveledList* list = mActorIndex->leveledList(current);
            if (list == nullptr || !list->mIsNpcList)
                return kNullFormId;
            const LeveledEntry* entry = mActorIndex->pickLeveled(current, level, pick);
            if (entry == nullptr)
                return kNullFormId;
            current = entry->mReference;
        }
        return kNullFormId;
    }

    bool SkyrimWorldView::describeBase(FormId base, sdkgame::PlacedObject& out) const
    {
        if (base == kNullFormId || base == kUnresolvedFormId || mBases.find(base) == mBases.end())
            return false;
        PlacedRef ref; // a reference that is nowhere: the origin, no rotation, scale 1
        ref.mBase = base;
        bool missing = false;
        sdkgame::PlacedObject described = makeObject(ref, missing);
        if (missing)
            return false;
        out = std::move(described);
        return true;
    }

    const LightData* SkyrimWorldView::lightOfBase(FormId base) const
    {
        if (base == kNullFormId || base == kUnresolvedFormId)
            return nullptr;
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return nullptr;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        return m.mLight ? &*m.mLight : nullptr;
    }

    sdkgame::PlacedObject SkyrimWorldView::makeObject(const PlacedRef& ref, bool& baseMissing) const
    {
        sdkgame::PlacedObject o;
        o.id = ref.mCommon.mId;
        o.position = { ref.mPosition.x, ref.mPosition.y, ref.mPosition.z };
        o.rotation = { ref.mRotation.x, ref.mRotation.y, ref.mRotation.z };
        o.scale = finite(ref.mScale) && ref.mScale > 0.0f ? ref.mScale : 1.0f;
        o.persistent = ref.persistent() || ref.mGroup == RefGroup::Persistent;
        o.initiallyDisabled = ref.initiallyDisabled();
        baseMissing = false;
        if (ref.mBase == kNullFormId || ref.mBase == kUnresolvedFormId)
        {
            baseMissing = true;
            return o;
        }
        o.base = ref.mBase;
        const auto it = mBases.find(ref.mBase);
        if (it == mBases.end())
        {
            baseMissing = true;
            return o;
        }
        const ParsedPlugin& p = mPlugins[it->second.mPlugin].mParsed;
        const auto bounds = [&](const std::optional<std::array<std::int16_t, 6>>& b) {
            if (!b)
                return;
            o.hasBounds = true;
            o.boundsMin = { static_cast<float>((*b)[0]), static_cast<float>((*b)[1]), static_cast<float>((*b)[2]) };
            o.boundsMax = { static_cast<float>((*b)[3]), static_cast<float>((*b)[4]), static_cast<float>((*b)[5]) };
        };
        switch (it->second.mTable)
        {
            case BaseTable::Static:
            {
                const StaticRecord& s = p.mStatics[it->second.mIndex];
                o.baseKind = sdkgame::BaseKind::Static;
                o.baseEditorId = s.mCommon.mEditorId;
                o.modelPath = s.mModel;
                bounds(s.mBounds);
                break;
            }
            case BaseTable::Model:
            {
                const ModelRecord& m = p.mModels[it->second.mIndex];
                o.baseKind = kindOfModelType(m.mType);
                o.baseEditorId = m.mCommon.mEditorId;
                o.modelPath = m.mModel;
                bounds(m.mBounds);
                break;
            }
            case BaseTable::Npc:
                o.baseKind = sdkgame::BaseKind::Actor;
                o.baseEditorId = p.mNpcs[it->second.mIndex].mCommon.mEditorId;
                break;
            case BaseTable::Item:
            {
                const ItemRecord& i = p.mItems[it->second.mIndex];
                o.baseKind = sdkgame::BaseKind::Item;
                o.baseEditorId = i.mCommon.mEditorId;
                o.modelPath = i.mModel;
                break;
            }
            case BaseTable::Leveled:
                o.baseKind = sdkgame::BaseKind::LeveledList;
                o.baseEditorId = p.mLeveledLists[it->second.mIndex].mCommon.mEditorId;
                break;
        }
        return o;
    }

    std::shared_ptr<const SkyrimWorldView::Materialised> SkyrimWorldView::materialise(FormId cell, std::string& error)
    {
        for (const auto& [id, content] : mCache)
        {
            if (id == cell)
            {
                ++mStats.mCacheHits;
                return content;
            }
        }
        const auto entryIt = mCells.find(cell);
        if (entryIt == mCells.end())
        {
            error = "there is no such cell";
            return nullptr;
        }
        const CellEntry& entry = entryIt->second;

        // Merge the placements of every plugin that touches the cell, in load order.
        std::map<FormId, PlacedRef> merged;
        for (const CellSource& source : entry.mSources)
        {
            std::vector<PlacedRef> refs;
            if (!readCellRefs(source, entry, refs, error))
                return nullptr;
            mStats.mReferencesDecoded += refs.size();
            for (PlacedRef& r : refs)
            {
                const FormId id = r.mCommon.mId;
                if (id == kNullFormId || id == kUnresolvedFormId)
                    continue;
                if (r.mCommon.deleted())
                    merged.erase(id);
                else
                    merged[id] = std::move(r);
            }
        }

        // Task 0299: the facing of a scripted placement and of every link target (a trap's ports are a chain of
        // plain references whose last link has no script of its own)
        std::unordered_set<FormId> linkTargets;
        for (const auto& [id, ref] : merged)
            for (const RefLink& link : ref.mLinks)
                linkTargets.insert(link.mRef);
        for (const auto& [id, ref] : merged)
        {
            if ((!ref.mScripts.empty() || linkTargets.count(id) != 0) && ref.mHasTransform
                && mRefRotations.size() < 1024u * 1024u)
                mRefRotations[id] = { ref.mRotation.x, ref.mRotation.y, ref.mRotation.z };
            if ((!ref.mLinks.empty() || ref.mPatrolIdle >= 0.f) && mRefLinks.size() < 1024u * 1024u)
            {
                if (!ref.mLinks.empty())
                    mRefLinks[id] = ref.mLinks;
                if (ref.mPatrolIdle >= 0.f)
                    mPatrolIdles[id] = ref.mPatrolIdle;
            }
        }

        auto result = std::make_shared<Materialised>();
        for (auto& [id, ref] : merged)
        {
            if (result->mObjects.size() >= mOptions.mMaxObjectsPerCell)
            {
                ++mStats.mObjectsDropped;
                continue;
            }
            bool missing = false;
            result->mObjects.push_back(makeObject(ref, missing));
            PlacementComponents components = makeComponents(ref, mRefCells, mStats.mComponents, NeutralLimits{});
            if (const LightData* light = lightOfBase(ref.mBase))
                components.light = makeLightComponent(*light, mStats.mComponents, NeutralLimits{});
            if (!components.empty())
                result->mComponents.emplace(id, std::move(components));
            if (ref.mEnableParent && ref.mEnableParent->mParent != kNullFormId
                && ref.mEnableParent->mParent != kUnresolvedFormId)
                result->mEnableParents.emplace(
                    id, std::make_pair(ref.mEnableParent->mParent, (ref.mEnableParent->mFlags & 1) != 0));
            if (mHelloGreetings) // task 0308: the template scripts of an NPC_ need the actor index
                (void)ensureActorIndex();
            std::vector<vmad::Script> allScripts = vmad::combineScripts(ref.mScripts, baseScriptsOf(ref.mBase));
            // task 0310: a trigger volume with no script of its own may hold a quest alias whose script listens
            if (!allScripts.empty() || (mPrimitiveTriggers && ref.mPrimitive))
            {
                ReferenceScripts scripts;
                scripts.mReference = id;
                scripts.mBase = ref.mBase;
                scripts.mActor = ref.mKind == RefKind::Actor;
                scripts.mScripts = std::move(allScripts);
                if (ref.mPrimitive) // task 0256
                {
                    scripts.mHasPrimitive = true;
                    scripts.mPrimitive = *ref.mPrimitive;
                    scripts.mPosition = ref.mPosition;
                    scripts.mRotation = ref.mRotation;
                    scripts.mCell = cell;
                }
                result->mScripts.push_back(std::move(scripts));
            }
            if (missing)
                ++result->mUnresolvedBases;
            // task 0330: the ACHR record flag "starts dead" (the corpses lying about a crypt)
            if (ref.mKind == RefKind::Actor && (ref.mCommon.mFlags & kRecordStartsDead) != 0
                && mStartsDeadRefs.size() < 1024u * 1024u)
                mStartsDeadRefs.insert(id);
        }

        mCache.emplace_back(cell, result);
        while (mCache.size() > std::max<std::size_t>(mOptions.mMaxCachedCells, 1))
            mCache.pop_front();
        return result;
    }

    const PlacementComponents* SkyrimWorldView::find(sdkgame::WorldId cell, sdkgame::WorldId placement) const
    {
        if (!awaitIndex() || cell == kNullFormId || cell != mLastCell)
            return nullptr;
        const auto it = mLastComponents.find(placement);
        return it != mLastComponents.end() ? &it->second : nullptr;
    }

    std::optional<sdkgame::CellLighting> SkyrimWorldView::findCellLighting(sdkgame::WorldId cell) const
    {
        if (!awaitIndex() || cell == kNullFormId || cell != mLastCell)
            return std::nullopt;
        return mLastLighting;
    }

    InteriorWaterEvidence SkyrimWorldView::gatherWaterEvidence(
        const std::vector<sdkgame::PlacedObject>& objects, const std::map<FormId, PlacementComponents>& components)
    {
        InteriorWaterEvidence evidence;
        std::vector<float> heights;
        heights.reserve(objects.size());
        for (const sdkgame::PlacedObject& o : objects)
        {
            if (!std::isfinite(o.position.z))
                continue;
            const float z = static_cast<float>(o.position.z);
            if (!evidence.mAnyObject || z < evidence.mMinObjectZ)
                evidence.mMinObjectZ = z;
            evidence.mAnyObject = true;
            heights.push_back(z);
            const auto c = components.find(static_cast<FormId>(o.id));
            if (c != components.end() && c->second.teleport)
            {
                if (!evidence.mAnyDoor || z < evidence.mMinDoorZ)
                    evidence.mMinDoorZ = z;
                evidence.mAnyDoor = true;
            }
        }
        if (!heights.empty())
        {
            const std::size_t quarter = heights.size() / 4;
            std::nth_element(heights.begin(), heights.begin() + static_cast<std::ptrdiff_t>(quarter), heights.end());
            evidence.mCount = static_cast<std::uint32_t>(heights.size());
            evidence.mLowQuarterZ = heights[quarter];
        }
        return evidence;
    }

    std::optional<float> SkyrimWorldView::findInteriorWater(sdkgame::WorldId cell) const
    {
        if (!awaitIndex() || cell == kNullFormId)
            return std::nullopt;
        const auto it = mCells.find(cell);
        if (it == mCells.end() || !it->second.mInfo.interior || !it->second.mInteriorWater)
            return std::nullopt;
        // The record's flag and height are not enough (71 cells have 0.0): the cell's content must agree. A cell the
        // view has not loaded yet is judged by the record alone (the engine builds a cell before it asks).
        // Task 0330: the engine of a start cell asks before it builds the cell (Dimhollow Crypt's puzzle room drowned
        // the player at once), so the content is read for the question here (the cell stays cached for the build).
        auto evidence = mWaterEvidence.find(cell);
        if (evidence == mWaterEvidence.end() && mJudgeWaterEarly)
        {
            std::string error;
            const std::shared_ptr<const Materialised> own = const_cast<SkyrimWorldView*>(this)->materialise(cell, error);
            if (own != nullptr && mWaterEvidence.size() < 65536)
                evidence = const_cast<SkyrimWorldView*>(this)
                               ->mWaterEvidence.emplace(cell, gatherWaterEvidence(own->mObjects, own->mComponents))
                               .first;
        }
        if (evidence != mWaterEvidence.end() && !interiorWaterPlausible(*it->second.mInteriorWater, evidence->second))
            return std::nullopt;
        return it->second.mInteriorWater;
    }

    // Task 0293: a placement with an enable parent starts in its parent's state (opposite when the XESP flag says so),
    // a chain of parents follows its root; a parent this cell and the world's persistent cell do not hold (the player, a
    // marker in another square) leaves the placement as its own flags say. The seven towns' Imperial and Stormcloak
    // garrisons are enabled by markers of which only one starts enabled.
    void SkyrimWorldView::foldEnableParents(
        std::vector<sdkgame::PlacedObject>& objects, const Materialised& own, const Materialised* persistent)
    {
        if (own.mEnableParents.empty() && (persistent == nullptr || persistent->mEnableParents.empty()))
            return;
        std::map<FormId, bool> rawDisabled;
        const auto note = [&](const Materialised& m) {
            for (const sdkgame::PlacedObject& o : m.mObjects)
                rawDisabled.emplace(static_cast<FormId>(o.id), o.initiallyDisabled);
        };
        note(own);
        if (persistent != nullptr)
            note(*persistent);
        const auto parentOf = [&](FormId id) -> const std::pair<FormId, bool>* {
            auto it = own.mEnableParents.find(id);
            if (it != own.mEnableParents.end())
                return &it->second;
            if (persistent != nullptr)
            {
                it = persistent->mEnableParents.find(id);
                if (it != persistent->mEnableParents.end())
                    return &it->second;
            }
            return nullptr;
        };
        constexpr int kMaxDepth = 8;
        const auto effective = [&](auto&& self, FormId id, int depth) -> bool {
            const auto raw = rawDisabled.find(id);
            const bool ownState = raw != rawDisabled.end() && raw->second;
            const std::pair<FormId, bool>* parent = parentOf(id);
            if (parent == nullptr || depth >= kMaxDepth || rawDisabled.find(parent->first) == rawDisabled.end())
                return ownState;
            const bool parentState = self(self, parent->first, depth + 1);
            return parent->second ? !parentState : parentState;
        };
        for (sdkgame::PlacedObject& o : objects)
            if (parentOf(static_cast<FormId>(o.id)) != nullptr)
                o.initiallyDisabled = effective(effective, static_cast<FormId>(o.id), 0);
    }

    bool SkyrimWorldView::loadCell(sdkgame::WorldId cell, sdkgame::WorldCellContent& out, std::string& error)
    {
        out = sdkgame::WorldCellContent{};
        error.clear();
        if (!awaitIndex())
        {
            error = "the world index is not ready";
            return false;
        }
        mLastCell = kNullFormId;
        mLastComponents.clear();
        mLastLighting.reset();
        try
        {
            const auto entryIt = mCells.find(cell);
            if (entryIt == mCells.end())
            {
                error = "there is no cell with this id";
                return false;
            }
            const CellEntry& entry = entryIt->second;
            const std::shared_ptr<const Materialised> own = materialise(cell, error);
            if (own == nullptr)
                return false;

            sdkgame::WorldCellContent content;
            content.cell = entry.mInfo;
            content.objects = own->mObjects;
            content.unresolvedBases = own->mUnresolvedBases;
            std::map<FormId, PlacementComponents> components = own->mComponents;
            std::shared_ptr<const Materialised> persistentCell; // task 0293: its markers can be enable parents

            // The world's persistent placements that stand in this exterior square.
            if (!entry.mInfo.interior && entry.mInfo.hasGrid && !entry.mWorldPersistent)
            {
                const auto world = mWorlds.find(entry.mInfo.world);
                if (world != mWorlds.end() && world->second.mPersistentCell != kNullFormId
                    && world->second.mPersistentCell != cell)
                {
                    std::string persistentError;
                    const std::shared_ptr<const Materialised> persistent
                        = materialise(world->second.mPersistentCell, persistentError);
                    if (persistent == nullptr)
                    {
                        error = persistentError;
                        return false;
                    }
                    // A placement can be in both lists: one plugin keeps it in the world's persistent cell and a later
                    // plugin moves it into this square's temporary children (same form id, persistent flag cleared;
                    // 5 squares of the five masters). The square's own copy is kept, so each id is listed once.
                    std::set<sdkgame::WorldId> ownIds;
                    for (const sdkgame::PlacedObject& placed : content.objects)
                        ownIds.insert(placed.id);
                    persistentCell = persistent;
                    for (const sdkgame::PlacedObject& o : persistent->mObjects)
                    {
                        if (!finite(o.position.x) || !finite(o.position.y))
                            continue;
                        if (ownIds.count(o.id) != 0)
                            continue;
                        if (static_cast<std::int32_t>(std::floor(o.position.x / kCellSize)) == entry.mInfo.gridX
                            && static_cast<std::int32_t>(std::floor(o.position.y / kCellSize)) == entry.mInfo.gridY)
                        {
                            content.objects.push_back(o);
                            if (const auto c = persistent->mComponents.find(o.id); c != persistent->mComponents.end())
                                components.emplace(o.id, c->second);
                            if (o.baseKind == sdkgame::BaseKind::Unknown)
                                ++content.unresolvedBases;
                        }
                    }
                    std::sort(content.objects.begin(), content.objects.end(),
                        [](const sdkgame::PlacedObject& a, const sdkgame::PlacedObject& b) { return a.id < b.id; });
                }
            }
            foldEnableParents(content.objects, *own, persistentCell.get());
            content.cell.objectCount = static_cast<std::uint32_t>(content.objects.size());
            if (mPlacements.size() < 4u * 1024 * 1024)
                for (const sdkgame::PlacedObject& o : content.objects)
                    mPlacements[static_cast<FormId>(o.id)] = { o.position.x, o.position.y, o.position.z };
            // Task 0135 S6b: furniture and actors by cell (bounded: the cells a session loads are few).
            if (mFurnitureByCell.size() < 4096)
            {
                std::vector<FurnitureRef>& furniture = mFurnitureByCell[cell];
                furniture.clear();
                for (const sdkgame::PlacedObject& o : content.objects)
                {
                    if (o.baseKind == sdkgame::BaseKind::Furniture && !o.initiallyDisabled && !o.modelPath.empty()
                        && furniture.size() < 4096)
                        furniture.push_back({ static_cast<FormId>(o.id), o.modelPath, o.position.x, o.position.y,
                            o.position.z, o.rotation.z, static_cast<double>(o.scale) });
                    else if (o.baseKind == sdkgame::BaseKind::Actor && mActorCell.size() < 1024 * 1024)
                        mActorCell[static_cast<FormId>(o.id)] = cell;
                }
            }
            if (entry.mInfo.interior && mWaterEvidence.size() < 65536)
                mWaterEvidence[cell] = gatherWaterEvidence(content.objects, components);
            out = std::move(content);
            mLastCell = cell;
            if (mOwners.size() < 1024u * 1024u)
                for (const auto& [placement, component] : components)
                    if (component.owner)
                        mOwners[placement] = *component.owner; // task 0204
            if (mItemCounts.size() < 1024u * 1024u)
                for (const auto& [placement, component] : components)
                    if (component.itemCount > 0)
                        mItemCounts[placement] = component.itemCount; // task 0208
            mLastComponents = std::move(components);
            if (entry.mLighting && entry.mInfo.interior)
            {
                // Task 0303: the values the cell inherits come from its lighting template, not from its own XCLL.
                const CellLightingData* lightingTemplate = nullptr;
                if (entry.mLightingTemplate != kNullFormId)
                {
                    const auto base = mBases.find(entry.mLightingTemplate);
                    if (base != mBases.end() && base->second.mTable == BaseTable::Model)
                    {
                        const ModelRecord& m = mPlugins[base->second.mPlugin].mParsed.mModels[base->second.mIndex];
                        if (m.mType == Content::fourCC("LGTM") && m.mLighting)
                            lightingTemplate = &*m.mLighting;
                    }
                }
                mLastLighting = makeCellLighting(applyLightingTemplate(*entry.mLighting, lightingTemplate));
            }
            return true;
        }
        catch (const std::exception& e)
        {
            out = sdkgame::WorldCellContent{};
            error = std::string("the cell could not be loaded: ") + e.what();
            return false;
        }
    }

    // ---- task 0204 (crime) ------------------------------------------------------------------------------------------

    std::optional<sdkgame::OwnerComponent> SkyrimWorldView::ownerOfPlacement(FormId placement) const
    {
        const auto it = mOwners.find(placement);
        if (it == mOwners.end())
            return std::nullopt;
        return it->second;
    }

    FormId SkyrimWorldView::cellOfPlacement(FormId placement) const
    {
        const PlacedRefIndex* refs = placedRefs();
        const PlacedEntry* entry = refs != nullptr && placement != kNullFormId ? refs->find(placement) : nullptr;
        return entry != nullptr ? entry->mCell : kNullFormId;
    }

    FormId SkyrimWorldView::cellIdByEditorId(std::string_view editorId)
    {
        if (!awaitIndex())
            return kNullFormId;
        const auto it = mCellByEditorId.find(lowerAscii(editorId));
        return it == mCellByEditorId.end() ? kNullFormId : it->second;
    }

    std::string SkyrimWorldView::cellEditorIdOf(FormId cell)
    {
        if (cell == kNullFormId || !awaitIndex())
            return {};
        for (const auto& [name, id] : mCellByEditorId)
            if (id == cell)
                return name;
        return {};
    }

    bool SkyrimWorldView::plantOf(FormId base, HarvestData& out, std::string& name)
    {
        if (base == kNullFormId || base == kUnresolvedFormId || !awaitIndex())
            return false;
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return false;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        if (!m.mHarvest || m.mHarvest->mIngredient == kNullFormId)
            return false;
        out = *m.mHarvest;
        name = m.mName.mPresent ? m.mName.mText : m.mCommon.mEditorId;
        return true;
    }

    FormId SkyrimWorldView::talkingActivatorVoice(FormId base)
    {
        if (base == kNullFormId || base == kUnresolvedFormId || !awaitIndex())
            return kNullFormId;
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return kNullFormId;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        return m.mType == Content::fourCC("TACT") ? m.mVoiceType : kNullFormId;
    }

    bool SkyrimWorldView::resourceOf(
        FormId base, mining::ResourceInfo& out, std::string& name, std::string& activateText)
    {
        if (base == kNullFormId || base == kUnresolvedFormId || !awaitIndex())
            return false;
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return false;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        if ((m.mType != Content::fourCC("ACTI") && m.mType != Content::fourCC("FURN")) || m.mScripts.empty())
            return false;
        const mining::ResourceInfo info = mining::resourceFromScripts(m.mScripts);
        if ((info.mKind == mining::ResourceKind::Ore && m.mType != Content::fourCC("ACTI"))
            || (info.mKind == mining::ResourceKind::Wood && m.mType != Content::fourCC("FURN"))
            || info.mKind == mining::ResourceKind::None)
            return false;
        out = info;
        name = m.mName.mPresent ? m.mName.mText : m.mCommon.mEditorId;
        activateText = m.mActivateText.mPresent ? m.mActivateText.mText : std::string();
        return true;
    }

    bool SkyrimWorldView::formListEntries(FormId list, std::vector<FormId>& out)
    {
        if (list == kNullFormId || !awaitIndex())
            return false;
        if (mDialogue == nullptr && !mDialogueThread.joinable())
            startDialoguePrefetch();
        if (!ensureDialogue() || mDialogue == nullptr || mDialogue->mData == nullptr)
            return false;
        const FormListRecord* record = mDialogue->mData->mDialogue.formList(list);
        if (record == nullptr)
            return false;
        out = record->mEntries;
        return true;
    }

    std::string SkyrimWorldView::messageTextOf(FormId message)
    {
        if (message == kNullFormId || message == kUnresolvedFormId || !awaitIndex())
            return {};
        const auto it = mBases.find(message);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return {};
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        if (m.mType != Content::fourCC("MESG") || !m.mDescription.mPresent)
            return {};
        if (!m.mDescription.mLocalized)
            return m.mDescription.mText;
        if (mFiles == nullptr)
            return {};
        // The description is in .DLSTRINGS in Skyrim.esm; the other tables are tried too, as the loading tips do.
        for (const StringTableKind kind :
            { StringTableKind::DlStrings, StringTableKind::Strings, StringTableKind::IlStrings })
        {
            const std::uint32_t key = it->second.mPlugin * 4u + static_cast<std::uint32_t>(kind);
            auto& slot = mMessageTables[key];
            if (slot == nullptr)
            {
                std::string bytes;
                const std::string path
                    = stringTablePath(stemOf(mPlugins[it->second.mPlugin].mPath), mOptions.mLanguage, kind);
                auto table = std::make_unique<StringTable>();
                Content::Error tableError;
                if (mFiles->read(path, bytes)
                    && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, kind,
                        mPlugins[it->second.mPlugin].mName, *table, tableError))
                    slot = std::move(table);
                else
                    slot = std::make_unique<StringTable>(); // empty: not tried again
            }
            if (const std::string* found = slot->find(m.mDescription.mId))
                return *found;
        }
        return {};
    }

    std::string SkyrimWorldView::scriptClassOf(FormId form)
    {
        if (form == kNullFormId || form == kUnresolvedFormId || !awaitIndex())
            return {};
        if (mCells.find(form) != mCells.end())
            return "Cell"; // task 0261: HousePurchaseScript's `cell` properties
        const auto it = mBases.find(form);
        if (it != mBases.end() && it->second.mTable == BaseTable::Item
            && mPlugins[it->second.mPlugin].mParsed.mItems[it->second.mIndex].mKind == ItemKind::Weapon)
            return "Weapon"; // task 0299: DartTrap's Weapon.Fire
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return {};
        const std::uint32_t type = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex].mType;
        if (type == Content::fourCC("AMMO"))
            return "Ammo"; // task 0299
        if (type == Content::fourCC("SPEL"))
            return "Spell";
        if (type == Content::fourCC("KYWD"))
            return "Keyword"; // task 0270
        if (type == Content::fourCC("FLST"))
            return "FormList"; // task 0317
        return type == Content::fourCC("MESG") ? "Message" : std::string();
    }

    std::string SkyrimWorldView::stoneNameOf(FormId base)
    {
        if (base == kNullFormId || base == kUnresolvedFormId || !awaitIndex())
            return {};
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return {};
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        if (m.mType != Content::fourCC("ACTI"))
            return {};
        for (const vmad::Script& script : m.mScripts)
        {
            std::string lowered = script.mName;
            for (char& c : lowered)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            if (lowered == "powershrinescript" || lowered == "templeblessingscript")
                return m.mName.mPresent && !m.mName.mText.empty() ? m.mName.mText : m.mCommon.mEditorId;
        }
        return {};
    }

    bool SkyrimWorldView::messageOf(FormId message, MessageData& out)
    {
        out = MessageData();
        if (message == kNullFormId || message == kUnresolvedFormId || !awaitIndex())
            return false;
        const auto it = mBases.find(message);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return false;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        if (m.mType != Content::fourCC("MESG"))
            return false;
        out.mText = messageTextOf(message);
        // the strings hold a printf style percent sign (the data says "20%%")
        for (std::size_t at = out.mText.find("%%"); at != std::string::npos; at = out.mText.find("%%", at + 1))
            out.mText.erase(at, 1);
        out.mBox = m.mMessageBox;
        for (const LString& button : m.mButtons)
        {
            std::string text;
            if (!button.mPresent)
            {
            }
            else if (!button.mLocalized)
                text = button.mText;
            else if (mFiles != nullptr)
                for (const StringTableKind kind :
                    { StringTableKind::Strings, StringTableKind::DlStrings, StringTableKind::IlStrings })
                {
                    const std::uint32_t key = it->second.mPlugin * 4u + static_cast<std::uint32_t>(kind);
                    auto& slot = mMessageTables[key];
                    if (slot == nullptr)
                    {
                        std::string bytes;
                        const std::string path
                            = stringTablePath(stemOf(mPlugins[it->second.mPlugin].mPath), mOptions.mLanguage, kind);
                        auto table = std::make_unique<StringTable>();
                        Content::Error tableError;
                        if (mFiles->read(path, bytes)
                            && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                                kind, mPlugins[it->second.mPlugin].mName, *table, tableError))
                            slot = std::move(table);
                        else
                            slot = std::make_unique<StringTable>();
                    }
                    if (const std::string* found = slot->find(button.mId))
                    {
                        text = *found;
                        break;
                    }
                }
            out.mButtons.push_back(std::move(text));
        }
        return true;
    }

    std::int32_t SkyrimWorldView::itemCountOfPlacement(FormId placement) const
    {
        const auto it = mItemCounts.find(placement);
        return it == mItemCounts.end() ? 0 : it->second;
    }

    bool SkyrimWorldView::isFactionForm(FormId form) const
    {
        return mActorIndex != nullptr && mActorIndex->faction(form) != nullptr;
    }

    FormId SkyrimWorldView::horseOf(FormId reference) const
    {
        const PlacedRefIndex* placed = placedRefs();
        const PlacedEntry* entry = placed != nullptr ? placed->find(reference) : nullptr;
        return entry != nullptr ? entry->mHorse : kNullFormId;
    }

    bool SkyrimWorldView::isActorBaseForm(FormId form) const
    {
        return mActorIndex != nullptr && mActorIndex->npc(form) != nullptr;
    }

    const CrimeData* SkyrimWorldView::crimeDataOf(FormId faction) const
    {
        if (mActorIndex == nullptr)
            return nullptr;
        const FactionRecord* record = mActorIndex->faction(faction);
        return record != nullptr && record->mCrime.mPresent ? &record->mCrime : nullptr;
    }

    std::string SkyrimWorldView::factionNameOf(FormId faction) const
    {
        if (mActorIndex == nullptr)
            return {};
        const FactionRecord* record = mActorIndex->faction(faction);
        if (record == nullptr)
            return {};
        return record->mName.mText.empty() ? record->mCommon.mEditorId : record->mName.mText;
    }

    bool SkyrimWorldView::jailDestination(FormId faction, crime::JailDestination& out)
    {
        out = {};
        const CrimeData* crimeData = crimeDataOf(faction);
        if (crimeData == nullptr || crimeData->mJail == kNullFormId || !awaitIndex())
            return false;
        const auto markerCell = mRefCells.find(crimeData->mJail);
        if (markerCell == mRefCells.end())
            return false;
        std::string error;
        const std::shared_ptr<const Materialised> cell = materialise(markerCell->second, error);
        if (cell == nullptr)
            return false;
        const auto components = cell->mComponents.find(crimeData->mJail);
        if (components == cell->mComponents.end() || !components->second.teleport)
            return false;
        const sdkgame::TeleportComponent& teleport = *components->second.teleport;
        sdkgame::WorldId destinationCell = teleport.destinationCell;
        if (destinationCell == sdkgame::kNoWorldId)
        {
            const auto door = mRefCells.find(static_cast<FormId>(teleport.destinationDoor));
            if (door == mRefCells.end())
                return false;
            destinationCell = door->second;
        }
        const auto entry = mCells.find(static_cast<FormId>(destinationCell));
        if (entry == mCells.end() || entry->second.mInfo.editorId.empty())
            return false;
        const sdkgame::Quatd& q = teleport.destination.rotation;
        out.mValid = true;
        out.mCell = entry->second.mInfo.editorId;
        out.mX = teleport.destination.position.x;
        out.mY = teleport.destination.position.y;
        out.mZ = teleport.destination.position.z;
        out.mHeading = std::atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.y * q.y + q.z * q.z));
        return true;
    }
}
