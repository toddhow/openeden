// Task 0086 stage a: the Skyrim SE compatibility pack (extensions/compat_skyrimse). Generated data only.
#include <compat_skyrimse/scene_options.hpp>
#include <compat_skyrimse/skyrimse_compat_extension.hpp>

#include <components/extension/manifest_io.hpp>
#include <components/nif/exception.hpp>
#include <components/nifscene/build.hpp>

#include <openeden/contracts/game_contract.hpp>
#include <openeden/registrar.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../nifscene/ssebuilder.hpp"

namespace
{
    using namespace NifScene::Testing;

    constexpr VFS::Path::NormalizedView path("skyrim-compat-test.nif");

    class MockRegistrar : public openeden::sdk::Registrar
    {
    public:
        std::vector<std::string> mProvided;
        void provideService(std::string_view contractId, openeden::sdk::Version, void*) override
        {
            mProvided.emplace_back(contractId);
        }
        void registerPhaseCallback(openeden::sdk::Phase, openeden::sdk::PhaseCallback, bool) override {}
    };

    GamebryoFile oneTriangle()
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1 }, { 1.f, 2.f, 3.f }, 1.f, "BSFadeNode"));
        ShapeSpec spec;
        spec.mName = "Tri";
        spec.mFlags = VF_Vertex | VF_UVs;
        Vertex a;
        Vertex b;
        Vertex c;
        b.mPosition = { 1, 0, 0 };
        c.mPosition = { 0, 1, 0 };
        spec.mVertices = { a, b, c };
        spec.mTriangles = { { 0, 1, 2 } };
        file.mBlocks.push_back(shape(file, spec));
        return file;
    }

    NifScene::Scene read(const GamebryoFile& file, const NifScene::SceneOptions& options)
    {
        auto input = std::make_unique<std::istringstream>(file.build());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return NifScene::readScene(std::move(input), path, options);
    }

    TEST(CompatSkyrimSe, theContractNamesTheGameAndItsConventions)
    {
        const compat_skyrimse::SkyrimGameContract contract;
        EXPECT_EQ(contract.getGameId(), "skyrimse");
        EXPECT_NE(contract.getGameDisplayName().find("Skyrim"), std::string_view::npos);
        EXPECT_FLOAT_EQ(contract.getUnitsPerMeter(), 69.99125109f);
        EXPECT_EQ(contract.getDefaultHumanoidSkeleton(), "meshes/actors/character/character assets/skeleton.nif");
        EXPECT_EQ(compat_skyrimse::defaultFemaleSkeleton(),
            "meshes/actors/character/character assets female/skeleton_female.nif");
        EXPECT_EQ(compat_skyrimse::defaultMaleSkeleton(), contract.getDefaultHumanoidSkeleton());
    }

    TEST(CompatSkyrimSe, theExtensionProvidesTheGameContract)
    {
        compat_skyrimse::SkyrimCompatExtension extension;
        MockRegistrar registrar;
        extension.registerServices(registrar);
        ASSERT_EQ(registrar.mProvided.size(), 1u);
        EXPECT_EQ(registrar.mProvided[0], openeden::contracts::GameContract::kId);
    }

    TEST(CompatSkyrimSe, theManifestIsACompatibilityPackProvidingTheGameContract)
    {
        openeden::extension::LoadError error;
        const auto manifest = openeden::extension::loadManifestFile(
            std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "extensions" / "compat_skyrimse" / "extension.json",
            error);
        ASSERT_TRUE(manifest.has_value()) << error.message;
        EXPECT_EQ(manifest->id, "compat_skyrimse");
        EXPECT_EQ(manifest->kind, "compatibility");
        ASSERT_EQ(manifest->providedContracts.size(), 1u);
        EXPECT_EQ(manifest->providedContracts[0].contract, "openeden.game");
    }

    TEST(CompatSkyrimSe, theReadOptionsAreSkyrimsAndNotMorrowinds)
    {
        const Nif::ReadOptions options = compat_skyrimse::skyrimReadOptions();
        EXPECT_EQ(options.mAllowedVersions, Nif::VersionSet::SkyrimSE);
        EXPECT_FALSE(options.mMorrowindRootRule);
        EXPECT_FALSE(options.mStrict);
        EXPECT_EQ(compat_skyrimse::skyrimSceneOptions().mRead.mAllowedVersions, Nif::VersionSet::SkyrimSE);
        EXPECT_FALSE(compat_skyrimse::skyrimSceneOptions().mKeepZeroWeights);
    }

    TEST(CompatSkyrimSe, aGeneratedSkyrimFileReadsWithThePacksOptionsAndTheRootKeepsItsTransform)
    {
        const NifScene::Scene scene = read(oneTriangle(), compat_skyrimse::skyrimSceneOptions());
        ASSERT_EQ(scene.mNodes.size(), 2u);
        EXPECT_EQ(scene.mNodes[0].mTransform.mTranslation, (NifScene::Vec3{ 1.f, 2.f, 3.f }));
        EXPECT_EQ(scene.mNodes[1].mGeometry, 0u);
        EXPECT_EQ(scene.mGeometries.size(), 1u);
    }

    TEST(CompatSkyrimSe, theMorrowindOptionsRefuseTheSameFile)
    {
        NifScene::SceneOptions morrowind;
        morrowind.mRead.mAllowedVersions = Nif::VersionSet::Morrowind;
        EXPECT_ANY_THROW(read(oneTriangle(), morrowind));
    }
}

namespace
{
    TEST(CompatSkyrimSe, theContractNamesTheSkeletonBonesTheEngineLooksUp)
    {
        const compat_skyrimse::SkyrimGameContract contract;
        const auto names = contract.getHumanoidBoneNames();
        EXPECT_EQ(names.root, "npc root [root]");
        EXPECT_TRUE(names.rootFallback.empty());
        EXPECT_EQ(names.head, "npc head [head]");
        EXPECT_EQ(names.spine1, "npc spine [spn0]");
        EXPECT_EQ(names.spine2, "npc spine1 [spn1]");
        const auto prefixes = contract.getSkeletonModelNamePrefixes();
        ASSERT_EQ(prefixes.size(), 2u);
        EXPECT_EQ(prefixes[0], "skeleton");
        EXPECT_EQ(prefixes[1], "skeletonbeast");
        // Skyrim has no body part records, so there is no first-person suffix
        EXPECT_TRUE(contract.getFirstPersonBodyPartSuffix().empty());
        EXPECT_EQ(contract.getFirstPersonBaseAnimationFile(), "meshes/actors/character/_1stperson/skeleton.nif");
    }

    TEST(CompatSkyrimSe, theContractVersionIsTheOneTheManifestProvides)
    {
        openeden::extension::LoadError error;
        const auto manifest = openeden::extension::loadManifestFile(
            std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "extensions" / "compat_skyrimse" / "extension.json",
            error);
        ASSERT_TRUE(manifest.has_value()) << error.message;
        EXPECT_TRUE(manifest->providedContracts[0].version == openeden::contracts::GameContract::kVersion);
    }
}
