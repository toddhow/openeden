// Task 0273b (review findings 1 and 5): the list of neutral actors removed with their cell.
#include <apps/openmw/mwphysics/neutralremovedrefs.hpp>

#include <gtest/gtest.h>

namespace
{
    using MWPhysics::NeutralRemovedRefs;

    const int a = 0;
    const int b = 0;
    const int c = 0;

    TEST(NeutralRemovedRefs, takeGivesTheNotedKeysInOrderAndEmptiesTheList)
    {
        NeutralRemovedRefs refs;
        refs.note(&a);
        refs.note(&b);
        EXPECT_EQ(refs.size(), 2u);
        const auto keys = refs.take();
        ASSERT_EQ(keys.size(), 2u);
        EXPECT_EQ(keys[0], &a);
        EXPECT_EQ(keys[1], &b);
        EXPECT_EQ(refs.size(), 0u);
        EXPECT_TRUE(refs.take().empty());
    }

    TEST(NeutralRemovedRefs, aKeyNotedTwiceIsListedOnce)
    {
        NeutralRemovedRefs refs;
        refs.note(&a);
        refs.note(&c);
        refs.note(&a);
        EXPECT_EQ(refs.take().size(), 2u);
    }

    TEST(NeutralRemovedRefs, aNullKeyIsIgnored)
    {
        NeutralRemovedRefs refs;
        refs.note(nullptr);
        EXPECT_EQ(refs.size(), 0u);
    }

    TEST(NeutralRemovedRefs, theListIsBoundedAndTheOldestAreDroppedAndCounted)
    {
        NeutralRemovedRefs refs;
        static int keys[NeutralRemovedRefs::kMax + 10];
        for (std::size_t i = 0; i < NeutralRemovedRefs::kMax + 10; ++i)
            refs.note(&keys[i]);
        EXPECT_EQ(refs.size(), NeutralRemovedRefs::kMax);
        EXPECT_EQ(refs.dropped(), 10u);
        const auto taken = refs.take();
        EXPECT_EQ(taken.front(), &keys[10]) << "the oldest ten were dropped";
        EXPECT_EQ(taken.back(), &keys[NeutralRemovedRefs::kMax + 9]);
    }
}
