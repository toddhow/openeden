// Tests for the engine-side bone interface (task 0011, "Design": "One engine-side bone interface"; "Constraints": no
// allocation per frame in the bone interface). Written from apps/openmw/mwrender/bonepose.hpp and the spec only.
//
// Conventions these tests rely on, all taken from the supporting headers:
// - Anim::Transform::t_transform (components/animation/transform.hpp) composes `in * local`, with local's column i
//   equal to rotation[i] * scale[i] and column 3 equal to the translation (vsg column-vector convention).
//   vsg's column-major column-vector matrix has the same memory layout as osg's row-major row-vector matrix, so the
//   osg form of a bone's local transform has row i = rotation[i] * scale[i] and row 3 = translation, and a bone's
//   model transform in osg form is local(bone) * local(parent) * ... * local(model root), applied as `p * M`.
// - Anim::Bones built from a scene (components/animation/bones.cpp, InitBones) registers every named
//   Anim::Transform under normalizeFunc(name); MWAnim::Bones (components/mwanimation/bones.hpp) lowercases.

#include "apps/openmw/pluginhost.hpp"
#include "apps/openmw/mwrender/bonepose.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/nodes/Group.h>

#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3f>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

// A narrowly scoped allocation counter for the no-allocation test. The replacement global operator new counts only
// while sCountAllocations is set on the calling thread, so every other test in openmw-tests is unaffected apart from
// one thread-local branch per allocation. Only the plain (non-aligned) forms are replaced; their deletes are replaced
// to match. The same technique backs CreativeMotion's opas_alloc_tests (components/creativemotion/core/motor/CLAUDE.md).
namespace
{
    thread_local bool sCountAllocations = false;
    thread_local std::size_t sAllocationCount = 0;

    void* countedAlloc(std::size_t size)
    {
        if (sCountAllocations)
            ++sAllocationCount;
        if (size == 0)
            size = 1;
        if (void* p = std::malloc(size))
            return p;
        throw std::bad_alloc();
    }

    struct AllocationCounter
    {
        AllocationCounter()
        {
            sAllocationCount = 0;
            sCountAllocations = true;
        }
        ~AllocationCounter() { sCountAllocations = false; }
        std::size_t count() const { return sAllocationCount; }
    };
}

void* operator new(std::size_t size)
{
    return countedAlloc(size);
}

