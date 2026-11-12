// Task 0133: the gated real-data survey behind Skyrim SE tree sway (docs/tasks/0133-skyrim-tree-sway.md).
// Not run by default. With OPENEDEN_SKYRIMSE_DATA set to the game's Data folder it reads the TREE records of the five
// masters and every .nif of their BSAs and prints AGGREGATE numbers only (lines starting SKYTREE-SURVEY): which
// subrecords a TREE has, the CNAM floats, which shapes carry the Tree_Anim shader flag (F2 bit 29), whether they sit
// below a BSTreeNode, and how their vertex colours relate to the shape's height. No path or name is printed.
#include <components/bsa/compressedbsafile.hpp>
#include <components/contentsource/plugin.hpp>
#include <components/nifscene/build.hpp>
#include <components/vfs/pathutil.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    void line(const std::string& text)
    {
        std::cout << "SKYTREE-SURVEY " << text << std::endl;
    }

    std::string normalise(std::string path)
    {
        std::replace(path.begin(), path.end(), '/', '\\');
        path = lower(path);
        if (path.rfind("meshes\\", 0) == 0)
            path = path.substr(7);
        return path;
    }

    struct Stat
    {
        std::uint64_t n = 0;
        double sum = 0, min = 1e300, max = -1e300;
        void add(double v)
        {
            ++n;
            sum += v;
            min = std::min(min, v);
            max = std::max(max, v);
        }
        std::string text() const
        {
            if (n == 0)
                return "none";
            std::ostringstream out;
            out << "n " << n << " min " << min << " mean " << sum / static_cast<double>(n) << " max " << max;
            return out.str();
        }
    };

    // Pearson correlation accumulator
    struct Corr
    {
        double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        void add(double x, double y)
        {
            n += 1;
            sx += x;
            sy += y;
            sxx += x * x;
            syy += y * y;
            sxy += x * y;
        }
        std::string text() const
        {
            const double vx = sxx - sx * sx / std::max(n, 1.0), vy = syy - sy * sy / std::max(n, 1.0);
            if (n < 2 || vx <= 1e-12 || vy <= 1e-12)
                return "undefined (constant)";
            return std::to_string((sxy - sx * sy / n) / std::sqrt(vx * vy));
        }
    };

    struct TreeVisitor final : Content::Visitor
    {
        std::string mFile;
        std::vector<std::byte> mBuffer;
        std::uint64_t mRecords = 0;
        std::set<std::string>* mModels = nullptr;
        std::map<std::string, std::uint64_t> mSubrecordCounts;
        std::map<std::string, std::uint64_t> mCnamSizes;
        Stat mCnam[16];
        bool onRecord(const Content::RawRecord& record) override
        {
            if (record.mType != Content::fourCC("TREE"))
                return true;
            ++mRecords;
            std::span<const std::byte> data = record.mData;
            if (record.isCompressed())
            {
                Content::Error error;
                if (!game_skyrimse::inflateRecord(record, mFile, mBuffer, error))
                    return true;
                data = mBuffer;
            }
            std::set<std::string> seen;
            game_skyrimse::survey::forEachSubrecord(data, [&](const game_skyrimse::survey::SubrecordView& s) {
                const std::string name = Content::fourCCToString(s.mType);
                seen.insert(name);
                if (name == "MODL")
                    mModels->insert(normalise(std::string(reinterpret_cast<const char*>(s.mData.data()),
                        strnlen(reinterpret_cast<const char*>(s.mData.data()), s.mData.size()))));
                if (name == "CNAM")
                {
                    ++mCnamSizes[std::to_string(s.mData.size())];
                    if (s.mData.size() == 48)
                        for (int i = 0; i < 12; ++i)
                        {
                            float f;
                            std::memcpy(&f, s.mData.data() + 4 * i, 4);
                            if (std::isfinite(f))
                                mCnam[i].add(f);
                        }
                }
            });
            for (const std::string& n : seen)
                ++mSubrecordCounts[n];
            return true;
        }
    };

    TEST(SkyrimTreesReal, aggregates_of_the_users_trees_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (data == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Data folder";

        // 1. TREE records of the five masters
        std::set<std::string> treeModels;
        std::uint64_t treeRecords = 0;
        std::map<std::string, std::uint64_t> subs, cnamSizes;
        Stat cnam[16];
        for (const char* master : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            Content::Error error;
            auto plugin = Content::Plugin::openFile(fs::path(data) / master, error);
            if (!plugin)
                continue;
            TreeVisitor visitor;
            visitor.mFile = master;
            visitor.mModels = &treeModels;
            plugin->walk(visitor);
            treeRecords += visitor.mRecords;
            for (auto& [k, v] : visitor.mSubrecordCounts)
                subs[k] += v;
            for (auto& [k, v] : visitor.mCnamSizes)
                cnamSizes[k] += v;
            for (int i = 0; i < 16; ++i)
                if (visitor.mCnam[i].n)
                {
                    cnam[i].n += visitor.mCnam[i].n;
                    cnam[i].sum += visitor.mCnam[i].sum;
                    cnam[i].min = std::min(cnam[i].min, visitor.mCnam[i].min);
                    cnam[i].max = std::max(cnam[i].max, visitor.mCnam[i].max);
                }
        }
        line("TREE records " + std::to_string(treeRecords) + " distinct models " + std::to_string(treeModels.size()));
        for (auto& [k, v] : subs)
            line("TREE subrecord " + k + " in " + std::to_string(v) + " records");
        for (auto& [k, v] : cnamSizes)
            line("TREE CNAM size " + k + ": " + std::to_string(v));
        for (int i = 0; i < 12; ++i)
            line("TREE CNAM float " + std::to_string(i) + " " + cnam[i].text());

        // 2. meshes
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

        std::uint64_t files = 0, failed = 0, treeModelFiles = 0, treeModelFilesRead = 0;
        std::set<std::string> swayTextures; // lower-case diffuse paths of Tree_Anim shapes
        // per group: [0] meshes of TREE records, [1] other meshes
        struct Group
        {
            std::uint64_t files = 0, withTreeAnim = 0, withTreeNode = 0, treeNodeAndAnim = 0;
            std::uint64_t shapes = 0, animShapes = 0, animShapesUnderTreeNode = 0, animShapesWithColours = 0,
                          animShapesSkinned = 0, animShapesAlphaTest = 0, shapesWithColours = 0;
            std::uint64_t animFlag1[32] = {}, animFlag2[32] = {};
            std::map<std::uint32_t, std::uint64_t> animShaderType;
            Stat animVerts, alphaMin, alphaMean, alphaMax, redMean, greenMean, blueMean;
            Corr aHeight, rHeight, gHeight, bHeight, aRadius, aR, aG, aB, aRadiusStatic;
            std::uint64_t alphaHist[10] = {};
            std::uint64_t skinnedNoFlag = 0, staticNoFlag = 0;
            std::uint64_t alpha255 = 0, alphaVertices = 0, distinctAlphaOver8 = 0, shapesAlphaVaries = 0;
            std::map<std::string, std::uint64_t> nodeTypes;
            std::uint64_t modelsWithoutBranchLeafSplit = 0;
        } groups[2];

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
                    ++files;
                    const bool isTree = treeModels.count(normalise(name)) != 0;
                    treeModelFiles += isTree;
                    Group& g = groups[isTree ? 0 : 1];
                    try
                    {
                        auto stream = archive.getFile(&entry);
                        std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                        Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                        const NifScene::Scene scene = NifScene::readScene(
                            std::move(input), VFS::Path::Normalized("survey.nif"), NifScene::SceneOptions());
                        treeModelFilesRead += isTree;
                        ++g.files;
                        bool anyTree = false, anyAnim = false;
                        for (const auto& n : scene.mNodes)
                            if (n.mRecordType == "BSTreeNode")
                                anyTree = true;
                        g.withTreeNode += anyTree;
                        for (const NifScene::Geometry& geo : scene.mGeometries)
                        {
                            ++g.shapes;
                            if (!geo.mColors.empty())
                                ++g.shapesWithColours;
                            if (geo.mMaterial >= scene.mMaterials.size())
                                continue;
                            const NifScene::ShaderMaterial& m = scene.mMaterials[geo.mMaterial];
                            if (m.mKind != NifScene::ShaderKind::Lighting || !(m.mFlags2 & 0x20000000u))
                            {
                                (geo.mSkin != NifScene::sNone ? g.skinnedNoFlag : g.staticNoFlag) += 1;
                                continue;
                            }
                            anyAnim = true;
                            if (m.mTextureSet < scene.mTextureSets.size() && !scene.mTextureSets[m.mTextureSet].mTextures.empty())
                                swayTextures.insert(lower(scene.mTextureSets[m.mTextureSet].mTextures[0]));
                            ++g.animShapes;
                            ++g.animShaderType[m.mShaderType];
                            for (int b = 0; b < 32; ++b)
                            {
                                g.animFlag1[b] += (m.mFlags1 >> b) & 1;
                                g.animFlag2[b] += (m.mFlags2 >> b) & 1;
                            }
                            if (geo.mSkin != NifScene::sNone)
                                ++g.animShapesSkinned;
                            if (geo.mAlpha < scene.mAlphaProperties.size() && scene.mAlphaProperties[geo.mAlpha].mTest)
                                ++g.animShapesAlphaTest;
                            // under a tree node?
                            std::uint32_t at = geo.mNode;
                            bool under = false;
                            for (int hops = 0; at != NifScene::sNone && hops < 64; ++hops)
                            {
                                if (scene.mNodes[at].mRecordType == "BSTreeNode")
                                {
                                    under = true;
                                    break;
                                }
                                at = scene.mNodes[at].mParent;
                            }
                            g.animShapesUnderTreeNode += under;
                            g.animVerts.add(static_cast<double>(geo.mPositions.size()));
                            if (geo.mColors.size() == geo.mPositions.size() && !geo.mPositions.empty())
                            {
                                ++g.animShapesWithColours;
                                double zmin = 1e30, zmax = -1e30, amin = 1e30, amax = -1e30, asum = 0, rs = 0, gs = 0,
                                       bs = 0;
                                std::set<int> distinct;
                                for (std::size_t v = 0; v < geo.mPositions.size(); ++v)
                                {
                                    zmin = std::min<double>(zmin, geo.mPositions[v][2]);
                                    zmax = std::max<double>(zmax, geo.mPositions[v][2]);
                                    const double a = geo.mColors[v][3];
                                    amin = std::min(amin, a);
                                    amax = std::max(amax, a);
                                    asum += a;
                                    rs += geo.mColors[v][0];
                                    gs += geo.mColors[v][1];
                                    bs += geo.mColors[v][2];
                                    distinct.insert(static_cast<int>(std::lround(a * 255.0)));
                                    g.alpha255 += geo.mColors[v][3] >= 0.999;
                                    ++g.alphaVertices;
                                }
                                const double n = static_cast<double>(geo.mPositions.size());
                                g.alphaMin.add(amin);
                                g.alphaMax.add(amax);
                                g.alphaMean.add(asum / n);
                                g.redMean.add(rs / n);
                                g.greenMean.add(gs / n);
                                g.blueMean.add(bs / n);
                                if (distinct.size() > 8)
                                    ++g.distinctAlphaOver8;
                                if (amax - amin > 0.02)
                                    ++g.shapesAlphaVaries;
                                const double span = zmax - zmin;
                                double rmax = 1e-9;
                                for (const auto& pt : geo.mPositions)
                                    rmax = std::max(rmax, std::hypot(static_cast<double>(pt[0]), static_cast<double>(pt[1])));
                                for (std::size_t v = 0; v < geo.mPositions.size(); ++v)
                                {
                                    const double a = geo.mColors[v][3];
                                    const double r = std::hypot(static_cast<double>(geo.mPositions[v][0]),
                                                         static_cast<double>(geo.mPositions[v][1]))
                                        / rmax;
                                    g.aRadius.add(r, a);
                                    if (geo.mSkin == NifScene::sNone)
                                        g.aRadiusStatic.add(r, a);
                                    g.aR.add(geo.mColors[v][0], a);
                                    g.aG.add(geo.mColors[v][1], a);
                                    g.aB.add(geo.mColors[v][2], a);
                                    ++g.alphaHist[std::min(9, static_cast<int>(a * 10.0))];
                                }
                                if (span > 1e-3)
                                    for (std::size_t v = 0; v < geo.mPositions.size(); ++v)
                                    {
                                        const double h = (geo.mPositions[v][2] - zmin) / span;
                                        g.aHeight.add(h, geo.mColors[v][3]);
                                        g.rHeight.add(h, geo.mColors[v][0]);
                                        g.gHeight.add(h, geo.mColors[v][1]);
                                        g.bHeight.add(h, geo.mColors[v][2]);
                                    }
                            }
                        }
                        g.withTreeAnim += anyAnim;
                        g.treeNodeAndAnim += anyAnim && anyTree;
                    }
                    catch (const std::exception&)
                    {
                        ++failed;
                    }
                }
            }
            catch (const std::exception& error)
            {
                line(std::string("archive skipped: ") + error.what());
            }
        }
        {
            std::set<std::string> existing;
            for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
            {
                if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".bsa")
                    continue;
                const std::string stem = lower(it->path().filename().string());
                bool wanted = false;
                for (const char* prefix : { "skyrim - textures", "_resourcepack", "update", "dawnguard", "hearthfires", "dragonborn" })
                    wanted = wanted || stem.rfind(prefix, 0) == 0;
                if (!wanted)
                    continue;
                try
                {
                    Bsa::CompressedBSAFile archive;
                    archive.open(it->path());
                    for (const auto& entry : archive.getList())
                    {
                        std::string n = lower(std::string(entry.name()));
                        std::replace(n.begin(), n.end(), '/', '\\');
                        existing.insert(n);
                    }
                }
                catch (const std::exception&)
                {
                }
            }
            std::uint64_t missing = 0;
            std::map<std::string, std::uint64_t> missingRoots;
            for (const std::string& path : swayTextures)
            {
                std::string n = path;
                std::replace(n.begin(), n.end(), '/', '\\');
                if (n.rfind("textures\\", 0) != 0)
                    n = "textures\\" + n;
                if (!existing.count(n))
                {
                    ++missing;
                    ++missingRoots[n.substr(0, n.find('\\', 9))];
                }
            }
            line("Tree_Anim diffuse textures: " + std::to_string(swayTextures.size()) + " distinct, " + std::to_string(missing)
                + " not in the five masters' texture archives");
            for (auto& [root, count] : missingRoots)
                line("  missing under " + root + ": " + std::to_string(count));
        }
        line("meshes " + std::to_string(files) + " failed " + std::to_string(failed) + " tree-record model files "
            + std::to_string(treeModelFiles) + " read " + std::to_string(treeModelFilesRead));
        for (int gi = 0; gi < 2; ++gi)
        {
            const Group& g = groups[gi];
            const std::string p = std::string(gi == 0 ? "[TREE models] " : "[other models] ");
            line(p + "files " + std::to_string(g.files) + " with-Tree_Anim-shape " + std::to_string(g.withTreeAnim)
                + " with-BSTreeNode " + std::to_string(g.withTreeNode) + " both " + std::to_string(g.treeNodeAndAnim));
            line(p + "shapes " + std::to_string(g.shapes) + " with-colours " + std::to_string(g.shapesWithColours)
                + " Tree_Anim shapes " + std::to_string(g.animShapes) + " under-BSTreeNode "
                + std::to_string(g.animShapesUnderTreeNode) + " with-colours "
                + std::to_string(g.animShapesWithColours) + " skinned " + std::to_string(g.animShapesSkinned)
                + " alpha-test " + std::to_string(g.animShapesAlphaTest));
            std::string types = p + "Tree_Anim shader types";
            for (auto& [t, c] : g.animShaderType)
                types += " " + std::to_string(t) + ":" + std::to_string(c);
            line(types);
            std::string f1 = p + "Tree_Anim flags1";
            std::string f2 = p + "Tree_Anim flags2";
            for (int b = 0; b < 32; ++b)
            {
                if (g.animFlag1[b])
                    f1 += " " + std::to_string(b) + ":" + std::to_string(g.animFlag1[b]);
                if (g.animFlag2[b])
                    f2 += " " + std::to_string(b) + ":" + std::to_string(g.animFlag2[b]);
            }
            line(f1);
            line(f2);
            line(p + "Tree_Anim vertices per shape " + g.animVerts.text());
            line(p + "alpha min " + g.alphaMin.text() + " | max " + g.alphaMax.text() + " | mean " + g.alphaMean.text());
            line(p + "rgb means R " + g.redMean.text() + " G " + g.greenMean.text() + " B " + g.blueMean.text());
            line(p + "alpha varies in " + std::to_string(g.shapesAlphaVaries) + " shapes, more than 8 distinct values in "
                + std::to_string(g.distinctAlphaOver8) + "; vertices with alpha 1: " + std::to_string(g.alpha255)
                + " of " + std::to_string(g.alphaVertices));
            line(p + "correlation of alpha with: radius " + g.aRadius.text() + " radius (unskinned) "
                + g.aRadiusStatic.text() + " R " + g.aR.text() + " G " + g.aG.text() + " B " + g.aB.text());
            {
                std::string hist = p + "alpha histogram (10 bins)";
                for (std::uint64_t h : g.alphaHist)
                    hist += " " + std::to_string(h);
                line(hist);
                line(p + "shapes WITHOUT Tree_Anim: skinned " + std::to_string(g.skinnedNoFlag) + " static "
                    + std::to_string(g.staticNoFlag));
            }
            line(p + "correlation with normalised height: alpha " + g.aHeight.text() + " R " + g.rHeight.text() + " G "
                + g.gHeight.text() + " B " + g.bHeight.text());
        }
    }
}
