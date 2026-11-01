// Tests of the CreativeMotion pose driver on vsgopenmw's animation system (task 0011, rebuilt from task 0007's tests).
// Written from the driver's header and task 0011's specification only, on a synthetic humanoid of Anim::Transform
// nodes and synthetic Anim::ControllerMap sources.

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
#include <components/settings/values.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using MWRender::CreativeMotionActiveSource;
    using MWRender::CreativeMotionPoseDriver;
    using ActiveArray = std::array<CreativeMotionActiveSource, MWRender::sNumBlendMasks>;

    constexpr float sMetreTol = 1e-5f;
    constexpr float sUnitTol = 1e-3f;
    constexpr float sQuatTol = 1e-5f;
    constexpr float sPi = 3.14159265358979f;

    std::string toLower(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    }

    // The bone groups MWAnim::Play uses: children inherit their nearest grouped ancestor's group, all else is 0.
    const std::map<std::string, uint32_t> sGroups
        = { { "bip01 spine1", 1 }, { "bip01 l clavicle", 2 }, { "bip01 r clavicle", 3 } };

    vsg::quat axisAngle(float radians, const vsg::vec3& axis)
    {
        return vsg::quat(radians, axis);
    }

    float quatDot(const vsg::quat& a, const vsg::quat& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    // Quaternions q and -q are the same rotation; compare after aligning their signs.
    void expectQuatNear(const vsg::quat& actual, const vsg::quat& expected, float tol)
    {
        const float sign = quatDot(actual, expected) < 0.f ? -1.f : 1.f;
        EXPECT_NEAR(actual.x * sign, expected.x, tol);
        EXPECT_NEAR(actual.y * sign, expected.y, tol);
        EXPECT_NEAR(actual.z * sign, expected.z, tol);
        EXPECT_NEAR(actual.w * sign, expected.w, tol);
    }

    void expectVec3Near(const vsg::vec3& actual, const vsg::vec3& expected, float tol)
    {
        EXPECT_NEAR(actual.x, expected.x, tol);
        EXPECT_NEAR(actual.y, expected.y, tol);
        EXPECT_NEAR(actual.z, expected.z, tol);
    }

    float distance(const vsg::vec3& a, const vsg::vec3& b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Angle between two rotations in radians.
    float quatAngle(const vsg::quat& a, const vsg::quat& b)
    {
        const float d = std::min(1.f, std::abs(quatDot(a, b)));
        return 2.f * std::acos(d);
    }

    // A synthetic scene graph of named Anim::Transform nodes and the detached Anim::Bones copied from it.
    struct Rig
    {
        vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
        std::unique_ptr<Anim::Bones> bones;

        vsg::ref_ptr<Anim::Transform> add(vsg::Group& parent, const std::string& name, const vsg::vec3& translation,
            const vsg::quat& rotation = vsg::quat(0.f, 0.f, 0.f, 1.f))
        {
            auto node = Anim::Transform::create();
            if (!name.empty())
                vsgUtil::setName(*node, name);
            node->translation = translation;
            node->setAttitude(rotation);
            parent.addChild(node);
            return node;
        }

        void finish() { bones = std::make_unique<Anim::Bones>(toLower, *root, sGroups); }

        // The bones' own (copied) transform of a bone, which the driver reads and writes.
        Anim::Transform* node(const std::string& normalizedName)
        {
            Anim::Bone* bone = bones->search(normalizedName);
            return bone ? bone->node() : nullptr;
        }
    };

    // Rest translations in engine units, as in the task 0007 tests.
    const vsg::vec3 sPelvisRest{ 0.f, 0.f, 66.5f };
    const vsg::vec3 sSpineRest{ 0.f, 0.f, 8.4f };
    const vsg::vec3 sNeckRest{ 0.f, 0.f, 15.4f };
    const vsg::vec3 sHeadRest{ 0.f, 0.f, 4.9f };
    const vsg::vec3 sLClavicleRest{ -5.f, 0.f, 12.f };
    const vsg::vec3 sLUpperArmRest{ -14.f, 0.f, 0.f };
    const vsg::vec3 sRClavicleRest{ 5.f, 0.f, 12.f };
    const vsg::vec3 sRUpperArmRest{ 14.f, 0.f, 0.f };
    const vsg::vec3 sLThighRest{ -6.3f, 0.f, -1.f };
    const vsg::vec3 sRThighRest{ 6.3f, 0.f, -1.f };
    const vsg::quat sHeadRestRotation = axisAngle(0.5f, vsg::vec3(1.f, 0.f, 0.f));
    const vsg::quat sLUpperArmRestRotation = axisAngle(-0.3f, vsg::vec3(0.f, 1.f, 0.f));

    // Bip01 -> Pelvis -> {Spine1 -> {Neck -> Head, L Clavicle -> L UpperArm, R Clavicle -> R UpperArm}, L Thigh,
    // R Thigh}. Names keep Morrowind's mixed case; Anim::Bones normalizes them to lower case.
    struct Humanoid : Rig
    {
        vsg::ref_ptr<Anim::Transform> bip01, pelvis, spine1, neck, head, lClavicle, lUpperArm, rClavicle, rUpperArm,
            lThigh, rThigh;

        explicit Humanoid(bool finishNow = true)
        {
            bip01 = add(*root, "Bip01", vsg::vec3(0.f, 0.f, 0.f));
            pelvis = add(*bip01, "Bip01 Pelvis", sPelvisRest);
            // Children added out of name order, so the skeleton's order cannot come from traversal order.
            rThigh = add(*pelvis, "Bip01 R Thigh", sRThighRest);
            spine1 = add(*pelvis, "Bip01 Spine1", sSpineRest);
            lThigh = add(*pelvis, "Bip01 L Thigh", sLThighRest);
            rClavicle = add(*spine1, "Bip01 R Clavicle", sRClavicleRest);
            rUpperArm = add(*rClavicle, "Bip01 R UpperArm", sRUpperArmRest);
            neck = add(*spine1, "Bip01 Neck", sNeckRest);
            head = add(*neck, "Bip01 Head", sHeadRest, sHeadRestRotation);
            lClavicle = add(*spine1, "Bip01 L Clavicle", sLClavicleRest);
            lUpperArm = add(*lClavicle, "Bip01 L UpperArm", sLUpperArmRest, sLUpperArmRestRotation);
            if (finishNow)
                finish();
        }
    };

    // Channel builders.
    template <class T>
    vsg::ref_ptr<Anim::Linear<T>> linear(const std::map<float, T>& keys)
    {
        auto channel = Anim::make_channel<Anim::Linear<T>>();
        channel->keys = keys;
        return channel;
    }

    template <class T>
    vsg::ref_ptr<Anim::Linear<T>> constant(const T& value)
    {
        return linear<T>({ { 0.f, value } });
    }

    template <class T>
    vsg::ref_ptr<Anim::Flip<T>> flip(const std::map<float, T>& keys)
    {
        auto channel = Anim::make_channel<Anim::Flip<T>>();
        channel->keys = keys;
        return channel;
    }

    // A channel type the driver cannot know: a smooth curve with no keys.
    class WaveChannel : public Anim::Channel<vsg::vec3>
    {
    public:
        vsg::vec3 value(float time) const override
        {
            return vsg::vec3(10.f * std::sin(2.f * sPi * time), 5.f * std::cos(2.f * sPi * time), 3.f * time);
        }
    };

    // An animation source (one .kf file) built from controllers keyed by raw bone name.
    struct Source
    {
        vsg::ref_ptr<Anim::ControllerMap> map{ new Anim::ControllerMap };

        Source() { map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags); }

        void add(const std::string& boneName, Anim::channel_ptr<vsg::vec3> translate,
            Anim::channel_ptr<vsg::quat> rotate = {}, Anim::channel_ptr<float> scale = {})
        {
            vsg::ref_ptr<Anim::TransformController> controller(new Anim::TransformController);
            controller->translate = translate;
            controller->rotate = rotate;
            controller->scale = scale;
            map->map[boneName] = controller;
        }

        void tag(float time, std::string text) { map->tags->emplace(time, std::move(text)); }

        const Anim::ControllerMap* get() const { return map.get(); }
        const Anim::ControllerMap& ref() const { return *map; }
    };

    ActiveArray noneActive()
    {
        return ActiveArray{};
    }

    ActiveArray activeIn(size_t group, const Source& source, std::string_view groupName, float time)
    {
        ActiveArray active{};
        active[group] = CreativeMotionActiveSource{ source.get(), groupName, time };
        return active;
    }

    int index(const CreativeMotionPoseDriver& driver, std::string_view name)
    {
        const int i = driver.getBoneIndex(name);
        EXPECT_GE(i, 0) << name;
        return i;
    }

    vsg::vec3 restTranslationMetres(const opas::rig::Bone& bone)
    {
        return vsg::vec3(static_cast<float>(bone.localRest.translation.x),
            static_cast<float>(bone.localRest.translation.y), static_cast<float>(bone.localRest.translation.z));
    }

    vsg::quat restRotation(const opas::rig::Bone& bone)
    {
        return vsg::quat(static_cast<float>(bone.localRest.rotation.x), static_cast<float>(bone.localRest.rotation.y),
            static_cast<float>(bone.localRest.rotation.z), static_cast<float>(bone.localRest.rotation.w));
    }

    void expectRotationMatrixOf(const Anim::Transform& transform, const vsg::quat& expected)
    {
        auto reference = Anim::Transform::create();
        reference->setAttitude(expected);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                EXPECT_NEAR(transform.rotation[i][j], reference->rotation[i][j], sMetreTol) << i << "," << j;
            }
        }
    }
}

