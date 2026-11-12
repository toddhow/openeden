// Task 0135 stage S5: the aggregate check of the PACK records and of the packages the Bannered Mare's people would run,
// on the player's own Skyrim SE masters. Prints counts and official editor ids only (lines starting SKYRIMSE-PACKAGES),
// never a text, a path or a mod name, and writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    const char* behaviorName(PackageBehavior b)
    {
        switch (b)
        {
            case PackageBehavior::Stand:
                return "Stand";
            case PackageBehavior::Travel:
                return "Travel";
            case PackageBehavior::Sandbox:
                return "Sandbox";
            case PackageBehavior::Wander:
                return "Wander";
            case PackageBehavior::Sleep:
                return "Sleep";
            case PackageBehavior::Eat:
                return "Eat";
            case PackageBehavior::Unsupported:
                return "Unsupported";
        }
        return "?";
    }

    class RealSource final : public PackageSource
    {
    public:
        RealSource(const ActorIndex& actors, const DialogueIndex& dialogue, const QuestIndex& quests,
            const PackageIndex& packages)
            : mActors(actors)
            , mDialogue(dialogue)
            , mPackages(packages)
            , mState(QuestState::initial(quests))
            , mWorld(actors, dialogue)
        {
            mWorld.setQuests(&quests, &mState);
        }
        bool ready() const override { return true; }
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override
        {
            out.clear();
            ActorIndex::Resolved resolved;
            if (mActors.resolve(base, 1, 0, resolved) != ActorIndex::ResolveStatus::Ok)
                return false;
            for (const FormId id : resolved.mPackages)
                if (const PackageRecord* p = mPackages.package(id))
                    out.push_back(p);
            if (const ActorNpc* holder = mActors.npc(resolved.mPackagesFrom))
                if (const FormListRecord* flst = mDialogue.formList(holder->mDefaultPackageList))
                    for (const FormId id : flst->mEntries)
                        if (const PackageRecord* p = mPackages.package(id))
                            out.push_back(p);
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord& p) const override { return mPackages.behaviorOf(p); }
        bool conditionsPass(const PackageRecord& p, FormId base, FormId reference) override
        {
            if (!p.mConditionsOk)
                return false;
            DialogueSpeaker speaker;
            speaker.mBase = base;
            speaker.mReference = reference;
            mWorld.setSpeaker(speaker);
            mWorld.setPlugin(&mPackages.resolverOf(p));
            return mEvaluator.evaluate(p.mConditions, mWorld);
        }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        conditions::Evaluator mEvaluator;

    private:
        const ActorIndex& mActors;
        const DialogueIndex& mDialogue;
        const PackageIndex& mPackages;
        QuestState mState;
        SkyrimConditionWorld mWorld;
    };

    TEST(SkyrimPackagesRealData, theFiveMastersDecodeAndTheMaresPeopleGetPackages)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        ActorIndex actors;
        DialogueIndex dialogue;
        QuestIndex quests;
        PackageIndex packages;
        std::map<std::string, FormId> npcByEditorId;
        std::uint64_t seen = 0, parsed = 0, failed = 0, unresolved = 0, odd = 0, errors = 0, badConditions = 0,
                      withTemplate = 0, withLocation = 0, scheduled = 0, wrongSizeLocations = 0;
        std::map<std::string, std::uint64_t> oddBy;
        std::map<int, std::uint64_t> byType;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            ActorRecords ar;
            ASSERT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), ar));
            QuestRecords qr;
            ASSERT_TRUE(parseQuestRecords(*plugin, order, QuestLimits(), qr));
            PackageRecords pr;
            ASSERT_TRUE(parsePackageRecords(*plugin, order, PackageLimits(), pr));
            ParsedPlugin pp;
            Content::Error rerr;
            ParseLimits skip;
            skip.mSkipReferences = true;
            ASSERT_TRUE(parseRecordsFile(data / n, order, skip, pp, rerr)) << rerr.mMessage;
            for (const ActorNpc& npc : ar.mNpcs)
                if (!npc.mCommon.mEditorId.empty())
                    npcByEditorId[npc.mCommon.mEditorId] = npc.mCommon.mId;
            actors.add(ar);
            actors.addLeveledLists(pp.mLeveledLists);
            dialogue.add(std::move(dr), FormIdResolver(plugin->header(), n, order));
            quests.add(std::move(qr), FormIdResolver(plugin->header(), n, order));

            const TypeCount& c = pr.mCounts[Content::fourCC("PACK")];
            seen += c.mSeen;
            parsed += c.mParsed;
            failed += c.mFailed;
            unresolved += pr.mUnresolvedFormIds;
            odd += pr.mOddSubrecords;
            errors += pr.mErrors.size() + pr.mErrorsDropped;
            for (const auto& [k, v] : pr.mOddBy)
                oddBy[k] += v;
            for (const PackageRecord& p : pr.mPackages)
            {
                ++byType[p.mType];
                badConditions += p.mConditionsOk ? 0 : 1;
                withTemplate += p.mTemplate != kNullFormId ? 1 : 0;
                withLocation += p.mLocations.empty() ? 0 : 1;
                scheduled += p.mSchedule.mHour != -1 ? 1 : 0;
            }
            packages.add(std::move(pr), FormIdResolver(plugin->header(), n, order));
        }
        (void)wrongSizeLocations;
        std::cout << "SKYRIMSE-PACKAGES seen " << seen << " parsed " << parsed << " failed " << failed
                  << " unresolved-ids " << unresolved << " odd-subrecords " << odd << " errors " << errors
                  << " merged " << packages.packageCount() << std::endl;
        for (const auto& [k, v] : oddBy)
            std::cout << "SKYRIMSE-PACKAGES odd " << k << " " << v << std::endl;
        for (const auto& [t, v] : byType)
            std::cout << "SKYRIMSE-PACKAGES type " << t << " " << v << std::endl;
        std::cout << "SKYRIMSE-PACKAGES bad-condition-lists " << badConditions << " with-template " << withTemplate
                  << " with-location " << withLocation << " scheduled " << scheduled << std::endl;
        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(odd, 0u);
        EXPECT_EQ(errors, 0u);
        EXPECT_EQ(unresolved, 0u);
        EXPECT_GT(parsed, 7000u);

        // The behaviour of every package (a count by kind).
        std::map<std::string, std::uint64_t> byBehavior;
        for (const FormId id : packages.packageIds())
            ++byBehavior[behaviorName(packages.behaviorOf(*packages.package(id)))];
        for (const auto& [k, v] : byBehavior)
            std::cout << "SKYRIMSE-PACKAGES behavior " << k << " " << v << std::endl;

        // The Bannered Mare's people: the first package in force at noon and at midnight.
        RealSource source(actors, dialogue, quests, packages);
        const std::vector<std::string> mare{ "Hulda", "Mikael", "Brenuin", "Sinmir", "Saadia", "BYOHUrchin_Lucia" };
        for (const float hour : { 12.f, 0.f, 20.f })
        {
            std::map<std::string, std::uint64_t> firstKinds;
            for (const std::string& name : mare)
            {
                const auto found = npcByEditorId.find(name);
                ASSERT_NE(found, npcByEditorId.end()) << name;
                std::vector<const PackageRecord*> list;
                ASSERT_TRUE(source.packagesFor(found->second, list)) << name;
                const PackageRecord* first = selectPackage(list, hour, 0, true, source, found->second, 0);
                const std::string kind = first == nullptr ? "none" : behaviorName(packages.behaviorOf(*first));
                ++firstKinds[kind];
                std::cout << "SKYRIMSE-PACKAGES mare hour " << hour << " " << name << " packages " << list.size()
                          << " first " << (first == nullptr ? "-" : first->mCommon.mEditorId) << " kind " << kind
                          << std::endl;
            }
            for (const auto& [k, v] : firstKinds)
                std::cout << "SKYRIMSE-PACKAGES mare-aggregate hour " << hour << " " << k << " " << v << std::endl;
        }
    }
}
