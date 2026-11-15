// Task 0171 review fixes: the cache of the clip metas the player's swing clock reads (mwrender/playerclipmeta.hpp).
// Written first and shown red against the stub (which never answers).
#include "apps/openmw/mwrender/playerclipmeta.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <future>

namespace
{
    using MWRender::PlayerClipMetaCache;

    std::shared_ptr<const HavokPack::ClipMeta> meta()
    {
        auto m = std::make_shared<HavokPack::ClipMeta>();
        m->mDuration = 1.f;
        return m;
    }

    PlayerClipMetaCache::Future readyFuture(std::shared_ptr<const HavokPack::ClipMeta> value)
    {
        std::promise<std::shared_ptr<const HavokPack::ClipMeta>> promise;
        promise.set_value(std::move(value));
        return promise.get_future().share();
    }

    TEST(PlayerClipMeta, anUnknownClipAsksTheCallerToStartTheRead)
    {
        PlayerClipMetaCache cache;
        const auto answer = cache.lookup("1HM_AttackRight", false, 0.0);
        EXPECT_TRUE(answer.mStart);
        EXPECT_FALSE(answer.mMeta);
    }

    TEST(PlayerClipMeta, aReadThatIsNotReadyAnswersNothingAndDoesNotStartAgain)
    {
        PlayerClipMetaCache cache;
        std::promise<std::shared_ptr<const HavokPack::ClipMeta>> promise;
        ASSERT_TRUE(cache.lookup("a", false, 0.0).mStart);
        cache.begin("a", false, promise.get_future().share());
        const auto answer = cache.lookup("a", false, 0.1);
        EXPECT_FALSE(answer.mStart);
        EXPECT_FALSE(answer.mMeta);
        promise.set_value(meta());
        EXPECT_TRUE(cache.lookup("a", false, 0.2).mMeta);
    }

    TEST(PlayerClipMeta, theSameNameWithAndWithoutASexFolderAreTwoClips)
    {
        PlayerClipMetaCache cache;
        cache.begin("a", false, readyFuture(meta()));
        EXPECT_TRUE(cache.lookup("a", false, 0.0).mMeta);
        EXPECT_TRUE(cache.lookup("a", true, 0.0).mStart);
    }

    TEST(PlayerClipMeta, aKnownMetaIsAnsweredAgainWithoutAllocatingOrAskingTheFuture)
    {
        PlayerClipMetaCache cache;
        cache.begin("1HM_AttackRight", false, readyFuture(meta()));
        ASSERT_TRUE(cache.lookup("1HM_AttackRight", false, 0.0).mMeta);
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 1000; ++i)
                ASSERT_TRUE(cache.lookup("1HM_AttackRight", false, 1.0 + i).mMeta);
            allocations = counter.count();
        }
        EXPECT_EQ(allocations, 0u); // before the fix: a std::string key a call
    }

    TEST(PlayerClipMeta, aPendingReadIsAskedWithoutAllocating)
    {
        PlayerClipMetaCache cache;
        std::promise<std::shared_ptr<const HavokPack::ClipMeta>> promise;
        cache.begin("1HM_AttackRight", false, promise.get_future().share());
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 1000; ++i)
                (void)cache.lookup("1HM_AttackRight", false, 0.0);
            allocations = counter.count();
        }
        EXPECT_EQ(allocations, 0u);
        promise.set_value(nullptr);
    }

    TEST(PlayerClipMeta, aFailedReadIsNotRememberedForEverButTriedOnceMoreLater)
    {
        PlayerClipMetaCache cache;
        cache.begin("a", false, readyFuture(nullptr));
        auto first = cache.lookup("a", false, 10.0);
        EXPECT_FALSE(first.mMeta);
        EXPECT_TRUE(first.mWarn);
        EXPECT_FALSE(first.mStart); // not at once
        EXPECT_FALSE(cache.lookup("a", false, 10.5).mStart);
        const auto retry = cache.lookup("a", false, 10.0 + PlayerClipMetaCache::kRetrySeconds + 0.1);
        EXPECT_TRUE(retry.mStart); // before the fix the failure stayed for the session
        cache.begin("a", false, readyFuture(meta()));
        EXPECT_TRUE(cache.lookup("a", false, 20.0).mMeta);
    }

    TEST(PlayerClipMeta, aClipThatFailsTwiceIsGivenUpAndWarnedAboutOnce)
    {
        PlayerClipMetaCache cache;
        int warnings = 0;
        double now = 0.0;
        cache.begin("a", false, readyFuture(nullptr));
        for (int i = 0; i < 20; ++i)
        {
            const auto answer = cache.lookup("a", false, now);
            warnings += answer.mWarn ? 1 : 0;
            if (answer.mStart)
                cache.begin("a", false, readyFuture(nullptr));
            now += 1.0;
        }
        EXPECT_EQ(warnings, 1);
        EXPECT_FALSE(cache.lookup("a", false, 1000.0).mStart);
    }

    TEST(PlayerClipMeta, thereIsARoomLimit)
    {
        PlayerClipMetaCache cache;
        for (std::size_t i = 0; i < PlayerClipMetaCache::kMaxEntries; ++i)
            cache.begin("clip" + std::to_string(i), false, readyFuture(meta()));
        const auto answer = cache.lookup("another", false, 0.0);
        EXPECT_FALSE(answer.mStart);
        EXPECT_FALSE(answer.mMeta);
        EXPECT_EQ(cache.size(), PlayerClipMetaCache::kMaxEntries);
    }
}