// --- Settings ---------------------------------------------------------------------------------------------------

TEST(CreativeMotionRuntimeTest, SettingsDefaults)
{
    // Off by default, so the game plays exactly as before.
    EXPECT_FALSE(Settings::game().mCreativeMotionAnimation.get());
    EXPECT_FALSE(Settings::game().mCreativeMotionAnimationCompare.get());
    EXPECT_NEAR(Settings::game().mCreativeMotionAnimationTransition.get(), 0.25f, 1e-4f);
}

// --- Skeleton ---------------------------------------------------------------------------------------------------

TEST(CreativeMotionRuntimeTest, SkeletonDiscoveryAndHierarchyValidation)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);

    ASSERT_TRUE(driver.isValid());
    const auto& skel = driver.getSkeleton();
    EXPECT_EQ(skel.bones.size(), 11u);

    for (const char* name : { "bip01", "bip01 pelvis", "bip01 spine1", "bip01 neck", "bip01 head", "bip01 l clavicle",
             "bip01 l upperarm", "bip01 r clavicle", "bip01 r upperarm", "bip01 l thigh", "bip01 r thigh" })
    {
        const int i = driver.getBoneIndex(name);
        ASSERT_GE(i, 0) << name;
        EXPECT_EQ(skel.bones[i].name, name);
    }

    const int bip01 = driver.getBoneIndex("bip01");
    const int pelvis = driver.getBoneIndex("bip01 pelvis");
    const int spine = driver.getBoneIndex("bip01 spine1");
    EXPECT_EQ(skel.bones[bip01].parent, -1);
    EXPECT_EQ(skel.bones[pelvis].parent, bip01);
    EXPECT_EQ(skel.bones[spine].parent, pelvis);
    EXPECT_EQ(skel.bones[driver.getBoneIndex("bip01 l thigh")].parent, pelvis);
    EXPECT_EQ(skel.bones[driver.getBoneIndex("bip01 neck")].parent, spine);
    EXPECT_EQ(skel.bones[driver.getBoneIndex("bip01 head")].parent, driver.getBoneIndex("bip01 neck"));
    EXPECT_EQ(skel.bones[driver.getBoneIndex("bip01 l clavicle")].parent, spine);
    EXPECT_EQ(skel.bones[driver.getBoneIndex("bip01 l upperarm")].parent, driver.getBoneIndex("bip01 l clavicle"));
    EXPECT_EQ(skel.bones[driver.getBoneIndex("bip01 r upperarm")].parent, driver.getBoneIndex("bip01 r clavicle"));

    // Parent first.
    for (size_t i = 0; i < skel.bones.size(); ++i)
    {
        if (skel.bones[i].parent >= 0)
        {
            EXPECT_LT(skel.bones[i].parent, static_cast<int>(i));
        }
    }
}

TEST(CreativeMotionRuntimeTest, SkeletonOrderIsByDepthThenNormalizedName)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    const std::vector<std::string> expected
        = { "bip01", "bip01 pelvis", "bip01 l thigh", "bip01 r thigh", "bip01 spine1", "bip01 l clavicle", "bip01 neck",
              "bip01 r clavicle", "bip01 head", "bip01 l upperarm", "bip01 r upperarm" };
    const auto& skel = driver.getSkeleton();
    ASSERT_EQ(skel.bones.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(skel.bones[i].name, expected[i]) << i;
        EXPECT_EQ(driver.getBoneIndex(expected[i]), static_cast<int>(i));
    }
}

TEST(CreativeMotionRuntimeTest, GetBoneIndexUnknownOrNotNormalizedReturnsMinusOne)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    EXPECT_EQ(driver.getBoneIndex("bip01 tail"), -1);
    EXPECT_EQ(driver.getBoneIndex(""), -1);
    // Lookup is by normalized name.
    EXPECT_EQ(driver.getBoneIndex("Bip01 Pelvis"), -1);
}

