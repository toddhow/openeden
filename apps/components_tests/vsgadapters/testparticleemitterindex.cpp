#include <components/vsgadapters/nif/particle.hpp>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{
    // The crash of 2026-10-31: the index was rand() / RAND_MAX * count, which equals count when rand() returns
    // RAND_MAX (1 call in 32768 on MSVC). The emitter path read one past the vector's end.
    TEST(VsgAdaptersChooseEmitterTest, largestRandomValueStaysInRange)
    {
        for (size_t count : { 1u, 2u, 3u, 7u, 64u })
            EXPECT_LT(vsgAdapters::chooseEmitter(RAND_MAX, count), count) << count;
    }

    TEST(VsgAdaptersChooseEmitterTest, everyRandomValueStaysInRangeAndSpreadsEvenly)
    {
        const size_t count = 3;
        size_t seen[count] = {};
        for (int r = 0; r <= RAND_MAX; ++r)
        {
            const size_t index = vsgAdapters::chooseEmitter(r, count);
            ASSERT_LT(index, count) << r;
            ++seen[index];
        }
        for (size_t n : seen)
            EXPECT_NEAR(static_cast<double>(n), (RAND_MAX + 1.0) / count, 1.0);
    }

    TEST(VsgAdaptersChooseEmitterTest, singleEmitterIsAlwaysFirst)
    {
        EXPECT_EQ(vsgAdapters::chooseEmitter(0, 1), 0u);
        EXPECT_EQ(vsgAdapters::chooseEmitter(RAND_MAX, 1), 0u);
    }
}
