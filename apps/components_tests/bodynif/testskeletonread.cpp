// Task 0070 (stage a): reading a skeleton NIF's node tree and a body NIF's skin weights into plain values, for the
// read-only measurement of extended skeletons (components/bodynif/skeletonread.hpp). Every model is built by hand from
// Nif records in memory: no files, no game or mod data. The routes of the weights (NiSkinData lists, NiSkinPartition
// lists, the packed vertex data of BSTriShape) are each built as the reader in components/nif stores them.
#include <gtest/gtest.h>

#include <components/body/skeletonmapping.hpp>
#include <components/bodynif/skeletonread.hpp>

#include <components/nif/data.hpp>
#include <components/nif/node.hpp>

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

    using WeightList = std::vector<std::pair<unsigned short, float>>;

    constexpr std::uint16_t halfOne = 0x3C00;
    constexpr std::uint16_t halfThreeQuarters = 0x3A00;
    constexpr std::uint16_t halfQuarter = 0x3400;
    constexpr std::uint16_t halfNan = 0x7E00;

    // Owns every record of one hand-made model; every record pointer a reader could follow is set (to null where there
    // is nothing), because a default RecordPtrT is not readable in debug builds.
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

        Nif::NiGeometry& triShape(Nif::NiNode& parent, const std::string& name, std::size_t vertices)
        {
            Nif::NiGeometry& result = create<Nif::NiTriShape>(Nif::RC_NiTriShape);
            result.mName = name;
            Nif::NiTriShapeData& data = create<Nif::NiTriShapeData>(Nif::RC_NiTriShapeData);
            data.mNumVertices = static_cast<std::uint16_t>(vertices);
            data.mVertices.assign(vertices, vsg::vec3(0, 0, 0));
            result.mData = Nif::NiGeometryDataPtr(&data);
            attach(parent, result);
            return result;
        }

        Nif::BSTriShape& bsTriShape(Nif::NiNode& parent, const std::string& name, std::size_t vertices)
        {
            Nif::BSTriShape& result = create<Nif::BSTriShape>(Nif::RC_BSTriShape);
            result.mName = name;
            result.mNumVertices = static_cast<std::uint16_t>(vertices);
            result.mDataSize = 0;
            result.mVertDesc.mFlags = 0;
            attach(parent, result);
            return result;
        }

        // An NiSkinInstance (or BSDismemberSkinInstance) over the named bones, with one weight list per bone.
        template <class Instance = Nif::NiSkinInstance>
        Instance& skin(const std::vector<std::string>& boneNames, const std::vector<WeightList>& weights,
            Nif::RecordType type = Nif::RC_NiSkinInstance)
        {
            Instance& instance = create<Instance>(type);
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
            return instance;
        }

        Nif::NiSkinPartition& partitions(Nif::NiSkinInstance& instance)
        {
            Nif::NiSkinPartition& result = create<Nif::NiSkinPartition>(Nif::RC_NiSkinPartition);
            result.mDataSize = 0;
            result.mVertexSize = 0;
            result.mVertexDesc.mFlags = 0;
            instance.mPartitions = Nif::NiSkinPartitionPtr(&result);
            return result;
        }

        Nif::BSSkinInstance& bsSkin(const std::vector<std::string>& boneNames)
        {
            Nif::BSSkinInstance& instance = create<Nif::BSSkinInstance>(Nif::RC_BSSkinInstance);
            Nif::BSSkinBoneData& data = create<Nif::BSSkinBoneData>(Nif::RC_BSSkinBoneData);
            for (const std::string& name : boneNames)
            {
                Nif::NiNode& bone = node(name);
                instance.mBones.push_back(Nif::NiAVObjectPtr(&bone));
                data.mBones.emplace_back();
                data.mBones.back().mTransform = Nif::NiTransform::getIdentity();
            }
            instance.mData = Nif::BSSkinBoneDataPtr(&data);
            instance.mRoot = Nif::NiAVObjectPtr(nullptr);
            return instance;
        }

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
                result.mTransform = Nif::NiTransform::getIdentity();
                result.mCollision = Nif::NiCollisionObjectPtr(nullptr);
            }
            if constexpr (std::is_base_of_v<Nif::NiGeometry, T>)
            {
                result.mData = Nif::NiGeometryDataPtr(nullptr);
                result.mSkin = Nif::NiSkinInstancePtr(nullptr);
                result.mShaderProperty = Nif::BSShaderPropertyPtr(nullptr);
                result.mAlphaProperty = Nif::NiAlphaPropertyPtr(nullptr);
            }
            if constexpr (std::is_base_of_v<Nif::BSTriShape, T>)
            {
                result.mSkin = Nif::RecordPtrT<Nif::Record>(nullptr);
                result.mShaderProperty = Nif::BSShaderPropertyPtr(nullptr);
                result.mAlphaProperty = Nif::NiAlphaPropertyPtr(nullptr);
            }
            if constexpr (std::is_base_of_v<Nif::NiSkinInstance, T>)
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

    Nif::BSVertexData packedVertex(
        const std::array<std::uint16_t, 4>& weights, const std::array<unsigned char, 4>& indices)
    {
        Nif::BSVertexData vertex{};
        for (std::size_t k = 0; k < 4; ++k)
        {
            vertex.mBoneWeights[k] = weights[k];
            vertex.mBoneIndices[k] = static_cast<char>(indices[k]);
        }
        return vertex;
    }

    constexpr std::uint16_t skinnedFlags
        = Nif::BSVertexDesc::VertexAttribute::Vertex | Nif::BSVertexDesc::VertexAttribute::Skinned;

    bool anyErrorContains(const std::vector<std::string>& errors, const std::string& text)
    {
        for (const std::string& error : errors)
        {
            if (error.find(text) != std::string::npos)
                return true;
        }
        return false;
    }

    // ---- half floats ---------------------------------------------------------------------------------------------

    TEST(BodyNifHalfFloat, decodes_normal_values)
    {
        EXPECT_EQ(halfToFloat(0x3C00), 1.f);
        EXPECT_EQ(halfToFloat(0x3800), 0.5f);
        EXPECT_EQ(halfToFloat(0xC000), -2.f);
        EXPECT_EQ(halfToFloat(0x3555), 0.333251953125f);
        EXPECT_EQ(halfToFloat(0x7BFF), 65504.f);
    }

    TEST(BodyNifHalfFloat, decodes_zeros_and_subnormals)
    {
        EXPECT_EQ(halfToFloat(0x0000), 0.f);
        EXPECT_FALSE(std::signbit(halfToFloat(0x0000)));
        EXPECT_EQ(halfToFloat(0x8000), 0.f);
        EXPECT_TRUE(std::signbit(halfToFloat(0x8000)));
        EXPECT_EQ(halfToFloat(0x0001), std::ldexp(1.f, -24));
        EXPECT_EQ(halfToFloat(0x03FF), std::ldexp(1023.f, -24));
    }

    TEST(BodyNifHalfFloat, decodes_infinities_and_nan)
    {
        EXPECT_EQ(halfToFloat(0x7C00), std::numeric_limits<float>::infinity());
        EXPECT_EQ(halfToFloat(0xFC00), -std::numeric_limits<float>::infinity());
        EXPECT_TRUE(std::isnan(halfToFloat(halfNan)));
    }

    // ---- the skeleton --------------------------------------------------------------------------------------------

    TEST(BodyNifSkeletonRead, nodes_are_depth_first_with_parents_before_children_in_file_order)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& a = model.node("A");
        Nif::NiNode& a1 = model.node("A1");
        Nif::NiNode& b = model.node("B");
        model.attach(root, a);
        model.attach(a, a1);
        model.attach(root, b);
        const SkeletonRead read = readSkeleton(root);
        ASSERT_EQ(read.mNodes.size(), 4u);
        EXPECT_EQ(read.mNodes[0].mName, "Root");
        EXPECT_EQ(read.mNodes[1].mName, "A");
        EXPECT_EQ(read.mNodes[2].mName, "A1");
        EXPECT_EQ(read.mNodes[3].mName, "B");
        EXPECT_EQ(read.mNodes[0].mParent, -1);
        EXPECT_EQ(read.mNodes[1].mParent, 0);
        EXPECT_EQ(read.mNodes[2].mParent, 1);
        EXPECT_EQ(read.mNodes[3].mParent, 0);
        EXPECT_EQ(read.mNodes[2].mDepth, 2u);
        EXPECT_EQ(read.mMaxDepth, 2u);
        EXPECT_TRUE(read.mErrors.empty());
    }

    TEST(BodyNifSkeletonRead, a_translation_is_multiplied_by_the_scales_of_all_ancestors)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        root.mTransform.mScale = 2.f;
        Nif::NiNode& a = model.node("A");
        a.mTransform.mTranslation = vsg::vec3(1, 0, 0);
        a.mTransform.mScale = 3.f;
        Nif::NiNode& a1 = model.node("A1");
        a1.mTransform.mTranslation = vsg::vec3(0, 1, 0);
        model.attach(root, a);
        model.attach(a, a1);
        const SkeletonRead read = readSkeleton(root);
        ASSERT_EQ(read.mNodes.size(), 3u);
        EXPECT_DOUBLE_EQ(read.mNodes[1].mTranslation[0], 2.0);
        EXPECT_DOUBLE_EQ(read.mNodes[2].mTranslation[1], 6.0);
        EXPECT_DOUBLE_EQ(read.mNodes[1].mScale, 3.0);
        EXPECT_EQ(read.mScaled, 2u);
    }

    TEST(BodyNifSkeletonRead, a_quarter_turn_about_z_gives_its_quaternion)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& a = model.node("A");
        // Column-vector convention: x goes to y.
        a.mTransform.mRotation.mValues[0][0] = 0.f;
        a.mTransform.mRotation.mValues[0][1] = -1.f;
        a.mTransform.mRotation.mValues[1][0] = 1.f;
        a.mTransform.mRotation.mValues[1][1] = 0.f;
        model.attach(root, a);
        const SkeletonRead read = readSkeleton(root);
        ASSERT_EQ(read.mNodes.size(), 2u);
        const std::array<double, 4>& q = read.mNodes[1].mRotation;
        EXPECT_NEAR(q[0], 0.0, 1e-7);
        EXPECT_NEAR(q[1], 0.0, 1e-7);
        EXPECT_NEAR(q[2], std::sqrt(0.5), 1e-7);
        EXPECT_NEAR(q[3], std::sqrt(0.5), 1e-7);
        EXPECT_TRUE(read.mNodes[1].mOrthonormal);
        EXPECT_EQ(read.mNonOrthonormal, 0u);
    }

    // The world rest position of every bone, composed the NIF way (column vectors, W = Wparent * [s R | t]), equals the
    // one components/body composes from the values read (row vectors, world = local * parentWorld), so the measurement
    // can hand the read values to compareRest and the suggestion engine as they are.
    TEST(BodyNifSkeletonRead, world_positions_from_the_values_read_equal_the_nif_world_positions)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        root.mTransform.mScale = 1.5f;
        Nif::NiNode& a = model.node("A");
        a.mTransform.mTranslation = vsg::vec3(1, 2, 3);
        a.mTransform.mScale = 2.f;
        // A quarter turn about x: y goes to z.
        a.mTransform.mRotation.mValues[1][1] = 0.f;
        a.mTransform.mRotation.mValues[1][2] = -1.f;
        a.mTransform.mRotation.mValues[2][1] = 1.f;
        a.mTransform.mRotation.mValues[2][2] = 0.f;
        Nif::NiNode& b = model.node("B");
        b.mTransform.mTranslation = vsg::vec3(0.5f, -1, 4);
        // A quarter turn about z.
        b.mTransform.mRotation.mValues[0][0] = 0.f;
        b.mTransform.mRotation.mValues[0][1] = -1.f;
        b.mTransform.mRotation.mValues[1][0] = 1.f;
        b.mTransform.mRotation.mValues[1][1] = 0.f;
        Nif::NiNode& c = model.node("C");
        c.mTransform.mTranslation = vsg::vec3(2, 0, -1);
        model.attach(root, a);
        model.attach(a, b);
        model.attach(b, c);

        // The NIF composition, by hand.
        struct Affine
        {
            std::array<double, 9> m{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            std::array<double, 3> t{};
        };
        const auto local = [](const Nif::NiTransform& transform) {
            Affine result;
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                    result.m[i * 3 + j] = static_cast<double>(transform.mRotation.mValues[i][j]) * transform.mScale;
            }
            result.t = { transform.mTranslation.x, transform.mTranslation.y, transform.mTranslation.z };
            return result;
        };
        const auto compose = [](const Affine& parent, const Affine& child) {
            Affine result;
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    double sum = 0;
                    for (int k = 0; k < 3; ++k)
                        sum += parent.m[i * 3 + k] * child.m[k * 3 + j];
                    result.m[i * 3 + j] = sum;
                }
                double t = parent.t[i];
                for (int k = 0; k < 3; ++k)
                    t += parent.m[i * 3 + k] * child.t[k];
                result.t[i] = t;
            }
            return result;
        };
        const Affine wRoot = local(root.mTransform);
        const Affine wA = compose(wRoot, local(a.mTransform));
        const Affine wB = compose(wA, local(b.mTransform));
        const Affine wC = compose(wB, local(c.mTransform));
        const std::array<std::array<double, 3>, 4> expected = { wRoot.t, wA.t, wB.t, wC.t };

        const SkeletonRead read = readSkeleton(root);
        ASSERT_EQ(read.mNodes.size(), 4u);
        std::vector<Body::RestBone> bones;
        for (const SkeletonNode& n : read.mNodes)
        {
            Body::RestBone bone;
            bone.mName = n.mName;
            bone.mParent = n.mParent;
            bone.mTranslation = n.mTranslation;
            bone.mRotation = n.mRotation;
            bones.push_back(bone);
        }
        const std::vector<Body::Mat4> world = Body::worldRestMatrices(bones);
        ASSERT_EQ(world.size(), 4u);
        for (std::size_t i = 0; i < 4; ++i)
        {
            const std::array<double, 3> p = Body::transformPoint({ 0, 0, 0 }, world[i]);
            for (std::size_t axis = 0; axis < 3; ++axis)
                EXPECT_NEAR(p[axis], expected[i][axis], 1e-5) << "bone " << i << " axis " << axis;
        }
    }

    TEST(BodyNifSkeletonRead, a_mirrored_or_scaled_rotation_matrix_is_not_orthonormal)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& mirrored = model.node("Mirrored");
        mirrored.mTransform.mRotation.mValues[0][0] = -1.f;
        Nif::NiNode& scaled = model.node("Scaled");
        scaled.mTransform.mRotation.mValues[2][2] = 2.f;
        model.attach(root, mirrored);
        model.attach(root, scaled);
        const SkeletonRead read = readSkeleton(root);
        ASSERT_EQ(read.mNodes.size(), 3u);
        EXPECT_TRUE(read.mNodes[0].mOrthonormal);
        EXPECT_FALSE(read.mNodes[1].mOrthonormal);
        EXPECT_FALSE(read.mNodes[2].mOrthonormal);
        EXPECT_EQ(read.mNonOrthonormal, 2u);
        for (const SkeletonNode& n : read.mNodes)
        {
            const double length = std::sqrt(n.mRotation[0] * n.mRotation[0] + n.mRotation[1] * n.mRotation[1]
                + n.mRotation[2] * n.mRotation[2] + n.mRotation[3] * n.mRotation[3]);
            EXPECT_NEAR(length, 1.0, 1e-9);
        }
    }

    TEST(BodyNifSkeletonRead, geometry_and_other_objects_are_counted_and_are_not_bones)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        model.triShape(root, "Shape", 3);
        model.bsTriShape(root, "Packed", 3);
        Nif::NiCamera& camera = model.create<Nif::NiCamera>(Nif::RC_NiCamera);
        camera.mName = "Camera";
        model.attach(root, camera);
        const SkeletonRead read = readSkeleton(root);
        EXPECT_EQ(read.mNodes.size(), 1u);
        EXPECT_EQ(read.mGeometry, 2u);
        EXPECT_EQ(read.mOtherObjects, 1u);
    }

    TEST(BodyNifSkeletonRead, unnamed_nodes_are_kept_and_counted_and_empty_children_are_skipped)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& unnamed = model.node("");
        model.attach(root, unnamed);
        root.mChildren.push_back(Nif::NiAVObjectPtr(nullptr));
        const SkeletonRead read = readSkeleton(root);
        ASSERT_EQ(read.mNodes.size(), 2u);
        EXPECT_EQ(read.mNodes[1].mName, "");
        EXPECT_EQ(read.mUnnamed, 1u);
    }

    TEST(BodyNifSkeletonRead, a_node_reached_twice_is_read_once)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& a = model.node("A");
        Nif::NiNode& b = model.node("B");
        Nif::NiNode& shared = model.node("Shared");
        model.attach(root, a);
        model.attach(root, b);
        model.attach(a, shared);
        model.attach(b, shared);
        const SkeletonRead read = readSkeleton(root);
        EXPECT_EQ(read.mNodes.size(), 4u);
        EXPECT_EQ(read.mRevisits, 1u);
    }

    TEST(BodyNifSkeletonRead, a_node_deeper_than_the_cap_is_left_out_with_one_error)
    {
        Model model;
        Nif::NiNode& root = model.node("N0");
        Nif::NiNode* parent = &root;
        for (int i = 1; i < 6; ++i)
        {
            Nif::NiNode& child = model.node("N" + std::to_string(i));
            model.attach(*parent, child);
            parent = &child;
        }
        SkeletonLimits limits;
        limits.mMaxDepth = 2;
        const SkeletonRead read = readSkeleton(root, limits);
        EXPECT_EQ(read.mNodes.size(), 3u);
        EXPECT_EQ(read.mErrors.size(), 1u);
        EXPECT_TRUE(anyErrorContains(read.mErrors, "depth"));
    }

    TEST(BodyNifSkeletonRead, reading_stops_at_the_node_cap_with_one_error)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        for (int i = 0; i < 4; ++i)
            model.attach(root, model.node("C" + std::to_string(i)));
        SkeletonLimits limits;
        limits.mMaxNodes = 2;
        const SkeletonRead read = readSkeleton(root, limits);
        EXPECT_EQ(read.mNodes.size(), 2u);
        EXPECT_EQ(read.mErrors.size(), 1u);
        EXPECT_TRUE(anyErrorContains(read.mErrors, "nodes"));
    }

    // ---- skin weights --------------------------------------------------------------------------------------------

    TEST(BodyNifSkinWeights, the_skin_data_lists_of_an_nitrishape_give_the_masses)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& shape = model.triShape(root, "Body", 3);
        Nif::NiSkinInstance& skin = model.skin(
            { "A", "B" }, { WeightList{ { 0, 1.f }, { 1, 0.5f } }, WeightList{ { 1, 0.5f }, { 2, 1.f } } });
        shape.mSkin = Nif::NiSkinInstancePtr(&skin);
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mKind, "NiGeometry+NiSkinInstance");
        EXPECT_EQ(w.mRoute, WeightRoute::SkinData);
        EXPECT_EQ(w.mVertices, 3u);
        ASSERT_EQ(w.mBones.size(), 2u);
        EXPECT_EQ(w.mBones[0], "A");
        EXPECT_EQ(w.mBones[1], "B");
        ASSERT_EQ(w.mMass.size(), 2u);
        EXPECT_DOUBLE_EQ(w.mMass[0], 1.5);
        EXPECT_DOUBLE_EQ(w.mMass[1], 1.5);
        EXPECT_DOUBLE_EQ(w.mTotal, 3.0);
        EXPECT_EQ(w.mWeightedVertices, 3u);
        EXPECT_EQ(w.mBadSums, 0u);
        EXPECT_EQ(w.mUnweightedVertices, 0u);
        EXPECT_TRUE(w.mHasSkinDataWeights);
        EXPECT_FALSE(w.mHasPartitionWeights);
        EXPECT_FALSE(w.mHasVertexData);
        EXPECT_EQ(w.mPartitionAgreement, -1.0);
    }

    TEST(BodyNifSkinWeights, bad_sums_unweighted_vertices_out_of_range_and_non_finite_weights_are_counted)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& shape = model.triShape(root, "Body", 3);
        Nif::NiSkinInstance& skin = model.skin({ "A", "B" },
            { WeightList{ { 0, 0.5f }, { 1, std::numeric_limits<float>::quiet_NaN() } }, WeightList{ { 5, 1.f } } });
        shape.mSkin = Nif::NiSkinInstancePtr(&skin);
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mOutOfRange, 1u);
        EXPECT_EQ(w.mNonFinite, 1u);
        EXPECT_EQ(w.mBadSums, 1u); // vertex 0 sums to 0.5
        EXPECT_EQ(w.mUnweightedVertices, 2u); // vertices 1 and 2
        EXPECT_EQ(w.mWeightedVertices, 1u);
        EXPECT_DOUBLE_EQ(w.mTotal, 0.5);
    }

    // Skyrim SE style: a BSTriShape whose NiSkinData lists are empty, with the weights in NiSkinPartition. A shape
    // vertex two partitions list counts once.
    TEST(BodyNifSkinWeights, the_partitions_give_the_masses_when_the_skin_data_has_no_weights)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 4);
        auto& skin = model.skin<Nif::BSDismemberSkinInstance>(
            { "A", "B" }, { WeightList{}, WeightList{} }, Nif::RC_BSDismemberSkinInstance);
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        Nif::NiSkinPartition& parts = model.partitions(skin);
        Nif::NiSkinPartition::Partition p0{};
        p0.mBones = { 0, 1 };
        p0.mVertexMap = { 0, 1, 2 };
        p0.mWeights = { 1.f, 0.f, 0.5f, 0.5f, 0.f, 1.f };
        p0.mBoneIndices = { 0, 1, 0, 1, 0, 1 };
        Nif::NiSkinPartition::Partition p1{};
        p1.mBones = { 1 };
        p1.mVertexMap = { 2, 3 };
        p1.mWeights = { 1.f, 1.f };
        p1.mBoneIndices = { 0, 0 };
        parts.mPartitions = { p0, p1 };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mKind, "BSTriShape+NiSkinInstance");
        EXPECT_EQ(w.mRoute, WeightRoute::Partitions);
        EXPECT_FALSE(w.mHasSkinDataWeights);
        EXPECT_TRUE(w.mHasPartitionWeights);
        EXPECT_EQ(w.mVertices, 4u);
        ASSERT_EQ(w.mMass.size(), 2u);
        EXPECT_DOUBLE_EQ(w.mMass[0], 1.5);
        EXPECT_DOUBLE_EQ(w.mMass[1], 2.5);
        EXPECT_EQ(w.mBadSums, 0u);
        EXPECT_EQ(w.mWeightedVertices, 4u);
    }

    // The packed vertex data of a Skyrim SE file: read with partition-local indices it agrees with the skin data,
    // read with global indices it does not (the bones of the partition are listed in another order).
    TEST(BodyNifSkinWeights, packed_vertex_data_is_compared_with_local_and_with_global_indices)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 3);
        auto& skin = model.skin<Nif::BSDismemberSkinInstance>({ "A", "B" },
            { WeightList{ { 0, 1.f } }, WeightList{ { 1, 1.f }, { 2, 1.f } } }, Nif::RC_BSDismemberSkinInstance);
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        Nif::NiSkinPartition& parts = model.partitions(skin);
        parts.mVertexDesc.mFlags = skinnedFlags;
        parts.mVertexData = { packedVertex({ halfOne, 0, 0, 0 }, { 1, 0, 0, 0 }),
            packedVertex({ halfOne, 0, 0, 0 }, { 0, 0, 0, 0 }), packedVertex({ halfOne, 0, 0, 0 }, { 0, 0, 0, 0 }) };
        Nif::NiSkinPartition::Partition p0{};
        p0.mBones = { 1, 0 }; // B, A
        p0.mVertexMap = { 0, 1, 2 };
        p0.mWeights = { 1.f, 1.f, 1.f };
        p0.mBoneIndices = { 1, 0, 0 };
        parts.mPartitions = { p0 };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mRoute, WeightRoute::SkinData);
        EXPECT_TRUE(w.mHasVertexData);
        EXPECT_NEAR(w.mPartitionAgreement, 0.0, 1e-12);
        EXPECT_NEAR(w.mVertexDataLocalAgreement, 0.0, 1e-12);
        EXPECT_NEAR(w.mVertexDataGlobalAgreement, 1.0 / 3.0, 1e-9);
        EXPECT_EQ(w.mOutOfRange, 0u);
    }

    // With partitions but weights only in the packed data, the indices are read as skin bone indices: stage a found
    // the Skyrim SE files to store them so (research note 0036, section 2).
    TEST(BodyNifSkinWeights, packed_data_alone_is_read_with_skin_bone_indices_even_with_partitions)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 2);
        auto& skin = model.skin<Nif::BSDismemberSkinInstance>(
            { "A", "B" }, { WeightList{}, WeightList{} }, Nif::RC_BSDismemberSkinInstance);
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        Nif::NiSkinPartition& parts = model.partitions(skin);
        parts.mVertexDesc.mFlags = skinnedFlags;
        parts.mVertexData = { packedVertex({ halfOne, 0, 0, 0 }, { 1, 0, 0, 0 }),
            packedVertex({ halfOne, 0, 0, 0 }, { 1, 0, 0, 0 }) };
        Nif::NiSkinPartition::Partition p0{};
        p0.mBones = { 1, 0 };
        p0.mVertexMap = { 0, 1 };
        parts.mPartitions = { p0 };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mRoute, WeightRoute::VertexData);
        EXPECT_TRUE(w.mGlobalIndices);
        ASSERT_EQ(w.mMass.size(), 2u);
        EXPECT_DOUBLE_EQ(w.mMass[0], 0.0);
        EXPECT_DOUBLE_EQ(w.mMass[1], 2.0);
        EXPECT_NEAR(w.mVertexDataLocalAgreement, 1.0, 1e-12);
    }

    // Fallout 4 style: BSTriShape with BSSkin::Instance; the weights are only in the packed vertex data and its
    // indices are the skin's bone indices.
    TEST(BodyNifSkinWeights, a_fallout4_shape_is_read_from_its_vertex_data_with_global_indices)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 2);
        Nif::BSSkinInstance& skin = model.bsSkin({ "A", "B", "C" });
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        shape.mVertDesc.mFlags = skinnedFlags;
        shape.mDataSize = 2;
        shape.mVertData = { packedVertex({ halfThreeQuarters, halfQuarter, 0, 0 }, { 0, 2, 0, 0 }),
            packedVertex({ halfOne, 0, 0, 0 }, { 1, 0, 0, 0 }) };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mKind, "BSTriShape+BSSkin::Instance");
        EXPECT_EQ(w.mRoute, WeightRoute::VertexData);
        EXPECT_TRUE(w.mGlobalIndices);
        ASSERT_EQ(w.mMass.size(), 3u);
        EXPECT_DOUBLE_EQ(w.mMass[0], 0.75);
        EXPECT_DOUBLE_EQ(w.mMass[1], 1.0);
        EXPECT_DOUBLE_EQ(w.mMass[2], 0.25);
        EXPECT_EQ(w.mWeightedVertices, 2u);
        EXPECT_EQ(w.mBadSums, 0u);
    }

    TEST(BodyNifSkinWeights, an_index_out_of_range_and_a_nan_weight_in_vertex_data_are_counted_and_ignored)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 2);
        Nif::BSSkinInstance& skin = model.bsSkin({ "A", "B" });
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        shape.mVertDesc.mFlags = skinnedFlags;
        shape.mDataSize = 2;
        shape.mVertData = { packedVertex({ halfOne, 0, 0, 0 }, { 7, 0, 0, 0 }),
            packedVertex({ halfNan, halfOne, 0, 0 }, { 0, 1, 0, 0 }) };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mOutOfRange, 1u);
        EXPECT_EQ(w.mNonFinite, 1u);
        EXPECT_DOUBLE_EQ(w.mMass[0], 0.0);
        EXPECT_DOUBLE_EQ(w.mMass[1], 1.0);
        EXPECT_EQ(w.mUnweightedVertices, 1u);
    }

    TEST(BodyNifSkinWeights, vertex_data_without_the_skinned_flag_carries_no_weights)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 1);
        Nif::BSSkinInstance& skin = model.bsSkin({ "A" });
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        shape.mVertDesc.mFlags = Nif::BSVertexDesc::VertexAttribute::Vertex;
        shape.mDataSize = 1;
        shape.mVertData = { packedVertex({ halfOne, 0, 0, 0 }, { 0, 0, 0, 0 }) };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        EXPECT_FALSE(read.mShapes[0].mHasVertexData);
        EXPECT_EQ(read.mShapes[0].mRoute, WeightRoute::None);
        EXPECT_DOUBLE_EQ(read.mShapes[0].mTotal, 0.0);
    }

    TEST(BodyNifSkinWeights, unskinned_shapes_are_counted_and_nested_shapes_are_found)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiNode& group = model.node("Group");
        model.attach(root, group);
        model.triShape(root, "Plain", 3);
        model.bsTriShape(group, "PlainPacked", 3);
        Nif::NiGeometry& skinned = model.triShape(group, "Skinned", 1);
        Nif::NiSkinInstance& skin = model.skin({ "A" }, { WeightList{ { 0, 1.f } } });
        skinned.mSkin = Nif::NiSkinInstancePtr(&skin);
        const SkinWeightsRead read = readSkinWeights(root);
        EXPECT_EQ(read.mUnskinnedShapes, 2u);
        ASSERT_EQ(read.mShapes.size(), 1u);
        EXPECT_EQ(read.mShapes[0].mName, "Skinned");
    }

    // Skyrim SE files can store 0 as the BSTriShape's vertex count when the vertices live in the skin partition: the
    // count is then the partition's packed vertex data (or the largest vertex the partitions map, plus one).
    TEST(BodyNifSkinWeights, a_vertex_count_of_zero_is_taken_from_the_partition)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 0);
        auto& skin = model.skin<Nif::BSDismemberSkinInstance>({ "A", "B" },
            { WeightList{ { 0, 1.f } }, WeightList{ { 1, 1.f }, { 2, 1.f } } }, Nif::RC_BSDismemberSkinInstance);
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        Nif::NiSkinPartition& parts = model.partitions(skin);
        parts.mVertexDesc.mFlags = skinnedFlags;
        parts.mVertexData = { packedVertex({ halfOne, 0, 0, 0 }, { 0, 0, 0, 0 }),
            packedVertex({ halfOne, 0, 0, 0 }, { 1, 0, 0, 0 }), packedVertex({ halfOne, 0, 0, 0 }, { 1, 0, 0, 0 }) };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        const ShapeWeights& w = read.mShapes[0];
        EXPECT_EQ(w.mVertices, 3u);
        EXPECT_FALSE(w.mVertexCountFromFile);
        EXPECT_EQ(w.mRoute, WeightRoute::SkinData);
        ASSERT_EQ(w.mMass.size(), 2u);
        EXPECT_DOUBLE_EQ(w.mMass[0], 1.0);
        EXPECT_DOUBLE_EQ(w.mMass[1], 2.0);
        EXPECT_EQ(w.mOutOfRange, 0u);
        EXPECT_NEAR(w.mVertexDataGlobalAgreement, 0.0, 1e-12);
    }

    TEST(BodyNifSkinWeights, a_vertex_count_of_zero_without_packed_data_comes_from_the_vertex_maps)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::BSTriShape& shape = model.bsTriShape(root, "Body", 0);
        auto& skin = model.skin<Nif::BSDismemberSkinInstance>(
            { "A" }, { WeightList{ { 0, 1.f }, { 4, 1.f } } }, Nif::RC_BSDismemberSkinInstance);
        shape.mSkin = Nif::RecordPtrT<Nif::Record>(&skin);
        Nif::NiSkinPartition& parts = model.partitions(skin);
        Nif::NiSkinPartition::Partition p0{};
        p0.mBones = { 0 };
        p0.mVertexMap = { 0, 4 };
        parts.mPartitions = { p0 };
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        EXPECT_EQ(read.mShapes[0].mVertices, 5u);
        EXPECT_FALSE(read.mShapes[0].mVertexCountFromFile);
        EXPECT_DOUBLE_EQ(read.mShapes[0].mTotal, 2.0);
    }

    TEST(BodyNifSkinWeights, a_vertex_count_in_the_file_is_used_as_it_is)
    {
        Model model;
        Nif::NiNode& root = model.node("Root");
        Nif::NiGeometry& shape = model.triShape(root, "Body", 2);
        Nif::NiSkinInstance& skin = model.skin({ "A" }, { WeightList{ { 0, 1.f }, { 1, 1.f } } });
        shape.mSkin = Nif::NiSkinInstancePtr(&skin);
        const SkinWeightsRead read = readSkinWeights(root);
        ASSERT_EQ(read.mShapes.size(), 1u);
        EXPECT_EQ(read.mShapes[0].mVertices, 2u);
        EXPECT_TRUE(read.mShapes[0].mVertexCountFromFile);
    }

    TEST(BodyNifSkinWeights, the_route_names_are_fixed)
    {
        EXPECT_STREQ(toString(WeightRoute::None), "none");
        EXPECT_STREQ(toString(WeightRoute::SkinData), "skindata");
        EXPECT_STREQ(toString(WeightRoute::Partitions), "partitions");
        EXPECT_STREQ(toString(WeightRoute::VertexData), "vertexdata");
    }
}