TEST(CreativeMotionRuntimeTest, SkeletonRestPoseIsInMetresWithUnitQuaternions)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const auto& skel = driver.getSkeleton();

    const std::vector<std::pair<std::string, vsg::vec3>> translations = { { "bip01", vsg::vec3(0.f, 0.f, 0.f) },
        { "bip01 pelvis", sPelvisRest }, { "bip01 spine1", sSpineRest }, { "bip01 neck", sNeckRest },
        { "bip01 head", sHeadRest }, { "bip01 l clavicle", sLClavicleRest }, { "bip01 l upperarm", sLUpperArmRest },
        { "bip01 r clavicle", sRClavicleRest }, { "bip01 r upperarm", sRUpperArmRest },
        { "bip01 l thigh", sLThighRest }, { "bip01 r thigh", sRThighRest } };
    for (const auto& [name, units] : translations)
    {
        const int i = index(driver, name);
        ASSERT_GE(i, 0);
        SCOPED_TRACE(name);
        expectVec3Near(restTranslationMetres(skel.bones[i]), units / Constants::UnitsPerMeter, sMetreTol);
    }

    for (const auto& bone : skel.bones)
    {
        const auto& q = bone.localRest.rotation;
        EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0, 1e-5) << bone.name;
    }

    expectQuatNear(restRotation(skel.bones[index(driver, "bip01 head")]), sHeadRestRotation, sQuatTol);
    expectQuatNear(restRotation(skel.bones[index(driver, "bip01 l upperarm")]), sLUpperArmRestRotation, sQuatTol);
    expectQuatNear(restRotation(skel.bones[index(driver, "bip01 pelvis")]), vsg::quat(0.f, 0.f, 0.f, 1.f), sQuatTol);
}

TEST(CreativeMotionRuntimeTest, SkeletonDiscoverySkipsUnnamedNodes)
{
    Humanoid rig(false);
    // An unnamed intermediate transform (an unnamed NIF node) between the head and an attachment bone.
    auto unnamed = rig.add(*rig.head, "", vsg::vec3(0.f, 0.f, 1.f));
    rig.add(*unnamed, "Accessory_Bone", vsg::vec3(1.f, 2.f, 3.f));
    rig.finish();

    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    const auto& skel = driver.getSkeleton();
    EXPECT_EQ(skel.bones.size(), 12u);
    EXPECT_EQ(driver.getBoneIndex(""), -1);
    for (const auto& bone : skel.bones)
    {
        EXPECT_FALSE(bone.name.empty());
    }

    const int head = driver.getBoneIndex("bip01 head");
    const int accessory = driver.getBoneIndex("accessory_bone");
    ASSERT_GE(head, 0);
    ASSERT_GE(accessory, 0);
    // The nearest named ancestor is the parent.
    EXPECT_EQ(skel.bones[accessory].parent, head);
    EXPECT_LT(head, accessory);
}

TEST(CreativeMotionRuntimeTest, UnnamedRootTransformIsSkippedAndChildrenBecomeRoots)
{
    Rig rig;
    auto unnamed = rig.add(*rig.root, "", vsg::vec3(0.f, 0.f, 0.f));
    rig.add(*unnamed, "Bip01", vsg::vec3(0.f, 0.f, 0.f));
    rig.finish();

    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    ASSERT_EQ(driver.getSkeleton().bones.size(), 1u);
    EXPECT_EQ(driver.getSkeleton().bones[0].name, "bip01");
    EXPECT_EQ(driver.getSkeleton().bones[0].parent, -1);
}

TEST(CreativeMotionRuntimeTest, InvalidWithoutNamedBones)
{
    Rig rig;
    auto a = rig.add(*rig.root, "", vsg::vec3(0.f, 0.f, 0.f));
    rig.add(*a, "", vsg::vec3(1.f, 0.f, 0.f));
    rig.finish();

    CreativeMotionPoseDriver driver(*rig.bones);
    EXPECT_FALSE(driver.isValid());

    Source source;
    source.add("Bip01", constant(vsg::vec3(1.f, 2.f, 3.f)));
    EXPECT_FALSE(driver.registerSource(source.ref()));

    float maxTrans = -1.f, maxRot = -1.f;
    std::string bone;
    EXPECT_FALSE(driver.compareWithCurrentPose(maxTrans, maxRot, bone));
}

// --- Source registration ----------------------------------------------------------------------------------------

TEST(CreativeMotionRuntimeTest, RegisterSourceFailsWhenNoControllerNamesASkeletonBone)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source unrelated;
    unrelated.add("Bip01 Tail", constant(vsg::vec3(1.f, 2.f, 3.f)));
    unrelated.add("Some Other Node", constant(vsg::vec3(1.f, 2.f, 3.f)));
    EXPECT_FALSE(driver.registerSource(unrelated.ref()));

    Source empty;
    EXPECT_FALSE(driver.registerSource(empty.ref()));
}

TEST(CreativeMotionRuntimeTest, RegisterSourceMatchesBoneNamesInAnyCase)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("BIP01 PELVIS", constant(vsg::vec3(1.f, 2.f, 70.f)));
    EXPECT_TRUE(driver.registerSource(source.ref()));

    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);
    const int pelvis = index(driver, "bip01 pelvis");
    EXPECT_TRUE(driver.isBoneAnimated(pelvis));
    expectVec3Near(driver.getBoneTranslation(pelvis), vsg::vec3(1.f, 2.f, 70.f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, RegisterSourceIsCached)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    auto channel = constant(vsg::vec3(1.f, 2.f, 70.f));
    Source source;
    source.add("Bip01 Pelvis", channel);
    EXPECT_TRUE(driver.registerSource(source.ref()));

    // A second registration returns the cached clip without converting again, so a later change to the source's
    // keys is not seen.
    channel->keys[0.f] = vsg::vec3(-5.f, -5.f, -5.f);
    EXPECT_TRUE(driver.registerSource(source.ref()));

    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);
    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01 pelvis")), vsg::vec3(1.f, 2.f, 70.f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, ControllersForUnknownBonesAreIgnored)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Bip01 Pelvis", constant(vsg::vec3(0.f, 0.f, 60.f)));
    source.add("Bip01 Tail", constant(vsg::vec3(9.f, 9.f, 9.f)));
    EXPECT_TRUE(driver.registerSource(source.ref()));

    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);
    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01 pelvis")), vsg::vec3(0.f, 0.f, 60.f), sUnitTol);
    EXPECT_FALSE(driver.isBoneAnimated(index(driver, "bip01 l thigh")));
}

