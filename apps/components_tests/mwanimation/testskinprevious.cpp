// Tests for Anim::Skin's previous-frame matrices (task 0026's motion output, task 0069 stage e review finding 5): a
// bone matrix array twice as long as the skin's bones holds this frame's matrices and the previous frame's. A skin
// that did not run for a while forgets its previous matrices, so its first frame has no motion instead of the motion
// from the pose of when it stopped. A one-bone skeleton built from plain nodes, no device.

#include <components/animation/bones.hpp>
#include <components/animation/context.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/core/Array.h>

#include <gtest/gtest.h>

namespace
{
    struct OneBoneSkin
    {
        vsg::ref_ptr<Anim::Transform> mRoot = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> mBone = Anim::Transform::create();
        std::unique_ptr<Anim::Bones> mBones;
        vsg::ref_ptr<Anim::Skin> mSkin = Anim::Skin::create();
        vsg::ref_ptr<vsg::mat4Array> mMatrices = vsg::mat4Array::create(2); // this frame's, then the previous one

        OneBoneSkin()
        {
            vsgUtil::setName(*mRoot, "Root");
            vsgUtil::setName(*mBone, "Bone");
            mRoot->addChild(mBone);
            mBones = std::make_unique<Anim::Bones>([](const std::string& name) { return name; }, *mRoot);
            mSkin->bones = { Anim::BoneData{ vsg::mat4(), {}, "Bone" } };
            Anim::Context context{ {}, { mRoot.get() }, mBones.get(), nullptr };
            mSkin->link(context, *mMatrices);
        }

        float current() const { return (*mMatrices)[0][3][0]; }
        float previous() const { return (*mMatrices)[1][3][0]; }

        void frame(float x)
        {
            mBone->translation = { x, 0.f, 0.f };
            mSkin->apply(*mMatrices, 0.f);
        }
    };

    TEST(SkinPreviousMatrices, the_first_frame_has_no_motion_and_later_frames_keep_the_last_one)
    {
        OneBoneSkin skin;
        skin.frame(1.f);
        EXPECT_FLOAT_EQ(skin.current(), 1.f);
        EXPECT_FLOAT_EQ(skin.previous(), 1.f);
        skin.frame(2.f);
        EXPECT_FLOAT_EQ(skin.current(), 2.f);
        EXPECT_FLOAT_EQ(skin.previous(), 1.f);
    }

    TEST(SkinPreviousMatrices, a_skin_that_forgets_its_previous_matrices_starts_again_without_motion)
    {
        OneBoneSkin skin;
        skin.frame(1.f);
        skin.frame(2.f);
        // The skin stops running (its draw is hidden) while the bone moves on; then it runs again.
        skin.mSkin->forgetPreviousMatrices();
        skin.frame(9.f);
        EXPECT_FLOAT_EQ(skin.current(), 9.f);
        EXPECT_FLOAT_EQ(skin.previous(), 9.f); // not 2: no motion from the pose of when it stopped
        skin.frame(10.f);
        EXPECT_FLOAT_EQ(skin.previous(), 9.f);
    }
}
