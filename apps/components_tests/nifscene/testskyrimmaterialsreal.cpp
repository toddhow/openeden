// Task 0130: the gated real-data survey behind the Skyrim SE material mapping (docs/tasks/0130-skyrim-material-mapping-v1.md).
// Not run by default. With OPENEDEN_SKYRIMSE_DATA set to the game's Data folder it reads every .nif of the five masters'
// BSAs with the scene reader and prints AGGREGATE numbers only (lines starting SKYMAT-SURVEY): shader types, texture
// slot use per shader type, shader flag bits, and the agreement of each flag with what the texture set contains. No
// path, name or texture name is printed and nothing is written. OPENEDEN_SKYMAT_SURVEY_MAX stops after that many files.
#include <components/bsa/compressedbsafile.hpp>
#include <components/nifscene/build.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/tangents.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
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

    bool endsWith(const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void line(const std::string& text)
    {
        std::cout << "SKYMAT-SURVEY " << text << std::endl;
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

    struct PerType
    {
        std::uint64_t shapes = 0;
        std::uint64_t skinned = 0;
        std::uint64_t slotUsed[10] = {};
        std::uint64_t flag1[32] = {};
        std::uint64_t flag2[32] = {};
        std::uint64_t withTangents = 0;
        std::uint64_t slot1AndTangents = 0;
        std::uint64_t alphaTest = 0, alphaBlend = 0;
        Stat glossiness, specularStrength, emissiveMultiple, alpha;
        std::uint64_t emissiveNonZero = 0;
        std::map<std::string, std::uint64_t> slotSuffix[10];
    };

    // The last "_x" part of a texture name before the extension, "" when none.
    std::string suffixOf(const std::string& path)
    {
        const std::size_t dot = path.rfind('.');
        const std::string stem = dot == std::string::npos ? path : path.substr(0, dot);
        const std::size_t us = stem.rfind('_');
        if (us == std::string::npos || stem.size() - us > 4)
            return "";
        return stem.substr(us);
    }

    TEST(SkyrimMaterialsReal, aggregates_of_the_users_meshes_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (data == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Data folder";
        std::uint64_t limit = ~0ull;
        if (const char* env = std::getenv("OPENEDEN_SKYMAT_SURVEY_MAX"); env != nullptr && *env)
            limit = std::strtoull(env, nullptr, 10);

        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".bsa")
                continue;
            const std::string stem = lower(it->path().filename().string());
            for (const char* prefix : { "skyrim - ", "update", "dawnguard", "hearthfires", "dragonborn" })
                if (stem.rfind(prefix, 0) == 0)
                {
                    archives.push_back(it->path());
                    break;
                }
        }
        std::sort(archives.begin(), archives.end());

        std::map<std::uint32_t, PerType> types;
        std::uint64_t files = 0, failed = 0, geometries = 0, noMaterial = 0, effects = 0, noTextureSet = 0;
        // Agreement counts: [flag set][slot populated / name has the marker]
        std::uint64_t glowFlag[2][2] = {}; // F2 bit 6 against slot 2 populated
        std::uint64_t envFlag[2][2] = {}; // F1 bit 7 against slot 4 populated
        std::uint64_t parFlag[2][2] = {}; // F1 bit 11 against slot 3 populated
        std::uint64_t msnFlag[2][2] = {}; // F1 bit 12 against the normal map name ending _msn
        std::uint64_t specFlag[2][2] = {}; // F1 bit 0 against specular strength > 0
        std::uint64_t ownEmit[2][2] = {}; // F1 bit 22 against slot 2 populated
        std::uint64_t slot1Msn = 0, slot1Total = 0;
        // emissive colour non-zero against Own_Emit (F1.22) and against the glow flag (F2.6)
        std::uint64_t emitOwn[2][2] = {}; // F1.22 against emissive colour non-zero
        std::uint64_t emitGlow[2][2] = {}; // F2.6 against emissive colour non-zero
        std::map<std::uint32_t, std::uint64_t> emitByType, shapesByTypeAll;
        // tangent convention: stored tangent against the engine's own generator on the shape's UVs
        std::uint64_t tanVerts = 0, tanSame = 0, tanSignFlipped = 0, tanOpposite = 0, tanOther = 0, tanShapes = 0;

        const NifScene::SceneOptions options;
        for (const fs::path& archivePath : archives)
        {
            if (files >= limit)
                break;
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(archivePath);
                for (const auto& entry : archive.getList())
                {
                    if (files >= limit)
                        break;
                    const std::string name = lower(std::string(entry.name()));
                    if (!endsWith(name, ".nif"))
                        continue;
                    ++files;
                    try
                    {
                        auto stream = archive.getFile(&entry);
                        std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                        Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                        const NifScene::Scene scene
                            = NifScene::readScene(std::move(input), VFS::Path::Normalized("survey.nif"), options);
                        for (const NifScene::Geometry& g : scene.mGeometries)
                        {
                            ++geometries;
                            if (g.mMaterial >= scene.mMaterials.size())
                            {
                                ++noMaterial;
                                continue;
                            }
                            const NifScene::ShaderMaterial& m = scene.mMaterials[g.mMaterial];
                            if (m.mKind == NifScene::ShaderKind::Effect)
                            {
                                ++effects;
                                continue;
                            }
                            if (m.mTextureSet >= scene.mTextureSets.size())
                            {
                                ++noTextureSet;
                                continue;
                            }
                            const auto& set = scene.mTextureSets[m.mTextureSet].mTextures;
                            PerType& t = types[m.mShaderType];
                            ++t.shapes;
                            if (g.mSkin != NifScene::sNone)
                                ++t.skinned;
                            bool populated[10] = {};
                            for (std::size_t s = 0; s < set.size() && s < 10; ++s)
                            {
                                populated[s] = !set[s].empty();
                                if (populated[s])
                                {
                                    ++t.slotUsed[s];
                                    ++t.slotSuffix[s][suffixOf(lower(set[s]))];
                                }
                            }
                            for (int b = 0; b < 32; ++b)
                            {
                                if (m.mFlags1 & (1u << b))
                                    ++t.flag1[b];
                                if (m.mFlags2 & (1u << b))
                                    ++t.flag2[b];
                            }
                            if (!g.mTangents.empty())
                            {
                                ++t.withTangents;
                                if (populated[1])
                                    ++t.slot1AndTangents;
                            }
                            if (g.mAlpha < scene.mAlphaProperties.size())
                            {
                                t.alphaTest += scene.mAlphaProperties[g.mAlpha].mTest ? 1 : 0;
                                t.alphaBlend += scene.mAlphaProperties[g.mAlpha].mBlend ? 1 : 0;
                            }
                            t.glossiness.add(m.mGlossiness);
                            t.specularStrength.add(m.mSpecularStrength);
                            t.emissiveMultiple.add(m.mEmissiveMultiple);
                            t.alpha.add(m.mAlpha);
                            if (m.mEmissiveColor[0] > 0 || m.mEmissiveColor[1] > 0 || m.mEmissiveColor[2] > 0)
                                ++t.emissiveNonZero;

                            const bool emitNonZero
                                = m.mEmissiveColor[0] > 0 || m.mEmissiveColor[1] > 0 || m.mEmissiveColor[2] > 0;
                            ++emitOwn[(m.mFlags1 >> 22) & 1][emitNonZero];
                            ++emitGlow[(m.mFlags2 >> 6) & 1][emitNonZero];
                            ++shapesByTypeAll[m.mShaderType];
                            emitByType[m.mShaderType] += emitNonZero;
                            if (populated[1] && g.mTangents.size() == g.mPositions.size() && g.mUV0.size() == g.mPositions.size()
                                && g.mNormals.size() == g.mPositions.size() && g.mPositions.size() < 65000
                                && g.mSkin == NifScene::sNone)
                            {
                                std::vector<vsg::vec3> vertices, normals;
                                std::vector<vsg::vec2> uvs;
                                for (std::size_t v = 0; v < g.mPositions.size(); ++v)
                                {
                                    vertices.emplace_back(g.mPositions[v][0], g.mPositions[v][1], g.mPositions[v][2]);
                                    normals.emplace_back(g.mNormals[v][0], g.mNormals[v][1], g.mNormals[v][2]);
                                    uvs.emplace_back(g.mUV0[v][0], g.mUV0[v][1]);
                                }
                                std::vector<std::uint16_t> tris(g.mTriangles.begin(), g.mTriangles.end());
                                const auto generated = vsgAdapters::generateTangents(vertices, normals, uvs, { tris }, false);
                                if (generated.size() == g.mPositions.size())
                                {
                                    ++tanShapes;
                                    for (std::size_t v = 0; v < generated.size(); ++v)
                                    {
                                        const auto& a = g.mTangents[v];
                                        const auto& b = generated[v];
                                        const double dot = a[0] * b.x + a[1] * b.y + a[2] * b.z;
                                        ++tanVerts;
                                        if (dot > 0.5)
                                            ((a[3] < 0) == (b.w < 0)) ? ++tanSame : ++tanSignFlipped;
                                        else if (dot < -0.5)
                                            ++tanOpposite;
                                        else
                                            ++tanOther;
                                    }
                                }
                            }
                            ++glowFlag[(m.mFlags2 >> 6) & 1][populated[2]];
                            ++envFlag[(m.mFlags1 >> 7) & 1][populated[4]];
                            ++parFlag[(m.mFlags1 >> 11) & 1][populated[3]];
                            ++specFlag[m.mFlags1 & 1][m.mSpecularStrength > 0.f];
                            ++ownEmit[(m.mFlags1 >> 22) & 1][populated[2]];
                            if (populated[1])
                            {
                                ++slot1Total;
                                const bool msn = endsWith(lower(set[1]), "_msn.dds");
                                slot1Msn += msn;
                                ++msnFlag[(m.mFlags1 >> 12) & 1][msn];
                            }
                        }
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

        line("files " + std::to_string(files) + " failed " + std::to_string(failed) + " geometries "
            + std::to_string(geometries) + " no-material " + std::to_string(noMaterial) + " effect-shader "
            + std::to_string(effects) + " no-texture-set " + std::to_string(noTextureSet));
        for (const auto& [type, t] : types)
        {
            const std::string p = "type " + std::to_string(type) + ": ";
            line(p + "shapes " + std::to_string(t.shapes) + " skinned " + std::to_string(t.skinned) + " with-tangents "
                + std::to_string(t.withTangents) + " slot1+tangents " + std::to_string(t.slot1AndTangents)
                + " alpha-test " + std::to_string(t.alphaTest) + " alpha-blend " + std::to_string(t.alphaBlend));
            std::string slots = p + "slot use";
            for (int s = 0; s < 10; ++s)
                slots += " [" + std::to_string(s) + "]=" + std::to_string(t.slotUsed[s]);
            line(slots);
            for (int s = 0; s < 10; ++s)
            {
                if (t.slotSuffix[s].empty())
                    continue;
                std::vector<std::pair<std::string, std::uint64_t>> rows(t.slotSuffix[s].begin(), t.slotSuffix[s].end());
                std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                std::string text = p + "slot " + std::to_string(s) + " name suffixes";
                for (std::size_t i = 0; i < rows.size() && i < 6; ++i)
                    text += " '" + rows[i].first + "'=" + std::to_string(rows[i].second);
                line(text);
            }
            std::string f1 = p + "flags1 bits set";
            std::string f2 = p + "flags2 bits set";
            for (int b = 0; b < 32; ++b)
            {
                if (t.flag1[b])
                    f1 += " " + std::to_string(b) + ":" + std::to_string(t.flag1[b]);
                if (t.flag2[b])
                    f2 += " " + std::to_string(b) + ":" + std::to_string(t.flag2[b]);
            }
            line(f1);
            line(f2);
            line(p + "glossiness " + t.glossiness.text());
            line(p + "specular strength " + t.specularStrength.text());
            line(p + "emissive multiple " + t.emissiveMultiple.text() + " emissive colour non-zero "
                + std::to_string(t.emissiveNonZero));
            line(p + "alpha " + t.alpha.text());
        }
        auto cross = [](const char* name, const std::uint64_t (&c)[2][2]) {
            line(std::string("agreement ") + name + ": flag0/absent " + std::to_string(c[0][0]) + " flag0/present "
                + std::to_string(c[0][1]) + " flag1/absent " + std::to_string(c[1][0]) + " flag1/present "
                + std::to_string(c[1][1]));
        };
        cross("F2.6 glow-map flag vs slot 2", glowFlag);
        cross("F1.22 own-emit flag vs slot 2", ownEmit);
        cross("F1.7 environment-mapping flag vs slot 4", envFlag);
        cross("F1.11 parallax flag vs slot 3", parFlag);
        cross("F1.12 model-space-normals flag vs name _msn", msnFlag);
        cross("F1.0 specular flag vs specular strength > 0", specFlag);
        cross("F1.22 own-emit flag vs emissive colour non-zero", emitOwn);
        cross("F2.6 glow-map flag vs emissive colour non-zero", emitGlow);
        for (const auto& [type, n] : shapesByTypeAll)
            line("type " + std::to_string(type) + ": emissive colour non-zero " + std::to_string(emitByType[type]) + " of "
                + std::to_string(n));
        line("tangent convention (stored vs vsgAdapters::generateTangents on the shape's UVs): shapes "
            + std::to_string(tanShapes) + " vertices " + std::to_string(tanVerts) + " same-direction-same-sign "
            + std::to_string(tanSame) + " same-direction-opposite-sign " + std::to_string(tanSignFlipped)
            + " opposite-direction " + std::to_string(tanOpposite) + " neither " + std::to_string(tanOther));
        line("slot 1 populated " + std::to_string(slot1Total) + " of which _msn " + std::to_string(slot1Msn));
        EXPECT_GT(files, 0u);
    }
}