TEST(CreativeMotionRuntimeTest, UnregisteredSourceIsRegisteredOnTheFly)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add(
        "Bip01 Pelvis", linear<vsg::vec3>({ { 0.f, vsg::vec3(0.f, 0.f, 60.f) }, { 1.f, vsg::vec3(0.f, 0.f, 70.f) } }));
    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.5f), 0.f);

    const int pelvis = index(driver, "bip01 pelvis");
    EXPECT_TRUE(driver.isBoneAnimated(pelvis));
    expectVec3Near(driver.getBoneTranslation(pelvis), vsg::vec3(0.f, 0.f, 65.f), sUnitTol);
    EXPECT_TRUE(driver.registerSource(source.ref()));
}

// --- Channel conversion -----------------------------------------------------------------------------------------

namespace
{
    // Samples the pelvis (group 0, not the accumulation root) of a source at time t.
    struct PelvisSampler
    {
        Humanoid rig;
        CreativeMotionPoseDriver driver{ *rig.bones };
        int pelvis = driver.getBoneIndex("bip01 pelvis");

        void sample(const Source& source, float t) { driver.update(1.f / 60.f, activeIn(0, source, "idle", t), 0.f); }
    };

    const std::vector<float> sSampleTimes = { -0.5f, 0.f, 0.05f, 0.1f, 0.2f, 0.25f, 0.3f, 0.33f, 0.4f, 0.5f, 0.61f,
        0.7f, 0.75f, 0.9f, 1.f, 1.2f, 1.37f, 1.5f, 1.8f, 2.f, 2.5f, 10.f };
}

TEST(CreativeMotionRuntimeTest, LinearTranslationMatchesChannel)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    auto channel = linear<vsg::vec3>({ { 0.f, vsg::vec3(0.f, 0.f, 60.f) }, { 0.5f, vsg::vec3(4.f, -2.f, 66.f) },
        { 1.f, vsg::vec3(-3.f, 7.f, 62.f) }, { 2.f, vsg::vec3(10.f, 1.f, 70.f) } });
    Source source;
    source.add("Bip01 Pelvis", channel);
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    for (float t : sSampleTimes)
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), channel->value(t), sUnitTol);
    }
}

TEST(CreativeMotionRuntimeTest, LinearRotationIsSlerpedLikeChannel)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    const vsg::vec3 z(0.f, 0.f, 1.f);
    auto channel = linear<vsg::quat>({ { 0.f, axisAngle(0.f, z) }, { 0.5f, axisAngle(1.2f, z) },
        { 1.f, axisAngle(0.6f, vsg::vec3(1.f, 0.f, 0.f)) }, { 2.f, axisAngle(-1.f, vsg::vec3(0.f, 1.f, 0.f)) } });
    Source source;
    source.add("Bip01 Pelvis", {}, channel);
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    for (float t : sSampleTimes)
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        EXPECT_TRUE(s.driver.isBoneAnimated(s.pelvis));
        expectQuatNear(s.driver.getBoneRotation(s.pelvis), channel->value(t), sQuatTol);
    }
}

TEST(CreativeMotionRuntimeTest, FlipChannelsBecomeStepTracks)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    const vsg::vec3 x(1.f, 0.f, 0.f);
    auto translation = flip<vsg::vec3>({ { 0.f, vsg::vec3(0.f, 0.f, 60.f) }, { 1.f, vsg::vec3(5.f, 0.f, 64.f) },
        { 2.f, vsg::vec3(-5.f, 3.f, 68.f) } });
    auto rotation
        = flip<vsg::quat>({ { 0.f, axisAngle(0.f, x) }, { 1.f, axisAngle(0.8f, x) }, { 2.f, axisAngle(-0.4f, x) } });
    Source source;
    source.add("Bip01 Pelvis", translation, rotation);
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    // Every time except exactly at the midpoints 0.5 and 1.5.
    for (float t : { -1.f, 0.f, 0.1f, 0.25f, 0.45f, 0.49f, 0.51f, 0.55f, 0.75f, 0.99f, 1.f, 1.01f, 1.3f, 1.49f, 1.51f,
             1.7f, 2.f, 3.f })
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), translation->value(t), sUnitTol);
        expectQuatNear(s.driver.getBoneRotation(s.pelvis), rotation->value(t), sQuatTol);
    }
}

TEST(CreativeMotionRuntimeTest, CubicHermiteRotationIsSlerpedLikeLinear)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    const vsg::vec3 y(0.f, 1.f, 0.f);
    auto channel = Anim::make_channel<Anim::CubicHermiteSpline<vsg::quat>>();
    channel->keys = { { 0.f, axisAngle(0.f, y) }, { 0.4f, axisAngle(1.f, y) },
        { 1.f, axisAngle(0.3f, vsg::vec3(0.f, 0.f, 1.f)) } };
    Source source;
    source.add("Bip01 Pelvis", {}, channel);
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    for (float t : sSampleTimes)
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectQuatNear(s.driver.getBoneRotation(s.pelvis), channel->value(t), sQuatTol);
    }
}

