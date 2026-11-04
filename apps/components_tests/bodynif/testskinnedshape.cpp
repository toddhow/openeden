// Task 0069 (stage a): reading the skinned shapes of a body part NIF into plain arrays. The models are built by hand
// from Nif records in memory; no files, no VFS, no game data, no GPU. The cases come from the spec's list: one
// skinned shape, two shapes with names for the skin filter, a rigid shape, and hostile input refused with located
// errors (zero bones, weights for a missing vertex, huge counts).
#include <gtest/gtest.h>

#include <components/bodynif/skinnedshape.hpp>

#include <components/animation/skin.hpp>
#include <components/animation/softwareskin.hpp>
#include <components/animation/transform.hpp>
#include <components/nif/data.hpp>
#include <components/nif/node.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>
#include <components/vsgadapters/nif/anim.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace BodyNif;

    constexpr float tolerance = 1e-5f;

    using WeightList = std::vector<std::pair<unsigned short, float>>;

    // Owns every record of one hand-made model. Every record pointer a reader could follow is set (to null where
    // there is nothing), because a default RecordPtrT is not readable in debug builds.
    class Model
    {
    public:
        Nif::NiNode& node(const std::string& name)
        {
            Nif::NiNode& result = create<Nif::NiNode>(Nif::RC_NiNode);
            result.mName = name;
            return result;
        }

        void attach(Nif::NiNode& parent, Nif::NiAVObject& child)
        {
            parent.mChildren.push_back(Nif::NiAVObjectPtr(&child));
            child.mParents.push_back(&parent);
        }

        void setTransform(Nif::NiAVObject& object, float scale, const vsg::vec3& translation)
        {
            object.mTransform.mScale = scale;
            object.mTransform.mTranslation = translation;
        }

        void mirrorX(Nif::NiAVObject& object) { object.mTransform.mRotation.mValues[0][0] = -1.f; }

        // One shape with the given vertices and triangles (a triangle list), a name, and no skin.
        Nif::NiGeometry& shape(Nif::NiNode& parent, const std::string& name, const std::vector<vsg::vec3>& vertices,
            const std::vector<unsigned short>& triangles)
        {
            Nif::NiGeometry& result = create<Nif::NiTriShape>(Nif::RC_NiTriShape);
            result.mName = name;
            Nif::NiTriShapeData& data = create<Nif::NiTriShapeData>(Nif::RC_NiTriShapeData);
            data.mNumVertices = static_cast<std::uint16_t>(vertices.size());
            data.mVertices = vertices;
            data.mTriangles = triangles;
            data.mNumTriangles = static_cast<std::uint16_t>(triangles.size() / 3);
            result.mData = Nif::NiGeometryDataPtr(&data);
            attach(parent, result);
            return result;
        }

        Nif::NiGeometry& stripShape(Nif::NiNode& parent, const std::string& name,
            const std::vector<vsg::vec3>& vertices, const std::vector<std::vector<unsigned short>>& strips)
        {
            Nif::NiGeometry& result = create<Nif::NiTriStrips>(Nif::RC_NiTriStrips);
            result.mName = name;
            Nif::NiTriStripsData& data = create<Nif::NiTriStripsData>(Nif::RC_NiTriStripsData);
            data.mNumVertices = static_cast<std::uint16_t>(vertices.size());
            data.mVertices = vertices;
            data.mStrips = strips;
            result.mData = Nif::NiGeometryDataPtr(&data);
            attach(parent, result);
            return result;
        }

        // Gives the shape a skin over the named bones (created as plain nodes) with one weight list per bone.
        Nif::NiSkinInstance& skin(Nif::NiGeometry& geometry, const std::vector<std::string>& boneNames,
            const std::vector<WeightList>& weights)
        {
            Nif::NiSkinInstance& instance = create<Nif::NiSkinInstance>(Nif::RC_NiSkinInstance);
            Nif::NiSkinData& data = create<Nif::NiSkinData>(Nif::RC_NiSkinData);
            data.mTransform = Nif::NiTransform::getIdentity();
            data.mPartitions = Nif::NiSkinPartitionPtr(nullptr);
            for (std::size_t i = 0; i < boneNames.size(); ++i)
            {
                Nif::NiNode& bone = node(boneNames[i]);
                instance.mBones.push_back(Nif::NiAVObjectPtr(&bone));
                Nif::NiSkinData::BoneInfo info;
                info.mTransform = Nif::NiTransform::getIdentity();
                if (i < weights.size())
                    info.mWeights = weights[i];
                data.mBones.push_back(std::move(info));
            }
            instance.mData = Nif::NiSkinDataPtr(&data);
            geometry.mSkin = Nif::NiSkinInstancePtr(&instance);
            return instance;
        }

        void setTexture(Nif::NiAVObject& object, const std::string& file)
        {
            Nif::NiSourceTexture& source = create<Nif::NiSourceTexture>(Nif::RC_NiSourceTexture);
            source.mExternal = 1;
            source.mFile = file;
            Nif::NiTexturingProperty& property = create<Nif::NiTexturingProperty>(Nif::RC_NiTexturingProperty);
            property.mTextures.resize(7);
            for (Nif::NiTexturingProperty::Texture& texture : property.mTextures)
            {
                texture.mEnabled = false;
                texture.mSourceTexture = Nif::NiSourceTexturePtr(nullptr);
                texture.mClamp = 3;
                texture.mFilter = 0;
                texture.mMaxAnisotropy = 1;
                texture.mUVSet = 0;
                texture.mHasTransform = false;
            }
            Nif::NiTexturingProperty::Texture& base = property.mTextures[Nif::NiTexturingProperty::BaseTexture];
            base.mEnabled = true;
            base.mSourceTexture = Nif::NiSourceTexturePtr(&source);
            object.mProperties.push_back(Nif::RecordPtrT<Nif::NiProperty>(&property));
        }

        void setAlphaTest(Nif::NiGeometry& geometry, std::uint8_t threshold)
        {
            Nif::NiAlphaProperty& property = create<Nif::NiAlphaProperty>(Nif::RC_NiAlphaProperty);
            property.mFlags = Nif::NiAlphaProperty::Flag_Testing;
            property.mThreshold = threshold;
            geometry.mProperties.push_back(Nif::RecordPtrT<Nif::NiProperty>(&property));
        }

        Nif::NiSkinInstance& skinOf(Nif::NiGeometry& geometry) { return *geometry.mSkin.getPtr(); }

        template <class T>
        T& create(Nif::RecordType type)
        {
            std::unique_ptr<T> owned = std::make_unique<T>();
            T& result = *owned;
            result.mRecordType = type;
            if constexpr (std::is_base_of_v<Nif::NiObjectNET, T>)
            {
                result.mExtra = Nif::ExtraPtr(nullptr);
                result.mController = Nif::NiTimeControllerPtr(nullptr);
            }
            if constexpr (std::is_base_of_v<Nif::NiAVObject, T>)
            {
                result.mFlags = 0;
                result.mTransform.mScale = 1.f;
                result.mCollision = Nif::NiCollisionObjectPtr(nullptr);
            }
            if constexpr (std::is_base_of_v<Nif::NiGeometry, T>)
            {
                result.mData = Nif::NiGeometryDataPtr(nullptr);
                result.mSkin = Nif::NiSkinInstancePtr(nullptr);
                result.mShaderProperty = Nif::BSShaderPropertyPtr(nullptr);
                result.mAlphaProperty = Nif::NiAlphaPropertyPtr(nullptr);
            }
            if constexpr (std::is_same_v<T, Nif::NiSourceTexture>)
            {
                result.mData = Nif::NiPixelDataPtr(nullptr);
            }
            if constexpr (std::is_same_v<T, Nif::NiSkinInstance>)
            {
                result.mData = Nif::NiSkinDataPtr(nullptr);
                result.mPartitions = Nif::NiSkinPartitionPtr(nullptr);
                result.mRoot = Nif::NiAVObjectPtr(nullptr);
            }
            mOwned.push_back(std::move(owned));
            return result;
        }

    private:
        std::vector<std::unique_ptr<Nif::Record>> mOwned;
    };

    const std::vector<vsg::vec3> triangleVertices = { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) };
    const std::vector<unsigned short> oneTriangle = { 0, 1, 2 };

    bool anyErrorContains(const ReadResult& result, const std::string& text)
    {
        for (const std::string& error : result.mErrors)
        {
            if (error.find(text) != std::string::npos)
                return true;
        }
        return false;
    }

    // ---- the skin filter -----------------------------------------------------------------------------------------

    TEST(BodyNifFilter, MatchesTheNameOrTriNameIgnoringCase)
    {
        EXPECT_TRUE(filterMatches("Right Hand", "Right Hand"));
        EXPECT_TRUE(filterMatches("right hand", "Right Hand"));
        EXPECT_TRUE(filterMatches("Tri Right Hand", "Right Hand"));
        EXPECT_TRUE(filterMatches("TRI RIGHT HAND 01", "Right Hand"));
        EXPECT_TRUE(filterMatches("Right Hand 2", "Right Hand")); // a prefix match, as the engine's
        EXPECT_FALSE(filterMatches("Left Hand", "Right Hand"));
        EXPECT_FALSE(filterMatches("Tri Left Hand", "Right Hand"));
        EXPECT_FALSE(filterMatches("Hand", "Right Hand"));
        EXPECT_FALSE(filterMatches("", "Right Hand"));
        EXPECT_TRUE(filterMatches("anything", ""));
        EXPECT_TRUE(filterMatches("", ""));
    }

    // ---- one skinned shape ---------------------------------------------------------------------------------------

    TEST(BodyNifReader, ReadsOneSkinnedShape)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& chest = model.node("Chest");
        model.attach(root, chest);
        Nif::NiGeometry& geometry = model.shape(chest, "Tri Chest 0", triangleVertices, oneTriangle);
        model.skin(geometry, { "Bip01 Spine1", "Bip01 Spine2" },
            { { { 0, 0.75f }, { 1, 1.0f } }, { { 0, 0.25f }, { 2, 1.0f } } });
        model.setTexture(geometry, "tx_chest.tga");

        const ReadResult result = readShapes(root, "Chest");

        EXPECT_TRUE(result.mErrors.empty());
        EXPECT_TRUE(result.mSkinning);
        ASSERT_EQ(result.mShapes.size(), 1u);
        const Shape& shape = result.mShapes[0];
        EXPECT_EQ(shape.mName, "Tri Chest 0");
        EXPECT_TRUE(shape.mSkinned);
        EXPECT_EQ(shape.mTexture, "tx_chest.tga");
        EXPECT_EQ(shape.mPositions.size(), 3u);
        EXPECT_EQ(shape.mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2 }));
        ASSERT_EQ(shape.mBones.size(), 2u);
        EXPECT_EQ(shape.mBones[0], "Bip01 Spine1");
        EXPECT_EQ(shape.mBones[1], "Bip01 Spine2");
        ASSERT_EQ(shape.mSkin.size(), 3u);
        // vertex 0: bone 0 at 0.75 and bone 1 at 0.25
        EXPECT_EQ(shape.mSkin[0].mCount, 2);
        EXPECT_EQ(shape.mSkin[0].mInfluences[0].mBone, 0);
        EXPECT_NEAR(shape.mSkin[0].mInfluences[0].mWeight, 0.75f, tolerance);
        EXPECT_EQ(shape.mSkin[0].mInfluences[1].mBone, 1);
        EXPECT_NEAR(shape.mSkin[0].mInfluences[1].mWeight, 0.25f, tolerance);
        // vertex 1: bone 0 only; vertex 2: bone 1 only
        EXPECT_EQ(shape.mSkin[1].mCount, 1);
        EXPECT_EQ(shape.mSkin[2].mCount, 1);
        EXPECT_EQ(shape.mSkin[2].mInfluences[0].mBone, 1);
        EXPECT_TRUE(shape.mIdentityTransform);
        EXPECT_FALSE(shape.mMirrored);
    }

    // ---- two shapes for the skin filter --------------------------------------------------------------------------

    class TwoHands : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            mRoot = &mModel.node("Bip01");
            Nif::NiNode& right = mModel.node("Tri Right Hand");
            Nif::NiNode& left = mModel.node("Tri Left Hand");
            mModel.attach(*mRoot, right);
            mModel.attach(*mRoot, left);
            Nif::NiGeometry& rightShape = mModel.shape(right, "Right Hand shape", triangleVertices, oneTriangle);
            Nif::NiGeometry& leftShape = mModel.shape(left, "Left Hand shape", triangleVertices, oneTriangle);
            mModel.skin(rightShape, { "Right Hand" }, { { { 0, 1.f }, { 1, 1.f }, { 2, 1.f } } });
            mModel.skin(leftShape, { "Left Hand" }, { { { 0, 1.f }, { 1, 1.f }, { 2, 1.f } } });
        }

        Model mModel;
        Nif::NiNode* mRoot = nullptr;
    };

    TEST_F(TwoHands, TheFilterSelectsOneSide)
    {
        const ReadResult right = readShapes(*mRoot, "Right Hand");
        ASSERT_EQ(right.mShapes.size(), 1u);
        EXPECT_EQ(right.mShapes[0].mName, "Right Hand shape");
        const ReadResult left = readShapes(*mRoot, "Left Hand");
        ASSERT_EQ(left.mShapes.size(), 1u);
        EXPECT_EQ(left.mShapes[0].mName, "Left Hand shape");
    }

    TEST_F(TwoHands, ANoMatchFilterGivesNothingAndNoError)
    {
        const ReadResult none = readShapes(*mRoot, "Neck");
        EXPECT_TRUE(none.mShapes.empty());
        EXPECT_TRUE(none.mErrors.empty());
    }

    TEST_F(TwoHands, AnEmptyFilterReadsBothInTreeOrder)
    {
        const ReadResult both = readShapes(*mRoot, "");
        ASSERT_EQ(both.mShapes.size(), 2u);
        EXPECT_EQ(both.mShapes[0].mName, "Right Hand shape");
        EXPECT_EQ(both.mShapes[1].mName, "Left Hand shape");
    }

    TEST(BodyNifReader, TheFilterIsIgnoredWhenNothingIsSkinned)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& right = model.node("Tri Right Hand");
        Nif::NiNode& left = model.node("Tri Left Hand");
        model.attach(root, right);
        model.attach(root, left);
        model.shape(right, "Right rigid", triangleVertices, oneTriangle);
        model.shape(left, "Left rigid", triangleVertices, oneTriangle);

        const ReadResult result = readShapes(root, "Right Hand");

        EXPECT_FALSE(result.mSkinning);
        ASSERT_EQ(result.mShapes.size(), 2u); // as the engine: the filter only works on a skinned file
        EXPECT_FALSE(result.mShapes[0].mSkinned);
        EXPECT_TRUE(result.mShapes[0].mBones.empty());
        EXPECT_TRUE(result.mShapes[0].mSkin.empty());
    }

    // ---- transforms ----------------------------------------------------------------------------------------------

    TEST(BodyNifReader, FoldsTheNodeTransformsFromTheMatchedNodeDown)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        model.setTransform(root, 10.f, vsg::vec3(100, 100, 100)); // an ancestor: dropped, as the engine does
        Nif::NiNode& chest = model.node("Chest");
        model.setTransform(chest, 2.f, vsg::vec3(1, 2, 3));
        model.attach(root, chest);
        Nif::NiGeometry& geometry = model.shape(
            chest, "Tri Chest", { vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(0, 0, 1) }, oneTriangle);
        model.skin(geometry, { "Bip01" }, { { { 0, 1.f }, { 1, 1.f }, { 2, 1.f } } });

        const ReadResult result = readShapes(root, "Chest");

        ASSERT_EQ(result.mShapes.size(), 1u);
        const Shape& shape = result.mShapes[0];
        EXPECT_NEAR(shape.mPositions[0][0], 3.f, tolerance); // 1 * 2 + 1
        EXPECT_NEAR(shape.mPositions[0][1], 2.f, tolerance);
        EXPECT_NEAR(shape.mPositions[0][2], 3.f, tolerance);
        EXPECT_NEAR(shape.mPositions[2][2], 5.f, tolerance); // 1 * 2 + 3
        EXPECT_FALSE(shape.mIdentityTransform);
        EXPECT_FALSE(shape.mMirrored);
    }

    TEST(BodyNifReader, FlagsAMirroredNodeChain)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        model.mirrorX(root);
        model.shape(root, "Mirrored", triangleVertices, oneTriangle);

        const ReadResult result = readShapes(root, "");

        ASSERT_EQ(result.mShapes.size(), 1u);
        EXPECT_TRUE(result.mShapes[0].mMirrored);
        EXPECT_FALSE(result.mShapes[0].mIdentityTransform);
        EXPECT_NEAR(result.mShapes[0].mPositions[1][0], -1.f, tolerance);
    }

    // ---- the rest pose and placing rigid parts -------------------------------------------------------------------

    // Bip01 (0,0,10) > Chest (1,0,0) scale 2 > Right Hand (0,3,0): the hand's world matrix is scale 2, translation
    // (1,0,10) + 2 * (0,3,0) = (1,6,10).
    Nif::NiNode& buildSkeleton(Model& model)
    {
        Nif::NiNode& root = model.node("Bip01");
        model.setTransform(root, 1.f, vsg::vec3(0, 0, 10));
        Nif::NiNode& chest = model.node("Chest");
        model.setTransform(chest, 2.f, vsg::vec3(1, 0, 0));
        model.attach(root, chest);
        Nif::NiNode& hand = model.node("Right Hand");
        model.setTransform(hand, 1.f, vsg::vec3(0, 3, 0));
        model.attach(chest, hand);
        return root;
    }

    TEST(BodyNifRestPose, WorldMatricesFollowTheNodeChainAndKeysAreLowerCase)
    {
        Model model;
        const RestPose rest = readRestPose(buildSkeleton(model));

        ASSERT_EQ(rest.count("right hand"), 1u);
        EXPECT_EQ(rest.count("Right Hand"), 0u);
        const Matrix34& hand = rest.at("right hand");
        EXPECT_NEAR(hand[0], 2.0, 1e-9); // the rotation and scale block
        EXPECT_NEAR(hand[5], 2.0, 1e-9);
        EXPECT_NEAR(hand[3], 1.0, 1e-9); // translation
        EXPECT_NEAR(hand[7], 6.0, 1e-9);
        EXPECT_NEAR(hand[11], 10.0, 1e-9);
        EXPECT_NEAR(rest.at("bip01")[11], 10.0, 1e-9);
        EXPECT_EQ(rest.size(), 3u);
    }

    TEST(BodyNifRestPose, AnEmptyNameIsNotAKeyAndTheFirstOfADuplicateWins)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& first = model.node("Twin");
        model.setTransform(first, 1.f, vsg::vec3(1, 0, 0));
        Nif::NiNode& second = model.node("TWIN");
        model.setTransform(second, 1.f, vsg::vec3(2, 0, 0));
        Nif::NiNode& unnamed = model.node("");
        model.attach(root, first);
        model.attach(root, second);
        model.attach(root, unnamed);

        const RestPose rest = readRestPose(root);

        EXPECT_EQ(rest.size(), 2u); // root and twin
        EXPECT_NEAR(rest.at("twin")[3], 1.0, 1e-9);
    }

    TEST(BodyNifPlacement, ARigidShapeIsBroughtIntoTheSkeletonSpaceByItsBone)
    {
        Model skeleton;
        const RestPose rest = readRestPose(buildSkeleton(skeleton));
        Model part;
        Nif::NiNode& root = part.node("Root");
        part.shape(root, "Rigid", { vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(0, 0, 1) }, oneTriangle);
        Placement placement;
        placement.mRest = &rest;
        placement.mBone = "Right Hand";

        const ReadResult result = readShapes(root, "", {}, placement);

        EXPECT_TRUE(result.mErrors.empty());
        ASSERT_EQ(result.mShapes.size(), 1u);
        const Shape& shape = result.mShapes[0];
        EXPECT_TRUE(shape.mPlaced);
        // world = scale 2, translation (1,6,10): (1,0,0) -> (3,6,10), (0,1,0) -> (1,8,10), (0,0,1) -> (1,6,12)
        EXPECT_NEAR(shape.mPositions[0][0], 3.f, tolerance);
        EXPECT_NEAR(shape.mPositions[0][1], 6.f, tolerance);
        EXPECT_NEAR(shape.mPositions[0][2], 10.f, tolerance);
        EXPECT_NEAR(shape.mPositions[1][1], 8.f, tolerance);
        EXPECT_NEAR(shape.mPositions[2][2], 12.f, tolerance);
        EXPECT_TRUE(shape.mIdentityTransform); // that flag describes the part's own node chain only
    }

    // The engine mirrors a rigid part attached to a bone whose name contains "Left" (npc.cpp, insertBoundedPart:
    // MWAnim::decorate with scale (-1, 1, 1) in the bone's space), because the left and right parts share one file.
    TEST(BodyNifPlacement, ARigidPartOnALeftBoneIsMirroredInTheBonesSpace)
    {
        RestPose rest;
        rest["left hand"] = { 1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0 }; // translation (10, 0, 0)
        rest["right hand"] = { 1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0 };
        Model part;
        Nif::NiNode& root = part.node("Root");
        part.shape(root, "Rigid", { vsg::vec3(1, 2, 3), vsg::vec3(0, 1, 0), vsg::vec3(0, 0, 1) }, oneTriangle);
        Placement left;
        left.mRest = &rest;
        left.mBone = "Left Hand";
        Placement right = left;
        right.mBone = "Right Hand";

        const ReadResult onLeft = readShapes(root, "", {}, left);
        const ReadResult onRight = readShapes(root, "", {}, right);

        ASSERT_EQ(onLeft.mShapes.size(), 1u);
        ASSERT_EQ(onRight.mShapes.size(), 1u);
        EXPECT_NEAR(onLeft.mShapes[0].mPositions[0][0], 9.f, tolerance); // x mirrored: -1, then the bone's 10
        EXPECT_NEAR(onLeft.mShapes[0].mPositions[0][1], 2.f, tolerance);
        EXPECT_NEAR(onLeft.mShapes[0].mPositions[0][2], 3.f, tolerance);
        EXPECT_TRUE(onLeft.mShapes[0].mMirrored);
        EXPECT_NEAR(onRight.mShapes[0].mPositions[0][0], 11.f, tolerance);
        EXPECT_FALSE(onRight.mShapes[0].mMirrored);
    }

    // A skinned shape's vertices are in the skin's space: with the rest pose each vertex goes to
    // sum over its influences of weight * (bone world matrix * the bone's skin transform * the skin's own transform)
    // * v, the bind pose in the skeleton's space. The attach bone is not used for a skinned shape.
    RestPose twoBoneSkeleton()
    {
        RestPose rest;
        rest["a"] = { 1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0 }; // translation (10, 0, 0)
        rest["b"] = { 1, 0, 0, 0, 0, 1, 0, 20, 0, 0, 1, 0 }; // translation (0, 20, 0)
        return rest;
    }

    TEST(BodyNifPlacement, ASkinnedShapeGoesToItsBindPoseByItsBones)
    {
        const RestPose rest = twoBoneSkeleton();
        Model part;
        Nif::NiNode& root = part.node("Root");
        Nif::NiGeometry& geometry = part.shape(root, "Skinned", triangleVertices, oneTriangle);
        // vertex 0: bone A; vertex 1: A and B half each; vertex 2: bone B
        part.skin(geometry, { "A", "B" }, { { { 0, 1.f }, { 1, 0.5f } }, { { 1, 0.5f }, { 2, 0.f } } });
        part.skinOf(geometry).mData->mBones[1].mWeights.push_back({ 2, 1.f });
        Placement placement;
        placement.mRest = &rest;
        placement.mBone = "Right Hand"; // ignored for a skinned shape

        const ReadResult result = readShapes(root, "", {}, placement);

        EXPECT_TRUE(result.mErrors.empty());
        ASSERT_EQ(result.mShapes.size(), 1u);
        const Shape& shape = result.mShapes[0];
        EXPECT_TRUE(shape.mPlaced);
        // triangleVertices: (0,0,0), (1,0,0), (0,1,0)
        EXPECT_NEAR(shape.mPositions[0][0], 10.f, tolerance); // A moves by (10, 0, 0)
        EXPECT_NEAR(shape.mPositions[0][1], 0.f, tolerance);
        EXPECT_NEAR(shape.mPositions[1][0], 1.f + 5.f, tolerance); // half A (10, 0, 0) and half B (0, 20, 0)
        EXPECT_NEAR(shape.mPositions[1][1], 10.f, tolerance);
        EXPECT_NEAR(shape.mPositions[2][0], 0.f, tolerance); // B moves by (0, 20, 0)
        EXPECT_NEAR(shape.mPositions[2][1], 21.f, tolerance);
    }

    TEST(BodyNifPlacement, TheBonesSkinTransformAndTheSkinsOwnTransformAreApplied)
    {
        const RestPose rest = twoBoneSkeleton();
        Model part;
        Nif::NiNode& root = part.node("Root");
        Nif::NiGeometry& geometry = part.shape(root, "Skinned", triangleVertices, oneTriangle);
        part.skin(geometry, { "A" }, { { { 0, 1.f }, { 1, 1.f }, { 2, 1.f } } });
        // the bone's skin transform moves by (0, 0, 5); the skin's own transform scales by 2 (applied first)
        part.skinOf(geometry).mData->mBones[0].mTransform.mTranslation = vsg::vec3(0, 0, 5);
        part.skinOf(geometry).mData->mTransform.mScale = 2.f;
        Placement placement;
        placement.mRest = &rest;

        const ReadResult result = readShapes(root, "", {}, placement);

        ASSERT_EQ(result.mShapes.size(), 1u);
        const Shape& shape = result.mShapes[0];
        EXPECT_TRUE(shape.mPlaced);
        // v = (1,0,0): scaled to (2,0,0), moved to (2,0,5), then by the bone's world (10,0,0): (12,0,5)
        EXPECT_NEAR(shape.mPositions[1][0], 12.f, tolerance);
        EXPECT_NEAR(shape.mPositions[1][1], 0.f, tolerance);
        EXPECT_NEAR(shape.mPositions[1][2], 5.f, tolerance);
        EXPECT_FALSE(shape.mIdentityTransform); // the skin's own transform is not the identity
    }

    TEST(BodyNifPlacement, ASkinnedShapeWhoseBoneTheSkeletonLacksStaysWhereItIs)
    {
        const RestPose rest = twoBoneSkeleton();
        Model part;
        Nif::NiNode& root = part.node("Root");
        Nif::NiGeometry& geometry = part.shape(root, "Skinned", triangleVertices, oneTriangle);
        part.skin(geometry, { "A", "Missing" }, { { { 0, 1.f } }, { { 1, 1.f }, { 2, 1.f } } });
        Placement placement;
        placement.mRest = &rest;

        const ReadResult result = readShapes(root, "", {}, placement);

        ASSERT_EQ(result.mShapes.size(), 1u);
        EXPECT_FALSE(result.mShapes[0].mPlaced);
        EXPECT_NEAR(result.mShapes[0].mPositions[1][0], 1.f, tolerance);
        ASSERT_EQ(result.mErrors.size(), 1u);
        EXPECT_TRUE(anyErrorContains(result, "Missing"));
    }

    TEST(BodyNifPlacement, ABoneTheSkeletonLacksIsOneErrorAndTheShapesStayPut)
    {
        Model skeleton;
        const RestPose rest = readRestPose(buildSkeleton(skeleton));
        Model part;
        Nif::NiNode& root = part.node("Root");
        part.shape(root, "One", triangleVertices, oneTriangle);
        part.shape(root, "Two", triangleVertices, oneTriangle);
        Placement placement;
        placement.mRest = &rest;
        placement.mBone = "Nowhere";

        const ReadResult result = readShapes(root, "", {}, placement);

        ASSERT_EQ(result.mShapes.size(), 2u);
        EXPECT_FALSE(result.mShapes[0].mPlaced);
        EXPECT_NEAR(result.mShapes[0].mPositions[1][0], 1.f, tolerance);
        ASSERT_EQ(result.mErrors.size(), 1u); // once, not once per shape
        EXPECT_TRUE(anyErrorContains(result, "Nowhere"));
    }

    TEST(BodyNifPlacement, WithoutARestPoseNothingIsPlaced)
    {
        Model part;
        Nif::NiNode& root = part.node("Root");
        part.shape(root, "Rigid", triangleVertices, oneTriangle);
        Placement placement;
        placement.mBone = "Right Hand"; // a name but no pose

        const ReadResult result = readShapes(root, "", {}, placement);

        ASSERT_EQ(result.mShapes.size(), 1u);
        EXPECT_FALSE(result.mShapes[0].mPlaced);
        EXPECT_TRUE(result.mErrors.empty());
    }

    // ---- the influence cut ---------------------------------------------------------------------------------------

    TEST(BodyNifReader, TheCutKeepsTheFourLargestAndBreaksTiesByBoneIndex)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Cut", triangleVertices, oneTriangle);
        // Vertex 0 has six influences: bone 1 at 0.3 and bones 2 to 5 at 0.2, bone 0 at 0.1 (sum 1.1 is fine here).
        model.skin(geometry, { "b0", "b1", "b2", "b3", "b4", "b5" },
            { { { 0, 0.1f } }, { { 0, 0.3f } }, { { 0, 0.2f } }, { { 0, 0.2f } }, { { 0, 0.2f } }, { { 0, 0.2f } } });

        const ReadResult result = readShapes(root, "");

        ASSERT_EQ(result.mShapes.size(), 1u);
        const VertexSkin& vertex = result.mShapes[0].mSkin[0];
        EXPECT_EQ(vertex.mBeforeCut, 6);
        ASSERT_EQ(vertex.mCount, 4);
        EXPECT_EQ(vertex.mInfluences[0].mBone, 1); // 0.3
        EXPECT_EQ(vertex.mInfluences[1].mBone, 2); // ties at 0.2: the lowest bone indices
        EXPECT_EQ(vertex.mInfluences[2].mBone, 3);
        EXPECT_EQ(vertex.mInfluences[3].mBone, 4);
        // the cut is not renormalised: 0.3 + 0.2 + 0.2 + 0.2
        float sum = 0.f;
        for (int i = 0; i < vertex.mCount; ++i)
            sum += vertex.mInfluences[i].mWeight;
        EXPECT_NEAR(sum, 0.9f, tolerance);
        // the other vertices have none
        EXPECT_EQ(result.mShapes[0].mSkin[1].mCount, 0);
        EXPECT_EQ(result.mShapes[0].mSkin[1].mBeforeCut, 0);
    }

    TEST(BodyNifReader, ZeroWeightsAreNotInfluences)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Zero", triangleVertices, oneTriangle);
        model.skin(geometry, { "b0", "b1" }, { { { 0, 0.0f } }, { { 0, 1.0f } } });

        const ReadResult result = readShapes(root, "");

        ASSERT_EQ(result.mShapes.size(), 1u);
        EXPECT_EQ(result.mShapes[0].mSkin[0].mCount, 1);
        EXPECT_EQ(result.mShapes[0].mSkin[0].mInfluences[0].mBone, 1);
    }

    // ---- triangle strips and materials ---------------------------------------------------------------------------

    TEST(BodyNifReader, StripsBecomeTrianglesAndDegenerateOnesAreDropped)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        const std::vector<vsg::vec3> vertices
            = { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(1, 1, 0) };
        model.stripShape(root, "Strip", vertices, { { 0, 1, 2, 3, 3 } });

        const ReadResult result = readShapes(root, "");

        ASSERT_EQ(result.mShapes.size(), 1u);
        // (0,1,2) and (1,3,2) for the odd one; the strip's last triangle (2,3,3) is degenerate
        EXPECT_EQ(result.mShapes[0].mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2, 1, 3, 2 }));
    }

    TEST(BodyNifReader, ReadsTheAlphaTest)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Cutout", triangleVertices, oneTriangle);
        model.setAlphaTest(geometry, 128);

        const ReadResult result = readShapes(root, "");

        ASSERT_EQ(result.mShapes.size(), 1u);
        EXPECT_TRUE(result.mShapes[0].mAlphaTest);
        EXPECT_FALSE(result.mShapes[0].mAlphaBlend);
    }

    // ---- hostile input -------------------------------------------------------------------------------------------

    TEST(BodyNifHostile, ASkinWithNoBonesIsRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Bad Shape", triangleVertices, oneTriangle);
        model.skin(geometry, {}, {});

        const ReadResult result = readShapes(root, "");

        EXPECT_TRUE(result.mShapes.empty());
        ASSERT_EQ(result.mErrors.size(), 1u);
        EXPECT_TRUE(anyErrorContains(result, "Bad Shape"));
        EXPECT_TRUE(anyErrorContains(result, "no bones"));
    }

    TEST(BodyNifHostile, AWeightForAMissingVertexIsRefusedWithTheVertexAndBone)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Tri Neck", triangleVertices, oneTriangle);
        model.skin(geometry, { "Neck" }, { { { 0, 1.f }, { 99, 1.f } } });

        const ReadResult result = readShapes(root, "");

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "Tri Neck"));
        EXPECT_TRUE(anyErrorContains(result, "vertex 99"));
        EXPECT_TRUE(anyErrorContains(result, "Neck"));
    }

    TEST(BodyNifHostile, AMismatchOfBoneListsIsRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Mismatch", triangleVertices, oneTriangle);
        Nif::NiSkinInstance& instance = model.skin(geometry, { "b0", "b1" }, { { { 0, 1.f } }, { { 1, 1.f } } });
        instance.mData->mBones.pop_back(); // the data describes one bone, the instance lists two

        const ReadResult result = readShapes(root, "");

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "Mismatch"));
        EXPECT_TRUE(anyErrorContains(result, "bone"));
    }

    TEST(BodyNifHostile, ANullBoneIsRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Null Bone", triangleVertices, oneTriangle);
        Nif::NiSkinInstance& instance = model.skin(geometry, { "b0" }, { { { 0, 1.f } } });
        instance.mBones[0] = Nif::NiAVObjectPtr(nullptr);

        const ReadResult result = readShapes(root, "");

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "Null Bone"));
    }

    TEST(BodyNifHostile, TriangleIndicesOutOfRangeAreRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        model.shape(root, "Wild", triangleVertices, { 0, 1, 7 });

        const ReadResult result = readShapes(root, "");

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "Wild"));
        EXPECT_TRUE(anyErrorContains(result, "index"));
    }

    TEST(BodyNifHostile, CountsOverTheLimitsAreRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        model.shape(root, "Big", triangleVertices, oneTriangle);
        ReadLimits limits;
        limits.mMaxVertices = 2;

        const ReadResult tooManyVertices = readShapes(root, "", limits);
        EXPECT_TRUE(tooManyVertices.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(tooManyVertices, "limit"));

        limits = ReadLimits{};
        limits.mMaxTriangles = 0;
        const ReadResult tooManyTriangles = readShapes(root, "", limits);
        EXPECT_TRUE(tooManyTriangles.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(tooManyTriangles, "limit"));

        limits = ReadLimits{};
        limits.mMaxShapes = 0;
        const ReadResult tooManyShapes = readShapes(root, "", limits);
        EXPECT_TRUE(tooManyShapes.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(tooManyShapes, "limit"));
    }

    TEST(BodyNifHostile, MoreBonesThanTheLimitIsRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Many Bones", triangleVertices, oneTriangle);
        model.skin(geometry, { "a", "b", "c" }, { { { 0, 1.f } }, { { 1, 1.f } }, { { 2, 1.f } } });
        ReadLimits limits;
        limits.mMaxBones = 2;

        const ReadResult result = readShapes(root, "", limits);

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "limit"));
    }

    TEST(BodyNifHostile, ATreeDeeperThanTheLimitIsRefusedNotWalked)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode* parent = &root;
        for (int i = 0; i < 10; ++i)
        {
            Nif::NiNode& child = model.node("Level" + std::to_string(i));
            model.attach(*parent, child);
            parent = &child;
        }
        model.shape(*parent, "Deep", triangleVertices, oneTriangle);
        ReadLimits limits;
        limits.mMaxDepth = 4;

        const ReadResult result = readShapes(root, "", limits);

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "depth"));
    }

    TEST(BodyNifHostile, AVertexThatIsNotAFiniteNumberIsRefused)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        model.shape(root, "Not A Number",
            { vsg::vec3(0, 0, 0), vsg::vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0), vsg::vec3(0, 1, 0) },
            oneTriangle);

        const ReadResult result = readShapes(root, "");

        EXPECT_TRUE(result.mShapes.empty());
        EXPECT_TRUE(anyErrorContains(result, "Not A Number"));
        EXPECT_TRUE(anyErrorContains(result, "finite"));
    }

    TEST(BodyNifHostile, ANormalListOfTheWrongSizeIsDroppedNotRead)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& geometry = model.shape(root, "Short Normals", triangleVertices, oneTriangle);
        static_cast<Nif::NiGeometryData&>(*geometry.mData.getPtr()).mNormals = { vsg::vec3(0, 0, 1) };

        const ReadResult result = readShapes(root, "");

        ASSERT_EQ(result.mShapes.size(), 1u);
        EXPECT_TRUE(result.mShapes[0].mNormals.empty()); // never one short of the vertices
    }

    // ---- the bind pose against the engine's own skinning (task 0069, stage b) --------------------------------------
    //
    // Stage a placed a skinned part by "the weighted sum, over the kept influences, of (the bone's world matrix x the
    // bone's skin transform x the skin's own transform) applied to the stored vertex", read from the NIF format and not
    // proved. This runs the engine's own pieces on the same records: the engine's conversion of each NiTransform
    // (vsgAdapters::convertTrafo, then Anim::Transform::t_transform, as handleSkin and the node reader do), the bone
    // matrices composed as Anim::Skin::apply composes them (skin matrix x bone world x inverse bind; the skin matrix is
    // the identity unless the skin has a transform of its own) and Anim::SoftwareSkin, the class that skins vertices
    // on the CPU for intersections. The reader's bind pose must equal the engine's skinned vertices.
    // Where the skin's own transform is not the identity the engine multiplies it on the OTHER side (Skin::apply:
    // transform * skinMatrix): the second test pins that the two differ, so that the day a mod has one the difference
    // is known (the analysis of task 0069, stage a: every skinned stock part has the identity there).
    Nif::NiTransform makeTransform(const float (&rotation)[3][3], vsg::vec3 translation, float scale)
    {
        Nif::NiTransform t = Nif::NiTransform::getIdentity();
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
                t.mRotation.mValues[i][j] = rotation[i][j];
        }
        t.mTranslation = translation;
        t.mScale = scale;
        return t;
    }

    vsg::mat4 engineMatrix(const Nif::NiTransform& transform)
    {
        Anim::Transform converted;
        vsgAdapters::convertTrafo(converted, transform);
        return converted.t_transform(vsg::mat4());
    }

    using BindWeights = std::array<std::pair<int, float>, 2>;

    // The vertices the engine's software skin gives at the bind pose.
    std::vector<Vec3> engineBindPose(const std::vector<vsg::vec3>& vertices,
        const std::vector<Nif::NiTransform>& boneWorld, const std::vector<Nif::NiTransform>& boneSkin,
        const std::vector<BindWeights>& weights, const Nif::NiTransform* skinOwn)
    {
        const auto count = static_cast<std::uint32_t>(vertices.size());
        auto skin = vsg::ref_ptr{ new Anim::Skin };
        auto indices = vsg::vec4Array::create(count, vsg::vec4(0, 0, 0, 0));
        auto blend = vsg::vec4Array::create(count, vsg::vec4(0, 0, 0, 0));
        for (std::uint32_t v = 0; v < count; ++v)
        {
            for (std::uint32_t k = 0; k < 2; ++k)
            {
                indices->at(v)[k] = static_cast<float>(weights[v][k].first);
                blend->at(v)[k] = weights[v][k].second;
            }
        }
        skin->boneIndices = indices;
        skin->boneWeights = blend;
        // Anim::Skin::apply: matrix = (the skin's own transform, outermost) * (the inverse of the reference frame: the
        // identity here) * the bone's world * its inverse bind.
        vsg::mat4 skinMatrix;
        if (skinOwn != nullptr)
            skinMatrix = engineMatrix(*skinOwn) * skinMatrix;
        auto matrices = vsg::mat4Array::create(static_cast<std::uint32_t>(boneWorld.size()));
        for (std::uint32_t b = 0; b < boneWorld.size(); ++b)
            matrices->at(b) = skinMatrix * engineMatrix(boneWorld[b]) * engineMatrix(boneSkin[b]);
        auto software = Anim::SoftwareSkin::create(vsg::ref_ptr<const vsg::mat4Array>(matrices), skin);
        auto in = vsg::vec3Array::create(count);
        for (std::uint32_t v = 0; v < count; ++v)
            in->at(v) = vertices[v];
        software->apply(*in);
        std::vector<Vec3> out;
        for (std::uint32_t v = 0; v < count; ++v)
        {
            const vsg::vec3 p = software->vertices->at(v);
            out.push_back({ p.x, p.y, p.z });
        }
        return out;
    }

    struct BindFixture
    {
        std::vector<vsg::vec3> mVertices = { vsg::vec3(1, 2, 3), vsg::vec3(-2, 0.5f, 4), vsg::vec3(0, -1, 2) };
        std::vector<BindWeights> mWeights = { BindWeights{ std::pair{ 0, 1.f }, std::pair{ 1, 0.f } },
            BindWeights{ std::pair{ 0, 0.5f }, std::pair{ 1, 0.5f } },
            BindWeights{ std::pair{ 0, 0.25f }, std::pair{ 1, 0.75f } } };
        // A quarter turn about z and a quarter turn about x, with shifts: a composition that exposes any mix-up of
        // row and column order or of the multiplication order.
        std::vector<Nif::NiTransform> mBoneWorld
            = { makeTransform({ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, vsg::vec3(10, 0, 0), 1.f),
                  makeTransform({ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } }, vsg::vec3(0, 20, 0), 1.f) };
        std::vector<Nif::NiTransform> mBoneSkin
            = { makeTransform({ { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 } }, vsg::vec3(0, 0, 5), 1.f),
                  makeTransform({ { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } }, vsg::vec3(1, 2, 3), 1.f) };
    };

    ReadResult readBindFixture(
        Model& model, const BindFixture& fixture, const Nif::NiTransform* skinOwn, RestPose& rest)
    {
        // The skeleton: a root with the two bones at rest, so that readRestPose gives their world matrices.
        Nif::NiNode& skeleton = model.node("Root");
        Nif::NiNode& boneA = model.node("Bone A");
        Nif::NiNode& boneB = model.node("Bone B");
        boneA.mTransform = fixture.mBoneWorld[0];
        boneB.mTransform = fixture.mBoneWorld[1];
        model.attach(skeleton, boneA);
        model.attach(skeleton, boneB);
        rest = readRestPose(skeleton);

        Nif::NiNode& root = model.node("Part");
        Nif::NiGeometry& geometry = model.shape(root, "Skinned", fixture.mVertices, oneTriangle);
        WeightList weightsA;
        WeightList weightsB;
        for (std::size_t v = 0; v < fixture.mWeights.size(); ++v)
        {
            weightsA.push_back({ static_cast<unsigned short>(v), fixture.mWeights[v][0].second });
            weightsB.push_back({ static_cast<unsigned short>(v), fixture.mWeights[v][1].second });
        }
        model.skin(geometry, { "Bone A", "Bone B" }, { weightsA, weightsB });
        model.skinOf(geometry).mData->mBones[0].mTransform = fixture.mBoneSkin[0];
        model.skinOf(geometry).mData->mBones[1].mTransform = fixture.mBoneSkin[1];
        if (skinOwn != nullptr)
            model.skinOf(geometry).mData->mTransform = *skinOwn;
        Placement placement;
        placement.mRest = &rest;
        return readShapes(root, "", {}, placement);
    }

    TEST(BodyNifBindPose, TheReadersBindPoseEqualsTheEnginesSoftwareSkinWhenTheSkinsOwnTransformIsTheIdentity)
    {
        const BindFixture fixture;
        Model model;
        RestPose rest;
        const ReadResult result = readBindFixture(model, fixture, nullptr, rest);
        ASSERT_TRUE(result.mErrors.empty());
        ASSERT_EQ(result.mShapes.size(), 1u);
        ASSERT_TRUE(result.mShapes[0].mPlaced);
        const std::vector<Vec3> engine
            = engineBindPose(fixture.mVertices, fixture.mBoneWorld, fixture.mBoneSkin, fixture.mWeights, nullptr);
        ASSERT_EQ(engine.size(), 3u);
        for (std::size_t v = 0; v < 3; ++v)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                EXPECT_NEAR(result.mShapes[0].mPositions[v][axis], engine[v][axis], 1e-4f)
                    << "vertex " << v << " axis " << axis;
            }
        }
        // The fixture really moves vertices (a test that compares two identities would prove nothing).
        EXPECT_GT(std::abs(result.mShapes[0].mPositions[1][0] - fixture.mVertices[1].x), 1.f);
    }

    TEST(BodyNifBindPose, ASkinsOwnTransformIsMultipliedOnTheOtherSideByTheEngine)
    {
        const BindFixture fixture;
        const Nif::NiTransform skinOwn
            = makeTransform({ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, vsg::vec3(3, -2, 1), 1.f);
        Model model;
        RestPose rest;
        const ReadResult result = readBindFixture(model, fixture, &skinOwn, rest);
        ASSERT_TRUE(result.mErrors.empty());
        ASSERT_EQ(result.mShapes.size(), 1u);
        const std::vector<Vec3> engine
            = engineBindPose(fixture.mVertices, fixture.mBoneWorld, fixture.mBoneSkin, fixture.mWeights, &skinOwn);
        // Pinned: the two orders differ for a non-identity transform of the skin itself. Every skinned stock part has
        // the identity there (research note 0033), so the merger's input takes the reader's order; this test records
        // the difference so that a mod that has one is not a surprise.
        float largest = 0.f;
        for (std::size_t v = 0; v < 3; ++v)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
                largest = std::max(largest, std::abs(result.mShapes[0].mPositions[v][axis] - engine[v][axis]));
        }
        EXPECT_GT(largest, 0.5f);
    }
}
