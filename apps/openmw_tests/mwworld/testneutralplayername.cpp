// Task 0228: the core's decision to write a game pack's player name into the player record: only when the pack reports a
// new revision, never for revision 0 or an empty name.
#include "apps/openmw/mwworld/neutralplayername.hpp"

#include <gtest/gtest.h>

namespace
{
    using MWWorld::NeutralPlayerNameSync;
    using openeden::sdk::game::PlayerIdentitySource;
    using openeden::sdk::game::PlayerIdentityState;

    class FakeSource final : public PlayerIdentitySource
    {
    public:
        bool playerIdentity(PlayerIdentityState& out) override
        {
            ++mAsked;
            out = mState;
            return mOn;
        }
        PlayerIdentityState mState;
        bool mOn = true;
        int mAsked = 0;
    };

    TEST(NeutralPlayerNameSync, nothingIsWrittenWhileThePackHasNoName)
    {
        FakeSource source;
        NeutralPlayerNameSync sync;
        std::string name;
        EXPECT_FALSE(sync.poll(source, name));
        source.mState.name = "Ria"; // a name with revision 0 is no name
        EXPECT_FALSE(sync.poll(source, name));
        source.mOn = false;
        source.mState.revision = 3;
        EXPECT_FALSE(sync.poll(source, name));
        EXPECT_TRUE(name.empty());
    }

    TEST(NeutralPlayerNameSync, aNewRevisionIsWrittenOnceAndAnEmptyNameNever)
    {
        FakeSource source;
        NeutralPlayerNameSync sync;
        std::string name;
        source.mState = { 1, "Ria" };
        ASSERT_TRUE(sync.poll(source, name));
        EXPECT_EQ(name, "Ria");
        EXPECT_FALSE(sync.poll(source, name)) << "the same revision again";
        source.mState = { 2, "Bo" };
        ASSERT_TRUE(sync.poll(source, name));
        EXPECT_EQ(name, "Bo");
        source.mState = { 3, "" };
        EXPECT_FALSE(sync.poll(source, name));
    }
}
