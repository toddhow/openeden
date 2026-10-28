#include <gtest/gtest.h>

#include <apps/openmw/mwrender/creativemotiondriver.hpp>

#include <components/misc/constants.hpp>
#include <components/nifosg/controller.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/keyframe.hpp>
#include <components/settings/values.hpp>

#include <osg/Group>

#include <cmath>
#include <memory>
#include <vector>

namespace
{
    // Helper to create a synthetic MatrixTransform bone
    osg::ref_ptr<NifOsg::MatrixTransform> createBoneNode(const std::string& name, const osg::Vec3f& trans,
        const osg::Quat& rot = osg::Quat(0, 0, 0, 1))
    {
        osg::ref_ptr<NifOsg::MatrixTransform> node = new NifOsg::MatrixTransform;
        node->setName(name);
        node->setTranslation(trans);
        node->setRotation(rot);
        return node;
    }

    // Helper to construct a synthetic humanoid bone tree
    struct SyntheticHumanoidRig
    {
        osg::ref_ptr<osg::Group> root;
        MWRender::CreativeMotionPoseDriver::NodeMap nodeMap;

        osg::ref_ptr<NifOsg::MatrixTransform> bip01;
        osg::ref_ptr<NifOsg::MatrixTransform> pelvis;
        osg::ref_ptr<NifOsg::MatrixTransform> spine1;
        osg::ref_ptr<NifOsg::MatrixTransform> neck;
        osg::ref_ptr<NifOsg::MatrixTransform> head;
        osg::ref_ptr<NifOsg::MatrixTransform> lClavicle;
        osg::ref_ptr<NifOsg::MatrixTransform> lUpperArm;
        osg::ref_ptr<NifOsg::MatrixTransform> rClavicle;
        osg::ref_ptr<NifOsg::MatrixTransform> rUpperArm;
        osg::ref_ptr<NifOsg::MatrixTransform> lThigh;
        osg::ref_ptr<NifOsg::MatrixTransform> rThigh;

        SyntheticHumanoidRig()
        {
            root = new osg::Group;

            bip01 = createBoneNode("Bip01", osg::Vec3f(0.f, 0.f, 0.f));
            pelvis = createBoneNode("Bip01 Pelvis", osg::Vec3f(0.f, 0.f, 66.5f));
            spine1 = createBoneNode("Bip01 Spine1", osg::Vec3f(0.f, 0.f, 8.4f));
            neck = createBoneNode("Bip01 Neck", osg::Vec3f(0.f, 0.f, 15.4f));
            head = createBoneNode("Bip01 Head", osg::Vec3f(0.f, 0.f, 4.9f));

            lClavicle = createBoneNode("Bip01 L Clavicle", osg::Vec3f(-5.f, 0.f, 12.f));
            lUpperArm = createBoneNode("Bip01 L UpperArm", osg::Vec3f(-14.f, 0.f, 0.f));

            rClavicle = createBoneNode("Bip01 R Clavicle", osg::Vec3f(5.f, 0.f, 12.f));
            rUpperArm = createBoneNode("Bip01 R UpperArm", osg::Vec3f(14.f, 0.f, 0.f));

            lThigh = createBoneNode("Bip01 L Thigh", osg::Vec3f(-6.3f, 0.f, -1.f));
            rThigh = createBoneNode("Bip01 R Thigh", osg::Vec3f(6.3f, 0.f, -1.f));

            // Hierarchy
            root->addChild(bip01);
            bip01->addChild(pelvis);
            pelvis->addChild(spine1);
            pelvis->addChild(lThigh);
            pelvis->addChild(rThigh);

            spine1->addChild(neck);
            neck->addChild(head);

            spine1->addChild(lClavicle);
            lClavicle->addChild(lUpperArm);

            spine1->addChild(rClavicle);
            rClavicle->addChild(rUpperArm);

            // Populate nodeMap
            auto addMap = [&](const osg::ref_ptr<NifOsg::MatrixTransform>& n) {
                nodeMap[Misc::StringUtils::lowerCase(n->getName())] = n;
            };

            addMap(bip01);
            addMap(pelvis);
            addMap(spine1);
            addMap(neck);
            addMap(head);
            addMap(lClavicle);
            addMap(lUpperArm);
            addMap(rClavicle);
            addMap(rUpperArm);
            addMap(lThigh);
            addMap(rThigh);
        }
    };
}