TEST(CreativeMotionRuntimeTest, CubicHermiteTranslationIsResampled)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    using Spline = Anim::CubicHermiteSpline<vsg::vec3>;
    auto channel = Anim::make_channel<Spline>();
    // Keys at 0, 0.5 and 1 (on the 60 Hz grid) and at 0.71 (off it).
    channel->keys = {
        { 0.f, Spline::Data{ vsg::vec3(0.f, 0.f, 60.f), vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(8.f, 4.f, 2.f) } },
        { 0.5f, Spline::Data{ vsg::vec3(5.f, 2.f, 64.f), vsg::vec3(4.f, -6.f, 1.f), vsg::vec3(2.f, -3.f, 1.f) } },
        { 0.71f, Spline::Data{ vsg::vec3(3.f, -1.f, 66.f), vsg::vec3(-5.f, 2.f, 3.f), vsg::vec3(-5.f, 2.f, 3.f) } },
        { 1.f, Spline::Data{ vsg::vec3(-2.f, 1.f, 61.f), vsg::vec3(-3.f, 3.f, -4.f), vsg::vec3(0.f, 0.f, 0.f) } },
    };
    Source source;
    source.add("Bip01 Pelvis", channel);
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    const float step = 1.f / CreativeMotionPoseDriver::sResampleRate;

    // Exact at key times and at multiples of 1/60 s.
    for (float t : { 0.f, 0.5f, 0.71f, 1.f })
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), channel->value(t), sUnitTol);
    }
    for (int k = 0; k <= 60; ++k)
    {
        const float t = k * step;
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), channel->value(t), sUnitTol);
    }

    // Between samples: linear between the neighbouring samples (keys included), so within the linear-interpolation
    // error of the curve.
    for (int k = 0; k < 60; ++k)
    {
        float t0 = k * step;
        float t1 = (k + 1) * step;
        if (t0 < 0.71f && 0.71f < t1)
            continue; // a key splits this interval
        const float t = 0.5f * (t0 + t1);
        const vsg::vec3 a = channel->value(t0);
        const vsg::vec3 b = channel->value(t1);
        const vsg::vec3 lerp = a + (b - a) * 0.5f;
        // The linear-interpolation error bound: the distance between the chord and the curve at the midpoint.
        const float bound = distance(lerp, channel->value(t)) + sUnitTol;
        SCOPED_TRACE(t);
        s.sample(source, t);
        EXPECT_LE(distance(s.driver.getBoneTranslation(s.pelvis), channel->value(t)), bound);
    }

    // Clamped outside the keys.
    s.sample(source, -1.f);
    expectVec3Near(s.driver.getBoneTranslation(s.pelvis), vsg::vec3(0.f, 0.f, 60.f), sUnitTol);
    s.sample(source, 5.f);
    expectVec3Near(s.driver.getBoneTranslation(s.pelvis), vsg::vec3(-2.f, 1.f, 61.f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, UnknownChannelTypesAreResampled)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    auto wave = Anim::make_channel<WaveChannel>();
    Source source;
    // The source's timeline spans [0, 1]: text keys and a keyed channel on another bone mark it.
    source.tag(0.f, "idle: start");
    source.tag(1.f, "idle: stop");
    source.add("Bip01 Pelvis", wave);
    source.add("Bip01 L Thigh", linear<vsg::vec3>({ { 0.f, sLThighRest }, { 1.f, sLThighRest } }));
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    const float step = 1.f / CreativeMotionPoseDriver::sResampleRate;
    for (int k = 0; k <= 60; ++k)
    {
        const float t = k * step;
        SCOPED_TRACE(t);
        s.sample(source, t);
        EXPECT_TRUE(s.driver.isBoneAnimated(s.pelvis));
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), wave->value(t), sUnitTol);
    }
    for (int k = 0; k < 60; ++k)
    {
        const float t0 = k * step, t1 = (k + 1) * step, t = 0.5f * (t0 + t1);
        const vsg::vec3 a = wave->value(t0);
        const vsg::vec3 lerp = a + (wave->value(t1) - a) * 0.5f;
        const float bound = distance(lerp, wave->value(t)) + sUnitTol;
        SCOPED_TRACE(t);
        s.sample(source, t);
        EXPECT_LE(distance(s.driver.getBoneTranslation(s.pelvis), wave->value(t)), bound);
    }
}

TEST(CreativeMotionRuntimeTest, ChannelsClampBeforeFirstAndAfterLastKey)
{
    PelvisSampler s;
    ASSERT_TRUE(s.driver.isValid());
    const vsg::vec3 z(0.f, 0.f, 1.f);
    Source source;
    source.add("Bip01 Pelvis",
        linear<vsg::vec3>({ { 0.2f, vsg::vec3(1.f, 0.f, 61.f) }, { 0.8f, vsg::vec3(3.f, 0.f, 69.f) } }),
        linear<vsg::quat>({ { 0.2f, axisAngle(0.2f, z) }, { 0.8f, axisAngle(0.9f, z) } }));
    ASSERT_TRUE(s.driver.registerSource(source.ref()));

    for (float t : { -1.f, 0.f, 0.1f, 0.2f })
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), vsg::vec3(1.f, 0.f, 61.f), sUnitTol);
        expectQuatNear(s.driver.getBoneRotation(s.pelvis), axisAngle(0.2f, z), sQuatTol);
    }
    for (float t : { 0.8f, 0.9f, 2.f, 100.f })
    {
        SCOPED_TRACE(t);
        s.sample(source, t);
        expectVec3Near(s.driver.getBoneTranslation(s.pelvis), vsg::vec3(3.f, 0.f, 69.f), sUnitTol);
        expectQuatNear(s.driver.getBoneRotation(s.pelvis), axisAngle(0.9f, z), sQuatTol);
    }
}

TEST(CreativeMotionRuntimeTest, ScaleChannelsAreIgnored)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Bip01 Pelvis", constant(vsg::vec3(0.f, 0.f, 62.f)), {}, constant(2.f));
    ASSERT_TRUE(driver.registerSource(source.ref()));
    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);
    driver.apply();

    Anim::Transform* pelvis = rig.node("bip01 pelvis");
    ASSERT_NE(pelvis, nullptr);
    expectVec3Near(pelvis->translation, vsg::vec3(0.f, 0.f, 62.f), sUnitTol);
    expectVec3Near(pelvis->scale, vsg::vec3(1.f, 1.f, 1.f), sMetreTol);
}

// --- Sampling and per-group layering ------------------------------------------------------------------------------

