// Task 0251 (horse riding): a read-only survey of the installed horse skeleton. Prints the rest-pose world position of
// the saddle node (line SKYRIMSE-RIDING-SADDLE) and the nodes on its path to the root. Skipped when
// OPENEDEN_SKYRIMSE_DATA is not set.
#include <components/bsa/compressedbsafile.hpp>
#include <components/nifscene/build.hpp>
#include <components/vfs/pathutil.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    std::string lowerPath(std::string s)
    {
        for (char& c : s)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    TEST(SkyrimRidingSurvey, theSaddleNodeOfTheHorseSkeleton)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path path = fs::path(env) / "Skyrim - Meshes0.bsa";
        if (!fs::exists(path))
            GTEST_SKIP() << "Skyrim - Meshes0.bsa is missing";
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        for (const auto& entry : archive.getList())
        {
            if (lowerPath(std::string(entry.name())) != "meshes/actors/horse/character assets/skeleton.nif")
                continue;
            auto stream = archive.getFile(&entry);
            std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
            const NifScene::Scene scene = NifScene::readScene(std::move(input), VFS::Path::Normalized("horse.nif"), {});
            std::size_t found = scene.mNodes.size();
            for (std::size_t i = 0; i < scene.mNodes.size(); ++i)
                if (scene.mNodes[i].mName == "SaddleBone")
                    found = i;
            ASSERT_LT(found, scene.mNodes.size());
            // Walk up to the root, then compose down: world = parent * local (row-major rotation, uniform scale).
            std::vector<std::size_t> chain;
            for (std::uint32_t at = static_cast<std::uint32_t>(found); at != NifScene::sNone;
                 at = scene.mNodes[at].mParent)
                chain.push_back(at);
            std::array<float, 3> position{ 0.f, 0.f, 0.f };
            std::array<float, 9> rotation{ 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };
            float scale = 1.f;
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                const NifScene::Transform& local = scene.mNodes[*it].mTransform;
                const std::array<float, 3> moved{ rotation[0] * local.mTranslation[0]
                        + rotation[1] * local.mTranslation[1] + rotation[2] * local.mTranslation[2],
                    rotation[3] * local.mTranslation[0] + rotation[4] * local.mTranslation[1]
                        + rotation[5] * local.mTranslation[2],
                    rotation[6] * local.mTranslation[0] + rotation[7] * local.mTranslation[1]
                        + rotation[8] * local.mTranslation[2] };
                for (int k = 0; k < 3; ++k)
                    position[static_cast<std::size_t>(k)] += scale * moved[static_cast<std::size_t>(k)];
                std::array<float, 9> next{};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        for (int k = 0; k < 3; ++k)
                            next[static_cast<std::size_t>(r * 3 + c)] += rotation[static_cast<std::size_t>(r * 3 + k)]
                                * local.mRotation[static_cast<std::size_t>(k * 3 + c)];
                rotation = next;
                scale *= local.mScale;
                std::cout << "SKYRIMSE-RIDING-CHAIN " << scene.mNodes[*it].mName << " local (" << local.mTranslation[0]
                          << ", " << local.mTranslation[1] << ", " << local.mTranslation[2] << ") -> world ("
                          << position[0] << ", " << position[1] << ", " << position[2] << ")" << std::endl;
            }
            std::cout << "SKYRIMSE-RIDING-SADDLE x " << position[0] << " y " << position[1] << " z " << position[2]
                      << std::endl;
            return;
        }
        FAIL() << "the horse skeleton is not in Skyrim - Meshes0.bsa";
    }
}