TEST(CreativeMotionRuntimeTest, SettingsDefaults)
{
    // Verification that the setting is off by default preserving bit-identical OpenMW execution
    EXPECT_FALSE(Settings::game().mCreativeMotionAnimation.get());
    EXPECT_NEAR(Settings::game().mCreativeMotionAnimationTransition.get(), 0.25f, 1e-4f);
}

TEST(CreativeMotionRuntimeTest, SkeletonDiscoveryAndHierarchyValidation)
{
    SyntheticHumanoidRig rig;
    MWRender::CreativeMotionPoseDriver driver(rig.nodeMap, rig.root.get());

    ASSERT_TRUE(driver.isValid());
    const auto& skel = driver.getSkeleton();
    EXPECT_EQ(skel.bones.size(), 11);

    // Verify all bones can be looked up by lowercase name
    EXPECT_GE(driver.getBoneIndex("bip01"), 0);
    EXPECT_GE(driver.getBoneIndex("bip01 pelvis"), 0);
    EXPECT_GE(driver.getBoneIndex("bip01 spine1"), 0);
    EXPECT_GE(driver.getBoneIndex("bip01 head"), 0);
    EXPECT_GE(driver.getBoneIndex("bip01 l clavicle"), 0);
    EXPECT_GE(driver.getBoneIndex("bip01 r clavicle"), 0);

    // Verify parent relationships
    int bip01Idx = driver.getBoneIndex("bip01");
    int pelvisIdx = driver.getBoneIndex("bip01 pelvis");
    int spineIdx = driver.getBoneIndex("bip01 spine1");

    EXPECT_EQ(skel.bones[bip01Idx].parent, -1);
    EXPECT_EQ(skel.bones[pelvisIdx].parent, bip01Idx);
    EXPECT_EQ(skel.bones[spineIdx].parent, pelvisIdx);

    // Ancestor order invariant: parent must precede child
    for (size_t i = 0; i < skel.bones.size(); ++i)
    {
        if (skel.bones[i].parent >= 0)
        {
            EXPECT_LT(skel.bones[i].parent, static_cast<int>(i));
        }
    }
}

TEST(CreativeMotionRuntimeTest, PoseSamplingAndLayeringAcrossMasks)
{
    SyntheticHumanoidRig rig;
    MWRender::CreativeMotionPoseDriver driver(rig.nodeMap, rig.root.get());
    ASSERT_TRUE(driver.isValid());

    // Construct synthetic KeyframeHolder for lower body (Mask 0)
    osg::ref_ptr<SceneUtil::KeyframeHolder> holder = new SceneUtil::KeyframeHolder;
    osg::ref_ptr<NifOsg::KeyframeController> ctrlPelvis = new NifOsg::KeyframeController;
    ctrlPelvis->setName("Bip01 Pelvis");
    holder->mKeyframeControllers["Bip01 Pelvis"] = ctrlPelvis;

    // Register and update
    std::array<std::shared_ptr<float>, MWRender::sNumBlendMasks> timePtrs = {
        std::make_shared<float>(0.5f),
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f)
    };

    std::array<const SceneUtil::KeyframeHolder*, MWRender::sNumBlendMasks> activeHolders = {
        holder.get(), nullptr, nullptr, nullptr
    };

    driver.update(0.016f, timePtrs, activeHolders);

    int pelvisIdx = driver.getBoneIndex("bip01 pelvis");
    ASSERT_GE(pelvisIdx, 0);

    // Transforms should be valid, finite numbers
    const osg::Vec3f& trans = driver.getBoneTranslation(pelvisIdx);
    EXPECT_TRUE(std::isfinite(trans.x()));
    EXPECT_TRUE(std::isfinite(trans.y()));
    EXPECT_TRUE(std::isfinite(trans.z()));

    const osg::Quat& rot = driver.getBoneRotation(pelvisIdx);
    EXPECT_TRUE(std::isfinite(rot.x()));
    EXPECT_TRUE(std::isfinite(rot.y()));
    EXPECT_TRUE(std::isfinite(rot.z()));
    EXPECT_TRUE(std::isfinite(rot.w()));
    EXPECT_NEAR(rot.length(), 1.0f, 1e-4f);
}

