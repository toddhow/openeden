#include <compat_morrowind/kf_animation.hpp>
#include <compat_morrowind/kf_converter.hpp>
#include <compat_morrowind/morrowind_compat_extension.hpp>

#include <components/extension/manifest_io.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/data.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/nif/record.hpp>

#include <opas/anim/ClipSidecar.hpp>
#include <opas/anim/GltfReader.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class MockRegistrar : public openeden::sdk::Registrar
    {
    public:
        struct ProvidedService
        {
            std::string contractId;
            openeden::sdk::Version version;
            void* implPointer = nullptr;
        };

        std::vector<ProvidedService> mProvidedServices;

        void provideService(std::string_view contractId, openeden::sdk::Version version, void* implPointer) override
        {
            mProvidedServices.push_back({ std::string(contractId), version, implPointer });
        }

        void registerPhaseCallback(
            openeden::sdk::Phase /*phase*/, openeden::sdk::PhaseCallback /*callback*/, bool /*parallel*/) override
        {
        }
    };

    constexpr float kTolerance = 1e-5f;
    constexpr float kHalfPi = 1.5707963267948966f;

    // ---- Synthetic Nif record helpers ------------------------------------------------------------------------
    // Every RecordPtrT the code under test may look at is set explicitly: a default-constructed RecordPtrT holds an
    // unresolved index, not a null pointer.

    Nif::Vector3KeyMapPtr makeVec3Keys(
        Nif::InterpolationType type, const std::vector<std::pair<float, vsg::vec3>>& keys)
    {
        auto map = std::make_shared<Nif::Vector3KeyMap>();
        map->mInterpolationType = type;
        for (const auto& [time, value] : keys)
            map->mKeys.emplace_back(time, Nif::KeyT<vsg::vec3>{ value, vsg::vec3(), vsg::vec3() });
        return map;
    }

    Nif::FloatKeyMapPtr makeFloatKeys(Nif::InterpolationType type, const std::vector<std::pair<float, float>>& keys)
    {
        auto map = std::make_shared<Nif::FloatKeyMap>();
        map->mInterpolationType = type;
        for (const auto& [time, value] : keys)
            map->mKeys.emplace_back(time, Nif::KeyT<float>{ value, 0.f, 0.f });
        return map;
    }

    Nif::QuaternionKeyMapPtr makeQuatKeys(
        Nif::InterpolationType type, const std::vector<std::pair<float, vsg::dquat>>& keys)
    {
        auto map = std::make_shared<Nif::QuaternionKeyMap>();
        map->mInterpolationType = type;
        for (const auto& [time, value] : keys)
            map->mKeys.emplace_back(time, Nif::KeyT<vsg::dquat>{ value, vsg::dquat(), vsg::dquat() });
        return map;
    }

    std::unique_ptr<Nif::NiKeyframeData> makeKeyframeData()
    {
        auto data = std::make_unique<Nif::NiKeyframeData>();
        data->mRecordType = Nif::RC_NiKeyframeData;
        return data;
    }

    std::unique_ptr<Nif::NiKeyframeController> makeController(Nif::NiKeyframeData* data)
    {
        auto ctrl = std::make_unique<Nif::NiKeyframeController>();
        ctrl->mRecordType = Nif::RC_NiKeyframeController;
        ctrl->mNext = Nif::NiTimeControllerPtr(nullptr);
        ctrl->mTarget = Nif::NiObjectNETPtr(nullptr);
        ctrl->mFlags = Nif::NiTimeController::Flag_Active;
        ctrl->mFrequency = 1.f;
        ctrl->mPhase = 0.f;
        ctrl->mTimeStart = 0.f;
        ctrl->mTimeStop = 0.f;
        ctrl->mInterpolator = Nif::NiInterpolatorPtr(nullptr);
        ctrl->mData = Nif::NiKeyframeDataPtr(data);
        return ctrl;
    }

    void expectVec3Near(const vsg::vec3& actual, const vsg::vec3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, kTolerance);
        EXPECT_NEAR(actual.y, expected.y, kTolerance);
        EXPECT_NEAR(actual.z, expected.z, kTolerance);
    }

    void expectQuatNear(const vsg::dquat& actual, const vsg::dquat& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, kTolerance);
        EXPECT_NEAR(actual.y, expected.y, kTolerance);
        EXPECT_NEAR(actual.z, expected.z, kTolerance);
        EXPECT_NEAR(actual.w, expected.w, kTolerance);
    }

    vsg::vec3 translationAt(const compat_morrowind::KfTrack& track, float time)
    {
        const auto t = track.getTransform(time);
        EXPECT_TRUE(t.mTranslation.has_value()) << "no translation at time " << time;
        return t.mTranslation.value_or(vsg::vec3(-999.f, -999.f, -999.f));
    }

    vsg::dquat rotationAt(const compat_morrowind::KfTrack& track, float time)
    {
        const auto t = track.getTransform(time);
        EXPECT_TRUE(t.mRotation.has_value()) << "no rotation at time " << time;
        return t.mRotation.value_or(vsg::dquat(-9.f, -9.f, -9.f, -9.f));
    }

    // Reference cubic Hermite, written out from NifOsg::ValueInterpolator::interpolate
    // (components/nifosg/controller.hpp).
    vsg::vec3 hermite(
        const vsg::vec3& a, const vsg::vec3& aOutTan, const vsg::vec3& b, const vsg::vec3& bInTan, float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float b1 = 2.f * t3 - 3.f * t2 + 1;
        const float b2 = -2.f * t3 + 3.f * t2;
        const float b3 = t3 - 2.f * t2 + t;
        const float b4 = t3 - t2;
        return a * b1 + b * b2 + aOutTan * b3 + bInTan * b4;
    }

    vsg::dquat xyz(float x, float y, float z, Nif::NiKeyframeData::AxisOrder order)
    {
        // Reference from NifOsg::KeyframeController::getXYZRotation (components/nifosg/controller.cpp).
        const vsg::dquat xr = Misc::VsgMath::makeRotateFromAxis(x, vsg::vec3(1.f, 0.f, 0.f));
        const vsg::dquat yr = Misc::VsgMath::makeRotateFromAxis(y, vsg::vec3(0.f, 1.f, 0.f));
        const vsg::dquat zr = Misc::VsgMath::makeRotateFromAxis(z, vsg::vec3(0.f, 0.f, 1.f));
        switch (order)
        {
            case Nif::NiKeyframeData::AxisOrder::Order_XYZ:
                return xr * yr * zr;
            case Nif::NiKeyframeData::AxisOrder::Order_ZYX:
                return zr * yr * xr;
            case Nif::NiKeyframeData::AxisOrder::Order_YZX:
                return yr * zr * xr;
            default:
                break;
        }
        ADD_FAILURE() << "reference not written for this axis order";
        return vsg::dquat();
    }
} // namespace

