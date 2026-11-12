// Task 0136 stage log "skinned trunks": the gated real-data survey behind drawing the skinned shapes of static Skyrim SE
// models (trees) in their rest pose. Not run by default. With OPENEDEN_SKYRIMSE_DATA set to the game's Data folder it
// reads every .nif of the five masters' mesh archives and prints AGGREGATE numbers only (lines starting SKYTRUNK-SURVEY).
//
// The oracle: for each skinned shape the rest pose can be taken two ways, both in the model's frame:
//   A. the shape node's world transform applied to the stored vertex (what the static path does for an unskinned shape);
//   B. the skin's own rule with every bone at its node-tree world transform: sum_i w_i * boneWorld_i * bind_i * v.
// Their distance (model units) says whether the node tree is in the bind pose. NOTHING here is a game file's content.
#include <components/bsa/compressedbsafile.hpp>
#include <components/contentsource/plugin.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/flatten.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/survey.hpp>
#include <components/vfs/pathutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using V3 = std::array<double, 3>;

    struct P
    {
        std::array<double, 9> r{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        V3 t{ 0, 0, 0 };
        double s = 1;
    };

    P toP(const NifScene::Transform& x)
    {
        P p;
        for (int i = 0; i < 9; ++i)
            p.r[i] = x.mRotation[i];
        for (int i = 0; i < 3; ++i)
            p.t[i] = x.mTranslation[i];
        p.s = x.mScale;
        return p;
    }
    V3 ap(const P& p, const V3& v)
    {
        V3 o{};
        for (int row = 0; row < 3; ++row)
            o[row] = p.s * (p.r[row * 3] * v[0] + p.r[row * 3 + 1] * v[1] + p.r[row * 3 + 2] * v[2]) + p.t[row];
        return o;
    }
    P comp(const P& parent, const P& child)
    {
        P o;
        for (int row = 0; row < 3; ++row)
            for (int c = 0; c < 3; ++c)
            {
                double sum = 0;
                for (int k = 0; k < 3; ++k)
                    sum += parent.r[row * 3 + k] * child.r[k * 3 + c];
                o.r[row * 3 + c] = sum;
            }
        o.s = parent.s * child.s;
        o.t = ap(parent, child.t);
        return o;
    }

    std::vector<P> worlds(const NifScene::Scene& scene)
    {
        std::vector<P> w(scene.mNodes.size());
        std::vector<char> done(scene.mNodes.size(), 0);
        for (std::size_t start = 0; start < scene.mNodes.size(); ++start)
        {
            std::vector<std::uint32_t> chain;
            std::uint32_t at = static_cast<std::uint32_t>(start);
            while (at < scene.mNodes.size() && !done[at] && chain.size() < 256)
            {
                chain.push_back(at);
                at = scene.mNodes[at].mParent;
            }
            P above;
            if (at < scene.mNodes.size() && done[at])
                above = w[at];
            for (std::size_t k = chain.size(); k-- > 0;)
            {
                const auto id = chain[k];
                w[id] = comp(above, toP(scene.mNodes[id].mTransform));
                above = w[id];
                done[id] = 1;
            }
        }
        return w;
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    struct Acc
    {
        std::vector<double> v;
        void add(double x) { v.push_back(x); }
        std::string text()
        {
            if (v.empty())
                return "none";
            std::sort(v.begin(), v.end());
            std::ostringstream o;
            o << "n " << v.size() << " median " << v[v.size() / 2] << " p95 " << v[v.size() * 95 / 100] << " max "
              << v.back();
            return o.str();
        }
    };

    void line(const std::string& s)
    {
        std::cout << "SKYTRUNK-SURVEY " << s << std::endl;
    }

    bool isIdentity(const NifScene::Transform& t)
    {
        const double e = 1e-4;
        return std::abs(t.mScale - 1) < e && std::abs(t.mTranslation[0]) < e && std::abs(t.mTranslation[1]) < e
            && std::abs(t.mTranslation[2]) < e && std::abs(t.mRotation[0] - 1) < e && std::abs(t.mRotation[4] - 1) < e
            && std::abs(t.mRotation[8] - 1) < e;
    }

    double dist(const V3& x, const V3& y)
    {
        return std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
    }

    struct Group
    {
        std::uint64_t files = 0, filesWithSkinned = 0, skinnedShapes = 0, skinnedWithTreeAnim = 0, noBoneNodes = 0,
                      skinXformNonIdentity = 0, shapeNodeXformNonIdentity = 0;
        std::map<std::string, std::uint64_t> recordTypes, boneNodeTypes;
        Acc meanAB, worstAB, worstAv, worstBv, bonesPerShape, weightSum, divergentToLeavesA, divergentToLeavesB, axisA, axisB, zminA, zminB;
        std::uint64_t divergent = 0, divergentFiles = 0;
    };

    TEST(SkyrimTrunksReal, aggregates_of_skinned_shapes_in_static_models_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (data == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Data folder";

        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".bsa")
                continue;
            const std::string stem = lower(it->path().filename().string());
            for (const char* prefix : { "skyrim - meshes", "update", "dawnguard", "hearthfires", "dragonborn" })
                if (stem.rfind(prefix, 0) == 0)
                {
                    archives.push_back(it->path());
                    break;
                }
        }
        std::sort(archives.begin(), archives.end());

        // group 0: models with a BSTreeNode, 1: other non-actor models, 2: actor models (path has actors or character)
        Group g[3];
        std::uint64_t failed = 0;

        for (const fs::path& archivePath : archives)
        {
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(archivePath);
                for (const auto& entry : archive.getList())
                {
                    const std::string name = lower(std::string(entry.name()));
                    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".nif") != 0)
                        continue;
                    const bool actor = name.find("actors") != std::string::npos
                        || name.find("character") != std::string::npos;
                    try
                    {
                        auto stream = archive.getFile(&entry);
                        std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                        Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                        const NifScene::Scene scene = NifScene::readScene(
                            std::move(input), VFS::Path::Normalized("survey.nif"), NifScene::SceneOptions());
                        bool tree = false;
                        for (const auto& n : scene.mNodes)
                            tree = tree || n.mRecordType == "BSTreeNode";
                        Group& grp = g[tree ? 0 : (actor ? 2 : 1)];
                        ++grp.files;
                        const std::vector<P> w = worlds(scene);
                        bool any = false;
                        bool fileDivergent = false;
                        // centre of the model's unskinned shapes (the leaves) in the model frame, for the oracle below
                        V3 leaves{ 0, 0, 0 };
                        double leafCount = 0;
                        for (std::size_t ni = 0; ni < scene.mNodes.size(); ++ni)
                        {
                            const auto& nd = scene.mNodes[ni];
                            if (nd.mGeometry >= scene.mGeometries.size())
                                continue;
                            const auto& gg = scene.mGeometries[nd.mGeometry];
                            if (gg.mSkin < scene.mSkins.size())
                                continue;
                            for (const auto& q : gg.mPositions)
                            {
                                const V3 m = ap(w[ni], { q[0], q[1], q[2] });
                                for (int c = 0; c < 3; ++c)
                                    leaves[c] += m[c];
                                leafCount += 1;
                            }
                        }
                        for (std::size_t nodeIndex = 0; nodeIndex < scene.mNodes.size(); ++nodeIndex)
                        {
                            const NifScene::Node& node = scene.mNodes[nodeIndex];
                            if (node.mGeometry >= scene.mGeometries.size())
                                continue;
                            const NifScene::Geometry& geo = scene.mGeometries[node.mGeometry];
                            if (geo.mSkin >= scene.mSkins.size())
                                continue;
                            any = true;
                            ++grp.skinnedShapes;
                            ++grp.recordTypes[node.mRecordType];
                            if (geo.mMaterial < scene.mMaterials.size()
                                && (scene.mMaterials[geo.mMaterial].mFlags2 & 0x20000000u))
                                ++grp.skinnedWithTreeAnim;
                            const NifScene::Skin& skin = scene.mSkins[geo.mSkin];
                            grp.bonesPerShape.add(static_cast<double>(skin.mBoneNodes.size()));
                            const P shapeWorld = w[nodeIndex];
                            grp.skinXformNonIdentity += !isIdentity(skin.mSkinTransform);
                            grp.shapeNodeXformNonIdentity += !isIdentity(node.mTransform);
                            bool allBones
                                = skin.mBoneNodes.size() == skin.mBindTransforms.size() && !skin.mBoneNodes.empty();
                            for (auto b : skin.mBoneNodes)
                                allBones = allBones && b < scene.mNodes.size();
                            if (!allBones)
                            {
                                ++grp.noBoneNodes;
                                continue;
                            }
                            for (auto b : skin.mBoneNodes)
                                ++grp.boneNodeTypes[scene.mNodes[b].mRecordType];
                            std::vector<P> boneBind(skin.mBoneNodes.size());
                            for (std::size_t i = 0; i < boneBind.size(); ++i)
                                boneBind[i] = comp(w[skin.mBoneNodes[i]], toP(skin.mBindTransforms[i]));
                            double worstAB = 0, sumAB = 0, worstAv = 0, worstBv = 0;
                            V3 centreA{ 0, 0, 0 }, centreB{ 0, 0, 0 };
                            double zA = 1e30, zB = 1e30;
                            std::size_t counted = 0;
                            if (geo.mInfluenceStart.size() != geo.mPositions.size() + 1)
                                continue;
                            for (std::size_t v = 0; v < geo.mPositions.size(); ++v)
                            {
                                const V3 p{ geo.mPositions[v][0], geo.mPositions[v][1], geo.mPositions[v][2] };
                                const V3 a = ap(shapeWorld, p);
                                V3 b{ 0, 0, 0 };
                                double ws = 0;
                                for (auto k = geo.mInfluenceStart[v]; k < geo.mInfluenceStart[v + 1]; ++k)
                                {
                                    const auto& inf = geo.mInfluences[k];
                                    if (inf.mBone >= boneBind.size() || !std::isfinite(inf.mWeight))
                                        continue;
                                    const V3 q = ap(boneBind[inf.mBone], p);
                                    for (int c = 0; c < 3; ++c)
                                        b[c] += inf.mWeight * q[c];
                                    ws += inf.mWeight;
                                }
                                if (ws < 1e-6)
                                    continue;
                                grp.weightSum.add(ws);
                                for (int c = 0; c < 3; ++c)
                                    b[c] /= ws;
                                const double dab = dist(a, b);
                                worstAB = std::max(worstAB, dab);
                                sumAB += dab;
                                for (int c = 0; c < 3; ++c)
                                {
                                    centreA[c] += a[c];
                                    centreB[c] += b[c];
                                }
                                zA = std::min(zA, a[2]);
                                zB = std::min(zB, b[2]);
                                worstAv = std::max(worstAv, dist(a, p));
                                worstBv = std::max(worstBv, dist(b, p));
                                ++counted;
                            }
                            if (counted)
                            {
                                grp.meanAB.add(sumAB / static_cast<double>(counted));
                                grp.worstAB.add(worstAB);
                                grp.worstAv.add(worstAv);
                                grp.worstBv.add(worstBv);
                                if (worstAB > 1.0 && leafCount > 0)
                                {
                                    ++grp.divergent;
                                    fileDivergent = true;
                                    V3 lc, ca, cb;
                                    for (int c = 0; c < 3; ++c)
                                    {
                                        lc[c] = leaves[c] / leafCount;
                                        ca[c] = centreA[c] / static_cast<double>(counted);
                                        cb[c] = centreB[c] / static_cast<double>(counted);
                                    }
                                    grp.axisA.add(std::hypot(ca[0], ca[1]));
                                    grp.axisB.add(std::hypot(cb[0], cb[1]));
                                    grp.zminA.add(zA);
                                    grp.zminB.add(zB);
                                    grp.divergentToLeavesA.add(dist(ca, lc));
                                    grp.divergentToLeavesB.add(dist(cb, lc));
                                }
                            }
                        }
                        grp.filesWithSkinned += any;
                        grp.divergentFiles += fileDivergent;
                    }
                    catch (...)
                    {
                        ++failed;
                    }
                }
            }
            catch (...)
            {
                ++failed;
            }
        }
        const char* names[3] = { "BSTreeNode models", "other non-actor models", "actor models" };
        for (int i = 0; i < 3; ++i)
        {
            Group& x = g[i];
            const std::string n = names[i];
            line(n + ": files " + std::to_string(x.files) + " with skinned shapes " + std::to_string(x.filesWithSkinned)
                + " skinned shapes " + std::to_string(x.skinnedShapes) + " of those with Tree_Anim "
                + std::to_string(x.skinnedWithTreeAnim) + " without usable bone nodes "
                + std::to_string(x.noBoneNodes));
            for (auto& [k, v] : x.recordTypes)
                line(n + ": skinned shape node type " + k + " " + std::to_string(v));
            for (auto& [k, v] : x.boneNodeTypes)
                line(n + ": bone node type " + k + " " + std::to_string(v) + " (bone refs)");
            line(n + ": bones per skin " + x.bonesPerShape.text());
            line(n + ": influence weight sum " + x.weightSum.text());
            line(n + ": skin transform not identity " + std::to_string(x.skinXformNonIdentity)
                + ", shape node transform not identity " + std::to_string(x.shapeNodeXformNonIdentity));
            line(n + ": per shape MEAN |A-B| (A node world, B bone blend) " + x.meanAB.text());
            line(n + ": per shape WORST |A-B| " + x.worstAB.text());
            line(n + ": per shape worst |A-stored| " + x.worstAv.text());
            line(n + ": per shape worst |B-stored| " + x.worstBv.text());
            line(n + ": shapes with worst |A-B| > 1 unit: " + std::to_string(x.divergent) + " in "
                + std::to_string(x.divergentFiles) + " files; their centre's distance to the unskinned shapes' centre: A "
                + x.divergentToLeavesA.text() + " ; B " + x.divergentToLeavesB.text());
            line(n + ": divergent shapes, centre distance from the model's vertical axis: A " + x.axisA.text() + " ; B "
                + x.axisB.text() + " ; lowest vertex z: A " + x.zminA.text() + " ; B " + x.zminB.text());
        }
        line("files failed to read " + std::to_string(failed));
    }

    // Local aid for the in-game check (prints form ids and model names of the user's own data; never commit its output):
    // the TREE records of Skyrim.esm whose model has a BSTreeNode.
    struct TreeModels final : Content::Visitor
    {
        std::string mFile;
        std::vector<std::byte> mBuffer;
        std::vector<std::pair<std::uint32_t, std::string>>* mOut = nullptr;
        bool onRecord(const Content::RawRecord& record) override
        {
            if (record.mType != Content::fourCC("TREE"))
                return true;
            std::span<const std::byte> data = record.mData;
            if (record.isCompressed())
            {
                Content::Error error;
                if (!game_skyrimse::inflateRecord(record, mFile, mBuffer, error))
                    return true;
                data = mBuffer;
            }
            game_skyrimse::survey::forEachSubrecord(data, [&](const game_skyrimse::survey::SubrecordView& s) {
                if (Content::fourCCToString(s.mType) == "MODL")
                    mOut->emplace_back(record.mFormId,
                        std::string(reinterpret_cast<const char*>(s.mData.data()),
                            strnlen(reinterpret_cast<const char*>(s.mData.data()), s.mData.size())));
            });
            return true;
        }
    };

    std::string treeKey(std::string name);

    // Local aid: writes the flattened vertices of a few tree models, without and with Limits::mSkinnedAtRest, to the file
    // named by OPENEDEN_TRUNKS_DUMP (one line per vertex: model, mode, x, y, z), for a side view plotted outside the repository.
    TEST(SkyrimTrunksReal, dump_a_few_tree_models_with_and_without_the_rest_pose_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        const char* dump = std::getenv("OPENEDEN_TRUNKS_DUMP");
        if (data == nullptr || dump == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA and OPENEDEN_TRUNKS_DUMP are not both set";
        std::ofstream out(dump);
        const std::set<std::string> wanted{ "landscape\\trees\\treeaspen01.nif", "landscape\\trees\\treepineforest01.nif",
            "landscape\\trees\\treepineforestdead01.nif", "landscape\\trees\\treepineforestsnow01.nif" };
        for (const char* archiveName : { "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa" })
        {
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(fs::path(data) / archiveName);
                for (const auto& entry : archive.getList())
                {
                    const std::string key = treeKey(std::string(entry.name()));
                    if (wanted.count(key) == 0)
                        continue;
                    auto stream = archive.getFile(&entry);
                    std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                    Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                    const NifScene::Scene scene = NifScene::readScene(
                        std::move(input), VFS::Path::Normalized("survey.nif"), NifScene::SceneOptions());
                    for (bool rest : { false, true })
                    {
                        NifScene::Flat::Limits limits;
                        limits.mSkinnedAtRest = rest;
                        const NifScene::Flat::FlatModel model = NifScene::Flat::flatten(scene, limits);
                        for (const auto& shape : model.mShapes)
                            for (const auto& p : shape.mPositions)
                                out << key << ' ' << (rest ? "rest" : "static") << ' ' << p[0] << ' ' << p[1] << ' '
                                    << p[2] << '\n';
                        std::cout << "SKYTRUNK-LOCAL " << key << (rest ? " rest" : " static") << ": " << model.mReport.mVertices
                                  << " vertices, skinned at rest " << model.mReport.mSkinnedAtRest << std::endl;
                    }
                }
            }
            catch (...)
            {
            }
        }
    }

    std::string treeKey(std::string name)
    {
        name = lower(name);
        std::replace(name.begin(), name.end(), '/', '\\');
        if (name.rfind("meshes\\", 0) == 0)
            name = name.substr(7);
        return name;
    }

    TEST(SkyrimTrunksReal, tree_records_whose_model_has_a_tree_node_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (data == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Data folder";
        std::vector<std::pair<std::uint32_t, std::string>> records;
        Content::Error error;
        auto plugin = Content::Plugin::openFile(fs::path(data) / "Skyrim.esm", error);
        ASSERT_TRUE(plugin);
        TreeModels visitor;
        visitor.mFile = "Skyrim.esm";
        visitor.mOut = &records;
        plugin->walk(visitor);
        std::map<std::string, bool> hasNode;
        for (auto& r : records)
            hasNode[treeKey(r.second)] = false;
        for (const char* archiveName : { "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa" })
        {
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(fs::path(data) / archiveName);
                for (const auto& entry : archive.getList())
                {
                    const std::string key = treeKey(std::string(entry.name()));
                    if (hasNode.count(key) == 0)
                        continue;
                    auto stream = archive.getFile(&entry);
                    std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                    Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                    const NifScene::Scene scene = NifScene::readScene(
                        std::move(input), VFS::Path::Normalized("survey.nif"), NifScene::SceneOptions());
                    for (const auto& n : scene.mNodes)
                        if (n.mRecordType == "BSTreeNode")
                            hasNode[key] = true;
                }
            }
            catch (...)
            {
            }
        }
        for (auto& r : records)
            if (hasNode[treeKey(r.second)])
                std::cout << "SKYTRUNK-LOCAL form id " << (r.first & 0xFFFFFF) << " model " << r.second << std::endl;
    }
}
