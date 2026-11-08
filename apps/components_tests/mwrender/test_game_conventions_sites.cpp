// Task 0085, move 1: the CreativeMotion pose driver asks the game's conventions for its length unit and root bone
// names (apps/openmw/mwrender/creativemotiondriver.cpp). With the defaults it behaves as before (the characterization
// is in test_creativemotion_runtime.cpp); with another game's values the result changes. Written before the sites
// moved.

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/creativemotiondriver.hpp>

#include <components/animation/bones.hpp>
#include <components/animation/channel.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/interpolate.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <memory>
#include <string>

namespace
{
    using MWRender::CreativeMotionActiveSource;
    using MWRender::CreativeMotionPoseDriver;

    struct ConventionsGuard
    {
        ConventionsGuard() { Misc::resetGameConventions(); }
        ~ConventionsGuard() { Misc::resetGameConventions(); }
    };

    std::string toLower(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    }

    // Two bones under a root group: "Alpha" and its child "Beta".
    struct TwoBones
    {
        vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
        std::unique_ptr<Anim::Bones> bones;

        TwoBones()
        {
            auto alpha = Anim::Transform::create();
            vsgUtil::setName(*alpha, "Alpha");
            alpha->translation = vsg::vec3(0.f, 0.f, 0.f);
            root->addChild(alpha);
            auto beta = Anim::Transform::create();
            vsgUtil::setName(*beta, "Beta");
            beta->translation = vsg::vec3(0.f, -10.f, 20.f);
            alpha->addChild(beta);
            bones = std::make_unique<Anim::Bones>(toLower, *root, std::map<std::string, uint32_t>{});
        }
    };

    vsg::ref_ptr<Anim::Linear<vsg::vec3>> constant(const vsg::vec3& value)
    {
        auto channel = Anim::make_channel<Anim::Linear<vsg::vec3>>();
        channel->keys = { { 0.f, value } };
        return channel;
    }

    // Both bones keyed: Alpha at (10, 20, 30), Beta at (1, 2, 3).
    struct TwoBoneSource
    {
        vsg::ref_ptr<Anim::ControllerMap> map{ new Anim::ControllerMap };
        TwoBoneSource()
        {
            map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
            add("Alpha", vsg::vec3(10.f, 20.f, 30.f));
            add("Beta", vsg::vec3(1.f, 2.f, 3.f));
        }
        void add(const std::string& name, const vsg::vec3& value)
        {
            vsg::ref_ptr<Anim::TransformController> controller(new Anim::TransformController);
            controller->translate = constant(value);
            map->map[name] = controller;
        }
    };

    vsg::vec3 translationAfterUpdate(CreativeMotionPoseDriver& driver, const TwoBoneSource& source, const char* bone)
    {
        std::array<CreativeMotionActiveSource, MWRender::sNumBlendMasks> active{};
        active[0] = CreativeMotionActiveSource{ source.map.get(), "walkforward", 0.f };
        driver.update(1.f / 60.f, active, 0.f);
        return driver.getBoneTranslation(driver.getBoneIndex(bone));
    }
}

TEST(GameConventionsSites, the_default_root_bone_is_not_alpha_so_nothing_is_zeroed)
{
    const ConventionsGuard guard;
    TwoBones rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const TwoBoneSource source;
    const vsg::vec3 t = translationAfterUpdate(driver, source, "alpha");
    EXPECT_NEAR(t.x, 10.f, 1e-3f);
    EXPECT_NEAR(t.y, 20.f, 1e-3f);
    EXPECT_NEAR(t.z, 30.f, 1e-3f);
}

TEST(GameConventionsSites, another_games_root_bone_becomes_the_accumulation_root)
{
    const ConventionsGuard guard;
    Misc::GameConventions other;
    other.mRootBone = "alpha";
    Misc::setGameConventions(other);
    TwoBones rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const TwoBoneSource source;
    const vsg::vec3 t = translationAfterUpdate(driver, source, "alpha");
    // The default accumulation (x and y) is taken out of the pose of the root bone.
    EXPECT_NEAR(t.x, 0.f, 1e-3f);
    EXPECT_NEAR(t.y, 0.f, 1e-3f);
    EXPECT_NEAR(t.z, 30.f, 1e-3f);
}

TEST(GameConventionsSites, another_games_fallback_root_bone_is_used_when_the_root_is_missing)
{
    const ConventionsGuard guard;
    Misc::GameConventions other;
    other.mRootBoneFallback = "beta";
    Misc::setGameConventions(other);
    TwoBones rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const TwoBoneSource source;
    const vsg::vec3 t = translationAfterUpdate(driver, source, "beta");
    EXPECT_NEAR(t.x, 0.f, 1e-3f);
    EXPECT_NEAR(t.y, 0.f, 1e-3f);
    EXPECT_NEAR(t.z, 3.f, 1e-3f);
}

TEST(GameConventionsSites, the_rest_pose_is_in_metres_of_the_games_unit)
{
    const ConventionsGuard guard;
    {
        TwoBones rig;
        CreativeMotionPoseDriver driver(*rig.bones);
        ASSERT_TRUE(driver.isValid());
        const auto& beta = driver.getSkeleton().bones[driver.getBoneIndex("beta")];
        EXPECT_NEAR(beta.localRest.translation.z, 20.0 / Constants::UnitsPerMeter, 1e-6);
    }
    Misc::GameConventions other;
    other.mUnitsPerMeter = 100.0f;
    Misc::setGameConventions(other);
    TwoBones rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const auto& beta = driver.getSkeleton().bones[driver.getBoneIndex("beta")];
    EXPECT_NEAR(beta.localRest.translation.z, 0.2, 1e-6);
    EXPECT_NEAR(beta.localRest.translation.y, -0.1, 1e-6);
}

TEST(GameConventionsSites, the_pose_comparison_is_in_metres_of_the_games_unit)
{
    const ConventionsGuard guard;
    Misc::GameConventions other;
    other.mUnitsPerMeter = 100.0f;
    Misc::setGameConventions(other);
    TwoBones rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    TwoBoneSource source;
    source.add("Beta", vsg::vec3(0.f, -10.f, 120.f)); // 100 units above its rest
    std::array<CreativeMotionActiveSource, MWRender::sNumBlendMasks> active{};
    active[0] = CreativeMotionActiveSource{ source.map.get(), "idle", 0.f };
    driver.update(1.f / 60.f, active, 0.f);
    float maxTrans = -1.f, maxRot = -1.f;
    std::string bone;
    ASSERT_TRUE(driver.compareWithCurrentPose(maxTrans, maxRot, bone));
    // Alpha is also keyed (10, 20, 30 from rest 0): 37.4 units; Beta is 100 units off, which is the larger. At 100
    // units per metre that is 1 m (it would be 100 / 69.99 = 1.43 m with Morrowind's unit).
    EXPECT_NEAR(maxTrans, 1.0f, 1e-3f);
    EXPECT_EQ(bone, "beta");
}