TEST(CompatMorrowindTest, ManifestValidation)
{
    std::filesystem::path manifestPath = "extensions/compat_morrowind/extension.json";
    if (!std::filesystem::exists(manifestPath))
    {
        // Check relative to source dir
        manifestPath = "../extensions/compat_morrowind/extension.json";
    }

    openeden::extension::LoadError err;
    auto manifestOpt = openeden::extension::loadManifestFile(manifestPath, err);
    ASSERT_TRUE(manifestOpt.has_value()) << "Manifest parse error: " << err.message << " at " << err.file << ":"
                                         << err.line;

    const auto& m = *manifestOpt;
    EXPECT_EQ(m.id, "compat_morrowind");
    EXPECT_EQ(m.kind, "compatibility");
    EXPECT_EQ(m.sdkVersion.major, 4);
    EXPECT_EQ(m.sdkVersion.minor, 0);

    bool hasGame = false;
    bool hasKfConverter = false;
    for (const auto& c : m.providedContracts)
    {
        if (c.contract == "openeden.game" && c.version == openeden::sdk::Version{ 1, 1 })
            hasGame = true;
        if (c.contract == "openeden.compat.kf_converter" && c.version == openeden::sdk::Version{ 1, 0 })
            hasKfConverter = true;
    }

    EXPECT_TRUE(hasGame);
    EXPECT_TRUE(hasKfConverter);
}

TEST(CompatMorrowindTest, ExtensionRegistrationAndGameContract)
{
    compat_morrowind::MorrowindCompatExtension extension;
    MockRegistrar registrar;

    extension.registerServices(registrar);

    ASSERT_EQ(registrar.mProvidedServices.size(), 2);

    auto* gameContract = static_cast<openeden::contracts::GameContract*>(registrar.mProvidedServices[0].implPointer);
    ASSERT_NE(gameContract, nullptr);
    EXPECT_EQ(gameContract->getGameId(), "morrowind");
    EXPECT_EQ(gameContract->getGameDisplayName(), "The Elder Scrolls III: Morrowind");
    EXPECT_NEAR(gameContract->getUnitsPerMeter(), 69.9912f, 0.001f);
    EXPECT_EQ(gameContract->getDefaultHumanoidSkeleton(), "meshes/base_anim.nif");

    auto* kfConverter
        = static_cast<openeden::contracts::KfConverterContract*>(registrar.mProvidedServices[1].implPointer);
    ASSERT_NE(kfConverter, nullptr);
}

TEST(CompatMorrowindTest, KfToGltfConversionAndCreativeMotionIngestion)
{
    // 1. Build a synthetic KfAnimation with linear translations, slerped rotations, and text keys
    compat_morrowind::KfAnimation animation;

    // Set up text keys for walkforward animation group
    animation.mTextKeys.emplace(0.0f, "walkforward: start");
    animation.mTextKeys.emplace(0.4f, "walkforward: loop start");
    animation.mTextKeys.emplace(0.8f, "sound: FootBareLeft");
    animation.mTextKeys.emplace(1.6f, "walkforward: loop stop");
    animation.mTextKeys.emplace(2.0f, "walkforward: stop");

    // Set up a bone controller
    auto transMap = std::make_shared<Nif::Vector3KeyMap>();
    transMap->mInterpolationType = Nif::InterpolationType_Linear;
    Nif::KeyT<vsg::vec3> k0{ vsg::vec3(0.0f, 0.0f, 70.0f), {}, {} };
    Nif::KeyT<vsg::vec3> k1{ vsg::vec3(0.0f, 140.0f, 70.0f), {}, {} };
    transMap->mKeys.emplace_back(0.0f, k0);
    transMap->mKeys.emplace_back(2.0f, k1);

    auto rotMap = std::make_shared<Nif::QuaternionKeyMap>();
    rotMap->mInterpolationType = Nif::InterpolationType_Linear;
    Nif::KeyT<vsg::dquat> r0{ vsg::dquat(0.0f, 0.0f, 0.0f, 1.0f), {}, {} };
    Nif::KeyT<vsg::dquat> r1{ vsg::dquat(0.0f, 0.0f, 0.7071068f, 0.7071068f), {}, {} }; // 90 degree yaw
    rotMap->mKeys.emplace_back(0.0f, r0);
    rotMap->mKeys.emplace_back(2.0f, r1);

    // Create a mock NiKeyframeController with our synthetic data
    auto kfData = std::make_unique<Nif::NiKeyframeData>();
    kfData->mTranslations = transMap;
    kfData->mRotations = rotMap;

    auto ctrl = std::make_unique<Nif::NiKeyframeController>();
    ctrl->mInterpolator = Nif::NiInterpolatorPtr(nullptr);
    ctrl->mData = Nif::NiKeyframeDataPtr(kfData.get());

    animation.mTracks["bip01 pelvis"] = compat_morrowind::KfTrack(*ctrl);

    // Skeletons
    std::vector<compat_morrowind::BoneInfo> bones;
    compat_morrowind::BoneInfo pelvis;
    pelvis.name = "bip01 pelvis";
    pelvis.restTranslation = vsg::vec3(0.0f, 0.0f, 70.0f);
    bones.push_back(pelvis);

    // 2. Perform Conversion
    compat_morrowind::KfConverter converter;
    openeden::contracts::KfConverterContract::ConversionOptions options;
    options.unitsPerMeter = 70.0f;
    options.resampleRateHz = 60;

    auto convResult = converter.convertParsedKfToGltf(animation, bones, options);
    ASSERT_TRUE(convResult.success) << "Conversion failed: " << convResult.errorMessage;
    ASSERT_GE(convResult.glbData.size(), 20u);

    // Check GLB Header: magic = 0x46546C67 ("glTF"), version = 2
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t totalLen = 0;
    std::memcpy(&magic, convResult.glbData.data(), 4);
    std::memcpy(&version, convResult.glbData.data() + 4, 4);
    std::memcpy(&totalLen, convResult.glbData.data() + 8, 4);

    EXPECT_EQ(magic, 0x46546C67u);
    EXPECT_EQ(version, 2u);
    EXPECT_EQ(totalLen, convResult.glbData.size());

    // Check Sidecar JSON
    ASSERT_FALSE(convResult.sidecarJson.empty());
    auto sidecarJson = nlohmann::json::parse(convResult.sidecarJson);
    EXPECT_EQ(sidecarJson["format"], "opas.clipsidecar");
    EXPECT_EQ(sidecarJson["version"], 1);
    ASSERT_EQ(sidecarJson["clips"].size(), 1u);
    EXPECT_EQ(sidecarJson["clips"][0]["clip"], "walkforward");
    EXPECT_EQ(sidecarJson["clips"][0]["events"].size(), 5u);

    // 3. Feed directly into CreativeMotion's GltfReader!
    std::span<const std::byte> glbSpan(
        reinterpret_cast<const std::byte*>(convResult.glbData.data()), convResult.glbData.size());

    opas::anim::GltfReadOptions gltfOpts;
    gltfOpts.loadSidecar = false; // We will test applyClipSidecar explicitly

    auto gltfResult = opas::anim::readGltf(glbSpan, "walkforward.glb", gltfOpts);
    ASSERT_TRUE(gltfResult.value.has_value())
        << "CreativeMotion failed to read generated GLB: " << gltfResult.error.message;

    auto& anims = *gltfResult.value;
    ASSERT_EQ(anims.clips.size(), 1u);
    const auto& clip = anims.clips[0];
    EXPECT_EQ(clip.name, "walkforward");
    EXPECT_NEAR(clip.durationS, 2.0f, 0.02f);

    // 4. Test CreativeMotion Sidecar Parsing & Application
    auto sidecarLoadResult = opas::anim::loadClipSidecar(convResult.sidecarJson, "walkforward.events.json");
    ASSERT_TRUE(sidecarLoadResult.value.has_value()) << "Failed to parse sidecar: " << sidecarLoadResult.error.message;

    opas::anim::applyClipSidecar(sidecarLoadResult.value.value(), anims.clips);
    EXPECT_EQ(anims.clips[0].events.size(), 5u);
    EXPECT_EQ(anims.clips[0].events[0].name, "walkforward: start");
    EXPECT_NEAR(anims.clips[0].events[0].timeS, 0.0f, 0.001f);
    EXPECT_EQ(anims.clips[0].events[1].name, "walkforward: loop start");
    EXPECT_NEAR(anims.clips[0].events[1].timeS, 0.4f, 0.001f);
    EXPECT_EQ(anims.clips[0].events[4].name, "walkforward: stop");
    EXPECT_NEAR(anims.clips[0].events[4].timeS, 2.0f, 0.001f);
}

