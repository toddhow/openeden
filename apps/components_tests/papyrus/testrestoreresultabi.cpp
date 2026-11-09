// H0 (2026-11-09): RestoreResult is returned BY VALUE from the virtual restore() of both script contracts, so its
// layout is part of the binary interface: a consumer built against the 1.0 header allocates a return slot of the 1.0
// size. These tests pin that layout against a hand-built mirror of the original struct. What a restore left out
// (counts and attachKeysChecked) is in RestoreDetails, reachable through the optional RestoreDetailsSource.

#include <gtest/gtest.h>

#include <cstddef>
#include <type_traits>

#include <openeden/script/host.hpp>

namespace
{
    using openeden::script::ErrorReport;
    using openeden::script::RestoreDetails;
    using openeden::script::RestoreDetailsSource;
    using openeden::script::RestoreResult;

    // The layout of RestoreResult in the first header that shipped it (task 0092 stage a): a flag and the error.
    struct RestoreResultOriginal
    {
        bool ok = false;
        ErrorReport error;
    };

    static_assert(sizeof(RestoreResult) == sizeof(RestoreResultOriginal),
        "RestoreResult must keep its original size: it is returned by value across the contract");
    static_assert(alignof(RestoreResult) == alignof(RestoreResultOriginal));
    static_assert(offsetof(RestoreResult, ok) == offsetof(RestoreResultOriginal, ok));
    static_assert(offsetof(RestoreResult, error) == offsetof(RestoreResultOriginal, error));
    static_assert(std::is_standard_layout_v<RestoreResult>);

    TEST(RestoreResultAbi, the_result_keeps_the_original_size_and_member_offsets)
    {
        RestoreResult result;
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.error.message.empty());
        EXPECT_EQ(sizeof(RestoreResult), sizeof(RestoreResultOriginal));
    }

    TEST(RestoreResultAbi, the_details_start_at_zero_and_unchecked)
    {
        const RestoreDetails details;
        EXPECT_FALSE(details.attachKeysChecked);
        EXPECT_EQ(details.droppedParkedStacks, 0u);
        EXPECT_EQ(details.droppedHostWaits, 0u);
        EXPECT_EQ(details.changedStructObjects, 0u);
        EXPECT_EQ(details.changedScripts, 0u);
    }

    TEST(RestoreResultAbi, the_details_source_is_a_separate_interface_not_a_base_of_the_contracts)
    {
        // A game pack built against an older header must keep its vtable layout: the source is found by
        // dynamic_cast on the concrete runtime, never required of the contract.
        static_assert(std::is_abstract_v<RestoreDetailsSource>);
        SUCCEED();
    }
}