TEST(CreativeMotionRuntimeTest, PoseSamplingAndLayeringAcrossGroups)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    const vsg::vec3 z(0.f, 0.f, 1.f);
    // Source A animates bones of every group; source B too, with other values.
    Source a;
    a.add("Bip01 Pelvis", constant(vsg::vec3(1.f, 0.f, 60.f)), constant(axisAngle(0.1f, z)));
    a.add("Bip01 Spine1", constant(vsg::vec3(0.f, 1.f, 9.f)), constant(axisAngle(0.2f, z)));
    a.add("Bip01 L Clavicle", constant(vsg::vec3(-6.f, 0.f, 12.f)), constant(axisAngle(0.3f, z)));
    a.add("Bip01 R Clavicle", constant(vsg::vec3(6.f, 0.f, 12.f)), constant(axisAngle(0.4f, z)));
    Source b;
    b.add("Bip01 Pelvis", constant(vsg::vec3(2.f, 0.f, 61.f)), constant(axisAngle(-0.1f, z)));
    b.add("Bip01 Spine1", constant(vsg::vec3(0.f, 2.f, 10.f)), constant(axisAngle(-0.2f, z)));
    b.add("Bip01 L Clavicle", constant(vsg::vec3(-7.f, 0.f, 13.f)), constant(axisAngle(-0.3f, z)));
    ASSERT_TRUE(driver.registerSource(a.ref()));
    ASSERT_TRUE(driver.registerSource(b.ref()));

    // Lower body plays A, torso plays B, the left arm plays nothing, the right arm plays A.
    ActiveArray active{};
    active[0] = { a.get(), "idle", 0.f };
    active[1] = { b.get(), "idle", 0.f };
    active[3] = { a.get(), "idle", 0.f };
    driver.update(1.f / 60.f, active, 0.f);

    const int pelvis = index(driver, "bip01 pelvis");
    const int spine = index(driver, "bip01 spine1");
    const int neck = index(driver, "bip01 neck");
    const int head = index(driver, "bip01 head");
    const int lClavicle = index(driver, "bip01 l clavicle");
    const int lUpperArm = index(driver, "bip01 l upperarm");
    const int rClavicle = index(driver, "bip01 r clavicle");
    const int rUpperArm = index(driver, "bip01 r upperarm");
    const int lThigh = index(driver, "bip01 l thigh");

    // Each bone takes its own group's source.
    EXPECT_TRUE(driver.isBoneAnimated(pelvis));
    expectVec3Near(driver.getBoneTranslation(pelvis), vsg::vec3(1.f, 0.f, 60.f), sUnitTol);
    expectQuatNear(driver.getBoneRotation(pelvis), axisAngle(0.1f, z), sQuatTol);

    EXPECT_TRUE(driver.isBoneAnimated(spine));
    expectVec3Near(driver.getBoneTranslation(spine), vsg::vec3(0.f, 2.f, 10.f), sUnitTol);
    expectQuatNear(driver.getBoneRotation(spine), axisAngle(-0.2f, z), sQuatTol);

    EXPECT_TRUE(driver.isBoneAnimated(rClavicle));
    expectVec3Near(driver.getBoneTranslation(rClavicle), vsg::vec3(6.f, 0.f, 12.f), sUnitTol);
    expectQuatNear(driver.getBoneRotation(rClavicle), axisAngle(0.4f, z), sQuatTol);

    // The left arm's group plays nothing: its bones keep their rest pose, although A and B have a track for them.
    EXPECT_FALSE(driver.isBoneAnimated(lClavicle));
    expectVec3Near(driver.getBoneTranslation(lClavicle), sLClavicleRest, sUnitTol);
    expectQuatNear(driver.getBoneRotation(lClavicle), vsg::quat(0.f, 0.f, 0.f, 1.f), sQuatTol);
    EXPECT_FALSE(driver.isBoneAnimated(lUpperArm));
    expectVec3Near(driver.getBoneTranslation(lUpperArm), sLUpperArmRest, sUnitTol);
    expectQuatNear(driver.getBoneRotation(lUpperArm), sLUpperArmRestRotation, sQuatTol);

    // Bones whose group's source has no track for them keep their rest pose.
    EXPECT_FALSE(driver.isBoneAnimated(neck));
    expectVec3Near(driver.getBoneTranslation(neck), sNeckRest, sUnitTol);
    EXPECT_FALSE(driver.isBoneAnimated(head));
    expectVec3Near(driver.getBoneTranslation(head), sHeadRest, sUnitTol);
    expectQuatNear(driver.getBoneRotation(head), sHeadRestRotation, sQuatTol);
    EXPECT_FALSE(driver.isBoneAnimated(rUpperArm));
    expectVec3Near(driver.getBoneTranslation(rUpperArm), sRUpperArmRest, sUnitTol);
    EXPECT_FALSE(driver.isBoneAnimated(lThigh));
    expectVec3Near(driver.getBoneTranslation(lThigh), sLThighRest, sUnitTol);

    // Every output is finite with unit rotations.
    for (size_t i = 0; i < driver.getSkeleton().bones.size(); ++i)
    {
        const vsg::vec3& t = driver.getBoneTranslation(i);
        const vsg::quat& r = driver.getBoneRotation(i);
        EXPECT_TRUE(std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.z));
        EXPECT_NEAR(quatDot(r, r), 1.f, 1e-4f);
    }
}

TEST(CreativeMotionRuntimeTest, EachGroupSamplesAtItsOwnTime)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    auto pelvisChannel = linear<vsg::vec3>({ { 0.f, vsg::vec3(0.f, 0.f, 60.f) }, { 1.f, vsg::vec3(0.f, 0.f, 70.f) } });
    auto spineChannel = linear<vsg::vec3>({ { 0.f, vsg::vec3(0.f, 0.f, 8.f) }, { 1.f, vsg::vec3(0.f, 0.f, 18.f) } });
    Source source;
    source.add("Bip01 Pelvis", pelvisChannel);
    source.add("Bip01 Spine1", spineChannel);

    ActiveArray active{};
    active[0] = { source.get(), "walkforward", 0.25f };
    active[1] = { source.get(), "walkforward", 0.75f };
    driver.update(1.f / 60.f, active, 0.f);

    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01 pelvis")), pelvisChannel->value(0.25f), sUnitTol);
    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01 spine1")), spineChannel->value(0.75f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, GroupThatStopsPlayingReturnsToRest)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Bip01 Pelvis", constant(vsg::vec3(3.f, 3.f, 60.f)));
    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);
    const int pelvis = index(driver, "bip01 pelvis");
    EXPECT_TRUE(driver.isBoneAnimated(pelvis));

    driver.update(1.f / 60.f, noneActive(), 0.f);
    EXPECT_FALSE(driver.isBoneAnimated(pelvis));
    expectVec3Near(driver.getBoneTranslation(pelvis), sPelvisRest, sUnitTol);
}

// --- Accumulation ------------------------------------------------------------------------------------------------