void* operator new[](std::size_t size)
{
    return countedAlloc(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (sCountAllocations)
        ++sAllocationCount;
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    if (sCountAllocations)
        ++sAllocationCount;
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

namespace
{
    constexpr float sTolerance = 1e-5f;

    // The osg form of an Anim::Transform's local matrix (same memory layout as t_transform's vsg matrix).
    osg::Matrixf expectedLocal(const Anim::Transform& t)
    {
        osg::Matrixf m;
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
                m(i, j) = t.rotation[i][j] * t.scale[i];
            m(i, 3) = 0.f;
        }
        for (int j = 0; j < 3; ++j)
            m(3, j) = t.translation[j];
        m(3, 3) = 1.f;
        return m;
    }

    ::testing::AssertionResult matricesNear(const osg::Matrixf& actual, const osg::Matrixf& expected, float tolerance)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!(std::abs(actual(i, j) - expected(i, j)) <= tolerance))
                    return ::testing::AssertionFailure()
                        << "element (" << i << ", " << j << "): actual " << actual(i, j) << ", expected "
                        << expected(i, j);
        return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult vecNear(const osg::Vec3f& actual, const osg::Vec3f& expected, float tolerance)
    {
        for (int i = 0; i < 3; ++i)
            if (!(std::abs(actual[i] - expected[i]) <= tolerance))
                return ::testing::AssertionFailure()
                    << "component " << i << ": actual (" << actual.x() << ", " << actual.y() << ", " << actual.z()
                    << "), expected (" << expected.x() << ", " << expected.y() << ", " << expected.z() << ")";
        return ::testing::AssertionSuccess();
    }

    // A copy of a bone's decomposed transform (vsg objects cannot live on the stack).
    struct Snapshot
    {
        vsg::vec3 mTranslation;
        vsg::mat3 mRotation;
        vsg::vec3 mScale;

        explicit Snapshot(const Anim::Transform& t)
            : mTranslation(t.translation)
            , mRotation(t.rotation)
            , mScale(t.scale)
        {
        }
    };

    bool sameTransform(const Anim::Transform& a, const Snapshot& b)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (a.translation[i] != b.mTranslation[i] || a.scale[i] != b.mScale[i])
                return false;
            for (int j = 0; j < 3; ++j)
                if (a.rotation[i][j] != b.mRotation[i][j])
                    return false;
        }
        return true;
    }

    vsg::ref_ptr<Anim::Transform> makeBone(const std::string& name)
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        vsgUtil::setName(*t, name);
        return t;
    }

    // A three-bone chain "Bip01" -> "Bip01 Spine" -> "Bip01 Head" under a plain vsg::Group, with known locals:
    // - Bip01: translation (1, 0, 0), rotation +90 degrees about Z (x -> y, y -> -x), scale 1
    // - Bip01 Spine: translation (0, 2, 0), uniform scale 2
    // - Bip01 Head: translation (0, 0, 3)
    struct Chain
    {
        vsg::ref_ptr<vsg::Group> mScene{ new vsg::Group };
        vsg::ref_ptr<Anim::Transform> mRoot = makeBone("Bip01");
        vsg::ref_ptr<Anim::Transform> mChild = makeBone("Bip01 Spine");
        vsg::ref_ptr<Anim::Transform> mGrandchild = makeBone("Bip01 Head");

        Chain()
        {
            mRoot->translation = vsg::vec3(1.f, 0.f, 0.f);
            mRoot->rotation[0] = vsg::vec3(0.f, 1.f, 0.f);
            mRoot->rotation[1] = vsg::vec3(-1.f, 0.f, 0.f);
            mRoot->rotation[2] = vsg::vec3(0.f, 0.f, 1.f);

            mChild->translation = vsg::vec3(0.f, 2.f, 0.f);
            mChild->setScale(2.f);

            mGrandchild->translation = vsg::vec3(0.f, 0.f, 3.f);

            mChild->children.push_back(mGrandchild);
            mRoot->children.push_back(mChild);
            mScene->children.push_back(mRoot);
        }
    };

    // The actor's root transform: translation (100, 200, 300), rotation 180 degrees about Z.
    vsg::ref_ptr<Anim::Transform> makeActorRoot()
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        t->translation = vsg::vec3(100.f, 200.f, 300.f);
        t->rotation[0] = vsg::vec3(-1.f, 0.f, 0.f);
        t->rotation[1] = vsg::vec3(0.f, -1.f, 0.f);
        t->rotation[2] = vsg::vec3(0.f, 0.f, 1.f);
        return t;
    }

    using MWRender::BonePose;

    TEST(MWRenderBonePoseTest, FindReturnsHandleForExistingBone)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);

        const BonePose::Bone head = pose.find("Bip01 Head");
        ASSERT_TRUE(static_cast<bool>(head));
        EXPECT_EQ(head.mNode, chain.mGrandchild.get());
        ASSERT_EQ(head.mPath.size(), 3u);
        EXPECT_EQ(head.mPath[0], chain.mRoot.get());
        EXPECT_EQ(head.mPath[1], chain.mChild.get());
        EXPECT_EQ(head.mPath[2], chain.mGrandchild.get());

        const BonePose::Bone root = pose.find("Bip01");
        ASSERT_TRUE(static_cast<bool>(root));
        EXPECT_EQ(root.mNode, chain.mRoot.get());
        EXPECT_EQ(root.mPath.size(), 1u);
    }

    TEST(MWRenderBonePoseTest, FindIsCaseInsensitiveWithLowercaseNormalisation)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);

        const BonePose::Bone lower = pose.find("bip01 spine");
        const BonePose::Bone upper = pose.find("BIP01 SPINE");
        ASSERT_TRUE(static_cast<bool>(lower));
        ASSERT_TRUE(static_cast<bool>(upper));
        EXPECT_EQ(lower.mNode, chain.mChild.get());
        EXPECT_EQ(upper.mNode, chain.mChild.get());
    }

    TEST(MWRenderBonePoseTest, FindIsCaseSensitiveWithIdentityNormalisation)
    {
        Chain chain;
        Anim::Bones bones([](const std::string& s) { return s; }, *chain.mScene);
        const BonePose pose(bones, nullptr);

        EXPECT_TRUE(static_cast<bool>(pose.find("Bip01 Spine")));
        EXPECT_FALSE(static_cast<bool>(pose.find("bip01 spine")));
    }

    TEST(MWRenderBonePoseTest, FindReturnsEmptyHandleForMissingBone)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);

        const BonePose::Bone missing = pose.find("Bip01 Tail");
        EXPECT_FALSE(static_cast<bool>(missing));
        EXPECT_EQ(missing.mNode, nullptr);

        EXPECT_FALSE(static_cast<bool>(pose.find("")));
    }

    TEST(MWRenderBonePoseTest, EmptyHandleCallsAreHarmless)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);
        const BonePose::Bone empty;
        ASSERT_FALSE(static_cast<bool>(empty));

        const Snapshot rootBefore(*chain.mRoot);
        const Snapshot childBefore(*chain.mChild);
        const Snapshot grandchildBefore(*chain.mGrandchild);

        EXPECT_TRUE(matricesNear(pose.getLocal(empty), osg::Matrixf::identity(), 0.f));
        EXPECT_TRUE(matricesNear(pose.getModel(empty), osg::Matrixf::identity(), 0.f));
        EXPECT_TRUE(matricesNear(pose.getWorld(empty), osg::Matrixf::identity(), 0.f));

        pose.setLocal(empty, osg::Matrixf::translate(5.f, 6.f, 7.f));
        pose.setLocalRotation(empty, osg::Quat(1.f, osg::Vec3f(0.f, 0.f, 1.f)));

        EXPECT_TRUE(sameTransform(*chain.mRoot, rootBefore));
        EXPECT_TRUE(sameTransform(*chain.mChild, childBefore));
        EXPECT_TRUE(sameTransform(*chain.mGrandchild, grandchildBefore));

        // With an actor root, an empty handle still neither crashes nor writes.
        const auto actorRoot = makeActorRoot();
        const BonePose rootedPose(bones, actorRoot.get());
        (void)rootedPose.getWorld(empty);
        rootedPose.setLocal(empty, osg::Matrixf::translate(5.f, 6.f, 7.f));
        EXPECT_TRUE(sameTransform(*chain.mRoot, rootBefore));
    }

    TEST(MWRenderBonePoseTest, GetLocalMatchesTranslationRotationAndScale)
    {
        Chain chain;
        chain.mChild->setAttitude(vsg::quat(0.7f, vsg::normalize(vsg::vec3(1.f, 2.f, 3.f))));
        chain.mChild->scale = vsg::vec3(1.5f, 0.5f, 2.f);
        chain.mChild->translation = vsg::vec3(-4.f, 5.f, 6.f);

        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);

        EXPECT_TRUE(matricesNear(pose.getLocal(pose.find("Bip01")), expectedLocal(*chain.mRoot), sTolerance));
        EXPECT_TRUE(matricesNear(pose.getLocal(pose.find("Bip01 Spine")), expectedLocal(*chain.mChild), sTolerance));
        EXPECT_TRUE(
            matricesNear(pose.getLocal(pose.find("Bip01 Head")), expectedLocal(*chain.mGrandchild), sTolerance));
    }

    TEST(MWRenderBonePoseTest, SetLocalWithUniformScaleIsReadBack)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);
        const BonePose::Bone spine = pose.find("Bip01 Spine");
        ASSERT_TRUE(static_cast<bool>(spine));

        const osg::Quat rotation(0.9f, osg::Vec3f(0.f, 1.f, 1.f) / std::sqrt(2.f));
        const osg::Matrixf local = osg::Matrixf::scale(1.25f, 1.25f, 1.25f) * osg::Matrixf::rotate(rotation)
            * osg::Matrixf::translate(3.f, -2.f, 7.5f);

        pose.setLocal(spine, local);
        EXPECT_TRUE(matricesNear(pose.getLocal(spine), local, sTolerance));

        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(chain.mChild->scale[i], 1.25f, sTolerance);
        EXPECT_NEAR(chain.mChild->translation[0], 3.f, sTolerance);
        EXPECT_NEAR(chain.mChild->translation[1], -2.f, sTolerance);
        EXPECT_NEAR(chain.mChild->translation[2], 7.5f, sTolerance);
    }

    TEST(MWRenderBonePoseTest, SetLocalWithNonUniformScaleIsReadBack)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);
        const BonePose::Bone head = pose.find("Bip01 Head");
        ASSERT_TRUE(static_cast<bool>(head));

        const osg::Quat rotation(-1.3f, osg::Vec3f(1.f, -2.f, 0.5f) / osg::Vec3f(1.f, -2.f, 0.5f).length());
        const osg::Matrixf local = osg::Matrixf::scale(0.5f, 2.f, 1.5f) * osg::Matrixf::rotate(rotation)
            * osg::Matrixf::translate(-1.f, 0.25f, 4.f);

        pose.setLocal(head, local);
        EXPECT_TRUE(matricesNear(pose.getLocal(head), local, sTolerance));

        EXPECT_NEAR(chain.mGrandchild->scale[0], 0.5f, sTolerance);
        EXPECT_NEAR(chain.mGrandchild->scale[1], 2.f, sTolerance);
        EXPECT_NEAR(chain.mGrandchild->scale[2], 1.5f, sTolerance);
    }

    TEST(MWRenderBonePoseTest, SetLocalRotationKeepsTranslationAndScale)
    {
        Chain chain;
        chain.mChild->scale = vsg::vec3(1.5f, 0.5f, 2.f);
        chain.mChild->translation = vsg::vec3(-4.f, 5.f, 6.f);

        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);
        const BonePose::Bone spine = pose.find("Bip01 Spine");
        ASSERT_TRUE(static_cast<bool>(spine));

        const osg::Quat rotation(0.6f, osg::Vec3f(0.f, 0.f, 1.f));
        pose.setLocalRotation(spine, rotation);

        EXPECT_EQ(chain.mChild->translation, vsg::vec3(-4.f, 5.f, 6.f));
        EXPECT_EQ(chain.mChild->scale, vsg::vec3(1.5f, 0.5f, 2.f));

        const osg::Matrixf expected = osg::Matrixf::scale(1.5f, 0.5f, 2.f) * osg::Matrixf::rotate(rotation)
            * osg::Matrixf::translate(-4.f, 5.f, 6.f);
        EXPECT_TRUE(matricesNear(pose.getLocal(spine), expected, sTolerance));

        // A point on the local X axis turns by +0.6 rad about Z (the scale leaves the direction unchanged).
        const osg::Vec3f rotated = osg::Vec3f(1.f, 0.f, 0.f) * pose.getLocal(spine);
        EXPECT_TRUE(vecNear(rotated,
            osg::Vec3f(-4.f + 1.5f * std::cos(0.6f), 5.f + 1.5f * std::sin(0.6f), 6.f), sTolerance));
    }

    TEST(MWRenderBonePoseTest, GetModelComposesThePathInRowVectorConvention)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);
        const BonePose::Bone head = pose.find("Bip01 Head");
        ASSERT_TRUE(static_cast<bool>(head));

        // p = (1, 0, 0) in the head's frame:
        //   head  translate (0, 0, 3)            -> (1, 0, 3)
        //   spine scale 2, translate (0, 2, 0)   -> (2, 2, 6)
        //   root  rotate +90 about Z, then (1, 0, 0) -> (-2, 2, 6) -> (-1, 2, 6)
        const osg::Vec3f modelPoint = osg::Vec3f(1.f, 0.f, 0.f) * pose.getModel(head);
        EXPECT_TRUE(vecNear(modelPoint, osg::Vec3f(-1.f, 2.f, 6.f), sTolerance));

        // The origin of the head is the head's translation carried up the chain: (0,0,3) -> (0,2,6) -> (-1,0,6).
        const osg::Vec3f origin = osg::Vec3f(0.f, 0.f, 0.f) * pose.getModel(head);
        EXPECT_TRUE(vecNear(origin, osg::Vec3f(-1.f, 0.f, 6.f), sTolerance));

        const osg::Matrixf expected
            = expectedLocal(*chain.mGrandchild) * expectedLocal(*chain.mChild) * expectedLocal(*chain.mRoot);
        EXPECT_TRUE(matricesNear(pose.getModel(head), expected, sTolerance));

        // The model root's model transform is its local transform.
        const BonePose::Bone root = pose.find("Bip01");
        EXPECT_TRUE(matricesNear(pose.getModel(root), pose.getLocal(root), sTolerance));
    }

    TEST(MWRenderBonePoseTest, GetWorldAppliesTheActorRoot)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const auto actorRoot = makeActorRoot();
        const BonePose pose(bones, actorRoot.get());
        const BonePose::Bone head = pose.find("Bip01 Head");
        ASSERT_TRUE(static_cast<bool>(head));

        EXPECT_TRUE(matricesNear(pose.getRoot(), expectedLocal(*actorRoot), sTolerance));
        EXPECT_TRUE(matricesNear(pose.getWorld(head), pose.getModel(head) * pose.getRoot(), sTolerance));

        // Model point (-1, 2, 6), rotated 180 about Z -> (1, -2, 6), translated -> (101, 198, 306).
        const osg::Vec3f worldPoint = osg::Vec3f(1.f, 0.f, 0.f) * pose.getWorld(head);
        EXPECT_TRUE(vecNear(worldPoint, osg::Vec3f(101.f, 198.f, 306.f), 1e-4f));
    }

    TEST(MWRenderBonePoseTest, GetWorldEqualsModelWithoutRoot)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const BonePose pose(bones, nullptr);

        EXPECT_TRUE(matricesNear(pose.getRoot(), osg::Matrixf::identity(), 0.f));
        for (const char* name : { "Bip01", "Bip01 Spine", "Bip01 Head" })
        {
            const BonePose::Bone bone = pose.find(name);
            ASSERT_TRUE(static_cast<bool>(bone)) << name;
            EXPECT_TRUE(matricesNear(pose.getWorld(bone), pose.getModel(bone), 0.f)) << name;
        }
    }

    TEST(MWRenderBonePoseTest, ParentWriteIsVisibleInChildModel)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const auto actorRoot = makeActorRoot();
        const BonePose pose(bones, actorRoot.get());
        const BonePose::Bone spine = pose.find("Bip01 Spine");
        const BonePose::Bone head = pose.find("Bip01 Head");
        ASSERT_TRUE(static_cast<bool>(spine));
        ASSERT_TRUE(static_cast<bool>(head));

        // Replace the spine's local (was scale 2, translate (0, 2, 0)) with a pure translation (0, 0, 10).
        pose.setLocal(spine, osg::Matrixf::translate(0.f, 0.f, 10.f));

        // Head origin: (0, 0, 3) -> (0, 0, 13) -> rotate +90 about Z (unchanged) -> (1, 0, 13).
        const osg::Vec3f origin = osg::Vec3f(0.f, 0.f, 0.f) * pose.getModel(head);
        EXPECT_TRUE(vecNear(origin, osg::Vec3f(1.f, 0.f, 13.f), sTolerance));

        // And in world space: rotate 180 about Z -> (-1, 0, 13), translate -> (99, 200, 313).
        const osg::Vec3f worldOrigin = osg::Vec3f(0.f, 0.f, 0.f) * pose.getWorld(head);
        EXPECT_TRUE(vecNear(worldOrigin, osg::Vec3f(99.f, 200.f, 313.f), 1e-4f));

        // A rotation write on the root is visible too: rotating the root to identity leaves (0, 0, 13) + (1, 0, 0).
        pose.setLocalRotation(pose.find("Bip01"), osg::Quat());
        const osg::Vec3f afterRootWrite = osg::Vec3f(0.f, 0.f, 0.f) * pose.getModel(head);
        EXPECT_TRUE(vecNear(afterRootWrite, osg::Vec3f(1.f, 0.f, 13.f), sTolerance));
        const osg::Vec3f xAfterRootWrite = osg::Vec3f(1.f, 0.f, 0.f) * pose.getModel(head);
        EXPECT_TRUE(vecNear(xAfterRootWrite, osg::Vec3f(2.f, 0.f, 13.f), sTolerance));
    }

    TEST(MWRenderBonePoseTest, AllocationCounterDetectsAllocations)
    {
        // Guards the no-allocation test below: the counter must see an ordinary heap allocation.
        std::size_t count = 0;
        {
            AllocationCounter counter;
            auto* p = new int(42);
            count = counter.count();
            delete p;
        }
        EXPECT_GE(count, 1u);
    }

    TEST(MWRenderBonePoseTest, PerFrameCallsDoNotAllocateAfterFind)
    {
        Chain chain;
        Anim::Bones bones = MWAnim::Bones(*chain.mScene);
        const auto actorRoot = makeActorRoot();
        const BonePose pose(bones, actorRoot.get());
        const BonePose::Bone spine = pose.find("Bip01 Spine");
        const BonePose::Bone head = pose.find("Bip01 Head");
        ASSERT_TRUE(static_cast<bool>(spine));
        ASSERT_TRUE(static_cast<bool>(head));

        const osg::Matrixf newLocal = osg::Matrixf::scale(0.5f, 2.f, 1.5f)
            * osg::Matrixf::rotate(osg::Quat(0.4f, osg::Vec3f(1.f, 0.f, 0.f))) * osg::Matrixf::translate(1.f, 2.f, 3.f);
        const osg::Quat newRotation(0.3f, osg::Vec3f(0.f, 1.f, 0.f));

        float sink = 0.f;
        std::size_t allocations = 0;
        {
            AllocationCounter counter;
            for (int frame = 0; frame < 16; ++frame)
            {
                const osg::Matrixf local = pose.getLocal(head);
                pose.setLocal(spine, newLocal);
                pose.setLocalRotation(head, newRotation);
                const osg::Matrixf model = pose.getModel(head);
                const osg::Matrixf world = pose.getWorld(head);
                const osg::Matrixf root = pose.getRoot();
                sink += local(3, 0) + model(3, 1) + world(3, 2) + root(3, 0);
            }
            allocations = counter.count();
        }
        EXPECT_EQ(allocations, 0u);
        EXPECT_TRUE(std::isfinite(sink));
    }

    // Task 0015: with zero plugins the engine's phase calls cost nothing. Here because this file owns the counter.
    TEST(PluginHostAllocationTest, runPhaseAllocatesNothingWithZeroPlugins)
    {
        OMW::PluginHost host(OMW::PluginHost::Config{});
        host.start();
        std::size_t allocations = 0;
        {
            AllocationCounter counter;
            for (int frame = 0; frame < 100; ++frame)
                host.runPhase(openeden::sdk::Phase::Input, 0.016f);
            allocations = counter.count();
        }
        EXPECT_EQ(allocations, 0u);
    }
}