// ---- KfTrack: evaluation semantics of NifOsg::KeyframeController::getTransform ------------------------------------

TEST(CompatMorrowindKfTrackTest, DefaultTrackHasNoChannels)
{
    const compat_morrowind::KfTrack track;
    EXPECT_FALSE(track.hasTranslation());
    EXPECT_FALSE(track.hasRotation());
    const auto t = track.getTransform(0.5f);
    EXPECT_FALSE(t.mRotation.has_value());
    EXPECT_FALSE(t.mTranslation.has_value());
    EXPECT_FALSE(t.mScale.has_value());
}

TEST(CompatMorrowindKfTrackTest, LinearTranslationExactAtKeysLinearBetweenClampedOutside)
{
    auto data = makeKeyframeData();
    data->mTranslations = makeVec3Keys(Nif::InterpolationType_Linear,
        { { 1.f, vsg::vec3(0.f, 0.f, 0.f) }, { 3.f, vsg::vec3(10.f, 20.f, -40.f) },
            { 4.f, vsg::vec3(20.f, 20.f, -40.f) } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_TRUE(track.hasTranslation());
    EXPECT_FALSE(track.hasRotation());

    // Clamped before the first key
    expectVec3Near(translationAt(track, -5.f), vsg::vec3(0.f, 0.f, 0.f));
    expectVec3Near(translationAt(track, 0.f), vsg::vec3(0.f, 0.f, 0.f));
    // Exact at keys
    expectVec3Near(translationAt(track, 1.f), vsg::vec3(0.f, 0.f, 0.f));
    expectVec3Near(translationAt(track, 3.f), vsg::vec3(10.f, 20.f, -40.f));
    expectVec3Near(translationAt(track, 4.f), vsg::vec3(20.f, 20.f, -40.f));
    // Linear between
    expectVec3Near(translationAt(track, 2.f), vsg::vec3(5.f, 10.f, -20.f));
    expectVec3Near(translationAt(track, 1.5f), vsg::vec3(2.5f, 5.f, -10.f));
    expectVec3Near(translationAt(track, 3.5f), vsg::vec3(15.f, 20.f, -40.f));
    // Clamped after the last key
    expectVec3Near(translationAt(track, 4.5f), vsg::vec3(20.f, 20.f, -40.f));
    expectVec3Near(translationAt(track, 100.f), vsg::vec3(20.f, 20.f, -40.f));

    // Evaluating out of order gives the same answers (no stale cached segment)
    expectVec3Near(translationAt(track, 3.5f), vsg::vec3(15.f, 20.f, -40.f));
    expectVec3Near(translationAt(track, 1.5f), vsg::vec3(2.5f, 5.f, -10.f));
    expectVec3Near(translationAt(track, 2.f), vsg::vec3(5.f, 10.f, -20.f));

    const auto t = track.getTransform(2.f);
    EXPECT_FALSE(t.mRotation.has_value());
    EXPECT_FALSE(t.mScale.has_value());
}

TEST(CompatMorrowindKfTrackTest, SingleTranslationKeyHoldsEverywhere)
{
    auto data = makeKeyframeData();
    data->mTranslations = makeVec3Keys(Nif::InterpolationType_Linear, { { 1.f, vsg::vec3(1.f, 2.f, 3.f) } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectVec3Near(translationAt(track, 0.f), vsg::vec3(1.f, 2.f, 3.f));
    expectVec3Near(translationAt(track, 1.f), vsg::vec3(1.f, 2.f, 3.f));
    expectVec3Near(translationAt(track, 7.f), vsg::vec3(1.f, 2.f, 3.f));
}

TEST(CompatMorrowindKfTrackTest, ConstantKeysSwitchOnlyPastTheMidpoint)
{
    auto data = makeKeyframeData();
    data->mTranslations = makeVec3Keys(
        Nif::InterpolationType_Constant, { { 0.f, vsg::vec3(1.f, 1.f, 1.f) }, { 2.f, vsg::vec3(5.f, 6.f, 7.f) } });
    data->mScales = makeFloatKeys(Nif::InterpolationType_Constant, { { 0.f, 1.f }, { 2.f, 3.f } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectVec3Near(translationAt(track, 0.5f), vsg::vec3(1.f, 1.f, 1.f));
    // fraction exactly 0.5 keeps the earlier key (the switch needs fraction > 0.5)
    expectVec3Near(translationAt(track, 1.0f), vsg::vec3(1.f, 1.f, 1.f));
    expectVec3Near(translationAt(track, 1.1f), vsg::vec3(5.f, 6.f, 7.f));
    expectVec3Near(translationAt(track, 2.0f), vsg::vec3(5.f, 6.f, 7.f));

    auto scaleAt = [&](float time) { return track.getTransform(time).mScale; };
    ASSERT_TRUE(scaleAt(1.0f).has_value());
    EXPECT_NEAR(*scaleAt(1.0f), 1.f, kTolerance);
    EXPECT_NEAR(*scaleAt(0.9f), 1.f, kTolerance);
    EXPECT_NEAR(*scaleAt(1.2f), 3.f, kTolerance);
}

TEST(CompatMorrowindKfTrackTest, QuadraticVectorKeysUseCubicHermite)
{
    const vsg::vec3 a(0.f, 0.f, 0.f);
    const vsg::vec3 aOut(1.f, 2.f, 0.f);
    const vsg::vec3 b(4.f, 0.f, 2.f);
    const vsg::vec3 bIn(3.f, 0.f, -1.f);

    auto map = std::make_shared<Nif::Vector3KeyMap>();
    map->mInterpolationType = Nif::InterpolationType_Quadratic;
    map->mKeys.emplace_back(0.f, Nif::KeyT<vsg::vec3>{ a, vsg::vec3(9.f, 9.f, 9.f), aOut });
    map->mKeys.emplace_back(2.f, Nif::KeyT<vsg::vec3>{ b, bIn, vsg::vec3(-9.f, -9.f, -9.f) });

    auto data = makeKeyframeData();
    data->mTranslations = map;
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectVec3Near(translationAt(track, 0.f), a);
    expectVec3Near(translationAt(track, 2.f), b);
    expectVec3Near(translationAt(track, 0.5f), hermite(a, aOut, b, bIn, 0.25f));
    expectVec3Near(translationAt(track, 1.0f), hermite(a, aOut, b, bIn, 0.5f));
    expectVec3Near(translationAt(track, 1.5f), hermite(a, aOut, b, bIn, 0.75f));
    expectVec3Near(translationAt(track, 3.0f), b);

    // Sanity: the curve is not the linear one
    const vsg::vec3 linearMid = (a + b) * 0.5f;
    EXPECT_GT(vsg::length(translationAt(track, 1.0f) - linearMid), 0.1f);
}

TEST(CompatMorrowindKfTrackTest, TcbVectorKeysUseCubicHermiteWithStoredTangents)
{
    const vsg::vec3 a(1.f, -1.f, 0.f);
    const vsg::vec3 aOut(0.5f, 0.f, 2.f);
    const vsg::vec3 b(2.f, 3.f, 1.f);
    const vsg::vec3 bIn(-1.f, 1.f, 0.f);

    auto map = std::make_shared<Nif::Vector3KeyMap>();
    map->mInterpolationType = Nif::InterpolationType_TCB;
    map->mKeys.emplace_back(1.f, Nif::KeyT<vsg::vec3>{ a, vsg::vec3(), aOut });
    map->mKeys.emplace_back(5.f, Nif::KeyT<vsg::vec3>{ b, bIn, vsg::vec3() });

    auto data = makeKeyframeData();
    data->mTranslations = map;
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectVec3Near(translationAt(track, 2.f), hermite(a, aOut, b, bIn, 0.25f));
    expectVec3Near(translationAt(track, 4.6f), hermite(a, aOut, b, bIn, 0.9f));
}

TEST(CompatMorrowindKfTrackTest, QuaternionKeysSlerp)
{
    const vsg::dquat q0 = Misc::VsgMath::makeRotateFromAxis(0.f, vsg::vec3(0.f, 0.f, 1.f));
    const vsg::dquat q1 = Misc::VsgMath::makeRotateFromAxis(kHalfPi, vsg::vec3(0.f, 0.f, 1.f));
    const vsg::dquat q2 = Misc::VsgMath::makeRotateFromAxis(kHalfPi, vsg::vec3(1.f, 0.f, 0.f));

    auto data = makeKeyframeData();
    data->mRotations = makeQuatKeys(Nif::InterpolationType_Linear, { { 0.f, q0 }, { 2.f, q1 }, { 3.f, q2 } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_TRUE(track.hasRotation());
    EXPECT_FALSE(track.hasTranslation());

    expectQuatNear(rotationAt(track, -1.f), q0);
    expectQuatNear(rotationAt(track, 0.f), q0);
    expectQuatNear(rotationAt(track, 2.f), q1);
    expectQuatNear(rotationAt(track, 1.f), Misc::VsgMath::makeRotateFromAxis(kHalfPi * 0.5f, vsg::vec3(0.f, 0.f, 1.f)));
    expectQuatNear(
        rotationAt(track, 0.5f), Misc::VsgMath::makeRotateFromAxis(kHalfPi * 0.25f, vsg::vec3(0.f, 0.f, 1.f)));

    const vsg::dquat expected = Misc::VsgMath::slerp(0.3f, q1, q2);
    expectQuatNear(rotationAt(track, 2.3f), expected);

    expectQuatNear(rotationAt(track, 9.f), q2);

    const auto t = track.getTransform(1.f);
    EXPECT_FALSE(t.mTranslation.has_value());
    EXPECT_FALSE(t.mScale.has_value());
}

TEST(CompatMorrowindKfTrackTest, ConstantQuaternionKeysSwitchOnlyPastTheMidpoint)
{
    const vsg::dquat q0 = Misc::VsgMath::makeRotateFromAxis(0.2f, vsg::vec3(0.f, 1.f, 0.f));
    const vsg::dquat q1 = Misc::VsgMath::makeRotateFromAxis(1.1f, vsg::vec3(0.f, 1.f, 0.f));

    auto data = makeKeyframeData();
    data->mRotations = makeQuatKeys(Nif::InterpolationType_Constant, { { 0.f, q0 }, { 1.f, q1 } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectQuatNear(rotationAt(track, 0.25f), q0);
    expectQuatNear(rotationAt(track, 0.5f), q0);
    expectQuatNear(rotationAt(track, 0.51f), q1);
    expectQuatNear(rotationAt(track, 1.f), q1);
}

TEST(CompatMorrowindKfTrackTest, XyzRotationKeysCombineInXyzOrder)
{
    auto data = makeKeyframeData();
    data->mXRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.f }, { 2.f, 1.f } });
    data->mYRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.5f } });
    data->mZRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.f }, { 2.f, -0.8f } });
    data->mAxisOrder = Nif::NiKeyframeData::AxisOrder::Order_XYZ;
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_TRUE(track.hasRotation());
    EXPECT_EQ(track.getRotationKeys(), nullptr);

    const auto order = Nif::NiKeyframeData::AxisOrder::Order_XYZ;
    expectQuatNear(rotationAt(track, 0.f), xyz(0.f, 0.5f, 0.f, order));
    expectQuatNear(rotationAt(track, 1.f), xyz(0.5f, 0.5f, -0.4f, order));
    expectQuatNear(rotationAt(track, 2.f), xyz(1.f, 0.5f, -0.8f, order));
    expectQuatNear(rotationAt(track, 5.f), xyz(1.f, 0.5f, -0.8f, order));
}

TEST(CompatMorrowindKfTrackTest, XyzRotationKeysCombineInZyxOrder)
{
    auto data = makeKeyframeData();
    data->mXRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.f }, { 2.f, 1.f } });
    data->mYRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.5f } });
    data->mZRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.f }, { 2.f, -0.8f } });
    data->mAxisOrder = Nif::NiKeyframeData::AxisOrder::Order_ZYX;
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    const auto zyx = Nif::NiKeyframeData::AxisOrder::Order_ZYX;
    const auto xyzOrder = Nif::NiKeyframeData::AxisOrder::Order_XYZ;
    const vsg::dquat expected = xyz(0.5f, 0.5f, -0.4f, zyx);
    expectQuatNear(rotationAt(track, 1.f), expected);

    // The order matters for these angles, so the test can tell ZYX from XYZ
    const vsg::dquat other = xyz(0.5f, 0.5f, -0.4f, xyzOrder);
    EXPECT_GT(std::abs(expected.x - other.x) + std::abs(expected.y - other.y) + std::abs(expected.z - other.z)
            + std::abs(expected.w - other.w),
        1e-3f);
}

