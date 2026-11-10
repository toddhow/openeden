// Task 0096 (stages a2 and b): time-driven effects of far object clusters. The effect maths, layout version 2 of the
// cluster payload (an effect record per group), the builder keeping groups with different effects apart, and the NIF
// flattener turning a constant texture scroll into an effect (and seeing controllers on properties). Pure code: models
// are built by hand from Nif records in memory; no files, no VFS, no game data, no GPU.
#include <gtest/gtest.h>

#include <components/lod/clusterbuild.hpp>
#include <components/lod/clustereffect.hpp>
#include <components/lod/nifflatten.hpp>
#include <components/lod/objectcluster.hpp>
#include <components/lod/sceneryscript.hpp>

#include <components/nif/controller.hpp>
#include <components/nif/data.hpp>
#include <components/nif/node.hpp>
#include <components/nif/property.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    using namespace Lod;

    // ---- the effect record and its maths ------------------------------------------------------------------------

    ClusterEffect scroll(float u, float v)
    {
        ClusterEffect effect;
        effect.mKind = clusterEffectUvScroll;
        effect.mUvVelocity[0] = u;
        effect.mUvVelocity[1] = v;
        return effect;
    }

    TEST(LodClusterEffect, a_default_effect_is_none_and_valid)
    {
        const ClusterEffect effect;
        EXPECT_TRUE(effect.none());
        EXPECT_TRUE(isValidClusterEffect(effect));
    }

    TEST(LodClusterEffect, a_scroll_with_a_speed_is_valid)
    {
        EXPECT_TRUE(isValidClusterEffect(scroll(0.25f, 0)));
        EXPECT_TRUE(isValidClusterEffect(scroll(0, -0.5f)));
        EXPECT_TRUE(isValidClusterEffect(scroll(maxClusterUvVelocity, -maxClusterUvVelocity)));
    }

    TEST(LodClusterEffect, a_scroll_that_does_not_move_is_not_an_effect)
    {
        EXPECT_FALSE(isValidClusterEffect(scroll(0, 0)));
    }

    TEST(LodClusterEffect, unknown_kinds_and_unused_velocities_are_refused)
    {
        ClusterEffect effect = scroll(1, 0);
        effect.mKind |= 2u;
        EXPECT_FALSE(isValidClusterEffect(effect));
        ClusterEffect unused;
        unused.mUvVelocity[0] = 1.f; // a velocity with no scroll kind
        EXPECT_FALSE(isValidClusterEffect(unused));
    }

    TEST(LodClusterEffect, a_speed_beyond_the_cap_or_not_finite_is_refused)
    {
        EXPECT_FALSE(isValidClusterEffect(scroll(maxClusterUvVelocity * 1.01f, 0)));
        EXPECT_FALSE(isValidClusterEffect(scroll(0, -maxClusterUvVelocity * 2)));
        EXPECT_FALSE(isValidClusterEffect(scroll(std::numeric_limits<float>::quiet_NaN(), 0)));
        EXPECT_FALSE(isValidClusterEffect(scroll(1, std::numeric_limits<float>::infinity())));
    }

    TEST(LodClusterEffect, the_offset_is_the_speed_times_the_time_without_the_whole_part)
    {
        float offset[2];
        clusterUvOffset(scroll(0.25f, -0.5f), 1.0, offset);
        EXPECT_FLOAT_EQ(offset[0], 0.25f);
        EXPECT_FLOAT_EQ(offset[1], 0.5f); // -0.5 wraps to 0.5
        clusterUvOffset(scroll(0.25f, -0.5f), 6.0, offset);
        EXPECT_FLOAT_EQ(offset[0], 0.5f); // 1.5 wraps to 0.5
        EXPECT_FLOAT_EQ(offset[1], 0.0f); // -3 wraps to 0
        clusterUvOffset(scroll(0.25f, -0.5f), 0.0, offset);
        EXPECT_EQ(offset[0], 0.f);
        EXPECT_EQ(offset[1], 0.f);
    }

    TEST(LodClusterEffect, the_offset_stays_in_zero_to_one_and_keeps_precision_after_hours)
    {
        // Ten hours of play: a float time would lose the fraction, the double fraction does not.
        const double tenHours = 36000.0;
        float offset[2];
        clusterUvOffset(scroll(0.123f, 0), tenHours, offset);
        const double expected = std::fmod(static_cast<double>(0.123f) * tenHours, 1.0);
        EXPECT_NEAR(offset[0], expected, 1e-6);
        for (const double time : { 0.0, 0.1, 17.3, 1e5, 1e7 })
        {
            clusterUvOffset(scroll(0.37f, -0.91f), time, offset);
            for (int axis = 0; axis < 2; ++axis)
            {
                EXPECT_GE(offset[axis], 0.f);
                EXPECT_LT(offset[axis], 1.f);
            }
        }
    }

    TEST(LodClusterEffect, a_time_that_is_not_finite_or_a_missing_scroll_gives_no_offset)
    {
        float offset[2] = { 7, 7 };
        clusterUvOffset(scroll(1, 1), std::numeric_limits<double>::quiet_NaN(), offset);
        EXPECT_EQ(offset[0], 0.f);
        EXPECT_EQ(offset[1], 0.f);
        offset[0] = offset[1] = 7;
        clusterUvOffset(ClusterEffect{}, 12.0, offset);
        EXPECT_EQ(offset[0], 0.f);
        EXPECT_EQ(offset[1], 0.f);
    }

    // ---- the builder keeps groups with different effects apart ----------------------------------------------------

    SourcePiece trianglePiece(const std::string& texture, const ClusterEffect& effect, float x = 0)
    {
        SourcePiece piece;
        piece.mTexture = texture;
        piece.mEffect = effect;
        piece.mVertices.resize(3);
        piece.mVertices[0].mPosition[0] = x;
        piece.mVertices[1].mPosition[0] = x + 400;
        piece.mVertices[2].mPosition[0] = x;
        piece.mVertices[2].mPosition[1] = 400;
        piece.mIndices = { 0, 1, 2 };
        return piece;
    }

    ClusterBuildInput tileOf(std::vector<SourcePiece> pieces)
    {
        ClusterBuildInput input;
        input.mLevel.mLevel = 1;
        input.mLevel.mError = 1;
        input.mSide = 16384;
        input.mOriginCellX = 0;
        input.mOriginCellY = 0;
        std::uint32_t ref = 1;
        for (SourcePiece& piece : pieces)
        {
            SourceObject object;
            object.mRef = { ref++, 0 };
            object.mTraits.mRadius = 500;
            object.mTraits.mTriangleCount = 1;
            object.mPieces.push_back(std::move(piece));
            input.mObjects.push_back(std::move(object));
        }
        return input;
    }

    TEST(LodClusterEffectBuild, the_same_texture_with_different_effects_is_two_groups)
    {
        ObjectCluster cluster;
        const LodError error = buildObjectCluster(
            tileOf({ trianglePiece("a.dds", ClusterEffect{}), trianglePiece("a.dds", scroll(0.5f, 0), 1000),
                trianglePiece("a.dds", scroll(0, 0.5f), 2000) }),
            cluster);
        ASSERT_TRUE(error.ok()) << error.describe();
        ASSERT_EQ(cluster.mGroups.size(), 3u);
        EXPECT_TRUE(cluster.mGroups[0].mEffect.none());
        EXPECT_EQ(cluster.mGroups[1].mEffect, scroll(0, 0.5f)); // sorted by the speed after the kind
        EXPECT_EQ(cluster.mGroups[2].mEffect, scroll(0.5f, 0));
        for (const ClusterGroup& group : cluster.mGroups)
            EXPECT_EQ(group.mTexture, "a.dds");
    }

    TEST(LodClusterEffectBuild, equal_effects_share_one_group)
    {
        ObjectCluster cluster;
        const LodError error = buildObjectCluster(
            tileOf({ trianglePiece("a.dds", scroll(0.5f, 0)), trianglePiece("a.dds", scroll(0.5f, 0), 1000) }),
            cluster);
        ASSERT_TRUE(error.ok()) << error.describe();
        ASSERT_EQ(cluster.mGroups.size(), 1u);
        EXPECT_EQ(cluster.mGroups[0].mEffect, scroll(0.5f, 0));
        EXPECT_EQ(cluster.mGroups[0].mIndexCount, 6u);
    }

    TEST(LodClusterEffectBuild, an_invalid_effect_leaves_the_piece_out)
    {
        ObjectCluster cluster;
        ClusterEffect bad = scroll(1, 0);
        bad.mKind |= 0x80u;
        const LodError error = buildObjectCluster(tileOf({ trianglePiece("a.dds", bad) }), cluster);
        EXPECT_TRUE(error.ok()) << error.describe();
        EXPECT_TRUE(cluster.mGroups.empty());
    }

    // ---- layout version 2 ----------------------------------------------------------------------------------------

    constexpr std::size_t offVersion = 4;

    ObjectCluster builtCluster(const ClusterEffect& first, const ClusterEffect& second)
    {
        ObjectCluster cluster;
        const LodError error = buildObjectCluster(
            tileOf({ trianglePiece("a.dds", first), trianglePiece("a.dds", second, 1000) }), cluster);
        EXPECT_TRUE(error.ok()) << error.describe();
        return cluster;
    }

    std::vector<std::uint8_t> bytesOf(const ObjectCluster& cluster)
    {
        std::vector<std::uint8_t> bytes;
        const LodError error = writeObjectCluster(cluster, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
    }

    TEST(LodClusterEffectLayout, a_cluster_without_effects_is_still_written_as_version_one)
    {
        const std::vector<std::uint8_t> bytes = bytesOf(builtCluster(ClusterEffect{}, ClusterEffect{}));
        ASSERT_GT(bytes.size(), 8u);
        EXPECT_EQ(bytes[offVersion], 1u);
        EXPECT_EQ(bytes[offVersion + 1], 0u);
        ObjectCluster read;
        ASSERT_TRUE(readObjectCluster(bytes, read).ok());
        for (const ClusterGroup& group : read.mGroups)
            EXPECT_TRUE(group.mEffect.none());
    }

    TEST(LodClusterEffectLayout, a_cluster_with_an_effect_is_version_two_and_round_trips)
    {
        const ObjectCluster cluster = builtCluster(ClusterEffect{}, scroll(0.125f, -0.25f));
        const std::vector<std::uint8_t> bytes = bytesOf(cluster);
        EXPECT_EQ(bytes[offVersion], 2u);
        EXPECT_EQ(bytes[offVersion + 1], 0u);
        ObjectCluster read;
        const LodError error = readObjectCluster(bytes, read);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(read, cluster);
        ASSERT_EQ(read.mGroups.size(), 2u);
        EXPECT_TRUE(read.mGroups[0].mEffect.none());
        EXPECT_EQ(read.mGroups[1].mEffect, scroll(0.125f, -0.25f));
        std::vector<std::uint8_t> again;
        ASSERT_TRUE(writeObjectCluster(read, again).ok());
        EXPECT_EQ(again, bytes);
    }

    TEST(LodClusterEffectLayout, version_two_is_the_version_one_bytes_plus_one_record_per_group)
    {
        ObjectCluster withEffect;
        ASSERT_TRUE(buildObjectCluster(tileOf({ trianglePiece("a.dds", scroll(0.125f, -0.25f)) }), withEffect).ok());
        ASSERT_EQ(withEffect.mGroups.size(), 1u);
        ObjectCluster without = withEffect;
        without.mGroups[0].mEffect = ClusterEffect{};
        const std::vector<std::uint8_t> v2 = bytesOf(withEffect);
        const std::vector<std::uint8_t> v1 = bytesOf(without);
        ASSERT_EQ(v2.size(), v1.size() + clusterEffectRecordSize);
        // Everything before the effect table is the same but the version.
        for (std::size_t i = 0; i < v1.size(); ++i)
        {
            if (i != offVersion)
            {
                EXPECT_EQ(v2[i], v1[i]) << "byte " << i;
            }
        }
    }

    // The table of effect records starts at the 8-aligned end of the string table. Two groups of "a.dds" (5 bytes
    // each).
    std::size_t effectTableOffset(const std::vector<std::uint8_t>& bytes, std::size_t groups)
    {
        return bytes.size() - ((groups * clusterEffectRecordSize + 7) & ~std::size_t{ 7 });
    }

    void putU32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
            bytes[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }

    std::uint32_t floatBits(float value)
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    void expectRefusedAt(const std::vector<std::uint8_t>& bytes, std::size_t offset, const std::string& what)
    {
        ObjectCluster read;
        const LodError error = readObjectCluster(bytes, read);
        EXPECT_FALSE(error.ok()) << what;
        EXPECT_EQ(error.mOffset, offset) << what << ": " << error.describe();
    }

    TEST(LodClusterEffectLayout, hostile_effect_records_are_refused_at_their_byte)
    {
        const std::vector<std::uint8_t> good = bytesOf(builtCluster(ClusterEffect{}, scroll(0.5f, 0)));
        const std::size_t table = effectTableOffset(good, 2);
        const std::size_t second = table + clusterEffectRecordSize;

        {
            std::vector<std::uint8_t> bytes = good;
            putU32(bytes, second, 1u | 4u); // an unknown kind bit
            expectRefusedAt(bytes, second, "unknown kind");
        }
        {
            std::vector<std::uint8_t> bytes = good;
            putU32(bytes, second + 4, floatBits(std::numeric_limits<float>::quiet_NaN()));
            expectRefusedAt(bytes, second, "NaN velocity");
        }
        {
            std::vector<std::uint8_t> bytes = good;
            putU32(bytes, second + 4, floatBits(maxClusterUvVelocity * 4));
            expectRefusedAt(bytes, second, "velocity beyond the cap");
        }
        {
            std::vector<std::uint8_t> bytes = good;
            putU32(bytes, second + 4, floatBits(0.f)); // a scroll that does not move
            expectRefusedAt(bytes, second, "a zero scroll");
        }
        {
            std::vector<std::uint8_t> bytes = good;
            bytes[second + 13] = 1; // reserved
            expectRefusedAt(bytes, second + 13, "reserved byte");
        }
        {
            std::vector<std::uint8_t> bytes = good;
            putU32(bytes, table, 0);
            putU32(bytes, table + 4, floatBits(1.f)); // kind none with a velocity
            expectRefusedAt(bytes, table, "velocity without a kind");
        }
        {
            std::vector<std::uint8_t> bytes = good;
            putU32(bytes, second, 0);
            putU32(bytes, second + 4, 0);
            expectRefusedAt(bytes, table, "version 2 with no effect at all");
        }
    }

    TEST(LodClusterEffectLayout, a_version_two_header_on_version_one_bytes_and_the_reverse_are_refused)
    {
        {
            std::vector<std::uint8_t> bytes = bytesOf(builtCluster(ClusterEffect{}, ClusterEffect{}));
            bytes[offVersion] = 2; // no effect table follows
            ObjectCluster read;
            EXPECT_FALSE(readObjectCluster(bytes, read).ok());
        }
        {
            std::vector<std::uint8_t> bytes = bytesOf(builtCluster(ClusterEffect{}, scroll(0.5f, 0)));
            bytes[offVersion] = 1; // a table the version 1 layout does not have
            ObjectCluster read;
            EXPECT_FALSE(readObjectCluster(bytes, read).ok());
        }
        {
            std::vector<std::uint8_t> bytes = bytesOf(builtCluster(ClusterEffect{}, scroll(0.5f, 0)));
            bytes[offVersion] = 3;
            ObjectCluster read;
            const LodError error = readObjectCluster(bytes, read);
            EXPECT_FALSE(error.ok());
            EXPECT_EQ(error.mOffset, offVersion);
        }
    }

    TEST(LodClusterEffectLayout, every_truncation_and_an_extra_byte_are_refused)
    {
        const std::vector<std::uint8_t> good = bytesOf(builtCluster(ClusterEffect{}, scroll(0.5f, 0)));
        for (std::size_t length = 0; length < good.size(); ++length)
        {
            ObjectCluster read;
            EXPECT_FALSE(readObjectCluster(std::span<const std::uint8_t>(good.data(), length), read).ok())
                << "length " << length;
        }
        std::vector<std::uint8_t> extra = good;
        extra.push_back(0);
        ObjectCluster read;
        EXPECT_FALSE(readObjectCluster(extra, read).ok());
    }

    TEST(LodClusterEffectLayout, the_header_check_accepts_both_versions)
    {
        for (const ObjectCluster& cluster :
            { builtCluster(ClusterEffect{}, ClusterEffect{}), builtCluster(ClusterEffect{}, scroll(1, 0)) })
        {
            const std::vector<std::uint8_t> bytes = bytesOf(cluster);
            ClusterHeaderInfo info;
            const LodError error = readObjectClusterHeader(bytes, info);
            EXPECT_TRUE(error.ok()) << error.describe();
            EXPECT_EQ(info.mLevel, 1u);
        }
    }

    TEST(LodClusterEffectLayout, the_writer_refuses_an_invalid_effect_and_unsorted_equal_groups)
    {
        ObjectCluster cluster = builtCluster(ClusterEffect{}, scroll(0.5f, 0));
        ObjectCluster bad = cluster;
        bad.mGroups[1].mEffect.mKind |= 0x10u;
        std::vector<std::uint8_t> out;
        EXPECT_FALSE(writeObjectCluster(bad, out).ok());
        ObjectCluster twin = cluster;
        twin.mGroups[1].mEffect = ClusterEffect{}; // two groups of the same texture and state
        EXPECT_FALSE(writeObjectCluster(twin, out).ok());
    }

    TEST(LodClusterEffectLayout, an_effect_changes_only_the_groups_it_is_on)
    {
        const ObjectCluster a = builtCluster(scroll(0.5f, 0), ClusterEffect{});
        const ObjectCluster b = builtCluster(scroll(0.25f, 0), ClusterEffect{});
        EXPECT_NE(bytesOf(a), bytesOf(b));
    }

    // ---- the flattener -------------------------------------------------------------------------------------------

    class Model
    {
    public:
        Nif::NiNode& node(Nif::RecordType type = Nif::RC_NiNode)
        {
            Nif::NiNode& result = create<Nif::NiNode>(type);
            return result;
        }

        void attach(Nif::NiNode& parent, Nif::NiAVObject& child)
        {
            parent.mChildren.push_back(Nif::NiAVObjectPtr(&child));
            child.mParents.push_back(&parent);
        }

        Nif::NiTriShape& shape(Nif::NiNode& parent)
        {
            Nif::NiTriShape& result = create<Nif::NiTriShape>(Nif::RC_NiTriShape);
            Nif::NiTriShapeData& data = create<Nif::NiTriShapeData>(Nif::RC_NiTriShapeData);
            data.mNumVertices = 3;
            data.mVertices = { vsg::vec3(0, 0, 0), vsg::vec3(2, 0, 0), vsg::vec3(0, 2, 0) };
            data.mTriangles = { 0, 1, 2 };
            data.mNumTriangles = 1;
            result.mData = Nif::NiGeometryDataPtr(&data);
            attach(parent, result);
            return result;
        }

        struct Keys
        {
            std::vector<std::pair<float, float>> mPoints;
            std::uint32_t mInterpolation = Nif::InterpolationType_Linear;
            // For quadratic keys: tangents zero (eased), or equal to each segment's change (a straight line).
            enum class Tangents
            {
                Zero,
                FollowSegments
            } mTangents
                = Tangents::Zero;
        };

        static Nif::FloatKeyMapPtr keyMap(const Keys& keys)
        {
            auto map = std::make_shared<Nif::FloatKeyMap>();
            map->mInterpolationType = keys.mInterpolation;
            for (const auto& [time, value] : keys.mPoints)
                map->mKeys.emplace_back(time, Nif::FloatKeyMap::KeyType{ value, 0.f, 0.f });
            if (keys.mTangents == Keys::Tangents::FollowSegments)
                for (std::size_t i = 0; i + 1 < map->mKeys.size(); ++i)
                {
                    const float step = map->mKeys[i + 1].second.mValue - map->mKeys[i].second.mValue;
                    map->mKeys[i].second.mOutTan = step;
                    map->mKeys[i + 1].second.mInTan = step;
                }
            return map;
        }

        // A NiUVController on `target` with the given offset keys (u, v) and optional tiling keys.
        Nif::NiUVController& uvController(Nif::NiObjectNET& target, const Keys& u,
            const std::optional<Keys>& v = std::nullopt, float frequency = 1.f,
            std::uint16_t flags = Nif::NiTimeController::Flag_Active,
            const std::optional<Keys>& tilingU = std::nullopt)
        {
            Nif::NiUVController& controller = create<Nif::NiUVController>(Nif::RC_NiUVController);
            Nif::NiUVData& data = create<Nif::NiUVData>(Nif::RC_NiUVData);
            data.mKeyList[0] = keyMap(u);
            data.mKeyList[1] = keyMap(v.value_or(Keys{}));
            data.mKeyList[2] = keyMap(tilingU.value_or(Keys{}));
            data.mKeyList[3] = keyMap({});
            controller.mData = Nif::NiUVDataPtr(&data);
            controller.mUvSet = 0;
            controller.mFlags = flags;
            controller.mFrequency = frequency;
            controller.mPhase = 0;
            controller.mTimeStart = 0;
            controller.mTimeStop = 1;
            controller.mNext = Nif::NiTimeControllerPtr(nullptr);
            controller.mTarget = Nif::NiObjectNETPtr(&target);
            // Chain it behind whatever the target already has.
            if (target.mController.empty())
                target.mController = Nif::NiTimeControllerPtr(&controller);
            else
            {
                Nif::NiTimeController* last = target.mController.getPtr();
                while (!last->mNext.empty())
                    last = last->mNext.getPtr();
                last->mNext = Nif::NiTimeControllerPtr(&controller);
            }
            return controller;
        }

        // A controller of another kind (a keyframe controller) chained behind the target's others.
        void otherController(Nif::NiObjectNET& target)
        {
            Nif::NiTimeController& controller = create<Nif::NiTimeController>(Nif::RC_NiKeyframeController);
            controller.mFlags = Nif::NiTimeController::Flag_Active;
            if (target.mController.empty())
                target.mController = Nif::NiTimeControllerPtr(&controller);
            else
            {
                Nif::NiTimeController* last = target.mController.getPtr();
                while (!last->mNext.empty())
                    last = last->mNext.getPtr();
                last->mNext = Nif::NiTimeControllerPtr(&controller);
            }
        }

        // An alpha property with a controller (a pulse) on the shape.
        void alphaControlledProperty(Nif::NiAVObject& object)
        {
            Nif::NiAlphaProperty& property = create<Nif::NiAlphaProperty>(Nif::RC_NiAlphaProperty);
            property.mFlags = 0;
            property.mThreshold = 0;
            otherController(property);
            object.mProperties.push_back(Nif::RecordPtrT<Nif::NiProperty>(&property));
        }

        void plainProperty(Nif::NiAVObject& object)
        {
            Nif::NiMaterialProperty& property = create<Nif::NiMaterialProperty>(Nif::RC_NiMaterialProperty);
            property.mAlpha = 1.f;
            object.mProperties.push_back(Nif::RecordPtrT<Nif::NiProperty>(&property));
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
                result.mTransform.mRotation.mValues[0][0] = 1.f;
                result.mTransform.mRotation.mValues[1][1] = 1.f;
                result.mTransform.mRotation.mValues[2][2] = 1.f;
                result.mCollision = Nif::NiCollisionObjectPtr(nullptr);
            }
            if constexpr (std::is_base_of_v<Nif::NiGeometry, T>)
            {
                result.mData = Nif::NiGeometryDataPtr(nullptr);
                result.mSkin = Nif::NiSkinInstancePtr(nullptr);
                result.mShaderProperty = Nif::BSShaderPropertyPtr(nullptr);
                result.mAlphaProperty = Nif::NiAlphaPropertyPtr(nullptr);
            }
            if constexpr (std::is_same_v<T, Nif::NiTimeController>)
            {
                result.mNext = Nif::NiTimeControllerPtr(nullptr);
                result.mTarget = Nif::NiObjectNETPtr(nullptr);
            }
            mOwned.push_back(std::move(owned));
            return result;
        }

        std::vector<std::unique_ptr<Nif::Record>> mOwned;
    };

    // The straight line from 0 to `change` over `seconds` of key time.
    Model::Keys line(float change, float seconds = 1.f)
    {
        return Model::Keys{ { { 0.f, 0.f }, { seconds, change } } };
    }

    TEST(LodNifEffects, without_the_option_a_uv_controller_still_marks_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(1.f));
        const FlattenedModel result = flattenNifModel(root);
        EXPECT_TRUE(result.mTraits.mAnimated);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_TRUE(result.mPieces[0].mEffect.none());
    }

    TEST(LodNifEffects, a_straight_scroll_becomes_the_effect_of_the_pieces)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(0.5f, 2.f), line(-1.f, 2.f));
        const FlattenedModel result = flattenNifModel(root, false, true);
        EXPECT_FALSE(result.mTraits.mAnimated);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_EQ(result.mPieces[0].mEffect.mKind, clusterEffectUvScroll);
        EXPECT_FLOAT_EQ(result.mPieces[0].mEffect.mUvVelocity[0], 0.25f);
        EXPECT_FLOAT_EQ(result.mPieces[0].mEffect.mUvVelocity[1], -0.5f);
    }

    TEST(LodNifEffects, the_controllers_frequency_scales_the_speed)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(0.5f), {}, 3.f);
        const FlattenedModel result = flattenNifModel(root, false, true);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_FLOAT_EQ(result.mPieces[0].mEffect.mUvVelocity[0], 1.5f);
        EXPECT_FLOAT_EQ(result.mPieces[0].mEffect.mUvVelocity[1], 0.f);
    }

    TEST(LodNifEffects, only_the_shape_with_the_controller_scrolls)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& moving = model.shape(root);
        model.shape(root);
        model.uvController(moving, line(1.f));
        const FlattenedModel result = flattenNifModel(root, false, true);
        EXPECT_FALSE(result.mTraits.mAnimated);
        ASSERT_EQ(result.mPieces.size(), 2u);
        EXPECT_FALSE(result.mPieces[0].mEffect.none());
        EXPECT_TRUE(result.mPieces[1].mEffect.none());
    }

    TEST(LodNifEffects, keys_that_are_not_a_straight_line_leave_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, Model::Keys{ { { 0.f, 0.f }, { 0.5f, 0.9f }, { 1.f, 1.f } } });
        const FlattenedModel result = flattenNifModel(root, false, true);
        EXPECT_TRUE(result.mTraits.mAnimated);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_TRUE(result.mPieces[0].mEffect.none());
    }

    TEST(LodNifEffects, a_single_key_or_another_interpolation_is_not_a_scroll)
    {
        for (const Model::Keys& keys : { Model::Keys{ { { 0.f, 0.3f } } },
                 Model::Keys{ { { 0.f, 0.f }, { 1.f, 1.f } }, Nif::InterpolationType_Quadratic },
                 Model::Keys{ { { 0.f, 0.f }, { 1.f, 1.f } }, Nif::InterpolationType_Constant },
                 Model::Keys{ { { 1.f, 0.f }, { 1.f, 1.f } } } /* no time passes */ })
        {
            Model model;
            Nif::NiNode& root = model.node();
            Nif::NiTriShape& shape = model.shape(root);
            model.uvController(shape, keys);
            const FlattenedModel result = flattenNifModel(root, false, true);
            EXPECT_TRUE(result.mTraits.mAnimated);
        }
    }

    // Vanilla's UV keys: two "quadratic" keys per axis whose tangents equal the change (read from the installed lava,
    // waterfall and fence meshes by the bake's diagnostic: out tangent of key 0 and in tangent of key 1 equal the
    // step).
    TEST(LodNifEffects, quadratic_keys_whose_tangents_follow_the_line_are_a_scroll)
    {
        Model model;
        Nif::NiNode& root = model.node(Nif::RC_NiBSAnimationNode);
        Nif::NiTriShape& shape = model.shape(root);
        Model::Keys u{ { { 0.f, 0.f }, { 48.f, -2.f } }, Nif::InterpolationType_Quadratic,
            Model::Keys::Tangents::FollowSegments };
        model.uvController(shape, u, u);
        const FlattenedModel result = flattenNifModel(root, false, true);
        EXPECT_FALSE(result.mTraits.mAnimated);
        ASSERT_EQ(result.mPieces.size(), 1u);
        EXPECT_NEAR(result.mPieces[0].mEffect.mUvVelocity[0], -2.f / 48.f, 1e-6);
        EXPECT_NEAR(result.mPieces[0].mEffect.mUvVelocity[1], -2.f / 48.f, 1e-6);
    }

    TEST(LodNifEffects, quadratic_keys_with_eased_tangents_are_not_a_scroll)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape,
            Model::Keys{
                { { 0.f, 0.f }, { 48.f, -2.f } }, Nif::InterpolationType_Quadratic, Model::Keys::Tangents::Zero });
        EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, a_controller_that_does_not_cycle_or_is_not_active_is_not_a_scroll)
    {
        for (const std::uint16_t flags :
            { static_cast<std::uint16_t>(static_cast<std::uint16_t>(Nif::NiTimeController::Flag_Active)
                    | static_cast<std::uint16_t>(Nif::NiTimeController::Constant)),
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(Nif::NiTimeController::Flag_Active)
                    | static_cast<std::uint16_t>(Nif::NiTimeController::Reverse)),
                static_cast<std::uint16_t>(0) })
        {
            Model model;
            Nif::NiNode& root = model.node();
            Nif::NiTriShape& shape = model.shape(root);
            model.uvController(shape, line(1.f), {}, 1.f, flags);
            const FlattenedModel result = flattenNifModel(root, false, true);
            EXPECT_TRUE(result.mTraits.mAnimated) << "flags " << flags;
        }
    }

    TEST(LodNifEffects, an_animated_tiling_leaves_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(1.f), {}, 1.f, Nif::NiTimeController::Flag_Active, line(0.5f));
        EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, a_scroll_over_the_speed_cap_leaves_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(maxClusterUvVelocity * 10));
        EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, a_scroll_that_does_not_move_leaves_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(0.f));
        EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, another_controller_on_the_shape_or_a_second_scroll_leaves_the_model_animated)
    {
        {
            Model model;
            Nif::NiNode& root = model.node();
            Nif::NiTriShape& shape = model.shape(root);
            model.uvController(shape, line(1.f));
            model.otherController(shape);
            EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
        }
        {
            Model model;
            Nif::NiNode& root = model.node();
            Nif::NiTriShape& shape = model.shape(root);
            model.uvController(shape, line(1.f));
            model.uvController(shape, line(0.5f));
            EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
        }
        {
            Model model;
            Nif::NiNode& root = model.node();
            model.shape(root);
            model.otherController(root);
            EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
        }
    }

    TEST(LodNifEffects, a_uv_controller_on_a_group_node_is_not_an_effect)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.shape(root);
        model.uvController(root, line(1.f));
        EXPECT_TRUE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, an_animation_node_with_no_controller_is_animated_only_without_the_option)
    {
        Model model;
        Nif::NiNode& root = model.node(Nif::RC_NiBSAnimationNode);
        model.shape(root);
        EXPECT_TRUE(flattenNifModel(root).mTraits.mAnimated);
        EXPECT_FALSE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, an_animation_node_wrapping_a_scrolling_shape_gives_a_still_model_with_an_effect)
    {
        // The shape of vanilla's lava planes and the Ghostgate fence: an NiBSAnimationNode over a UV-scrolled shape.
        Model model;
        Nif::NiNode& root = model.node(Nif::RC_NiBSAnimationNode);
        Nif::NiTriShape& shape = model.shape(root);
        model.uvController(shape, line(0.2f), line(0.1f));
        const FlattenedModel off = flattenNifModel(root);
        EXPECT_TRUE(off.mTraits.mAnimated);
        const FlattenedModel on = flattenNifModel(root, false, true);
        EXPECT_FALSE(on.mTraits.mAnimated);
        ASSERT_EQ(on.mPieces.size(), 1u);
        EXPECT_FLOAT_EQ(on.mPieces[0].mEffect.mUvVelocity[0], 0.2f);
        EXPECT_FLOAT_EQ(on.mPieces[0].mEffect.mUvVelocity[1], 0.1f);
    }

    // Task 0096 stage a2: a controller on a property used to be missed.
    TEST(LodNifEffects, a_controller_on_a_property_marks_the_model_animated)
    {
        for (const bool effects : { false, true })
        {
            Model model;
            Nif::NiNode& root = model.node();
            Nif::NiTriShape& shape = model.shape(root);
            model.alphaControlledProperty(shape);
            const FlattenedModel result = flattenNifModel(root, false, effects);
            EXPECT_TRUE(result.mTraits.mAnimated) << "effects " << effects;
            EXPECT_EQ(result.mPieces.size(), 1u); // the geometry is still read
        }
    }

    TEST(LodNifEffects, a_property_without_a_controller_does_not)
    {
        Model model;
        Nif::NiNode& root = model.node();
        Nif::NiTriShape& shape = model.shape(root);
        model.plainProperty(shape);
        EXPECT_FALSE(flattenNifModel(root, false, false).mTraits.mAnimated);
        EXPECT_FALSE(flattenNifModel(root, false, true).mTraits.mAnimated);
    }

    TEST(LodNifEffects, a_property_controller_on_a_group_node_marks_the_model_animated)
    {
        Model model;
        Nif::NiNode& root = model.node();
        model.shape(root);
        model.alphaControlledProperty(root);
        EXPECT_TRUE(flattenNifModel(root).mTraits.mAnimated);
    }

    // ---- which scripts leave their activator alone ---------------------------------------------------------------

    TEST(LodSceneryScript, vanillas_lava_script_and_sound_loops_leave_the_object_alone)
    {
        EXPECT_FALSE(
            scriptChangesItsObject("begin lava\n\nif ( menumode == 1 )\n\treturn\nendif\n\n"
                                   "if ( CellChanged == 0 )\n\tif ( GetSoundPlaying \"lava layer\" == 0 )\n"
                                   "\t\tPlayLoopSound3DVP \"lava layer\", 1.0, 1.0\n\tendif\nendif\n\n"
                                   "HurtStandingActor, 20.0\t\t;20 pts of damage a sec\n\nend lava"));
        EXPECT_FALSE(scriptChangesItsObject(
            "begin Sound_Steam\nif ( CellChanged == 0 )\nplayloopsound3d \"steam\" 1 1\nendif\nend"));
        EXPECT_FALSE(scriptChangesItsObject(""));
    }

    TEST(LodSceneryScript, the_ghostgate_fence_script_that_disables_itself_is_refused)
    {
        EXPECT_TRUE(
            scriptChangesItsObject("Begin GhostfenceScript\nif ( GetDisabled == 1 )\n\tReturn\nendif\n"
                                   "if ( GetJournalIndex, C3_DestroyDagoth >= 20 )\n\tDisable\n"
                                   "\tstopsound \"x\"\nendif\nEnd"));
    }

    TEST(LodSceneryScript, each_command_that_changes_the_object_is_found_in_any_case)
    {
        for (const char* command : { "Enable", "DISABLE", "setpos z, 5", "SetAngle z 0", "setscale 2", "Move x 1",
                 "MoveWorld x 1", "rotate z 3", "RotateWorld z 3", "Position 0 0 0 0", "PositionCell 0 0 0 0 \"c\"",
                 "removeme", "SetDelete 1", "StartScript other", "stopscript other" })
            EXPECT_TRUE(scriptChangesItsObject(std::string("begin s\n") + command + "\nend")) << command;
    }

    TEST(LodSceneryScript, comments_strings_and_longer_words_do_not_count)
    {
        EXPECT_FALSE(scriptChangesItsObject("begin s\n; Disable the fence later\nend"));
        EXPECT_FALSE(scriptChangesItsObject("begin s\nPlaySound \"disable the move\"\nend"));
        EXPECT_FALSE(
            scriptChangesItsObject("begin s\nif ( GetDisabled == 1 )\nendif\nset moved to 1\nset enabled to 1\nend"));
        EXPECT_FALSE(scriptChangesItsObject("begin s\nset movement to 1\nset positions to 1\nend"));
        EXPECT_TRUE(scriptChangesItsObject("begin s\n; comment\nDisable ; comment\nend"));
    }
}