TEST(CreativeMotionRuntimeTest, InertializationNoDiscontinuityOnStateSwitch)
{
    SyntheticHumanoidRig rig;
    MWRender::CreativeMotionPoseDriver driver(rig.nodeMap, rig.root.get());
    ASSERT_TRUE(driver.isValid());

    osg::ref_ptr<SceneUtil::KeyframeHolder> holder1 = new SceneUtil::KeyframeHolder;
    osg::ref_ptr<SceneUtil::KeyframeHolder> holder2 = new SceneUtil::KeyframeHolder;

    std::array<std::shared_ptr<float>, MWRender::sNumBlendMasks> timePtrs = {
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f)
    };

    std::array<const SceneUtil::KeyframeHolder*, MWRender::sNumBlendMasks> holders = {
        holder1.get(), nullptr, nullptr, nullptr
    };

    // Frame 1 with state 1
    driver.update(0.016f, timePtrs, holders);
    int headIdx = driver.getBoneIndex("bip01 head");
    osg::Quat rotBefore = driver.getBoneRotation(headIdx);

    // Switch to state 2 with 0.25s inertialization
    driver.onActiveStateChanged(0, "runforward", holder2.get(), 0.0f, 0.25f);
    holders[0] = holder2.get();

    // Frame 2 right after switch
    driver.update(0.016f, timePtrs, holders);
    osg::Quat rotAfter = driver.getBoneRotation(headIdx);

    // Check no sudden pop: rotation should be continuous
    double diff = std::abs(rotBefore.x() - rotAfter.x()) +
                  std::abs(rotBefore.y() - rotAfter.y()) +
                  std::abs(rotBefore.z() - rotAfter.z()) +
                  std::abs(rotBefore.w() - rotAfter.w());
    EXPECT_LT(diff, 0.01);
}

TEST(CreativeMotionRuntimeTest, AccumulationRootResetPreserved)
{
    SyntheticHumanoidRig rig;
    MWRender::CreativeMotionPoseDriver driver(rig.nodeMap, rig.root.get());
    ASSERT_TRUE(driver.isValid());

    int bip01Idx = driver.getBoneIndex("bip01");
    ASSERT_GE(bip01Idx, 0);

    osg::ref_ptr<MWRender::CreativeMotionPoseWriterCallback> poseWriter =
        new MWRender::CreativeMotionPoseWriterCallback(&driver, static_cast<size_t>(bip01Idx));

    class DummyResetCallback : public SceneUtil::NodeCallback<DummyResetCallback, osg::MatrixTransform*>
    {
    public:
        void operator()(osg::MatrixTransform* transform, osg::NodeVisitor* nv)
        {
            osg::Matrix mat = transform->getMatrix();
            osg::Vec3f pos = mat.getTrans();
            pos.x() = 0.f;
            pos.y() = 0.f;
            mat.setTrans(pos);
            transform->setMatrix(mat);
            traverse(transform, nv);
        }
    };

    osg::ref_ptr<DummyResetCallback> resetCb = new DummyResetCallback;
    rig.bip01->addUpdateCallback(poseWriter);
    rig.bip01->addUpdateCallback(resetCb);

    osg::NodeVisitor nv(osg::NodeVisitor::UPDATE_VISITOR);
    (*poseWriter)(rig.bip01.get(), &nv);

    // Verification: Bip01 X and Y are reset to 0 by the nested reset callback
    osg::Vec3f trans = rig.bip01->getMatrix().getTrans();
    EXPECT_NEAR(trans.x(), 0.0f, 1e-4f);
    EXPECT_NEAR(trans.y(), 0.0f, 1e-4f);
}