TEST(CompatMorrowindKfTrackTest, XyzRotationKeysCombineInYzxOrder)
{
    auto data = makeKeyframeData();
    data->mXRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.3f } });
    data->mYRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, -0.6f } });
    data->mZRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 1.2f } });
    data->mAxisOrder = Nif::NiKeyframeData::AxisOrder::Order_YZX;
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectQuatNear(rotationAt(track, 0.f), xyz(0.3f, -0.6f, 1.2f, Nif::NiKeyframeData::AxisOrder::Order_YZX));
}

TEST(CompatMorrowindKfTrackTest, SingleAxisRotationKeysTreatMissingAxesAsZero)
{
    auto data = makeKeyframeData();
    data->mZRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.f }, { 1.f, kHalfPi } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_TRUE(track.hasRotation());
    EXPECT_FALSE(track.hasTranslation());
    expectQuatNear(
        rotationAt(track, 0.5f), Misc::VsgMath::makeRotateFromAxis(kHalfPi * 0.5f, vsg::vec3(0.f, 0.f, 1.f)));
    EXPECT_FALSE(track.getTransform(0.5f).mTranslation.has_value());
}

TEST(CompatMorrowindKfTrackTest, QuaternionKeysTakePrecedenceOverXyzKeys)
{
    const vsg::dquat q = Misc::VsgMath::makeRotateFromAxis(0.7f, vsg::vec3(0.f, 1.f, 0.f));
    auto data = makeKeyframeData();
    data->mRotations = makeQuatKeys(Nif::InterpolationType_Linear, { { 0.f, q } });
    data->mXRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 1.f } });
    data->mZRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, -1.f } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    expectQuatNear(rotationAt(track, 0.f), q);
    expectQuatNear(rotationAt(track, 3.f), q);
}

