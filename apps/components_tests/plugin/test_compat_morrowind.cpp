#include <compat_morrowind/kf_converter.hpp>
#include <compat_morrowind/morrowind_compat_plugin.hpp>

#include <components/nif/data.hpp>
#include <components/nif/extra.hpp>
#include <components/nifosg/controller.hpp>
#include <components/plugin/manifest_io.hpp>
#include <components/sceneutil/keyframe.hpp>

#include <opas/anim/ClipSidecar.hpp>
#include <opas/anim/GltfReader.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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

        void provideService(
            std::string_view contractId, openeden::sdk::Version version, void* implPointer) override
        {
            mProvidedServices.push_back({ std::string(contractId), version, implPointer });
        }

        void registerPhaseCallback(
            openeden::sdk::Phase /*phase*/, openeden::sdk::PhaseCallback /*callback*/, bool /*parallel*/) override
        {
        }

        void registerPhaseCallback(
            openeden::sdk::Phase /*phase*/, openeden::sdk::FixedStepPhaseCallback /*callback*/, bool /*parallel*/) override
        {
        }
    };
} // namespace

TEST(CompatMorrowindTest, ManifestValidation)
{
    std::filesystem::path manifestPath = "plugins/compat_morrowind/plugin.json";
    if (!std::filesystem::exists(manifestPath))
    {
        // Check relative to source dir
        manifestPath = "../plugins/compat_morrowind/plugin.json";
    }

    openeden::plugin::LoadError err;
    auto manifestOpt = openeden::plugin::loadManifestFile(manifestPath, err);
    ASSERT_TRUE(manifestOpt.has_value()) << "Manifest parse error: " << err.message << " at " << err.file << ":" << err.line;

    const auto& m = *manifestOpt;
    EXPECT_EQ(m.id, "compat_morrowind");
    EXPECT_EQ(m.kind, "compatibility");
    EXPECT_EQ(m.sdkVersion.major, 1);
    EXPECT_EQ(m.sdkVersion.minor, 0);

    bool hasGame = false;
    bool hasKfConverter = false;
    for (const auto& c : m.providedContracts)
    {
        if (c.contract == "openeden.game" && c.version == openeden::sdk::Version{ 1, 0 })
            hasGame = true;
        if (c.contract == "openeden.compat.kf_converter" && c.version == openeden::sdk::Version{ 1, 0 })
            hasKfConverter = true;
    }

    EXPECT_TRUE(hasGame);
    EXPECT_TRUE(hasKfConverter);
}

TEST(CompatMorrowindTest, PluginRegistrationAndGameContract)
{
    compat_morrowind::MorrowindCompatPlugin plugin;
    MockRegistrar registrar;

    plugin.registerServices(registrar);

    ASSERT_EQ(registrar.mProvidedServices.size(), 2);

    auto* gameContract = static_cast<openeden::contracts::GameContract*>(registrar.mProvidedServices[0].implPointer);
    ASSERT_NE(gameContract, nullptr);
    EXPECT_EQ(gameContract->getGameId(), "morrowind");
    EXPECT_EQ(gameContract->getGameDisplayName(), "The Elder Scrolls III: Morrowind");
    EXPECT_NEAR(gameContract->getUnitsPerMeter(), 69.9912f, 0.001f);
    EXPECT_EQ(gameContract->getDefaultHumanoidSkeleton(), "meshes/base_anim.nif");

    auto* kfConverter = static_cast<openeden::contracts::KfConverterContract*>(registrar.mProvidedServices[1].implPointer);
    ASSERT_NE(kfConverter, nullptr);
}

TEST(CompatMorrowindTest, KfToGltfConversionAndCreativeMotionIngestion)
{
    // 1. Build a synthetic KeyframeHolder with linear translations, slerped rotations, and text keys
    SceneUtil::KeyframeHolder kfHolder;

    // Set up text keys for walkforward animation group
    kfHolder.mTextKeys.emplace(0.0f, "walkforward: start");
    kfHolder.mTextKeys.emplace(0.4f, "walkforward: loop start");
    kfHolder.mTextKeys.emplace(0.8f, "sound: FootBareLeft");
    kfHolder.mTextKeys.emplace(1.6f, "walkforward: loop stop");
    kfHolder.mTextKeys.emplace(2.0f, "walkforward: stop");

    // Set up a bone controller
    auto transMap = std::make_shared<Nif::Vector3KeyMap>();
    transMap->mInterpolationType = Nif::InterpolationType_Linear;
    Nif::KeyT<osg::Vec3f> k0{ osg::Vec3f(0.0f, 0.0f, 70.0f), {}, {} };
    Nif::KeyT<osg::Vec3f> k1{ osg::Vec3f(0.0f, 140.0f, 70.0f), {}, {} };
    transMap->mKeys.emplace_back(0.0f, k0);
    transMap->mKeys.emplace_back(2.0f, k1);

    auto rotMap = std::make_shared<Nif::QuaternionKeyMap>();
    rotMap->mInterpolationType = Nif::InterpolationType_Linear;
    Nif::KeyT<osg::Quat> r0{ osg::Quat(0.0f, 0.0f, 0.0f, 1.0f), {}, {} };
    Nif::KeyT<osg::Quat> r1{ osg::Quat(0.0f, 0.0f, 0.7071068f, 0.7071068f), {}, {} }; // 90 degree yaw
    rotMap->mKeys.emplace_back(0.0f, r0);
    rotMap->mKeys.emplace_back(2.0f, r1);

    // Create a mock NiKeyframeController with our synthetic data
    auto kfData = std::make_unique<Nif::NiKeyframeData>();
    kfData->mTranslations = transMap;
    kfData->mRotations = rotMap;

    auto ctrl = std::make_unique<Nif::NiKeyframeController>();
    ctrl->mInterpolator = Nif::NiInterpolatorPtr(nullptr);
    ctrl->mData = Nif::NiKeyframeDataPtr(kfData.get());

    osg::ref_ptr<NifOsg::KeyframeController> osgCtrl = new NifOsg::KeyframeController(ctrl.get());
    kfHolder.mKeyframeControllers["bip01 pelvis"] = osgCtrl;

    // Skeletons
    std::vector<compat_morrowind::BoneInfo> bones;
    compat_morrowind::BoneInfo pelvis;
    pelvis.name = "bip01 pelvis";
    pelvis.restTranslation = osg::Vec3f(0.0f, 0.0f, 70.0f);
    bones.push_back(pelvis);

    // 2. Perform Conversion
    compat_morrowind::KfConverter converter;
    openeden::contracts::KfConverterContract::ConversionOptions options;
    options.unitsPerMeter = 70.0f;
    options.resampleRateHz = 60;

    auto convResult = converter.convertParsedKfToGltf(kfHolder, bones, options);
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
    ASSERT_TRUE(sidecarLoadResult.value.has_value())
        << "Failed to parse sidecar: " << sidecarLoadResult.error.message;

    opas::anim::applyClipSidecar(sidecarLoadResult.value.value(), anims.clips);
    EXPECT_EQ(anims.clips[0].events.size(), 5u);
    EXPECT_EQ(anims.clips[0].events[0].name, "walkforward: start");
    EXPECT_NEAR(anims.clips[0].events[0].timeS, 0.0f, 0.001f);
    EXPECT_EQ(anims.clips[0].events[1].name, "walkforward: loop start");
    EXPECT_NEAR(anims.clips[0].events[1].timeS, 0.4f, 0.001f);
    EXPECT_EQ(anims.clips[0].events[4].name, "walkforward: stop");
    EXPECT_NEAR(anims.clips[0].events[4].timeS, 2.0f, 0.001f);
}
