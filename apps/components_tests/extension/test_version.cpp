#include <openeden/version.hpp>

#include <gtest/gtest.h>

namespace
{
    using openeden::sdk::isCompatible;
    using openeden::sdk::Version;

    TEST(VersionTest, MajorAndMinorDefaultToZero)
    {
        const Version v;
        EXPECT_EQ(v.major, 0);
        EXPECT_EQ(v.minor, 0);
    }

    TEST(VersionTest, EqualityAndInequality)
    {
        const Version v1{ 1, 0 };
        const Version v2{ 1, 0 };
        const Version v3{ 1, 1 };
        const Version v4{ 2, 0 };

        // Equality
        EXPECT_TRUE(v1 == v2);
        EXPECT_FALSE(v1 == v3);
        EXPECT_FALSE(v1 == v4);
        EXPECT_EQ(v1, v2);

        // Inequality
        EXPECT_FALSE(v1 != v2);
        EXPECT_TRUE(v1 != v3);
        EXPECT_TRUE(v1 != v4);
        EXPECT_NE(v1, v3);
        EXPECT_NE(v1, v4);
    }

    TEST(VersionTest, OrderingOperators)
    {
        const Version v10{ 1, 0 };
        const Version v11{ 1, 1 };
        const Version v12{ 1, 2 };
        const Version v20{ 2, 0 };

        // Less than (<)
        EXPECT_TRUE(v10 < v11);
        EXPECT_TRUE(v11 < v12);
        EXPECT_TRUE(v12 < v20);
        EXPECT_FALSE(v11 < v10);
        EXPECT_FALSE(v10 < v10);

        // Less than or equal (<=)
        EXPECT_TRUE(v10 <= v11);
        EXPECT_TRUE(v10 <= v10);
        EXPECT_TRUE(v12 <= v20);
        EXPECT_FALSE(v11 <= v10);

        // Greater than (>)
        EXPECT_TRUE(v11 > v10);
        EXPECT_TRUE(v20 > v12);
        EXPECT_FALSE(v10 > v11);
        EXPECT_FALSE(v10 > v10);

        // Greater than or equal (>=)
        EXPECT_TRUE(v11 >= v10);
        EXPECT_TRUE(v10 >= v10);
        EXPECT_TRUE(v20 >= v12);
        EXPECT_FALSE(v10 >= v11);
    }

    TEST(VersionTest, CompatibilityMatchingMajorAndMinorGreaterOrEqual)
    {
        // provided.major == required.major && provided.minor >= required.minor -> true
        const Version required{ 1, 0 };
        const Version providedExact{ 1, 0 };
        const Version providedNewerMinor{ 1, 2 };

        EXPECT_TRUE(isCompatible(required, providedExact));
        EXPECT_TRUE(isCompatible(required, providedNewerMinor));

        const Version req13{ 1, 3 };
        const Version prov15{ 1, 5 };
        EXPECT_TRUE(isCompatible(req13, prov15));
    }

    TEST(VersionTest, IncompatibilityDifferentMajorVersions)
    {
        // Different major versions must return false
        const Version reqV1{ 1, 0 };
        const Version provV2{ 2, 0 };
        const Version provV0{ 0, 1 };

        EXPECT_FALSE(isCompatible(reqV1, provV2));
        EXPECT_FALSE(isCompatible(provV2, reqV1));
        EXPECT_FALSE(isCompatible(reqV1, provV0));

        const Version req15{ 1, 5 };
        const Version prov25{ 2, 5 };
        EXPECT_FALSE(isCompatible(req15, prov25));
    }

    TEST(VersionTest, IncompatibilityProvidedMinorLessThanRequired)
    {
        // provided.minor < required.minor must return false
        const Version required{ 1, 3 };
        const Version providedOlderMinor{ 1, 2 };
        const Version providedZeroMinor{ 1, 0 };

        EXPECT_FALSE(isCompatible(required, providedOlderMinor));
        EXPECT_FALSE(isCompatible(required, providedZeroMinor));
    }

    TEST(VersionTest, SdkVersionConstantIsDefined)
    {
        constexpr Version sdkVer = openeden::sdk::kSdkVersion;
        EXPECT_GE(sdkVer.major, 0);
        EXPECT_GE(sdkVer.minor, 0);
        EXPECT_TRUE(isCompatible(sdkVer, sdkVer));
    }

    TEST(VersionTest, SdkVersionIsFourPointZero)
    {
        // Decision 0003 removed API (2.0); task 0031 stage C removed the runtime editor hooks (3.0). Both are major
        // version changes (extension doc 4.2), so every older major is refused.
        static_assert(openeden::sdk::kSdkVersion == Version{ 4, 0 });
        EXPECT_EQ(openeden::sdk::kSdkVersion, (Version{ 4, 0 }));
        EXPECT_FALSE(isCompatible(Version{ 1, 0 }, openeden::sdk::kSdkVersion));
        EXPECT_FALSE(isCompatible(Version{ 2, 0 }, openeden::sdk::kSdkVersion));
    }
}