TEST(CompatMorrowindKfTrackTest, EmptyKeyMapsReportNoChannel)
{
    auto data = makeKeyframeData();
    // Maps that exist but hold no keys count as absent, like ValueInterpolator::empty()
    data->mRotations = makeQuatKeys(Nif::InterpolationType_Linear, {});
    data->mXRotations = makeFloatKeys(Nif::InterpolationType_Linear, {});
    data->mTranslations = makeVec3Keys(Nif::InterpolationType_Linear, {});
    data->mScales = makeFloatKeys(Nif::InterpolationType_Linear, {});
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_FALSE(track.hasRotation());
    EXPECT_FALSE(track.hasTranslation());
    const auto t = track.getTransform(0.f);
    EXPECT_FALSE(t.mRotation.has_value());
    EXPECT_FALSE(t.mTranslation.has_value());
    EXPECT_FALSE(t.mScale.has_value());
}

TEST(CompatMorrowindKfTrackTest, ScaleKeysInterpolateLinearly)
{
    auto data = makeKeyframeData();
    data->mScales = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 1.f }, { 1.f, 2.f } });
    auto ctrl = makeController(data.get());
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_FALSE(track.hasRotation());
    EXPECT_FALSE(track.hasTranslation());
    const auto t = track.getTransform(0.25f);
    ASSERT_TRUE(t.mScale.has_value());
    EXPECT_NEAR(*t.mScale, 1.25f, kTolerance);
    EXPECT_FALSE(t.mRotation.has_value());
    EXPECT_FALSE(t.mTranslation.has_value());
    EXPECT_NEAR(*track.getTransform(-1.f).mScale, 1.f, kTolerance);
    EXPECT_NEAR(*track.getTransform(4.f).mScale, 2.f, kTolerance);
}

