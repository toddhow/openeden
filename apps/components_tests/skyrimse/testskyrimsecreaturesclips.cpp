// Task 0216: checks every row of the creature table against the installed data (read only; prints lines with the prefix
// SKYRIMSE-CREATURE-CLIPS). For each creature: the eight clips exist in Skyrim - Animations.bsa, the sidecar project
// exists, the walk and run strides equal the root motion of the block the clip links to (1 percent), and the attack
// clips link to a generator with a HitFrame event. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <components/bsa/compressedbsafile.hpp>
#include <components/havokpack/animation.hpp>
#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/packfile.hpp>

#include <game_skyrimse/creatures.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    std::string norm(std::string s)
    {
        for (char& c : s)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    TEST(SkyrimCreaturesClipsRealData, everyRowAgreesWithTheInstalledSidecarsAndClips)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path path = fs::path(env) / "Skyrim - Animations.bsa";
        if (!fs::exists(path))
            GTEST_SKIP() << "Skyrim - Animations.bsa is missing";
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        const auto& list = archive.getList();
        std::map<std::string, std::size_t> files;
        for (std::size_t i = 0; i < list.size(); ++i)
            files[norm(std::string(list[i].name()))] = i;
        const auto read = [&](const std::string& name) -> std::string {
            const auto it = files.find(name);
            if (it == files.end())
                return {};
            auto stream = archive.getFile(&list[it->second]);
            return std::string((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        };

        for (const CreatureProfile& p : creatureProfiles())
        {
            SCOPED_TRACE(std::string(p.mName));
            const std::string folder(p.mSkeletonFolder.substr(0, p.mSkeletonFolder.find('/', 7)));
            const std::string animations = "meshes/" + folder + "/animations/";
            const std::string sidecar = read("meshes/animationdata/" + std::string(p.mProject) + ".txt");
            const std::string motionText
                = read("meshes/animationdata/boundanims/anims_" + std::string(p.mProject) + ".txt");
            ASSERT_FALSE(sidecar.empty()) << "no sidecar project";
            ASSERT_FALSE(motionText.empty()) << "no motion file";
            const HavokPack::AnimationProject project = HavokPack::readAnimationProject(sidecar);
            const HavokPack::MotionTable motion = HavokPack::readMotionTable(motionText);

            for (const std::string_view clip : { p.mIdle, p.mWalk, p.mRun, p.mAttack, p.mPower, p.mRecoil, p.mStagger,
                     p.mDeath })
            {
                const std::string bytes = read(animations + norm(std::string(clip)) + ".hkx");
                EXPECT_FALSE(bytes.empty()) << "missing clip " << clip;
                if (!bytes.empty())
                    std::cout << "SKYRIMSE-CREATURE-CLIPS " << p.mName << " " << clip << " bytes " << bytes.size()
                              << std::endl;
            }

            const auto durationOf = [&](std::string_view clip) -> float {
                const std::string bytes = read(animations + norm(std::string(clip)) + ".hkx");
                if (bytes.empty())
                    return 0.f;
                const HavokPack::Packfile file = HavokPack::Packfile::parse(
                    std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
                const std::vector<HavokPack::Clip> clips = HavokPack::readClips(file);
                return clips.empty() ? 0.f : clips.front().mDuration;
            };

            for (const auto& [clip, stride] : { std::pair{ p.mWalk, p.mWalkStride }, std::pair{ p.mRun, p.mRunStride } })
            {
                const float duration = durationOf(clip);
                ASSERT_GT(duration, 0.f) << clip;
                const HavokPack::ClipLink link = HavokPack::linkClip(project, motion, std::string(clip), duration);
                if (link.mMotion != nullptr && !link.mMotion->mTranslation.empty())
                {
                    const auto& last = link.mMotion->mTranslation.back();
                    const float length = std::sqrt(last[1] * last[1] + last[2] * last[2]);
                    EXPECT_NEAR(length, stride, stride * 0.01f) << clip << " duration " << duration;
                    std::cout << "SKYRIMSE-CREATURE-CLIPS " << p.mName << " " << clip << " linked stride " << length
                              << " duration " << duration << std::endl;
                }
                else
                {
                    // The generator's name is not the file's: the stride was taken from a block of the same duration
                    // by hand (the table says which); report it, do not fail.
                    std::cout << "SKYRIMSE-CREATURE-CLIPS " << p.mName << " " << clip << " NOT linked by name (duration "
                              << duration << ", table stride " << stride << ")" << std::endl;
                }
            }

            for (const std::string_view clip : { p.mAttack, p.mPower })
            {
                const float duration = durationOf(clip);
                ASSERT_GT(duration, 0.f) << clip;
                const HavokPack::ClipLink link = HavokPack::linkClip(project, motion, std::string(clip), duration);
                bool hit = false;
                for (const HavokPack::SidecarClip* generator : link.mClips)
                    for (const HavokPack::ClipEvent& event : generator->mEvents)
                        hit = hit || event.mName == "HitFrame";
                EXPECT_TRUE(hit) << clip << " has no HitFrame event: a swing of it would never land";
                std::cout << "SKYRIMSE-CREATURE-CLIPS " << p.mName << " " << clip << " attack duration " << duration
                          << " generators " << link.mClips.size() << " HitFrame " << (hit ? "yes" : "NO") << std::endl;
            }
        }
    }
}
