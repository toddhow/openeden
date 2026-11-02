// Task 0059 (stage S4c): flattening a NIF model into triangle lists for the object cluster baker. The models are built
// by hand from Nif records in memory; no files, no VFS, no game data, no GPU.
#include <gtest/gtest.h>

#include <components/lod/nifflatten.hpp>

#include <components/nif/data.hpp>
#include <components/nif/node.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float tolerance = 1e-4f;

    struct Rotation
    {
        float mValues[3][3];
    };

    const Rotation identityRotation = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
    // 90 degrees about Z: (1,0,0) -> (0,1,0), (0,1,0) -> (-1,0,0).
    const Rotation quarterTurnZ = { { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } } };
    // Mirror in X: determinant -1.
    const Rotation mirrorX = { { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };

    struct ShapeRef
    {
        Nif::NiGeometry* mShape = nullptr;
        Nif::NiGeometryData* mData = nullptr;
    };

    // Owns every record of one hand-made model. Every record pointer a flattener could read is set (to null where
    // there is nothing), because a default RecordPtrT is not readable in debug builds.
    class Model
    {
    public:
        Nif::NiNode& node(Nif::RecordType type = Nif::RC_NiNode, const std::string& name = std::string())
        {
            Nif::NiNode& result = create<Nif::NiNode>(type);
            result.mName = name;
            return result;
        }

        void attach(Nif::NiNode& parent, Nif::NiAVObject& child)
        {
            parent.mChildren.push_back(Nif::NiAVObjectPtr(&child));
            child.mParents.push_back(&parent);
        }

        void setTransform(Nif::NiAVObject& object, const Rotation& rotation, float scale, const vsg::vec3& translation)
        {
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    object.mTransform.mRotation.mValues[i][j] = rotation.mValues[i][j];
                }
            }
            object.mTransform.mScale = scale;
            object.mTransform.mTranslation = translation;
        }

        ShapeRef triShape(Nif::NiNode& parent, const std::vector<vsg::vec3>& vertices,
            const std::vector<unsigned short>& triangles, Nif::RecordType type = Nif::RC_NiTriShape)
        {
            Nif::NiGeometry& shape = create<Nif::NiTriShape>(type);
            Nif::NiTriShapeData& data = create<Nif::NiTriShapeData>(Nif::RC_NiTriShapeData);
            data.mNumVertices = static_cast<std::uint16_t>(vertices.size());
            data.mVertices = vertices;
            data.mTriangles = triangles;
            data.mNumTriangles = static_cast<std::uint16_t>(triangles.size() / 3);
            shape.mData = Nif::NiGeometryDataPtr(&data);
            attach(parent, shape);
            return ShapeRef{ &shape, &data };
        }

        ShapeRef stripShape(Nif::NiNode& parent, const std::vector<vsg::vec3>& vertices,
            const std::vector<std::vector<unsigned short>>& strips)
        {
            Nif::NiGeometry& shape = create<Nif::NiTriStrips>(Nif::RC_NiTriStrips);
            Nif::NiTriStripsData& data = create<Nif::NiTriStripsData>(Nif::RC_NiTriStripsData);
            data.mNumVertices = static_cast<std::uint16_t>(vertices.size());
            data.mVertices = vertices;
            data.mStrips = strips;
            shape.mData = Nif::NiGeometryDataPtr(&data);
            attach(parent, shape);
            return ShapeRef{ &shape, &data };
        }

        // Four vertices, two counter-clockwise triangles (0,1,2) and (0,2,3), with normals, UVs and colours.
        ShapeRef quad(Nif::NiNode& parent)
        {
            ShapeRef result
                = triShape(parent, { vsg::vec3(0, 0, 0), vsg::vec3(2, 0, 0), vsg::vec3(2, 3, 1), vsg::vec3(0, 3, 1) },
                    { 0, 1, 2, 0, 2, 3 });
            result.mData->mNormals = { vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(0, 0, 1) };
            result.mData->mUVList = { { vsg::vec2(0, 0), vsg::vec2(1, 0), vsg::vec2(1, 1), vsg::vec2(0, 1) } };
            result.mData->mColors
                = { vsg::vec4(1, 0, 0, 1), vsg::vec4(0, 1, 0, 1), vsg::vec4(0, 0, 1, 1), vsg::vec4(1, 1, 1, 0) };
            return result;
        }

        Nif::NiGeometry& particles(Nif::NiNode& parent)
        {
            Nif::NiGeometry& result = create<Nif::NiParticles>(Nif::RC_NiParticles);
            attach(parent, result);
            return result;
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

        void setAlpha(Nif::NiGeometry& shape, std::uint16_t flags, std::uint8_t threshold)
        {
            Nif::NiAlphaProperty& property = create<Nif::NiAlphaProperty>(Nif::RC_NiAlphaProperty);
            property.mFlags = flags;
            property.mThreshold = threshold;
            // Both places a reader may look: the property list (Morrowind files) and the geometry's own slot.
            shape.mProperties.push_back(Nif::RecordPtrT<Nif::NiProperty>(&property));
            shape.mAlphaProperty = Nif::NiAlphaPropertyPtr(&property);
        }

        void setController(Nif::NiObjectNET& object)
        {
            Nif::NiTimeController& controller = create<Nif::NiTimeController>(Nif::RC_NiKeyframeController);
            object.mController = Nif::NiTimeControllerPtr(&controller);
        }

        void setSkin(Nif::NiGeometry& shape)
        {
            Nif::NiSkinInstance& skin = create<Nif::NiSkinInstance>(Nif::RC_NiSkinInstance);
            shape.mSkin = Nif::NiSkinInstancePtr(&skin);
        }

    private:
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
            if constexpr (std::is_same_v<T, Nif::NiTimeController>)
            {
                result.mNext = Nif::NiTimeControllerPtr(nullptr);
                result.mTarget = Nif::NiObjectNETPtr(nullptr);
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

        std::vector<std::unique_ptr<Nif::Record>> mOwned;
    };

    void expectNear3(const float (&actual)[3], float x, float y, float z)
    {
        EXPECT_NEAR(actual[0], x, tolerance);
        EXPECT_NEAR(actual[1], y, tolerance);
        EXPECT_NEAR(actual[2], z, tolerance);
    }

    // Twice the signed area of a triangle of a piece, projected on the XY plane (positive: counter-clockwise).
    float areaZ(const SourcePiece& piece, std::size_t triangle)
    {
        const SourceVertex& a = piece.mVertices[piece.mIndices[triangle * 3]];
        const SourceVertex& b = piece.mVertices[piece.mIndices[triangle * 3 + 1]];
        const SourceVertex& c = piece.mVertices[piece.mIndices[triangle * 3 + 2]];
        return (b.mPosition[0] - a.mPosition[0]) * (c.mPosition[1] - a.mPosition[1])
            - (b.mPosition[1] - a.mPosition[1]) * (c.mPosition[0] - a.mPosition[0]);
    }

    // ---- one plain shape ------------------------------------------------------------------------------------

    TEST(LodNifFlatten, a_textured_shape_gives_one_piece_with_the_data_copied)
    {
        Model model;
        Nif::NiNode& root = model.node();
        ShapeRef shape = model.quad(root);
        model.setTexture(*shape.mShape, "tx_rock.dds");

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        const SourcePiece& piece = result.mPieces[0];
        EXPECT_EQ(piece.mTexture, "tx_rock.dds");
        EXPECT_EQ(piece.mFlags, 0u);
        EXPECT_EQ(piece.mIndices, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 }));
        ASSERT_EQ(piece.mVertices.size(), 4u);

        expectNear3(piece.mVertices[0].mPosition, 0, 0, 0);
        expectNear3(piece.mVertices[1].mPosition, 2, 0, 0);
        expectNear3(piece.mVertices[2].mPosition, 2, 3, 1);
        expectNear3(piece.mVertices[3].mPosition, 0, 3, 1);
        expectNear3(piece.mVertices[0].mNormal, 0, 0, 1);
        expectNear3(piece.mVertices[1].mNormal, 1, 0, 0);
        expectNear3(piece.mVertices[2].mNormal, 0, 1, 0);
        EXPECT_NEAR(piece.mVertices[2].mUv[0], 1.f, tolerance);
        EXPECT_NEAR(piece.mVertices[2].mUv[1], 1.f, tolerance);
        EXPECT_EQ(piece.mVertices[0].mColor[0], 255);
        EXPECT_EQ(piece.mVertices[0].mColor[1], 0);
        EXPECT_EQ(piece.mVertices[0].mColor[2], 0);
        EXPECT_EQ(piece.mVertices[0].mColor[3], 255);
        EXPECT_EQ(piece.mVertices[1].mColor[1], 255);
        EXPECT_EQ(piece.mVertices[3].mColor[3], 0);

        EXPECT_EQ(result.mTraits.mKind, ObjectKind::Static);
        EXPECT_EQ(result.mTraits.mTriangleCount, 2u);
        EXPECT_FALSE(result.mTraits.mAnimated);
        EXPECT_FALSE(result.mTraits.mSkinned);
        EXPECT_FALSE(result.mTraits.mHasParticles);
        EXPECT_FALSE(result.mTraits.mAlphaBlended);
    }

    TEST(LodNifFlatten, the_texture_name_is_kept_exactly_as_stored)
    {
        Model model;
        Nif::NiNode& root = model.node();
        ShapeRef shape = model.quad(root);
        model.setTexture(*shape.mShape, "Tx_Rock_01.DDS");
        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_EQ(result.mPieces[0].mTexture, "Tx_Rock_01.DDS");
    }

    TEST(LodNifFlatten, a_shape_without_a_texture_gives_an_empty_texture_string)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.quad(root);
        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_TRUE(result.mPieces[0].mTexture.empty());
        EXPECT_EQ(result.mPieces[0].mIndices.size(), 6u);
    }

    // ---- transforms ----------------------------------------------------------------------------------------

    TEST(LodNifFlatten, a_node_transform_is_folded_into_positions_and_normals)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& turned = model.node();
        model.setTransform(turned, quarterTurnZ, 2.f, vsg::vec3(10, 20, 30));
        model.attach(root, turned);
        model.quad(turned);

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        const SourcePiece& piece = result.mPieces[0];
        ASSERT_EQ(piece.mVertices.size(), 4u);
        // (x, y, z) -> R * (2 x, 2 y, 2 z) + t with R the quarter turn about Z.
        expectNear3(piece.mVertices[0].mPosition, 10, 20, 30);
        expectNear3(piece.mVertices[1].mPosition, 10, 24, 30);
        expectNear3(piece.mVertices[2].mPosition, 4, 24, 32);
        expectNear3(piece.mVertices[3].mPosition, 4, 20, 32);
        // Normals are rotated (not scaled, not translated) and unit length.
        expectNear3(piece.mVertices[0].mNormal, 0, 0, 1);
        expectNear3(piece.mVertices[1].mNormal, 0, 1, 0);
        expectNear3(piece.mVertices[2].mNormal, -1, 0, 0);
        for (const SourceVertex& vertex : piece.mVertices)
        {
            const float length = std::sqrt(vertex.mNormal[0] * vertex.mNormal[0] + vertex.mNormal[1] * vertex.mNormal[1]
                + vertex.mNormal[2] * vertex.mNormal[2]);
            EXPECT_NEAR(length, 1.f, 1e-3f);
        }
        // A pure rotation and uniform scale keep the winding.
        EXPECT_GT(areaZ(piece, 0), 0.f);
        EXPECT_GT(areaZ(piece, 1), 0.f);
    }

    TEST(LodNifFlatten, nested_transforms_apply_the_child_first_and_then_the_parent)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& parent = model.node();
        Nif::NiNode& child = model.node();
        model.setTransform(parent, quarterTurnZ, 2.f, vsg::vec3(10, 0, 0));
        model.setTransform(child, identityRotation, 1.f, vsg::vec3(1, 0, 0));
        model.attach(root, parent);
        model.attach(parent, child);
        // One triangle with its first vertex at the origin.
        model.triShape(child, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) }, { 0, 1, 2 });

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        const SourcePiece& piece = result.mPieces[0];
        ASSERT_EQ(piece.mVertices.size(), 3u);
        // (0,0,0) -> child: (1,0,0) -> parent: R * (2,0,0) + (10,0,0) = (10,2,0).
        // Applying the parent first would give (11,0,0).
        expectNear3(piece.mVertices[0].mPosition, 10, 2, 0);
        // (1,0,0) -> (2,0,0) -> (0,4,0) + (10,0,0).
        expectNear3(piece.mVertices[1].mPosition, 10, 4, 0);
        // (0,1,0) -> (1,1,0) -> R * (2,2,0) = (-2,2,0) + (10,0,0).
        expectNear3(piece.mVertices[2].mPosition, 8, 2, 0);
    }

    TEST(LodNifFlatten, a_mirrored_transform_flips_the_winding)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& mirrored = model.node();
        model.setTransform(mirrored, mirrorX, 1.f, vsg::vec3(0, 0, 0));
        model.attach(root, mirrored);
        model.quad(mirrored);

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        const SourcePiece& piece = result.mPieces[0];
        ASSERT_EQ(piece.mVertices.size(), 4u);
        ASSERT_EQ(piece.mIndices.size(), 6u);
        expectNear3(piece.mVertices[1].mPosition, -2, 0, 0);
        // The mirror turns counter-clockwise triangles clockwise; the flattener turns them back.
        EXPECT_GT(areaZ(piece, 0), 0.f);
        EXPECT_GT(areaZ(piece, 1), 0.f);
        // A mirrored X normal points the other way.
        expectNear3(piece.mVertices[1].mNormal, -1, 0, 0);
    }

    // ---- properties ---------------------------------------------------------------------------------------

    TEST(LodNifFlatten, a_texture_on_a_parent_node_is_inherited_and_one_on_the_shape_overrides)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& parent = model.node();
        model.attach(root, parent);
        model.setTexture(parent, "tx_parent.dds");
        model.quad(parent); // inherits
        ShapeRef own = model.quad(parent);
        model.setTexture(*own.mShape, "tx_own.dds");

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 2u);
        EXPECT_EQ(result.mPieces[0].mTexture, "tx_parent.dds");
        EXPECT_EQ(result.mPieces[1].mTexture, "tx_own.dds");
    }

    TEST(LodNifFlatten, alpha_testing_sets_the_flag_and_the_cutoff)
    {
        Model model;
        Nif::NiNode& root = model.node();
        ShapeRef shape = model.quad(root);
        model.setTexture(*shape.mShape, "tx_tree.dds");
        model.setAlpha(*shape.mShape, Nif::NiAlphaProperty::Flag_Testing, 128);

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_NE(result.mPieces[0].mFlags & clusterFlagAlphaTest, 0u);
        EXPECT_NEAR(result.mPieces[0].mAlphaCutoff, 128.f / 255.f, 1e-4f);
        EXPECT_FALSE(result.mTraits.mAlphaBlended);
    }

    TEST(LodNifFlatten, alpha_blending_alone_marks_the_model_and_gives_no_piece)
    {
        Model model;
        Nif::NiNode& root = model.node();
        ShapeRef shape = model.quad(root);
        model.setTexture(*shape.mShape, "tx_glass.dds");
        model.setAlpha(*shape.mShape, Nif::NiAlphaProperty::Flag_Blending, 0);

        const FlattenedModel result = flattenNifModel(root);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mAlphaBlended);
    }

    TEST(LodNifFlatten, alpha_testing_and_blending_together_are_treated_as_testing)
    {
        Model model;
        Nif::NiNode& root = model.node();
        ShapeRef shape = model.quad(root);
        model.setTexture(*shape.mShape, "tx_tree.dds");
        model.setAlpha(*shape.mShape, Nif::NiAlphaProperty::Flag_Testing | Nif::NiAlphaProperty::Flag_Blending, 128);

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_NE(result.mPieces[0].mFlags & clusterFlagAlphaTest, 0u);
        EXPECT_NEAR(result.mPieces[0].mAlphaCutoff, 128.f / 255.f, 1e-4f);
        EXPECT_FALSE(result.mTraits.mAlphaBlended);
    }

    // ---- triangle strips ------------------------------------------------------------------------------------

    TEST(LodNifFlatten, a_strip_becomes_triangles_with_alternating_winding)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.stripShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(1, 1, 0) },
            { { 0, 1, 2, 3 } });

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_EQ(result.mPieces[0].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2, 1, 3, 2 }));
        EXPECT_EQ(result.mTraits.mTriangleCount, 2u);
        // Both triangles face the same way (counter-clockwise).
        EXPECT_GT(areaZ(result.mPieces[0], 0), 0.f);
        EXPECT_GT(areaZ(result.mPieces[0], 1), 0.f);
    }

    TEST(LodNifFlatten, degenerate_triangles_of_a_strip_are_skipped)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.stripShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(1, 1, 0) },
            { { 0, 1, 2, 2, 3 } });

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        // (0,1,2) is the only triangle of that strip whose three indices differ.
        EXPECT_EQ(result.mPieces[0].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2 }));
    }

    // ---- skipped content ----------------------------------------------------------------------------------

    TEST(LodNifFlatten, hidden_nodes_and_hidden_shapes_are_skipped)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& hiddenNode = model.node();
        hiddenNode.mFlags = Nif::NiAVObject::Flag_Hidden;
        model.attach(root, hiddenNode);
        model.quad(hiddenNode);
        ShapeRef hiddenShape = model.quad(root);
        hiddenShape.mShape->mFlags = Nif::NiAVObject::Flag_Hidden;
        ShapeRef visible = model.quad(root);
        model.setTexture(*visible.mShape, "tx_visible.dds");

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_EQ(result.mPieces[0].mTexture, "tx_visible.dds");
        EXPECT_EQ(result.mTraits.mTriangleCount, 2u);
    }

    TEST(LodNifFlatten, collision_and_avoid_node_subtrees_are_skipped)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& collision = model.node(Nif::RC_RootCollisionNode);
        model.attach(root, collision);
        model.quad(collision);
        Nif::NiNode& avoid = model.node(Nif::RC_AvoidNode);
        model.attach(root, avoid);
        Nif::NiNode& inner = model.node();
        model.attach(avoid, inner);
        model.quad(inner);
        ShapeRef visible = model.quad(root);
        model.setTexture(*visible.mShape, "tx_visible.dds");

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_EQ(result.mPieces[0].mTexture, "tx_visible.dds");
    }

    TEST(LodNifFlatten, a_node_named_bounding_box_is_skipped_whatever_the_case)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& lower = model.node(Nif::RC_NiNode, "bounding box");
        model.attach(root, lower);
        model.quad(lower);
        Nif::NiNode& mixed = model.node(Nif::RC_NiNode, "Bounding Box");
        model.attach(root, mixed);
        model.quad(mixed);
        Nif::NiNode& upper = model.node(Nif::RC_NiNode, "BOUNDING BOX");
        model.attach(root, upper);
        model.quad(upper);
        ShapeRef namedShape = model.quad(root);
        namedShape.mShape->mName = "BOUNDING BOX";
        Nif::NiNode& other = model.node(Nif::RC_NiNode, "Bounding Boxes");
        model.attach(root, other);
        model.quad(other);

        const FlattenedModel result = flattenNifModel(root);
        EXPECT_EQ(result.mPieces.size(), 1u); // only the node with a different name
    }

    TEST(LodNifFlatten, geometry_with_an_index_beyond_the_vertex_count_gives_no_piece)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.triShape(
            root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(1, 1, 0) }, { 0, 1, 9 });
        const FlattenedModel bad = flattenNifModel(root);
        EXPECT_TRUE(bad.mPieces.empty());
        EXPECT_EQ(bad.mTraits.mTriangleCount, 0u);

        // A bad shape does not stop a good one beside it.
        model.quad(root);
        const FlattenedModel mixed = flattenNifModel(root);
        EXPECT_EQ(mixed.mPieces.size(), 1u);
    }

    TEST(LodNifFlatten, a_strip_with_an_index_beyond_the_vertex_count_gives_no_piece)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.stripShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) }, { { 0, 1, 2, 7 } });
        EXPECT_TRUE(flattenNifModel(root).mPieces.empty());
    }

    TEST(LodNifFlatten, geometry_without_triangles_gives_no_piece)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.triShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) }, {});
        model.stripShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) }, {});
        const FlattenedModel result = flattenNifModel(root);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_EQ(result.mTraits.mTriangleCount, 0u);
    }

    // ---- traits ------------------------------------------------------------------------------------------

    TEST(LodNifFlatten, a_skinned_shape_marks_the_model_and_gives_no_piece)
    {
        Model model;
        Nif::NiNode& root = model.node();
        ShapeRef shape = model.quad(root);
        model.setSkin(*shape.mShape);
        const FlattenedModel result = flattenNifModel(root);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mSkinned);
        EXPECT_FALSE(result.mTraits.mAnimated);
    }

    TEST(LodNifFlatten, a_controller_marks_the_model_animated)
    {
        {
            Model model;
            Nif::NiNode& root = model.node();
            Nif::NiNode& moving = model.node();
            model.attach(root, moving);
            model.setController(moving);
            model.quad(moving);
            EXPECT_TRUE(flattenNifModel(root).mTraits.mAnimated);
        }
        {
            Model model;
            Nif::NiNode& root = model.node();
            ShapeRef shape = model.quad(root);
            model.setController(*shape.mShape);
            EXPECT_TRUE(flattenNifModel(root).mTraits.mAnimated);
        }
        {
            Model model;
            Nif::NiNode& root = model.node();
            model.setController(root);
            model.quad(root);
            EXPECT_TRUE(flattenNifModel(root).mTraits.mAnimated);
        }
    }

    TEST(LodNifFlatten, an_animation_node_marks_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& animation = model.node(Nif::RC_NiBSAnimationNode);
        model.attach(root, animation);
        model.quad(animation);
        EXPECT_TRUE(flattenNifModel(root).mTraits.mAnimated);
    }

    TEST(LodNifFlatten, a_model_without_animation_is_not_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.quad(root);
        EXPECT_FALSE(flattenNifModel(root).mTraits.mAnimated);
    }

    TEST(LodNifFlatten, a_particle_system_marks_the_model)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.quad(root);
        model.particles(root);
        const FlattenedModel result = flattenNifModel(root);
        EXPECT_TRUE(result.mTraits.mHasParticles);
        EXPECT_EQ(result.mPieces.size(), 1u); // the particles themselves give no piece
    }

    TEST(LodNifFlatten, the_triangle_count_is_the_sum_over_the_pieces)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.quad(root); // 2
        model.triShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) }, { 0, 1, 2 }); // 1
        model.stripShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0), vsg::vec3(1, 1, 0) },
            { { 0, 1, 2, 3 } }); // 2

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 3u);
        std::uint32_t total = 0;
        for (const SourcePiece& piece : result.mPieces)
        {
            total += static_cast<std::uint32_t>(piece.mIndices.size() / 3);
        }
        EXPECT_EQ(total, 5u);
        EXPECT_EQ(result.mTraits.mTriangleCount, 5u);
        EXPECT_EQ(result.mTraits.mKind, ObjectKind::Static);
    }

    TEST(LodNifFlatten, the_radius_is_that_of_the_bounding_sphere_of_all_vertices)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.triShape(root, { vsg::vec3(0, 0, 0), vsg::vec3(10, 0, 0), vsg::vec3(0, 4, 0) }, { 0, 1, 2 });
        model.triShape(root, { vsg::vec3(10, 4, 0), vsg::vec3(10, 0, 0), vsg::vec3(0, 4, 0) }, { 0, 1, 2 });
        // The box is 10 x 4 x 0 with centre (5, 2, 0); every corner is sqrt(29) from it.
        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 2u);
        EXPECT_NEAR(result.mTraits.mRadius, std::sqrt(29.f), 1e-3f);
    }

    TEST(LodNifFlatten, the_radius_uses_the_folded_in_transforms)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode& scaled = model.node();
        model.setTransform(scaled, identityRotation, 3.f, vsg::vec3(100, 0, 0));
        model.attach(root, scaled);
        model.triShape(scaled, { vsg::vec3(0, 0, 0), vsg::vec3(2, 0, 0), vsg::vec3(0, 2, 0) }, { 0, 1, 2 });
        // After the transform the box is (100..106) x (0..6): centre (103, 3, 0), corner distance sqrt(9 + 9).
        EXPECT_NEAR(flattenNifModel(root).mTraits.mRadius, std::sqrt(18.f), 1e-3f);
    }

    TEST(LodNifFlatten, an_empty_model_has_no_pieces_and_radius_zero)
    {
        Model model;
        Nif::NiNode& root = model.node();
        const FlattenedModel result = flattenNifModel(root);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_EQ(result.mTraits.mRadius, 0.f);
        EXPECT_EQ(result.mTraits.mTriangleCount, 0u);
        EXPECT_EQ(result.mTraits.mKind, ObjectKind::Static);
        EXPECT_FALSE(result.mTraits.mAnimated);
        EXPECT_FALSE(result.mTraits.mSkinned);
        EXPECT_FALSE(result.mTraits.mHasParticles);
        EXPECT_FALSE(result.mTraits.mAlphaBlended);
    }

    TEST(LodNifFlatten, a_very_deep_chain_of_nodes_does_not_crash_and_the_deepest_geometry_is_ignored)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiNode* current = &root;
        for (int depth = 0; depth < 200; ++depth)
        {
            Nif::NiNode& next = model.node();
            model.attach(*current, next);
            current = &next;
        }
        model.quad(*current);
        // A shape near the top is still found.
        ShapeRef shallow = model.quad(root);
        model.setTexture(*shallow.mShape, "tx_shallow.dds");

        const FlattenedModel result = flattenNifModel(root);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_EQ(result.mPieces[0].mTexture, "tx_shallow.dds");
    }
}