TEST(CompatMorrowindKfTrackTest, ControllerWithoutDataOrInterpolatorGivesEmptyTrack)
{
    auto ctrl = makeController(nullptr);
    const compat_morrowind::KfTrack track(*ctrl);
    EXPECT_FALSE(track.hasRotation());
    EXPECT_FALSE(track.hasTranslation());
    const auto t = track.getTransform(0.f);
    EXPECT_FALSE(t.mRotation.has_value());
    EXPECT_FALSE(t.mTranslation.has_value());
    EXPECT_FALSE(t.mScale.has_value());
}

TEST(CompatMorrowindKfTrackTest, TrackSharesKeyMapsAndOutlivesTheRecords)
{
    auto translations = makeVec3Keys(
        Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(0.f, 0.f, 0.f) }, { 1.f, vsg::vec3(2.f, 0.f, 0.f) } });
    auto rotations = makeQuatKeys(
        Nif::InterpolationType_Linear, { { 0.f, Misc::VsgMath::makeRotateFromAxis(0.4f, vsg::vec3(0.f, 0.f, 1.f)) } });

    compat_morrowind::KfTrack track;
    {
        auto data = makeKeyframeData();
        data->mTranslations = translations;
        data->mRotations = rotations;
        auto ctrl = makeController(data.get());
        track = compat_morrowind::KfTrack(*ctrl);
        EXPECT_EQ(track.getTranslationKeys(), translations.get());
        EXPECT_EQ(track.getRotationKeys(), rotations.get());
    }
    translations.reset();
    rotations.reset();

    // The records and the test's references are gone; the track still owns the key maps.
    expectVec3Near(translationAt(track, 0.5f), vsg::vec3(1.f, 0.f, 0.f));
    expectQuatNear(rotationAt(track, 0.5f), Misc::VsgMath::makeRotateFromAxis(0.4f, vsg::vec3(0.f, 0.f, 1.f)));

    // And the track copies like a value
    const compat_morrowind::KfTrack copy = track;
    expectVec3Near(translationAt(copy, 0.25f), vsg::vec3(0.5f, 0.f, 0.f));
}

// ---- KfTrack from an NiTransformInterpolator (NifOsg::KeyframeController constructor's first branch) -------------

TEST(CompatMorrowindKfTrackTest, TransformInterpolatorDataIsUsedInsteadOfControllerData)
{
    auto interpData = makeKeyframeData();
    interpData->mTranslations = makeVec3Keys(
        Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(0.f, 0.f, 0.f) }, { 2.f, vsg::vec3(0.f, 4.f, 0.f) } });
    interpData->mXRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.f }, { 2.f, 1.f } });
    interpData->mYRotations = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 0.25f } });
    interpData->mAxisOrder = Nif::NiKeyframeData::AxisOrder::Order_ZYX;
    interpData->mScales = makeFloatKeys(Nif::InterpolationType_Linear, { { 0.f, 2.f } });

    Nif::NiTransformInterpolator interp;
    interp.mRecordType = Nif::RC_NiTransformInterpolator;
    interp.mDefaultValue.mTranslation = vsg::vec3(7.f, 7.f, 7.f);
    interp.mDefaultValue.mRotation = Misc::VsgMath::makeRotateFromAxis(0.9f, vsg::vec3(1.f, 0.f, 0.f));
    interp.mDefaultValue.mScale = 5.f;
    interp.mData = Nif::NiKeyframeDataPtr(interpData.get());

    // Controller data that must be ignored because the controller has an interpolator
    auto ctrlData = makeKeyframeData();
    ctrlData->mTranslations = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(-50.f, 0.f, 0.f) } });

    auto ctrl = makeController(ctrlData.get());
    ctrl->mInterpolator = Nif::NiInterpolatorPtr(&interp);
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_TRUE(track.hasTranslation());
    EXPECT_TRUE(track.hasRotation());
    EXPECT_EQ(track.getTranslationKeys(), interpData->mTranslations.get());

    const auto t = track.getTransform(1.f);
    ASSERT_TRUE(t.mTranslation.has_value());
    expectVec3Near(*t.mTranslation, vsg::vec3(0.f, 2.f, 0.f));
    ASSERT_TRUE(t.mRotation.has_value());
    expectQuatNear(*t.mRotation, xyz(0.5f, 0.25f, 0.f, Nif::NiKeyframeData::AxisOrder::Order_ZYX));
    ASSERT_TRUE(t.mScale.has_value());
    EXPECT_NEAR(*t.mScale, 2.f, kTolerance);
}

TEST(CompatMorrowindKfTrackTest, TransformInterpolatorWithoutDataReportsNoChannels)
{
    // In OpenMW the interpolator's default transform only seeds ValueInterpolator's default value, which
    // KeyframeController::getTransform never returns because it checks empty() first.
    Nif::NiTransformInterpolator interp;
    interp.mRecordType = Nif::RC_NiTransformInterpolator;
    interp.mDefaultValue.mTranslation = vsg::vec3(7.f, 7.f, 7.f);
    interp.mDefaultValue.mRotation = Misc::VsgMath::makeRotateFromAxis(0.9f, vsg::vec3(1.f, 0.f, 0.f));
    interp.mDefaultValue.mScale = 5.f;
    interp.mData = Nif::NiKeyframeDataPtr(nullptr);

    auto ctrl = makeController(nullptr);
    ctrl->mInterpolator = Nif::NiInterpolatorPtr(&interp);
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_FALSE(track.hasTranslation());
    EXPECT_FALSE(track.hasRotation());
    const auto t = track.getTransform(0.f);
    EXPECT_FALSE(t.mRotation.has_value());
    EXPECT_FALSE(t.mTranslation.has_value());
    EXPECT_FALSE(t.mScale.has_value());
}

