// H0 (2026-11-09): the ObScript runtime offers the optional RestoreDetailsSource, like the Papyrus one. ObScript has
// no attach keys, no parked stacks and no structs, so a restore reports zero counts and "keys checked" (there is
// nothing to check). The shared RestoreResult keeps its original layout (see papyrus/testrestoreresultabi.cpp).

#include <gtest/gtest.h>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::RestoreDetails;
    using openeden::script::RestoreDetailsSource;
    using openeden::script::SaveBlob;

    TEST(ObScriptRestoreDetails, the_runtime_offers_the_details_source)
    {
        RuntimeFixture fixture;
        EXPECT_NE(dynamic_cast<const RestoreDetailsSource*>(fixture.mRuntime.get()), nullptr);
    }

    TEST(ObScriptRestoreDetails, a_good_restore_reports_nothing_dropped_and_nothing_to_check)
    {
        RuntimeFixture first;
        const SaveBlob blob = first.mRuntime->serialise();
        RuntimeFixture second;
        ASSERT_TRUE(second.mRuntime->restore(blob).ok);
        const auto* source = dynamic_cast<const RestoreDetailsSource*>(second.mRuntime.get());
        ASSERT_NE(source, nullptr);
        const RestoreDetails& details = source->lastRestoreDetails();
        EXPECT_TRUE(details.attachKeysChecked);
        EXPECT_EQ(details.droppedParkedStacks, 0u);
        EXPECT_EQ(details.droppedHostWaits, 0u);
        EXPECT_EQ(details.changedStructObjects, 0u);
        EXPECT_EQ(details.changedScripts, 0u);
    }

    TEST(ObScriptRestoreDetails, a_refused_blob_leaves_the_details_at_their_start_values)
    {
        RuntimeFixture fixture;
        SaveBlob garbage;
        garbage.formatVersion = 99;
        EXPECT_FALSE(fixture.mRuntime->restore(garbage).ok);
        const auto* source = dynamic_cast<const RestoreDetailsSource*>(fixture.mRuntime.get());
        ASSERT_NE(source, nullptr);
        EXPECT_FALSE(source->lastRestoreDetails().attachKeysChecked);
    }
}
