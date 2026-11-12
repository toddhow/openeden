// Task 0121 (stages N5-1 and N5-2): the aggregate check of actor appearances on the player's own Skyrim SE masters. For
// every NPC_ the pack describes the appearance; then the skeleton and part models are read from the installed archives,
// the skinned shapes and the skeleton plan are made, and the REST POSE is checked: with the skeleton as the file stores
// it, a skinned vertex must land (nearly) where the shape file has it. Prints counts and distances only (lines starting
// SKYRIMSE-APPEARANCE), never a name or path, and writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required)
//   OPENEDEN_SKYRIMSE_APPEARANCE_PARTS   how many distinct part models to read (default 400)
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <components/nifscene/build.hpp>
#include <components/nifscene/skeleton.hpp>
#include <components/nifscene/skinned.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }

    // p_parent = s * R * p + t, the NIF convention (rotation row-major).
    struct Pose
    {
        std::array<double, 9> r{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        std::array<double, 3> t{ 0, 0, 0 };
        double s = 1;
    };

    Pose fromTransform(const NifScene::Transform& x)
    {
        Pose p;
        for (int i = 0; i < 9; ++i)
            p.r[static_cast<std::size_t>(i)] = x.mRotation[static_cast<std::size_t>(i)];
        for (int i = 0; i < 3; ++i)
            p.t[static_cast<std::size_t>(i)] = x.mTranslation[static_cast<std::size_t>(i)];
        p.s = x.mScale;
        return p;
    }

    std::array<double, 3> applyPose(const Pose& p, const std::array<double, 3>& v)
    {
        std::array<double, 3> out{};
        for (std::size_t row = 0; row < 3; ++row)
            out[row] = p.s * (p.r[row * 3] * v[0] + p.r[row * 3 + 1] * v[1] + p.r[row * 3 + 2] * v[2]) + p.t[row];
        return out;
    }

    Pose compose(const Pose& parent, const Pose& child)
    {
        Pose out;
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
            {
                double sum = 0;
                for (std::size_t k = 0; k < 3; ++k)
                    sum += parent.r[row * 3 + k] * child.r[k * 3 + column];
                out.r[row * 3 + column] = sum;
            }
        out.s = parent.s * child.s;
        out.t = applyPose(parent, child.t);
        return out;
    }

    struct Archives
    {
        std::map<std::string, fs::path> mWhere; // lower-case slashed name -> archive
    };

    TEST(SkyrimAppearanceRealData, npcAppearancesAndTheRestPoseOfTheirParts)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;
        ActorIndex index;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            ActorRecords actors;
            EXPECT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), actors));
            index.add(actors);
            ParsedPlugin records;
            ParseLimits limits;
            limits.mSkipReferences = true;
            EXPECT_TRUE(parseRecords(*plugin, order, limits, records));
            index.addLeveledLists(records.mLeveledLists);
        }

        // ---- the appearances ----
        std::uint64_t npcs = 0, ok = 0, failed = 0, partsTotal = 0;
        std::map<std::string, std::uint64_t> failures;
        std::map<std::string, std::string> firstSkeletonOfPart; // part -> skeleton path of the first NPC wearing it
        std::set<std::string> skeletons;
        std::map<std::string, int> kindOfPart;
        for (const FormId id : index.npcIds())
        {
            ++npcs;
            openeden::sdk::game::ActorAppearance a;
            std::string error;
            if (makeActorAppearance(index, id, static_cast<std::uint32_t>(id), a, error))
            {
                ++ok;
                partsTotal += a.parts.size();
                skeletons.insert(lower(a.skeletonPath));
                for (const auto& part : a.parts)
                {
                    firstSkeletonOfPart.emplace(lower(part.modelPath), lower(a.skeletonPath));
                    kindOfPart.emplace(lower(part.modelPath), static_cast<int>(part.kind));
                }
            }
            else
            {
                ++failed;
                ++failures[error];
            }
        }
        std::cout << "SKYRIMSE-APPEARANCE npcs " << npcs << " described " << ok << " refused " << failed
                  << " parts " << partsTotal << " distinct-skeletons " << skeletons.size()
                  << " distinct-part-models " << firstSkeletonOfPart.size() << "\n";
        for (const auto& [reason, count] : failures)
            std::cout << "SKYRIMSE-APPEARANCE refused " << count << " (" << reason << ")\n";
        EXPECT_GT(ok, 4000u);

        // ---- the archives ----
        Archives archives;
        for (const auto& entry : fs::directory_iterator(data))
        {
            if (!entry.is_regular_file() || lower(entry.path().extension().string()) != ".bsa")
                continue;
            const std::string stem = lower(entry.path().filename().string());
            if (stem.find("meshes") == std::string::npos)
                continue;
            ArchiveListing listing;
            std::string error;
            ASSERT_TRUE(readArchiveListing(entry.path(), listing, error)) << error;
            for (const std::string& name : listing.mFiles)
                archives.mWhere[lower(name)] = entry.path();
        }

        const auto sceneOf = [&](const std::string& path, NifScene::Scene& scene) {
            const auto found = archives.mWhere.find("meshes/" + path);
            if (found == archives.mWhere.end())
                return false;
            std::string bytes, error;
            if (!readArchiveEntry(found->second, "meshes/" + path, bytes, error))
                return false;
            try
            {
                Files::IStreamPtr stream(new std::istringstream(std::move(bytes)));
                scene = NifScene::readScene(std::move(stream), VFS::Path::Normalized("meshes/" + path), NifScene::SceneOptions{});
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        };

        std::map<std::string, NifScene::Skeleton::Plan> plans;
        std::uint64_t skeletonsRead = 0, skeletonsMissing = 0, bonesTotal = 0;
        for (const std::string& path : skeletons)
        {
            NifScene::Scene scene;
            if (!sceneOf(path, scene))
            {
                ++skeletonsMissing;
                continue;
            }
            ++skeletonsRead;
            plans[path] = NifScene::Skeleton::plan(scene);
            bonesTotal += plans[path].mNodes.size();
        }
        std::cout << "SKYRIMSE-APPEARANCE skeletons read " << skeletonsRead << " missing " << skeletonsMissing
                  << " bones " << bonesTotal << "\n";
        EXPECT_EQ(skeletonsMissing, 0u);

        std::size_t limit = 400; // about this many distinct models, every k-th of the sorted list
        if (const char* e = std::getenv("OPENEDEN_SKYRIMSE_APPEARANCE_PARTS"))
            limit = static_cast<std::size_t>(std::strtoul(e, nullptr, 10));
        std::uint64_t read = 0, unreadable = 0, withSkin = 0, shapes = 0, bones = 0, bonesFound = 0;
        std::uint64_t vertices = 0, farVertices = 0, withSkinTransform = 0;
        double worst = 0, sum = 0;
        std::uint64_t badInfluences = 0, withoutInfluence = 0;
        std::array<double, 3> kindSum{};
        std::array<std::uint64_t, 3> kindCount{}, kindFar{}, kindParts{};
        std::size_t done = 0;
        const std::size_t step = std::max<std::size_t>(1, firstSkeletonOfPart.size() / std::max<std::size_t>(1, limit));
        for (const auto& [path, skeletonPath] : firstSkeletonOfPart)
        {
            if (done++ % step != 0)
                continue;
            // Only the humanoid skeletons: a beast race has its own, and a part first worn by one of them would be
            // measured against the wrong rest pose.
            if (skeletonPath.find("actors/character/character assets") == std::string::npos)
                continue;
            const auto plan = plans.find(skeletonPath);
            NifScene::Scene scene;
            if (plan == plans.end() || !sceneOf(path, scene))
            {
                ++unreadable;
                continue;
            }
            ++read;
            const NifScene::Skinned::SkinnedModel model = NifScene::Skinned::flattenSkinned(scene);
            if (model.mShapes.empty())
                continue;
            ++withSkin;
            const std::size_t kind = static_cast<std::size_t>(kindOfPart[path]) % 3;
            ++kindParts[kind];
            badInfluences += model.mReport.mBadInfluences;
            withoutInfluence += model.mReport.mVerticesWithoutInfluence;
            // World rest pose of every bone of the skeleton.
            std::vector<Pose> world(plan->second.mNodes.size());
            for (std::size_t i = 0; i < world.size(); ++i)
            {
                const NifScene::Skeleton::BoneNode& n = plan->second.mNodes[i];
                const Pose local = fromTransform(n.mLocal);
                world[i] = n.mParent == NifScene::sNone ? local : compose(world[n.mParent], local);
            }
            for (const NifScene::Skinned::Shape& shape : model.mShapes)
            {
                ++shapes;
                if (shape.mHasSkinTransform)
                    ++withSkinTransform;
                std::vector<std::uint32_t> boneNode(shape.mBones.size(), NifScene::sNone);
                for (std::size_t b = 0; b < shape.mBones.size(); ++b)
                {
                    ++bones;
                    boneNode[b] = NifScene::Skeleton::find(plan->second, shape.mBones[b].mName);
                    if (boneNode[b] != NifScene::sNone)
                        ++bonesFound;
                }
                for (std::size_t v = 0; v < shape.mPositions.size(); ++v)
                {
                    const auto& pf = shape.mPositions[v];
                    const std::array<double, 3> p{ pf[0], pf[1], pf[2] };
                    std::array<double, 3> skinned{ 0, 0, 0 };
                    double used = 0;
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        const double w = shape.mBoneWeights[v][k];
                        if (w <= 0)
                            continue;
                        const std::uint32_t b = shape.mBoneIndices[v][k];
                        if (boneNode[b] == NifScene::sNone)
                            continue;
                        const auto q = applyPose(world[boneNode[b]], applyPose(fromTransform(shape.mBones[b].mInverseBind), p));
                        for (std::size_t c = 0; c < 3; ++c)
                            skinned[c] += w * q[c];
                        used += w;
                    }
                    if (used <= 0)
                        continue;
                    // Anim::Skin::apply: the skin's own transform, when it has one, is applied last (outermost).
                    if (shape.mHasSkinTransform)
                    {
                        const Pose pose = fromTransform(shape.mSkinTransform);
                        const auto moved = applyPose(pose, { skinned[0] / used, skinned[1] / used, skinned[2] / used });
                        for (std::size_t c = 0; c < 3; ++c)
                            skinned[c] = moved[c] * used;
                    }
                    double d2 = 0;
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        const double e = skinned[c] / used - p[c];
                        d2 += e * e;
                    }
                    const double d = std::sqrt(d2);
                    ++vertices;
                    sum += d;
                    worst = std::max(worst, d);
                    kindSum[kind] += d;
                    ++kindCount[kind];
                    if (d > 5.0)
                    {
                        ++farVertices;
                        ++kindFar[kind];
                    }
                }
            }
        }
        std::cout << "SKYRIMSE-APPEARANCE parts read " << read << " unreadable " << unreadable << " with-skinned-shapes "
                  << withSkin << " shapes " << shapes << " skin-bones " << bones << " found-in-skeleton " << bonesFound
                  << " with-skin-transform " << withSkinTransform << " bad-influences " << badInfluences << " vertices-without-influence " << withoutInfluence << "\n";
        std::cout << "SKYRIMSE-APPEARANCE rest-pose vertices " << vertices << " mean-distance-from-file-position "
                  << (vertices ? sum / static_cast<double>(vertices) : 0.0) << " max " << worst
                  << " over-5-units " << farVertices << "\n";
        for (std::size_t k = 0; k < 3; ++k)
            std::cout << "SKYRIMSE-APPEARANCE rest-pose kind " << (k == 0 ? "skin" : k == 1 ? "armor" : "head") << " parts " << kindParts[k] << " vertices " << kindCount[k] << " mean " << (kindCount[k] ? kindSum[k] / static_cast<double>(kindCount[k]) : 0.0) << " over-5-units " << kindFar[k] << "\n";
        EXPECT_GT(withSkin, 0u);
    }
}