TEST(CreativeMotionRuntimeTest, SkeletonDiscoverySkipsUnnamedNodes)
{
    SyntheticHumanoidRig rig;

    // Insert an unnamed intermediate transform node (simulating unnamed anchor in NIF)
    osg::ref_ptr<NifOsg::MatrixTransform> unnamedNode = new NifOsg::MatrixTransform;
    unnamedNode->setName(""); // empty name!
    unnamedNode->setTranslation(osg::Vec3f(0.f, 0.f, 0.f));

    osg::ref_ptr<NifOsg::MatrixTransform> attachmentNode = createBoneNode("accessory_bone", osg::Vec3f(1.f, 2.f, 3.f));

    rig.head->addChild(unnamedNode);
    unnamedNode->addChild(attachmentNode);

    // Node map contains accessory_bone and possibly an empty entry
    rig.nodeMap["accessory_bone"] = attachmentNode;
    rig.nodeMap[""] = unnamedNode;

    MWRender::CreativeMotionPoseDriver driver(rig.nodeMap, rig.root.get());
    EXPECT_TRUE(driver.isValid());

    const auto& skel = driver.getSkeleton();
    // Rig should have 12 bones (11 original + accessory_bone), unnamedNode skipped
    EXPECT_EQ(skel.bones.size(), 12);
    EXPECT_EQ(driver.getBoneIndex(""), -1);

    int headIdx = driver.getBoneIndex("bip01 head");
    int attachIdx = driver.getBoneIndex("accessory_bone");
    ASSERT_GE(headIdx, 0);
    ASSERT_GE(attachIdx, 0);

    // attachmentNode's parent should skip the unnamed node and point directly to head
    EXPECT_EQ(skel.bones[attachIdx].parent, headIdx);
}

TEST(CreativeMotionRuntimeTest, InertializationTriggersOnGroupNameChangeWithSameHolder)
{
    SyntheticHumanoidRig rig;
    MWRender::CreativeMotionPoseDriver driver(rig.nodeMap, rig.root.get());
    ASSERT_TRUE(driver.isValid());

    // Both animations are in the same KeyframeHolder (vanilla xbase_anim.nif pattern)
    osg::ref_ptr<SceneUtil::KeyframeHolder> sharedHolder = new SceneUtil::KeyframeHolder;

    std::array<std::shared_ptr<float>, MWRender::sNumBlendMasks> timePtrs = {
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f),
        std::make_shared<float>(0.0f)
    };

    std::array<const SceneUtil::KeyframeHolder*, MWRender::sNumBlendMasks> holders = {
        sharedHolder.get(), nullptr, nullptr, nullptr
    };

    // Initial state: walkforward
    driver.onActiveStateChanged(0, "walkforward", sharedHolder.get(), 0.0f, 0.25f);
    driver.update(0.016f, timePtrs, holders);

    // Transition to runforward with the same sharedHolder pointer
    driver.onActiveStateChanged(0, "runforward", sharedHolder.get(), 0.0f, 0.25f);

    // Frame after transition: should not crash or pop
    driver.update(0.016f, timePtrs, holders);

    int headIdx = driver.getBoneIndex("bip01 head");
    ASSERT_GE(headIdx, 0);
    const osg::Quat& rot = driver.getBoneRotation(headIdx);
    EXPECT_TRUE(std::isfinite(rot.x()));
    EXPECT_TRUE(std::isfinite(rot.y()));
    EXPECT_TRUE(std::isfinite(rot.z()));
    EXPECT_TRUE(std::isfinite(rot.w()));
}