TEST(CreativeMotionRuntimeTest, AccumulationRootResetDefaultZeroesXY)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Bip01", constant(vsg::vec3(10.f, 20.f, 30.f)));
    source.add("Bip01 Pelvis", constant(vsg::vec3(4.f, 5.f, 60.f)));
    driver.update(1.f / 60.f, activeIn(0, source, "walkforward", 0.f), 0.f);

    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01")), vsg::vec3(0.f, 0.f, 30.f), sUnitTol);
    // Only the accumulation root is affected.
    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01 pelvis")), vsg::vec3(4.f, 5.f, 60.f), sUnitTol);

    // Written to the bone as well.
    driver.apply();
    expectVec3Near(rig.node("bip01")->translation, vsg::vec3(0.f, 0.f, 30.f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, AccumulationFollowsSetAccumulation)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const int bip01 = index(driver, "bip01");

    Source source;
    source.add("Bip01", constant(vsg::vec3(10.f, 20.f, 30.f)));

    driver.setAccumulation(vsg::vec3(0.f, 0.f, 0.f));
    driver.update(1.f / 60.f, activeIn(0, source, "walkforward", 0.f), 0.f);
    expectVec3Near(driver.getBoneTranslation(bip01), vsg::vec3(10.f, 20.f, 30.f), sUnitTol);

    driver.setAccumulation(vsg::vec3(0.f, 0.f, 1.f));
    driver.update(1.f / 60.f, activeIn(0, source, "walkforward", 0.f), 0.f);
    expectVec3Near(driver.getBoneTranslation(bip01), vsg::vec3(10.f, 20.f, 0.f), sUnitTol);

    driver.setAccumulation(vsg::vec3(1.f, 1.f, 1.f));
    driver.update(1.f / 60.f, activeIn(0, source, "walkforward", 0.f), 0.f);
    expectVec3Near(driver.getBoneTranslation(bip01), vsg::vec3(0.f, 0.f, 0.f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, AccumulationRootFallsBackToRootBone)
{
    Rig rig;
    auto rootBone = rig.add(*rig.root, "Root Bone", vsg::vec3(0.f, 0.f, 0.f));
    rig.add(*rootBone, "Tail", vsg::vec3(0.f, -10.f, 20.f));
    rig.finish();
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Root Bone", constant(vsg::vec3(10.f, 20.f, 30.f)));
    source.add("Tail", constant(vsg::vec3(1.f, 2.f, 3.f)));
    driver.update(1.f / 60.f, activeIn(0, source, "walkforward", 0.f), 0.f);

    expectVec3Near(driver.getBoneTranslation(index(driver, "root bone")), vsg::vec3(0.f, 0.f, 30.f), sUnitTol);
    expectVec3Near(driver.getBoneTranslation(index(driver, "tail")), vsg::vec3(1.f, 2.f, 3.f), sUnitTol);
}

TEST(CreativeMotionRuntimeTest, AccumulationRootPrefersBip01OverRootBone)
{
    Rig rig;
    auto rootBone = rig.add(*rig.root, "Root Bone", vsg::vec3(0.f, 0.f, 0.f));
    rig.add(*rootBone, "Bip01", vsg::vec3(0.f, 0.f, 0.f));
    rig.finish();
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Root Bone", constant(vsg::vec3(10.f, 20.f, 30.f)));
    source.add("Bip01", constant(vsg::vec3(1.f, 2.f, 3.f)));
    driver.update(1.f / 60.f, activeIn(0, source, "walkforward", 0.f), 0.f);

    expectVec3Near(driver.getBoneTranslation(index(driver, "root bone")), vsg::vec3(10.f, 20.f, 30.f), sUnitTol);
    expectVec3Near(driver.getBoneTranslation(index(driver, "bip01")), vsg::vec3(0.f, 0.f, 3.f), sUnitTol);
}

// --- Transitions --------------------------------------------------------------------------------------------------

namespace
{
    const vsg::vec3 sAxisZ(0.f, 0.f, 1.f);
    const vsg::vec3 sPoseATranslation(0.f, 0.f, 60.f);
    const vsg::vec3 sPoseBTranslation(0.f, 0.f, 70.f);
    const vsg::quat sPoseARotation = axisAngle(0.f, sAxisZ);
    const vsg::quat sPoseBRotation = axisAngle(1.2f, sAxisZ);

    // Two sources holding the pelvis still in two different poses.
    struct TwoPoses
    {
        Source a, b;
        TwoPoses()
        {
            a.add("Bip01 Pelvis", constant(sPoseATranslation), constant(sPoseARotation));
            b.add("Bip01 Pelvis", constant(sPoseBTranslation), constant(sPoseBRotation));
        }
    };
}

TEST(CreativeMotionRuntimeTest, InertializationNoDiscontinuityOnStateSwitch)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    TwoPoses poses;
    const int pelvis = index(driver, "bip01 pelvis");
    const float dt = 1.f / 60.f;
    const float transition = 0.25f;

    float time = 0.f;
    for (int i = 0; i < 10; ++i, time += dt)
        driver.update(dt, activeIn(0, poses.a, "idle", time), transition);
    const vsg::vec3 transBefore = driver.getBoneTranslation(pelvis);
    const vsg::quat rotBefore = driver.getBoneRotation(pelvis);
    expectVec3Near(transBefore, sPoseATranslation, sUnitTol);

    // Switch to B: the first frame continues from the previous pose instead of jumping to B.
    driver.update(dt, activeIn(0, poses.b, "walkforward", 0.f), transition);
    const vsg::vec3 transAfter = driver.getBoneTranslation(pelvis);
    const vsg::quat rotAfter = driver.getBoneRotation(pelvis);
    const float jump = distance(sPoseATranslation, sPoseBTranslation);
    EXPECT_LT(distance(transAfter, transBefore), 0.25f * jump);
    EXPECT_GT(distance(transAfter, sPoseBTranslation), 0.5f * jump);
    EXPECT_LT(quatAngle(rotAfter, rotBefore), 0.25f * 1.2f);
    EXPECT_TRUE(driver.isBoneAnimated(pelvis));

    // Converges to B after the transition duration.
    float elapsed = dt;
    time = dt;
    while (elapsed < transition + 0.05f)
    {
        driver.update(dt, activeIn(0, poses.b, "walkforward", time), transition);
        elapsed += dt;
        time += dt;
    }
    expectVec3Near(driver.getBoneTranslation(pelvis), sPoseBTranslation, sUnitTol);
    expectQuatNear(driver.getBoneRotation(pelvis), sPoseBRotation, 1e-4f);
}

TEST(CreativeMotionRuntimeTest, InertializationTriggersOnGroupNameChangeWithSameSource)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    const int pelvis = index(driver, "bip01 pelvis");
    const float dt = 1.f / 60.f;
    const float transition = 0.25f;

    // Both groups live in the same source (vanilla base_anim pattern): walk on [0, 1], run on [2, 3].
    Source shared;
    shared.tag(0.f, "walkforward: start");
    shared.tag(1.f, "walkforward: stop");
    shared.tag(2.f, "runforward: start");
    shared.tag(3.f, "runforward: stop");
    shared.add("Bip01 Pelvis",
        linear<vsg::vec3>({ { 0.f, sPoseATranslation }, { 1.f, sPoseATranslation }, { 2.f, sPoseBTranslation },
            { 3.f, sPoseBTranslation } }));

    for (int i = 0; i < 10; ++i)
        driver.update(dt, activeIn(0, shared, "walkforward", 0.1f + i * dt), transition);
    const vsg::vec3 before = driver.getBoneTranslation(pelvis);
    expectVec3Near(before, sPoseATranslation, sUnitTol);

    driver.update(dt, activeIn(0, shared, "runforward", 2.1f), transition);
    const vsg::vec3 after = driver.getBoneTranslation(pelvis);
    const float jump = distance(sPoseATranslation, sPoseBTranslation);
    EXPECT_LT(distance(after, before), 0.25f * jump);
    EXPECT_GT(distance(after, sPoseBTranslation), 0.5f * jump);

    float time = 2.1f + dt;
    for (float elapsed = dt; elapsed < transition + 0.05f; elapsed += dt, time += dt)
        driver.update(dt, activeIn(0, shared, "runforward", time), transition);
    expectVec3Near(driver.getBoneTranslation(pelvis), sPoseBTranslation, sUnitTol);
}

