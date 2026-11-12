// Task 0118, stage N5-0: the aggregate check of the actor and appearance records on the player's own Skyrim SE masters.
// Prints counts only (lines starting SKYRIMSE-ACTORS), never a record name, id or path, and writes nothing. Skipped
// when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <components/bsa/compressedbsafile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::uint32_t cc(const char* s)
    {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0]))
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24);
    }

    /// Records the type of every record by its resolved id (all types, for the reference checks).
    class TypeCollector final : public Content::Visitor
    {
    public:
        TypeCollector(const FormIdResolver& resolver, std::unordered_map<FormId, std::uint32_t>& types)
            : mResolver(resolver)
            , mTypes(types)
        {
        }

        bool onRecord(const Content::RawRecord& record) override
        {
            if (record.mType == Content::fourCC("TES4"))
                return true;
            const std::optional<FormId> id = mResolver.resolve(record.mFormId);
            if (id && *id != kNullFormId)
                mTypes[*id] = record.mType;
            return true;
        }

    private:
        const FormIdResolver& mResolver;
        std::unordered_map<FormId, std::uint32_t>& mTypes;
    };

    struct RefCheck
    {
        std::uint64_t mTotal = 0;
        std::uint64_t mOk = 0;
        std::uint64_t mNull = 0;
        void count(FormId id, std::initializer_list<const char*> allowed,
            const std::unordered_map<FormId, std::uint32_t>& types)
        {
            if (id == kNullFormId)
            {
                ++mNull;
                return;
            }
            ++mTotal;
            const auto it = types.find(id);
            if (it == types.end())
                return;
            for (const char* a : allowed)
                if (it->second == cc(a))
                {
                    ++mOk;
                    return;
                }
        }
    };

    void report(const char* name, const RefCheck& c)
    {
        std::cout << "SKYRIMSE-ACTORS ref " << name << " total " << c.mTotal << " resolves-to-expected " << c.mOk
                  << " null " << c.mNull << "\n";
    }

    TEST(SkyrimActorsRealData, theFiveMastersDecodeAndResolve)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        std::vector<std::string> present;
        for (const std::string& n : names)
            if (fs::exists(data / n))
                present.push_back(n);
        if (present.size() != names.size())
            GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : present)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        ActorIndex index;
        std::unordered_map<FormId, std::uint32_t> types;
        std::map<std::uint32_t, TypeCount> counts;
        std::map<std::string, std::uint64_t> odd;
        std::uint64_t unresolved = 0, errors = 0, walkErrors = 0;
        std::uint64_t templateFlagOr = 0;
        for (const std::string& n : present)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            ActorRecords actors;
            const bool ok = parseActorRecords(*plugin, order, ActorLimits(), actors);
            EXPECT_TRUE(ok);
            if (!ok)
                ++walkErrors;
            for (const auto& [type, c] : actors.mCounts)
            {
                TypeCount& sum = counts[type];
                sum.mSeen += c.mSeen;
                sum.mParsed += c.mParsed;
                sum.mFailed += c.mFailed;
                sum.mDeleted += c.mDeleted;
            }
            for (const auto& [k, v] : actors.mOddBy)
                odd[k] += v;
            unresolved += actors.mUnresolvedFormIds;
            errors += actors.mErrors.size() + actors.mErrorsDropped;
            for (const ActorNpc& npc : actors.mNpcs)
                templateFlagOr |= npc.mTemplateFlags;
            index.add(actors);

            ParsedPlugin records;
            ParseLimits limits;
            limits.mSkipReferences = true;
            EXPECT_TRUE(parseRecords(*plugin, order, limits, records));
            index.addLeveledLists(records.mLeveledLists);

            const FormIdResolver resolver(plugin->header(), n, order);
            TypeCollector collector(resolver, types);
            EXPECT_FALSE(plugin->walk(collector).has_value());
        }

        for (const char* t : { "NPC_", "RACE", "ARMA", "ARMO", "OTFT", "HDPT", "CLAS" })
        {
            const TypeCount& c = counts[cc(t)];
            std::cout << "SKYRIMSE-ACTORS type " << t << " seen " << c.mSeen << " parsed " << c.mParsed << " failed "
                      << c.mFailed << " deleted " << c.mDeleted << "\n";
            EXPECT_EQ(c.mSeen, c.mParsed) << t;
            EXPECT_EQ(c.mFailed, 0u) << t;
        }
        for (const auto& [k, v] : odd)
            std::cout << "SKYRIMSE-ACTORS odd " << k << " " << v << "\n";
        std::cout << "SKYRIMSE-ACTORS unresolved-ids " << unresolved << " errors " << errors << " walk-errors "
                  << walkErrors << " template-flag-or 0x" << std::hex << templateFlagOr << std::dec << "\n";
        EXPECT_TRUE(odd.empty());
        EXPECT_EQ(unresolved, 0u);
        EXPECT_EQ(errors, 0u);

        // ---- references resolve to the type they are supposed to ----
        // The index holds the winning record per id, so the checks run on what the game would use.
        RefCheck race, cls, tmpl, outfit, sleepOutfit, skin, headPart, inv, raceSkin, raceHead, armaRace, armoArma,
            outfitItem, headPartTex;
        for (const FormId id : index.npcIds())
        {
            const ActorNpc& n = *index.npc(id);
            race.count(n.mRace, { "RACE" }, types);
            cls.count(n.mClass, { "CLAS" }, types);
            tmpl.count(n.mTemplate, { "NPC_", "LVLN" }, types);
            outfit.count(n.mDefaultOutfit, { "OTFT" }, types);
            sleepOutfit.count(n.mSleepOutfit, { "OTFT" }, types);
            skin.count(n.mSkin, { "ARMO" }, types);
            for (FormId h : n.mHeadParts)
                headPart.count(h, { "HDPT" }, types);
            for (const InventoryEntry& e : n.mInventory)
                inv.count(e.mItem,
                    { "ARMO", "WEAP", "MISC", "LVLI", "ALCH", "AMMO", "BOOK", "INGR", "KEYM", "SCRL", "SLGM", "LIGH",
                        "SPEL" },
                    types);
        }
        std::uint64_t racesTotal = 0, racesWithBothSkeletons = 0, racesWithHeadParts = 0;
        std::set<std::string> skeletonPaths;
        std::vector<FormId> raceIds;
        for (const auto& [id, t] : types)
            if (t == Content::fourCC("RACE"))
                raceIds.push_back(id);
        std::sort(raceIds.begin(), raceIds.end());
        for (const FormId id : raceIds)
        {
            const RaceRecord* r = index.race(id);
            if (r == nullptr)
                continue;
            ++racesTotal;
            raceSkin.count(r->mSkin, { "ARMO" }, types);
            for (const RaceHeadPart& h : r->mHeadParts)
                raceHead.count(h.mHeadPart, { "HDPT" }, types);
            if (!r->skeleton(Sex::Male).empty() && !r->skeleton(Sex::Female).empty())
                ++racesWithBothSkeletons;
            if (!r->mHeadParts.empty())
                ++racesWithHeadParts;
            for (Sex s : { Sex::Male, Sex::Female })
                if (!r->skeleton(s).empty())
                    skeletonPaths.insert(lower(r->skeleton(s)));
        }
        std::uint64_t armaTotal = 0, armaWithModel = 0;
        for (const auto& [id, t] : types)
        {
            if (t == Content::fourCC("ARMA"))
            {
                if (const ArmatureRecord* a = index.armature(id))
                {
                    ++armaTotal;
                    armaRace.count(a->mRace, { "RACE" }, types);
                    for (FormId r : a->mAdditionalRaces)
                        armaRace.count(r, { "RACE" }, types);
                    if (!a->mMaleModel.empty() || !a->mFemaleModel.empty())
                        ++armaWithModel;
                }
            }
            else if (t == Content::fourCC("ARMO"))
            {
                if (const ArmorExtra* a = index.armor(id))
                    for (FormId m : a->mArmatures)
                        armoArma.count(m, { "ARMA" }, types);
            }
            else if (t == Content::fourCC("OTFT"))
            {
                if (const OutfitRecord* o = index.outfit(id))
                    for (FormId i : o->mItems)
                        outfitItem.count(i, { "ARMO", "LVLI" }, types);
            }
            else if (t == Content::fourCC("HDPT"))
            {
                if (const HeadPartRecord* h = index.headPart(id))
                    headPartTex.count(h->mTextureSet, { "TXST" }, types);
            }
        }
        report("NPC_.RNAM->RACE", race);
        report("NPC_.CNAM->CLAS", cls);
        report("NPC_.TPLT->NPC_|LVLN", tmpl);
        report("NPC_.DOFT->OTFT", outfit);
        report("NPC_.SOFT->OTFT", sleepOutfit);
        report("NPC_.WNAM->ARMO", skin);
        report("NPC_.PNAM->HDPT", headPart);
        report("NPC_.CNTO->item", inv);
        report("RACE.WNAM->ARMO", raceSkin);
        report("RACE.HEAD->HDPT", raceHead);
        report("ARMA.RNAM|MODL->RACE", armaRace);
        report("ARMO.MODL->ARMA", armoArma);
        report("OTFT.INAM->ARMO|LVLI", outfitItem);
        report("HDPT.TNAM->TXST", headPartTex);
        for (const RefCheck* c : { &race, &cls, &tmpl, &outfit, &sleepOutfit, &skin, &headPart, &inv, &raceSkin,
                 &raceHead, &armaRace, &armoArma, &outfitItem, &headPartTex })
            EXPECT_EQ(c->mTotal, c->mOk);
        std::cout << "SKYRIMSE-ACTORS races " << racesTotal << " with-both-skeletons " << racesWithBothSkeletons
                  << " with-head-parts " << racesWithHeadParts << " distinct-skeleton-paths " << skeletonPaths.size()
                  << " armatures " << armaTotal << " with-a-model " << armaWithModel << "\n";

        // ---- template chains ----
        std::map<ActorIndex::ResolveStatus, std::uint64_t> status;
        std::map<std::uint32_t, std::uint64_t> depth;
        std::uint64_t checkedAtLevels = 0;
        for (const FormId id : index.npcIds())
            for (std::uint16_t level : { std::uint16_t(1), std::uint16_t(30), std::uint16_t(100) })
                for (std::uint32_t pick : { 0u, 1u, 5u })
                {
                    ActorIndex::Resolved out;
                    const auto s = index.resolve(id, level, pick, out);
                    ++status[s];
                    ++checkedAtLevels;
                    if (s == ActorIndex::ResolveStatus::Ok && level == 30 && pick == 0)
                        ++depth[out.mDepth];
                }
        std::cout << "SKYRIMSE-ACTORS template-resolutions " << checkedAtLevels << " ok "
                  << status[ActorIndex::ResolveStatus::Ok] << " cycle " << status[ActorIndex::ResolveStatus::Cycle]
                  << " too-deep " << status[ActorIndex::ResolveStatus::TooDeep] << " broken-link "
                  << status[ActorIndex::ResolveStatus::BrokenLink] << " unknown-npc "
                  << status[ActorIndex::ResolveStatus::UnknownNpc] << "\n";
        for (const auto& [d, c] : depth)
            std::cout << "SKYRIMSE-ACTORS template-depth " << d << " npcs " << c << "\n";
        EXPECT_EQ(status[ActorIndex::ResolveStatus::Cycle], 0u);
        EXPECT_EQ(status[ActorIndex::ResolveStatus::TooDeep], 0u);

        // ---- archives: which mesh paths exist ----
        std::set<std::string> files;
        for (const auto& item : fs::directory_iterator(data))
        {
            if (lower(item.path().extension().string()) != ".bsa")
                continue;
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(item.path());
                for (const Bsa::BSAFile::FileStruct& f : archive.getList())
                {
                    std::string name = lower(std::string(f.name()));
                    std::replace(name.begin(), name.end(), '/', '\\');
                    if (name.rfind("meshes\\", 0) == 0)
                        files.insert(std::move(name));
                }
            }
            catch (const std::exception&)
            {
            }
        }
        const auto exists = [&](std::string path) {
            path = lower(std::move(path));
            std::replace(path.begin(), path.end(), '/', '\\');
            return files.count("meshes\\" + path) != 0;
        };
        std::cout << "SKYRIMSE-ACTORS mesh-files-in-archives " << files.size() << "\n";

        std::uint64_t skeletonsExist = 0;
        for (const std::string& p : skeletonPaths)
            if (exists(p))
                ++skeletonsExist;
        std::cout << "SKYRIMSE-ACTORS skeleton-paths " << skeletonPaths.size() << " existing " << skeletonsExist
                  << "\n";

        std::uint64_t npcs = 0, npcsRaceKnown = 0, npcsSkeleton = 0, npcsSkeletonFile = 0;
        std::uint64_t withOutfit = 0, outfitKnown = 0, outfitWithModels = 0, outfitModelsTotal = 0,
                      outfitModelsExisting = 0, outfitsAllExisting = 0;
        std::uint64_t outfitArmorsTotal = 0, outfitArmorsWithAddon = 0;
        for (const FormId id : index.npcIds())
        {
            ActorIndex::Resolved res;
            if (index.resolve(id, 30, 0, res) != ActorIndex::ResolveStatus::Ok)
                continue;
            ++npcs;
            const RaceRecord* r = index.race(res.mRace);
            if (r == nullptr)
                continue;
            ++npcsRaceKnown;
            const ActorNpc* traits = index.npc(res.mTraitsFrom);
            const Sex sex = traits != nullptr ? traits->sex() : Sex::Male;
            if (!r->skeleton(sex).empty())
            {
                ++npcsSkeleton;
                if (exists(r->skeleton(sex)))
                    ++npcsSkeletonFile;
            }
            if (res.mDefaultOutfit == kNullFormId)
                continue;
            ++withOutfit;
            if (index.outfit(res.mDefaultOutfit) == nullptr)
                continue;
            ++outfitKnown;
            const auto worn = index.wornModels(res.mDefaultOutfit, res.mRace, sex, 30, 0);
            if (!worn.empty())
                ++outfitWithModels;
            std::uint64_t existing = 0;
            for (const ActorIndex::WornModel& w : worn)
            {
                ++outfitModelsTotal;
                if (exists(w.mModel))
                {
                    ++outfitModelsExisting;
                    ++existing;
                }
            }
            if (existing == worn.size() && !worn.empty())
                ++outfitsAllExisting;
            // How many of the outfit's direct ARMO items found an addon for this race.
            if (const OutfitRecord* o = index.outfit(res.mDefaultOutfit))
                for (FormId item : o->mItems)
                    if (const ArmorExtra* a = index.armor(item))
                    {
                        ++outfitArmorsTotal;
                        if (std::any_of(worn.begin(), worn.end(),
                                [&](const ActorIndex::WornModel& w) { return w.mArmor == item; }))
                            ++outfitArmorsWithAddon;
                        (void)a;
                    }
        }
        std::cout << "SKYRIMSE-ACTORS npcs-resolved " << npcs << " race-known " << npcsRaceKnown << " skeleton-set "
                  << npcsSkeleton << " skeleton-file-exists " << npcsSkeletonFile << "\n";
        std::cout << "SKYRIMSE-ACTORS default-outfits npcs-with-outfit " << withOutfit << " outfit-record-found "
                  << outfitKnown << " with-worn-models " << outfitWithModels << " worn-models " << outfitModelsTotal
                  << " model-files-existing " << outfitModelsExisting << " outfits-with-all-models-existing "
                  << outfitsAllExisting << " direct-armor-items " << outfitArmorsTotal << " with-an-addon-for-the-race "
                  << outfitArmorsWithAddon << "\n";
        EXPECT_EQ(npcsRaceKnown, npcs);
        EXPECT_EQ(npcsSkeleton, npcsRaceKnown);
        EXPECT_EQ(npcsSkeletonFile, npcsSkeleton);
        EXPECT_EQ(outfitModelsExisting, outfitModelsTotal);
    }
}
