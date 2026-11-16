// Task 0211: the data behind the story events and the radiant quests, on the player's own Skyrim SE masters. Prints
// counts and official editor ids only (lines starting SKYRIMSE-RAD). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/story.hpp>
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    class SubrecordHistogram final : public Content::Visitor
    {
    public:
        explicit SubrecordHistogram(std::uint32_t type, std::string name)
            : mType(type)
            , mName(std::move(name))
        {
        }

        bool onRecord(const Content::RawRecord& record) override
        {
            if (record.mType != mType)
                return true;
            ++mRecords;
            std::vector<std::byte> inflated;
            std::span<const std::byte> bytes = record.mData;
            if (record.isCompressed())
            {
                Content::Error error;
                if (!inflateRecord(record, mName, inflated, error))
                    return true;
                bytes = inflated;
            }
            std::set<std::string> seen;
            (void)survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                std::string name(reinterpret_cast<const char*>(&sub.mType), 4);
                ++mCount[name];
                mBytes[name] += sub.mData.size();
                if (seen.insert(name).second)
                    ++mRecordsWith[name];
            });
            return true;
        }

        std::uint32_t mType;
        std::string mName;
        std::uint64_t mRecords = 0;
        std::map<std::string, std::uint64_t> mCount, mRecordsWith, mBytes;
    };

    const char* fillName(AliasFillKind k)
    {
        static const char* names[] = { "FindMatching", "SpecificReference", "UniqueActor", "SpecificLocation",
            "ExternalAlias", "CreatedObject", "FromEvent", "NearAlias", "InLocationAlias" };
        return names[static_cast<int>(k)];
    }

    TEST(SkyrimRadiantSurvey, theQuestsEventsAndLocationsOfTheFiveMasters)
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

        QuestIndex quests;
        StoryIndex story;
        LocationIndex locations;
        std::uint64_t exteriorCells = 0, exteriorWithLocation = 0, interiorCells = 0, interiorWithLocation = 0;
        std::set<FormId> locationsOfExterior;
        struct XlcnCell
        {
            FormId mLocation, mWorld;
            std::int32_t mX, mY;
        };
        std::vector<XlcnCell> exteriorWithXlcn;
        SubrecordHistogram lctn(Content::fourCC("LCTN"), "LCTN");
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            QuestRecords qr;
            ASSERT_TRUE(parseQuestRecords(*plugin, order, QuestLimits(), qr));
            LocationRecords lr;
            ASSERT_TRUE(parseLocationRecords(*plugin, order, LocationLimits(), lr));
            ParsedPlugin pp;
            Content::Error rerr;
            ParseLimits skip;
            skip.mSkipReferences = true;
            ASSERT_TRUE(parseRecordsFile(data / n, order, skip, pp, rerr)) << rerr.mMessage;
            for (const CellRecord& c : pp.mCells)
            {
                const bool hasLocation = c.mLocation != kNullFormId && c.mLocation != kUnresolvedFormId;
                if (c.interior())
                {
                    ++interiorCells;
                    interiorWithLocation += hasLocation ? 1 : 0;
                }
                else
                {
                    ++exteriorCells;
                    exteriorWithLocation += hasLocation ? 1 : 0;
                    if (hasLocation)
                    {
                        locationsOfExterior.insert(c.mLocation);
                        if (c.mGrid)
                            exteriorWithXlcn.push_back({ c.mLocation, c.mWorld, c.mGrid->x, c.mGrid->y });
                    }
                }
            }
            (void)plugin->walk(lctn);
            const FormIdResolver resolver(plugin->header(), n, order);
            StoryRecords sr;
            ASSERT_TRUE(parseStoryRecords(*plugin, order, StoryLimits(), sr));
            story.add(std::move(sr), resolver);
            quests.add(std::move(qr), resolver);
            locations.add(std::move(lr));
        }
        std::cout << "SKYRIMSE-RAD cells interior " << interiorCells << " with XLCN " << interiorWithLocation
                  << "; exterior " << exteriorCells << " with XLCN " << exteriorWithLocation << " ("
                  << locationsOfExterior.size() << " distinct locations)\n";
        {
            // is the stored pair Y then X? count the cells whose own location (or an ancestor) lists the square both ways
            std::uint64_t listedYx = 0, listedXy = 0, listedNeither = 0;
            for (const XlcnCell& c : exteriorWithXlcn)
            {
                const FormId owner = locations.locationOfGrid(c.mWorld, c.mX, c.mY);
                const FormId swapped = locations.locationOfGrid(c.mWorld, c.mY, c.mX);
                if (owner != kNullFormId && locations.isWithin(c.mLocation, owner) || owner == c.mLocation)
                    ++listedYx;
                else if (swapped != kNullFormId && (locations.isWithin(c.mLocation, swapped) || swapped == c.mLocation))
                    ++listedXy;
                else
                    ++listedNeither;
            }
            std::cout << "SKYRIMSE-RAD LCEC order check over " << exteriorWithXlcn.size() << " cells with XLCN: Y-then-X agrees "
                      << listedYx << ", swapped agrees " << listedXy << ", neither " << listedNeither << "\n";
        }
        std::cout << "SKYRIMSE-RAD LCTN records " << lctn.mRecords << "\n";
        for (const auto& [k, v] : lctn.mRecordsWith)
            std::cout << "SKYRIMSE-RAD   LCTN " << k << " in " << v << " records, " << lctn.mCount[k] << " subrecords, "
                      << lctn.mBytes[k] << " bytes\n";

        std::map<std::string, std::uint64_t> byEvent;
        std::map<std::string, std::uint64_t> aliasKinds;
        std::map<std::string, std::uint64_t> locAliasFns;
        std::set<std::string> radiantNames;
        std::uint64_t questsWithFindLocation = 0, questsWithFindLocationAndRef = 0;
        for (const FormId id : quests.questIds())
        {
            const QuestRecord* q = quests.quest(id);
            ++byEvent[q->mEvent.empty() ? "-" : q->mEvent];
            bool findLocation = false, inLocation = false;
            for (const QuestAlias& a : q->mAliases)
            {
                ++aliasKinds[std::string(a.mIsLocation ? "LOC " : "REF ") + fillName(a.mFill)];
                if (a.mIsLocation && a.mFill == AliasFillKind::FindMatching)
                {
                    findLocation = true;
                    for (const survey::Ctda& c : a.mConditions)
                        ++locAliasFns["fn_" + std::to_string(c.mFunction)];
                }
                if (!a.mIsLocation && a.mFill == AliasFillKind::InLocationAlias)
                    inLocation = true;
            }
            questsWithFindLocation += findLocation ? 1 : 0;
            questsWithFindLocationAndRef += findLocation && inLocation ? 1 : 0;
            if (findLocation && inLocation)
                radiantNames.insert(q->mCommon.mEditorId);
            if (q->mCommon.mEditorId == "BQ01" || q->mCommon.mEditorId == "DLC1RV02")
            {
                std::cout << "SKYRIMSE-RAD ADIA sample " << q->mCommon.mEditorId << " aliases " << q->mAliases.size() << "\n";
                for (const QuestAlias& a : q->mAliases)
                {
                    std::string fns;
                    for (const survey::Ctda& c : a.mConditions)
                        fns += " fn" + std::to_string(c.mFunction) + "/run" + std::to_string(c.mRunOn);
                    std::cout << "SKYRIMSE-RAD    alias " << a.mId << " " << a.mName << " " << (a.mIsLocation ? "LOC " : "REF ")
                              << fillName(a.mFill) << " flags " << a.mFlags << fns << "\n";
                }
            }
            if (q->mEvent == "KILL")
            {
                std::cout << "SKYRIMSE-RAD KILL quest " << q->mCommon.mEditorId << " flags " << q->mFlags << " type "
                          << q->mType << " aliases " << q->mAliases.size() << " startconds "
                          << q->mStartConditions.size() << "\n";
                for (const QuestAlias& a : q->mAliases)
                {
                    std::string fns;
                    for (const survey::Ctda& c : a.mConditions)
                        fns += " fn" + std::to_string(c.mFunction) + "/run" + std::to_string(c.mRunOn);
                    std::cout << "SKYRIMSE-RAD    alias " << a.mId << " " << a.mName << " "
                              << (a.mIsLocation ? "LOC " : "REF ") << fillName(a.mFill) << " data "
                              << static_cast<char>(a.mEventData & 0xFF) << static_cast<char>((a.mEventData >> 8) & 0xFF)
                              << " flags " << a.mFlags << fns << "\n";
                }
                std::string fns;
                for (const survey::Ctda& c : q->mStartConditions)
                    fns += " fn" + std::to_string(c.mFunction) + "/run" + std::to_string(c.mRunOn) + "/ref"
                        + std::to_string(c.mReference);
                std::cout << "SKYRIMSE-RAD    start conditions:" << fns << "\n";
            }
        }
        {
            // the story nodes that start some quests worth a look: the node chain and the keyword or location it asks for
            const std::set<std::string> wanted = { "CR09", "DLC1RV02", "MGR20", "TGRGF", "WE06", "DLC2TTR1",
                "FreeformHighHrothgarA", "FreeformOldHroldanB", "DLC1RH02", "BQ01", "Favor001" };
            for (const FormId id : quests.questIds())
            {
                const QuestRecord* q = quests.quest(id);
                if (wanted.count(q->mCommon.mEditorId) == 0)
                    continue;
                std::cout << "SKYRIMSE-RAD node chain of " << q->mCommon.mEditorId << " (" << std::hex << id << std::dec << ")\n";
                for (const FormId nid : story.allNodeIds())
                {
                    const StoryNode* n = story.node(nid);
                    if (n == nullptr || n->mKind != StoryNodeKind::Quest
                        || std::find(n->mQuests.begin(), n->mQuests.end(), id) == n->mQuests.end())
                        continue;
                    for (const StoryNode* at = n; at != nullptr; at = story.node(at->mParent))
                    {
                        std::cout << "SKYRIMSE-RAD    node " << std::hex << at->mCommon.mId << std::dec << " " << at->mCommon.mEditorId
                                  << " event '" << at->mEvent << "' conds";
                        for (const survey::Ctda& c : at->mConditions)
                            std::cout << " fn" << c.mFunction << "(" << std::hex << c.mParam1 << "," << c.mParam2 << std::dec << ")";
                        std::cout << "\n";
                        if (at->mParent == kNullFormId)
                            break;
                    }
                }
            }
        }
        for (const auto& [k, v] : byEvent)
            std::cout << "SKYRIMSE-RAD quests with event " << k << ": " << v << "\n";
        for (const auto& [k, v] : aliasKinds)
            std::cout << "SKYRIMSE-RAD alias " << k << ": " << v << "\n";
        for (const auto& [k, v] : locAliasFns)
            std::cout << "SKYRIMSE-RAD location alias condition " << k << ": " << v << "\n";
        std::cout << "SKYRIMSE-RAD quests with a find-matching location alias " << questsWithFindLocation
                  << ", also an in-location reference alias " << questsWithFindLocationAndRef << "\n";
        for (const std::string& n : radiantNames)
            std::cout << "SKYRIMSE-RAD radiant-like " << n << "\n";
        EXPECT_GT(quests.questCount(), 0u);
    }
}
