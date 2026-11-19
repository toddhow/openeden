// Task 0359: the registry that scrolls the texture matrices of Skyrim effect shapes (apps/openmw/mwrender/
// skyrimeffectscroll.*). Animations are built by hand; no game asset is read.

#include <apps/openmw/mwrender/skyrimeffectscroll.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using MWRender::SkyrimEffectScroll::Driver;

    std::shared_ptr<NifScene::UvAnimation> scrollV(float to = 1.f)
    {
        auto a = std::make_shared<NifScene::UvAnimation>();
        a->mOffset[1].mKeys = { { 0.f, 0.f, 0.f, 0.f }, { 4.f, to, 0.f, 0.f } };
        a->mOffset[1].mStop = 4.f;
        return a;
    }

    TEST(SkyrimEffectScroll, a_new_uniform_holds_the_matrix_at_the_current_clock_at_once)
    {
        Driver driver;
        driver.update(2.0);
        auto uniform = vsg::mat4Value::create();
        driver.add(uniform, scrollV());
        EXPECT_FLOAT_EQ(uniform->value()(3, 1), 0.5f); // V offset at 2 s of a 4 s ramp
        EXPECT_EQ(uniform->properties.dataVariance, vsg::DataVariance::DYNAMIC_DATA);
        EXPECT_EQ(driver.liveCount(), 1u);
    }

    TEST(SkyrimEffectScroll, update_moves_every_uniform_by_its_own_animation)
    {
        Driver driver;
        auto one = vsg::mat4Value::create();
        auto two = vsg::mat4Value::create();
        driver.add(one, scrollV(1.f));
        driver.add(two, scrollV(2.f));
        driver.update(1.0);
        EXPECT_FLOAT_EQ(one->value()(3, 1), 0.25f);
        EXPECT_FLOAT_EQ(two->value()(3, 1), 0.5f);
        EXPECT_DOUBLE_EQ(driver.clockSeconds(), 1.0);
    }

    TEST(SkyrimEffectScroll, a_time_that_is_negative_or_not_a_number_does_not_move_the_clock)
    {
        Driver driver;
        auto u = vsg::mat4Value::create();
        driver.add(u, scrollV());
        driver.update(1.0);
        driver.update(-5.0);
        driver.update(std::numeric_limits<double>::quiet_NaN());
        driver.update(0.0);
        EXPECT_DOUBLE_EQ(driver.clockSeconds(), 1.0);
        EXPECT_FLOAT_EQ(u->value()(3, 1), 0.25f);
    }

    TEST(SkyrimEffectScroll, a_uniform_nothing_holds_any_more_is_dropped)
    {
        Driver driver;
        {
            auto gone = vsg::mat4Value::create();
            driver.add(gone, scrollV());
            EXPECT_EQ(driver.liveCount(), 1u);
        }
        EXPECT_EQ(driver.liveCount(), 0u);
        driver.update(1.0); // must not touch the dead one
        EXPECT_EQ(driver.liveCount(), 0u);
    }

    TEST(SkyrimEffectScroll, a_missing_uniform_or_animation_is_ignored)
    {
        Driver driver;
        driver.add({}, scrollV());
        driver.add(vsg::mat4Value::create(), {});
        EXPECT_EQ(driver.liveCount(), 0u);
    }
}