TEST(CompatMorrowindKfTrackTest, NonTransformInterpolatorIgnoresControllerData)
{
    Nif::NiFloatInterpolator interp;
    interp.mRecordType = Nif::RC_NiFloatInterpolator;
    interp.mDefaultValue = 0.f;
    interp.mData = Nif::NiFloatDataPtr(nullptr);

    auto ctrlData = makeKeyframeData();
    ctrlData->mTranslations = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(1.f, 2.f, 3.f) } });
    auto ctrl = makeController(ctrlData.get());
    ctrl->mInterpolator = Nif::NiInterpolatorPtr(&interp);
    const compat_morrowind::KfTrack track(*ctrl);

    EXPECT_FALSE(track.hasTranslation());
    EXPECT_FALSE(track.hasRotation());
    EXPECT_FALSE(track.getTransform(0.f).mTranslation.has_value());
}

// ---- loadKfAnimation: NifOsg::Loader::loadKf and extractTextKeys ------------------------------------------------

namespace
{
    constexpr VFS::Path::NormalizedView testKf("test.kf");

    /// A synthetic .kf: an NiSequenceStreamHelper root whose extra list is text keys followed by one
    /// NiStringExtraData per bone, paired with its controller chain.
    struct SyntheticKf
    {
        Nif::NIFFile mFile{ testKf };
        Nif::NiSequenceStreamHelper mSeq;
        Nif::NiTextKeyExtraData mTextKeys;
        std::vector<std::unique_ptr<Nif::NiStringExtraData>> mNames;
        std::vector<std::unique_ptr<Nif::NiKeyframeData>> mData;
        std::vector<std::unique_ptr<Nif::NiKeyframeController>> mControllers;

        SyntheticKf()
        {
            mSeq.mRecordType = Nif::RC_NiSequenceStreamHelper;
            mSeq.mName = "test";
            mSeq.mExtra = Nif::ExtraPtr(nullptr);
            mSeq.mController = Nif::NiTimeControllerPtr(nullptr);

            mTextKeys.mRecordType = Nif::RC_NiTextKeyExtraData;
            mTextKeys.mRecordName = "NiTextKeyExtraData";
            mTextKeys.mNext = Nif::ExtraPtr(nullptr);
            mSeq.mExtraList.emplace_back(&mTextKeys);

            mFile.mRoots.push_back(&mSeq);
        }

        void addTextKey(float time, std::string text)
        {
            Nif::NiTextKeyExtraData::TextKey key;
            key.mTime = time;
            key.mText = std::move(text);
            mTextKeys.mList.push_back(std::move(key));
        }

        /// Appends a name and a controller; returns the controller's data for filling in.
        Nif::NiKeyframeData& addBone(const std::string& name)
        {
            auto str = std::make_unique<Nif::NiStringExtraData>();
            str->mRecordType = Nif::RC_NiStringExtraData;
            str->mRecordName = "NiStringExtraData";
            str->mNext = Nif::ExtraPtr(nullptr);
            str->mData = name;
            mSeq.mExtraList.emplace_back(str.get());
            mNames.push_back(std::move(str));

            mData.push_back(makeKeyframeData());
            appendController(makeController(mData.back().get()));
            return *mData.back();
        }

        void appendController(std::unique_ptr<Nif::NiKeyframeController> ctrl)
        {
            Nif::NiTimeControllerPtr ptr(ctrl.get());
            if (mControllers.empty())
                mSeq.mController = ptr;
            else
                mControllers.back()->mNext = ptr;
            mControllers.push_back(std::move(ctrl));
        }

        Nif::FileView view() const { return Nif::FileView(mFile); }
    };

    std::vector<std::string> textKeysAt(const compat_morrowind::KfAnimation& anim, float time)
    {
        std::vector<std::string> out;
        auto [begin, end] = anim.mTextKeys.equal_range(time);
        for (auto it = begin; it != end; ++it)
            out.push_back(it->second);
        return out;
    }
} // namespace

TEST(CompatMorrowindLoadKfTest, ReadsTextKeysAndTracksByBoneName)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "WalkForward: Start\r\nSound: FootBareLeft");
    kf.addTextKey(1.f, "  WalkForward: Stop  ");
    kf.addTextKey(1.5f, "\r\n");

    Nif::NiKeyframeData& pelvis = kf.addBone("Bip01 Pelvis");
    pelvis.mTranslations = makeVec3Keys(
        Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(0.f, 0.f, 0.f) }, { 1.f, vsg::vec3(0.f, 10.f, 0.f) } });
    Nif::NiKeyframeData& spine = kf.addBone("Bip01 Spine");
    spine.mRotations = makeQuatKeys(Nif::InterpolationType_Linear,
        { { 0.f, Misc::VsgMath::makeRotateFromAxis(0.f, vsg::vec3(0.f, 0.f, 1.f)) },
            { 1.f, Misc::VsgMath::makeRotateFromAxis(kHalfPi, vsg::vec3(0.f, 0.f, 1.f)) } });

    const compat_morrowind::KfAnimation anim = compat_morrowind::loadKfAnimation(kf.view());

    // Text keys: split on line breaks, trimmed, lower-cased, empty lines dropped, order within a time kept
    EXPECT_EQ(anim.mTextKeys.size(), 3u);
    EXPECT_THAT(textKeysAt(anim, 0.f), ::testing::ElementsAre("walkforward: start", "sound: footbareleft"));
    EXPECT_THAT(textKeysAt(anim, 1.f), ::testing::ElementsAre("walkforward: stop"));
    EXPECT_TRUE(textKeysAt(anim, 1.5f).empty());

    ASSERT_EQ(anim.mTracks.size(), 2u);
    ASSERT_EQ(anim.mTracks.count("Bip01 Pelvis"), 1u);
    ASSERT_EQ(anim.mTracks.count("Bip01 Spine"), 1u);

    const auto& pelvisTrack = anim.mTracks.at("Bip01 Pelvis");
    EXPECT_TRUE(pelvisTrack.hasTranslation());
    EXPECT_FALSE(pelvisTrack.hasRotation());
    expectVec3Near(translationAt(pelvisTrack, 0.5f), vsg::vec3(0.f, 5.f, 0.f));

    const auto& spineTrack = anim.mTracks.at("Bip01 Spine");
    EXPECT_FALSE(spineTrack.hasTranslation());
    EXPECT_TRUE(spineTrack.hasRotation());
    expectQuatNear(
        rotationAt(spineTrack, 0.5f), Misc::VsgMath::makeRotateFromAxis(kHalfPi * 0.5f, vsg::vec3(0.f, 0.f, 1.f)));
}