TEST(CreativeMotionRuntimeTest, ZeroTransitionDurationSwitchesImmediately)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    TwoPoses poses;
    const int pelvis = index(driver, "bip01 pelvis");
    const float dt = 1.f / 60.f;

    for (int i = 0; i < 5; ++i)
        driver.update(dt, activeIn(0, poses.a, "idle", i * dt), 0.f);
    driver.update(dt, activeIn(0, poses.b, "walkforward", 0.f), 0.f);
    expectVec3Near(driver.getBoneTranslation(pelvis), sPoseBTranslation, sUnitTol);
    expectQuatNear(driver.getBoneRotation(pelvis), sPoseBRotation, sQuatTol);
}

TEST(CreativeMotionRuntimeTest, NoTransitionWhenGroupPlayedNothingBefore)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    TwoPoses poses;
    const int pelvis = index(driver, "bip01 pelvis");
    const float dt = 1.f / 60.f;

    driver.update(dt, noneActive(), 0.25f);
    driver.update(dt, noneActive(), 0.25f);
    driver.update(dt, activeIn(0, poses.b, "walkforward", 0.f), 0.25f);
    expectVec3Near(driver.getBoneTranslation(pelvis), sPoseBTranslation, sUnitTol);
    expectQuatNear(driver.getBoneRotation(pelvis), sPoseBRotation, sQuatTol);
}

TEST(CreativeMotionRuntimeTest, TransitionDoesNotAffectOtherGroups)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());
    TwoPoses poses;
    const int pelvis = index(driver, "bip01 pelvis");
    const int spine = index(driver, "bip01 spine1");
    const float dt = 1.f / 60.f;

    const vsg::vec3 torsoTranslation(0.f, 1.f, 9.f);
    const vsg::quat torsoRotation = axisAngle(0.4f, sAxisZ);
    Source torso;
    torso.add("Bip01 Spine1", constant(torsoTranslation), constant(torsoRotation));

    ActiveArray active{};
    active[1] = { torso.get(), "idle", 0.f };
    for (int i = 0; i < 5; ++i)
    {
        active[0] = { poses.a.get(), "idle", i * dt };
        driver.update(dt, active, 0.25f);
    }

    // The lower body switches; the torso keeps playing the same thing and is sampled as is.
    active[0] = { poses.b.get(), "walkforward", 0.f };
    driver.update(dt, active, 0.25f);
    EXPECT_GT(distance(driver.getBoneTranslation(pelvis), sPoseBTranslation), 1.f);
    expectVec3Near(driver.getBoneTranslation(spine), torsoTranslation, sUnitTol);
    expectQuatNear(driver.getBoneRotation(spine), torsoRotation, sQuatTol);
}

// --- Writing the pose -------------------------------------------------------------------------------------------

TEST(CreativeMotionRuntimeTest, ApplyWritesAnimatedBonesOnly)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    const vsg::quat pelvisRotation = axisAngle(0.7f, vsg::vec3(0.f, 1.f, 0.f));
    Source source;
    source.add("Bip01 Pelvis", constant(vsg::vec3(1.f, 2.f, 63.f)), constant(pelvisRotation));
    source.add("Bip01 L Thigh", {}, constant(axisAngle(-0.5f, vsg::vec3(1.f, 0.f, 0.f))));

    Anim::Transform* pelvisNode = rig.node("bip01 pelvis");
    Anim::Transform* thighNode = rig.node("bip01 l thigh");
    Anim::Transform* neckNode = rig.node("bip01 neck");
    ASSERT_NE(pelvisNode, nullptr);
    ASSERT_NE(thighNode, nullptr);
    ASSERT_NE(neckNode, nullptr);

    // Make a non-animated bone's transform differ from its rest pose; apply() must leave it alone.
    neckNode->translation = vsg::vec3(7.f, 8.f, 9.f);
    neckNode->setAttitude(axisAngle(0.9f, sAxisZ));
    const vsg::vec3 neckTranslation = neckNode->translation;
    const vsg::mat3 neckRotation = neckNode->rotation;

    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);
    driver.apply();

    const int pelvis = index(driver, "bip01 pelvis");
    const int thigh = index(driver, "bip01 l thigh");
    expectVec3Near(pelvisNode->translation, driver.getBoneTranslation(pelvis), sUnitTol);
    expectVec3Near(pelvisNode->translation, vsg::vec3(1.f, 2.f, 63.f), sUnitTol);
    expectRotationMatrixOf(*pelvisNode, pelvisRotation);

    // A bone with only a rotation track is animated: translation from rest, rotation from the track.
    EXPECT_TRUE(driver.isBoneAnimated(thigh));
    expectVec3Near(thighNode->translation, sLThighRest, sUnitTol);
    expectRotationMatrixOf(*thighNode, axisAngle(-0.5f, vsg::vec3(1.f, 0.f, 0.f)));

    EXPECT_FALSE(driver.isBoneAnimated(index(driver, "bip01 neck")));
    EXPECT_EQ(neckNode->translation, neckTranslation);
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            EXPECT_EQ(neckNode->rotation[i][j], neckRotation[i][j]);
        }
    }
}

TEST(CreativeMotionRuntimeTest, CompareWithCurrentPoseIsZeroAfterApply)
{
    Humanoid rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    ASSERT_TRUE(driver.isValid());

    Source source;
    source.add("Bip01 Pelvis", constant(vsg::vec3(0.f, 0.f, 80.f)), constant(axisAngle(0.6f, sAxisZ)));
    source.add("Bip01 L Thigh", constant(sLThighRest), constant(axisAngle(0.1f, sAxisZ)));
    driver.update(1.f / 60.f, activeIn(0, source, "idle", 0.f), 0.f);

    // Before apply() the bones still hold their rest pose; the pelvis differs most (13.5 units, 0.6 rad).
    float maxTrans = -1.f, maxRot = -1.f;
    std::string bone;
    ASSERT_TRUE(driver.compareWithCurrentPose(maxTrans, maxRot, bone));
    EXPECT_NEAR(maxTrans, 13.5f / Constants::UnitsPerMeter, 1e-4f);
    EXPECT_NEAR(maxRot, 0.6f, 1e-3f);
    EXPECT_EQ(bone, "bip01 pelvis");

    driver.apply();
    ASSERT_TRUE(driver.compareWithCurrentPose(maxTrans, maxRot, bone));
    EXPECT_LE(maxTrans, 1e-4f);
    EXPECT_LE(maxRot, 1e-4f);
    EXPECT_GE(maxTrans, 0.f);
    EXPECT_GE(maxRot, 0.f);
}

// No allocation-counting helper exists in components-tests, so update() and apply() allocating nothing (spec item 9)
// is not tested here.
