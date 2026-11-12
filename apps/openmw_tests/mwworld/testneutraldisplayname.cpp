// Task 0150: the display name of a neutral object (tooltip over Hulda reads "Hulda", not her editor id).
#include <gtest/gtest.h>

#include <map>
#include <string>

#include "apps/openmw/mwworld/neutraldisplayname.hpp"

namespace
{
    using namespace openeden::sdk::game;

    struct FakeNames final : DisplayNameSource
    {
        std::map<WorldId, std::string> mNames;
        int mAsked = 0;

        std::string displayNameOf(WorldId base) override
        {
            ++mAsked;
            const auto it = mNames.find(base);
            return it != mNames.end() ? it->second : std::string();
        }
    };

    MWWorld::NeutralBase baseOf(WorldId id, const std::string& editorId)
    {
        MWWorld::NeutralBase b;
        b.mGameId = "skyrimse";
        b.mBase = id;
        b.mEditorId = editorId;
        return b;
    }

    TEST(NeutralDisplayName, theSourcesNameWinsOverTheEditorId)
    {
        FakeNames source;
        source.mNames[10] = "Hulda";
        MWWorld::NeutralDisplayNames names;
        EXPECT_EQ(names.nameOf(baseOf(10, "BanneredMareHulda"), &source), "Hulda");
    }

    TEST(NeutralDisplayName, aBaseWithNoNameKeepsItsEditorId)
    {
        FakeNames source; // knows nothing
        MWWorld::NeutralDisplayNames names;
        EXPECT_EQ(names.nameOf(baseOf(11, "TGRStrongbox"), &source), "TGRStrongbox");
    }

    TEST(NeutralDisplayName, noSourceOrNoBaseKeepsTheEditorIdAndAsksNobody)
    {
        FakeNames source;
        source.mNames[0] = "Wrong";
        MWWorld::NeutralDisplayNames names;
        EXPECT_EQ(names.nameOf(baseOf(12, "Door01"), nullptr), "Door01");
        EXPECT_EQ(names.nameOf(baseOf(kNoWorldId, "PlacedOnly"), &source), "PlacedOnly");
        EXPECT_EQ(source.mAsked, 0);
    }

    TEST(NeutralDisplayName, anAnswerIsAskedOnceAndItsViewStaysValid)
    {
        FakeNames source;
        source.mNames[10] = "Hulda";
        MWWorld::NeutralDisplayNames names;
        const std::string_view first = names.nameOf(baseOf(10, "E"), &source);
        // Many more entries must not move the first string (a view is handed out and kept).
        for (WorldId id = 100; id < 400; ++id)
        {
            source.mNames[id] = "N" + std::to_string(id);
            (void)names.nameOf(baseOf(id, "E"), &source);
        }
        EXPECT_EQ(first, "Hulda");
        EXPECT_EQ(names.nameOf(baseOf(10, "E"), &source).data(), first.data());
        EXPECT_EQ(source.mAsked, 301);
    }

    TEST(NeutralDisplayName, twoGamesWithTheSameBaseNumberDoNotShareAnAnswer)
    {
        FakeNames source;
        source.mNames[5] = "FromSource";
        MWWorld::NeutralDisplayNames names;
        MWWorld::NeutralBase a = baseOf(5, "Ed");
        MWWorld::NeutralBase b = baseOf(5, "Ed");
        b.mGameId = "other";
        EXPECT_EQ(names.nameOf(a, &source), "FromSource");
        FakeNames none;
        EXPECT_EQ(names.nameOf(b, &none), "Ed");
    }
}