TEST(CompatMorrowindLoadKfTest, NoSequenceStreamHelperRootGivesEmptyAnimation)
{
    Nif::NIFFile file(testKf);
    Nif::NiTextKeyExtraData notASequence;
    notASequence.mRecordType = Nif::RC_NiTextKeyExtraData;
    notASequence.mNext = Nif::ExtraPtr(nullptr);
    file.mRoots.push_back(&notASequence);

    const auto anim = compat_morrowind::loadKfAnimation(Nif::FileView(file));
    EXPECT_TRUE(anim.mTextKeys.empty());
    EXPECT_TRUE(anim.mTracks.empty());

    Nif::NIFFile emptyFile(testKf);
    const auto anim2 = compat_morrowind::loadKfAnimation(Nif::FileView(emptyFile));
    EXPECT_TRUE(anim2.mTextKeys.empty());
    EXPECT_TRUE(anim2.mTracks.empty());
}

TEST(CompatMorrowindLoadKfTest, FirstExtraNotTextKeysGivesEmptyAnimation)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "idle: start");
    kf.addBone("Bip01").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(1.f, 1.f, 1.f) } });
    // Replace the text keys with the bone's name record, so the first extra is an NiStringExtraData
    kf.mSeq.mExtraList.erase(kf.mSeq.mExtraList.begin());

    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    EXPECT_TRUE(anim.mTextKeys.empty());
    EXPECT_TRUE(anim.mTracks.empty());
}

TEST(CompatMorrowindLoadKfTest, SequenceWithoutExtrasGivesEmptyAnimation)
{
    SyntheticKf kf;
    kf.mSeq.mExtraList.clear();
    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    EXPECT_TRUE(anim.mTextKeys.empty());
    EXPECT_TRUE(anim.mTracks.empty());
}

TEST(CompatMorrowindLoadKfTest, MismatchedExtraSkipsThatPairOnly)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "idle: start");
    kf.addBone("Bip01 A").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(1.f, 0.f, 0.f) } });
    kf.addBone("Bip01 B").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(2.f, 0.f, 0.f) } });

    // Turn the first bone's name record into something that is not an NiStringExtraData
    kf.mNames[0]->mRecordType = Nif::RC_NiTextKeyExtraData;

    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    EXPECT_EQ(anim.mTextKeys.size(), 1u);
    ASSERT_EQ(anim.mTracks.size(), 1u);
    ASSERT_EQ(anim.mTracks.count("Bip01 B"), 1u);
    // B pairs with the second controller, not the first
    expectVec3Near(translationAt(anim.mTracks.at("Bip01 B"), 0.f), vsg::vec3(2.f, 0.f, 0.f));
}

TEST(CompatMorrowindLoadKfTest, DuplicateBoneNameKeepsTheFirstTrack)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "idle: start");
    kf.addBone("Bip01 Head").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(1.f, 0.f, 0.f) } });
    kf.addBone("Bip01 Head").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(9.f, 0.f, 0.f) } });

    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    ASSERT_EQ(anim.mTracks.size(), 1u);
    expectVec3Near(translationAt(anim.mTracks.at("Bip01 Head"), 0.f), vsg::vec3(1.f, 0.f, 0.f));
}

TEST(CompatMorrowindLoadKfTest, ControllersWithoutKeysOrWithUnsupportedInterpolatorAreSkipped)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "idle: start");
    kf.addBone("Bip01 Empty");
    kf.mControllers[0]->mData = Nif::NiKeyframeDataPtr(nullptr); // no data and no interpolator

    kf.addBone("Bip01 Float").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(1.f, 0.f, 0.f) } });
    Nif::NiFloatInterpolator floatInterp;
    floatInterp.mRecordType = Nif::RC_NiFloatInterpolator;
    floatInterp.mDefaultValue = 0.f;
    floatInterp.mData = Nif::NiFloatDataPtr(nullptr);
    kf.mControllers[1]->mInterpolator = Nif::NiInterpolatorPtr(&floatInterp);

    kf.addBone("Bip01 Good").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(3.f, 0.f, 0.f) } });

    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    ASSERT_EQ(anim.mTracks.size(), 1u);
    ASSERT_EQ(anim.mTracks.count("Bip01 Good"), 1u);
    expectVec3Near(translationAt(anim.mTracks.at("Bip01 Good"), 0.f), vsg::vec3(3.f, 0.f, 0.f));
}

TEST(CompatMorrowindLoadKfTest, ControllerWithTransformInterpolatorIsLoaded)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "idle: start");
    kf.addBone("Bip01 Interp");

    auto interpData = makeKeyframeData();
    interpData->mTranslations = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(0.f, 0.f, 4.f) } });
    Nif::NiTransformInterpolator interp;
    interp.mRecordType = Nif::RC_NiTransformInterpolator;
    interp.mDefaultValue.mTranslation = vsg::vec3();
    interp.mDefaultValue.mRotation = vsg::dquat();
    interp.mDefaultValue.mScale = 1.f;
    interp.mData = Nif::NiKeyframeDataPtr(interpData.get());
    kf.mControllers[0]->mInterpolator = Nif::NiInterpolatorPtr(&interp);
    kf.mControllers[0]->mData = Nif::NiKeyframeDataPtr(nullptr);

    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    ASSERT_EQ(anim.mTracks.count("Bip01 Interp"), 1u);
    expectVec3Near(translationAt(anim.mTracks.at("Bip01 Interp"), 0.f), vsg::vec3(0.f, 0.f, 4.f));
}

TEST(CompatMorrowindLoadKfTest, ExtrasBeyondTheControllerChainAreIgnored)
{
    SyntheticKf kf;
    kf.addTextKey(0.f, "idle: start");
    kf.addBone("Bip01 A").mTranslations
        = makeVec3Keys(Nif::InterpolationType_Linear, { { 0.f, vsg::vec3(1.f, 0.f, 0.f) } });

    // A second name with no controller left to pair with
    Nif::NiStringExtraData orphan;
    orphan.mRecordType = Nif::RC_NiStringExtraData;
    orphan.mNext = Nif::ExtraPtr(nullptr);
    orphan.mData = "Bip01 Orphan";
    kf.mSeq.mExtraList.emplace_back(&orphan);

    const auto anim = compat_morrowind::loadKfAnimation(kf.view());
    ASSERT_EQ(anim.mTracks.size(), 1u);
    EXPECT_EQ(anim.mTracks.count("Bip01 A"), 1u);
}
