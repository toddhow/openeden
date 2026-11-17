// Task 0268: where a neutral session's new game starts: a game with an opening of its own (Skyrim's cart at Helgen)
// says so and that wins over `--start`; no answer leaves `--start`. Pure; written with the code (see the task doc).
#include "apps/openmw/mwworld/neutralstart.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace MWWorld;

    TEST(NeutralNewGameStart, aGameStartWinsOverTheArgument)
    {
        EXPECT_EQ(chooseNeutralStart("WhiterunBanneredMare", "@Tamriel,22499.8,-94862.6,13905.8"),
            "@Tamriel,22499.8,-94862.6,13905.8");
    }

    TEST(NeutralNewGameStart, noGameStartLeavesTheArgumentAlone)
    {
        EXPECT_EQ(chooseNeutralStart("WhiterunBanneredMare", ""), "WhiterunBanneredMare");
        EXPECT_EQ(chooseNeutralStart("", ""), "");
    }

    TEST(NeutralNewGameStart, aGameStartWorksWithNoArgumentAtAll)
    {
        EXPECT_EQ(chooseNeutralStart("", "@Tamriel,1,2"), "@Tamriel,1,2");
    }

    TEST(NeutralNewGameStart, theGameStartTextParsesAsAnExteriorTarget)
    {
        const NeutralStartTarget target = parseNeutralStart("@Tamriel,22499.8,-94862.6,13905.8");
        EXPECT_EQ(target.mKind, NeutralStartTarget::Kind::Exterior);
        EXPECT_EQ(target.mWorld, "Tamriel");
        ASSERT_TRUE(target.mZ.has_value());
        EXPECT_NEAR(*target.mZ, 13905.8, 1e-6);
    }
}
