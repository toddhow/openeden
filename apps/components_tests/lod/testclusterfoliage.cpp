// Task 0059 (stage S4d): foliage in the object clusters: which texture names are translucent, and how blended models
// become cutouts. Written from the specification in components/lod/nifflatten.hpp only. The models are built by hand
// from Nif records in memory; no files, no VFS, no game data, no GPU.
#include <gtest/gtest.h>

#include <components/lod/nifflatten.hpp>

#include <components/nif/data.hpp>
#include <components/nif/node.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    // Alpha flags: blending (bit 0), source blend mode in bits 1 to 4, destination blend mode in bits 5 to 8, alpha
    // test in bit 9 (Nif::NiAlphaProperty::sourceBlendMode, destinationBlendMode).
    constexpr std::uint16_t flagBlend = 0x0001;
    constexpr std::uint16_t flagTest = 0x0200;
    constexpr std::uint16_t srcAlpha = 6 << 1;
    constexpr std::uint16_t oneMinusSrcAlpha = 7 << 5;
    constexpr std::uint16_t destinationOne = 0 << 5;
    constexpr std::uint16_t ordinaryBlend = flagBlend | srcAlpha | oneMinusSrcAlpha;
    constexpr std::uint16_t additiveBlend = flagBlend | srcAlpha | destinationOne;

    // ---- textureLooksTranslucent -----------------------------------------------------------------------------------

    TEST(LodClusterFoliage, translucent_texture_names_are_recognised)
    {
        const char* const names[]
            = { "Tx_V_water_01.tga", "textures\\tx_bm_icelayer_01.dds", "textures/tx_ghost_01.tga", "tx_bubbles.dds",
                  "tx_waterfall_02.dds", "tx_glass_01.tga", "tx_mist.dds", "tx_rays_01", "tx_web_01", "tx_window_03",
                  "tx_bottle", "tx_smoke_01", "tx_steam", "tx_cloud_02", "tx_glow_blue", "tx_rope_01", "tx_fog" };
        for (const char* name : names)
            EXPECT_TRUE(textureLooksTranslucent(name)) << name;
    }

    TEST(LodClusterFoliage, ordinary_texture_names_are_not_translucent)
    {
        const char* const names[] = { "tx_gray_rock_01.dds", "tx_tray.dds", "tx_array_01.tga", "tx_office_01.tga",
            "tx_notice.dds", "tx_bc_leaves_02.tga", "tx_leaves_07.tga", "tx_bc_fern_01.tga", "tx_spiderweb_01.tga",
            "tx_wood_01.dds", "", "12345", "123_456.dds" };
        for (const char* name : names)
            EXPECT_FALSE(textureLooksTranslucent(name)) << name;
    }

    TEST(LodClusterFoliage, only_the_base_name_counts_not_the_folders)
    {
        EXPECT_FALSE(textureLooksTranslucent("textures/water/tx_rock.dds"));
        EXPECT_FALSE(textureLooksTranslucent("textures\\water\\tx_rock.dds"));
        EXPECT_TRUE(textureLooksTranslucent("textures/rock/tx_water.dds"));
    }

    TEST(LodClusterFoliage, the_match_ignores_case)
    {
        EXPECT_TRUE(textureLooksTranslucent("TX_WATER_01.TGA"));
        EXPECT_TRUE(textureLooksTranslucent("Tx_Glass_01.Tga"));
        EXPECT_FALSE(textureLooksTranslucent("TX_GRAY_ROCK_01.DDS"));
    }

    TEST(LodClusterFoliage, very_long_names_and_non_ascii_bytes_do_not_crash)
    {
        EXPECT_FALSE(textureLooksTranslucent(std::string(100000, 'x')));
        EXPECT_TRUE(textureLooksTranslucent("water" + std::string(100000, 'x')));
        EXPECT_FALSE(textureLooksTranslucent(std::string(100000, '_')));
        EXPECT_FALSE(textureLooksTranslucent("\xFF\xFE\x80\xC3\xA9"));
        EXPECT_FALSE(textureLooksTranslucent(std::string(1000, '\xC3')));
        EXPECT_TRUE(textureLooksTranslucent("tx_\xFF\xFE_ghost"));
    }

    // ---- flattenNifModel with blendAsCutout ------------------------------------------------------------------------

    // Owns every record of one hand-made model. Every record pointer the flattener could read is set (to null where
    // there is nothing), because a default RecordPtrT is not readable in debug builds.
    class Model
    {
    public:
        Nif::NiNode& root()
        {
            Nif::NiNode& result = create<Nif::NiNode>(Nif::RC_NiNode);
            return result;
        }

        // One triangle.
        Nif::NiGeometry& triangle(Nif::NiNode& parent)
        {
            Nif::NiGeometry& shape = create<Nif::NiTriShape>(Nif::RC_NiTriShape);
            Nif::NiTriShapeData& data = create<Nif::NiTriShapeData>(Nif::RC_NiTriShapeData);
            data.mVertices = { vsg::vec3(0, 0, 0), vsg::vec3(1, 0, 0), vsg::vec3(0, 1, 0) };
            data.mNumVertices = 3;
            data.mTriangles = { 0, 1, 2 };
            data.mNumTriangles = 1;
            shape.mData = Nif::NiGeometryDataPtr(&data);
            parent.mChildren.push_back(Nif::NiAVObjectPtr(&shape));
            shape.mParents.push_back(&parent);
            return shape;
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

        void setMaterialAlpha(Nif::NiGeometry& shape, float alpha)
        {
            Nif::NiMaterialProperty& property = create<Nif::NiMaterialProperty>(Nif::RC_NiMaterialProperty);
            property.mAlpha = alpha;
            shape.mProperties.push_back(Nif::RecordPtrT<Nif::NiProperty>(&property));
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
            mOwned.push_back(std::move(owned));
            return result;
        }

        std::vector<std::unique_ptr<Nif::Record>> mOwned;
    };

    constexpr float cutoffTolerance = 1e-4f;

    TEST(LodClusterFoliage, the_rule_constants_are_as_specified)
    {
        EXPECT_GE(foliageRuleVersion, 2u);
        EXPECT_EQ(blendCutoutThreshold, 128);
    }

    TEST(LodClusterFoliage, ordinary_blend_with_a_leaf_texture_becomes_a_cutout)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_bc_leaves_02.tga");
        model.setAlpha(shape, ordinaryBlend, 0);

        const FlattenedModel result = flattenNifModel(root, true);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_NE(result.mPieces[0].mFlags & clusterFlagAlphaTest, 0u);
        EXPECT_NEAR(result.mPieces[0].mAlphaCutoff, static_cast<float>(blendCutoutThreshold) / 255.f, cutoffTolerance);
        EXPECT_EQ(result.mPieces[0].mTexture, "tx_bc_leaves_02.tga");
        EXPECT_FALSE(result.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, ordinary_blend_without_the_option_is_left_out_and_marks_the_model_blended)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_bc_leaves_02.tga");
        model.setAlpha(shape, ordinaryBlend, 0);

        const FlattenedModel result = flattenNifModel(root, false);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mAlphaBlended);
        const FlattenedModel byDefault = flattenNifModel(root);
        EXPECT_TRUE(byDefault.mPieces.empty());
        EXPECT_TRUE(byDefault.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, additive_blend_is_never_a_cutout)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_bc_leaves_02.tga");
        model.setAlpha(shape, additiveBlend, 0);

        const FlattenedModel result = flattenNifModel(root, true);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, ordinary_blend_with_a_translucent_texture_is_left_out)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_v_water_01.tga");
        model.setAlpha(shape, ordinaryBlend, 0);

        const FlattenedModel result = flattenNifModel(root, true);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, ordinary_blend_with_a_see_through_material_is_left_out)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_bc_leaves_02.tga");
        model.setMaterialAlpha(shape, 0.5f);
        model.setAlpha(shape, ordinaryBlend, 0);

        const FlattenedModel result = flattenNifModel(root, true);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, ordinary_blend_with_an_opaque_material_still_becomes_a_cutout)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_bc_leaves_02.tga");
        model.setMaterialAlpha(shape, 1.0f);
        model.setAlpha(shape, ordinaryBlend, 0);

        const FlattenedModel result = flattenNifModel(root, true);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_NE(result.mPieces[0].mFlags & clusterFlagAlphaTest, 0u);
        EXPECT_FALSE(result.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, ordinary_blend_without_a_texture_is_left_out)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setAlpha(shape, ordinaryBlend, 0);

        const FlattenedModel result = flattenNifModel(root, true);
        EXPECT_TRUE(result.mPieces.empty());
        EXPECT_TRUE(result.mTraits.mAlphaBlended);
    }

    TEST(LodClusterFoliage, blend_plus_alpha_test_keeps_the_propertys_own_threshold)
    {
        Model model;
        Nif::NiNode& root = model.root();
        Nif::NiGeometry& shape = model.triangle(root);
        model.setTexture(shape, "tx_bc_leaves_02.tga");
        model.setAlpha(shape, ordinaryBlend | flagTest, 200);

        const FlattenedModel result = flattenNifModel(root, true);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_NE(result.mPieces[0].mFlags & clusterFlagAlphaTest, 0u);
        EXPECT_NEAR(result.mPieces[0].mAlphaCutoff, 200.f / 255.f, cutoffTolerance);
        EXPECT_GT(std::abs(result.mPieces[0].mAlphaCutoff - static_cast<float>(blendCutoutThreshold) / 255.f), 0.1f);
    }
}
