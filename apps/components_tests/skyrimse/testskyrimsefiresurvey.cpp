// Task 0359: the gated survey of Skyrim SE's fire and effect models (docs/research/0148-skyrim-fire-effects.md).
// Prints AGGREGATE lines only (SKYRIMSE-FIRE): block type counts by model class and the flags of the effect shaders
// (refraction, palette, falloff, soft depth, blend). No mesh path is printed. Skipped when OPENEDEN_SKYRIMSE_DATA is
// not set. Not part of any acceptance test; it only reads.
#include <game_skyrimse/survey_nif.hpp>

#include <gtest/gtest.h>

#include <components/bsa/compressedbsafile.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/data.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>
#include <components/vfs/pathutil.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    std::string lowerOf(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool has(const std::string& s, const char* part)
    {
        return s.find(part) != std::string::npos;
    }

    // A model class from its path: where the model is used, not what it is called in particular.
    std::string classOf(const std::string& p)
    {
        if (has(p, "meshes/magic/"))
            return "magic";
        if (has(p, "meshes/effects/"))
            return "effects";
        if (has(p, "torch") || has(p, "candle") || has(p, "lantern") || has(p, "sconce"))
            return "light-prop";
        if (has(p, "fire") || has(p, "flame") || has(p, "brazier") || has(p, "hearth") || has(p, "forge"))
            return "fire-prop";
        if (has(p, "smoke") || has(p, "steam") || has(p, "mist") || has(p, "fog"))
            return "smoke";
        return "other";
    }

    void line(const std::string& text)
    {
        std::cout << "SKYRIMSE-FIRE " << text << "\n";
    }

    struct Group
    {
        std::uint64_t mFiles = 0, mWithEffectShader = 0, mEffectShapes = 0, mRefract = 0, mFireRefract = 0,
                      mPalette = 0, mFalloff = 0, mSoft = 0, mLightingRefract = 0, mLightingFilesRefract = 0,
                      mTwoSided = 0, mAdditive = 0, mVertexAlpha = 0, mAllRefract = 0;
        std::map<std::string, std::uint64_t> mFilesByType;
        std::map<std::string, std::uint64_t> mVariables; // effect float controller variable: blocks, managed or not
    };
}

TEST(SkyrimSeFireSurvey, effects)
{
    const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    if (env == nullptr || !fs::is_directory(env))
        GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder";
    std::map<std::string, Group> groups;
    std::vector<fs::path> archives;
    for (const auto& e : fs::directory_iterator(env))
    {
        const std::string name = lowerOf(e.path().filename().string());
        if (e.path().extension() == ".bsa"
            && (name.rfind("skyrim - ", 0) == 0 || name.rfind("update", 0) == 0 || name.rfind("dawnguard", 0) == 0
                || name.rfind("hearthfires", 0) == 0 || name.rfind("dragonborn", 0) == 0))
            archives.push_back(e.path());
    }
    std::sort(archives.begin(), archives.end());
    for (const fs::path& path : archives)
    {
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        for (const auto& entry : archive.getList())
        {
            std::string name = lowerOf(std::string(entry.name()));
            std::replace(name.begin(), name.end(), static_cast<char>(92), '/');
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".nif") != 0)
                continue;
            auto stream = archive.getFile(&entry);
            std::string data((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            const auto header = game_skyrimse::survey::readNifHeader(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()));
            if (!header.mOk)
                continue;
            std::set<std::string> types(header.mTypeNames.begin(), header.mTypeNames.end());
            Group& g = groups[classOf(name)];
            ++g.mFiles;
            for (const std::string& t : types)
                if (t.find("Particle") != std::string::npos || t.find("PSys") != std::string::npos
                    || t.find("Effect") != std::string::npos || t.find("Controller") != std::string::npos
                    || t.find("Alpha") != std::string::npos || t.find("Flip") != std::string::npos
                    || t.find("Emitter") != std::string::npos)
                    ++g.mFilesByType[t];
            {
                // lighting shaders that refract (the heat planes of hearths and braziers are of this kind)
                bool any = false;
                try
                {
                    NifScene::SceneOptions options;
                    const NifScene::Scene scene
                        = NifScene::readScene(std::make_unique<std::istringstream>(data, std::ios::binary),
                            VFS::Path::Normalized("s.nif"), options);
                    for (const auto& geometry : scene.mGeometries)
                        if (geometry.mMaterial < scene.mMaterials.size())
                        {
                            const auto& m = scene.mMaterials[geometry.mMaterial];
                            if (m.mKind != NifScene::ShaderKind::Effect && (m.mFlags1 & ((1u << 15) | (1u << 16))) != 0)
                            {
                                ++g.mLightingRefract;
                                any = true;
                            }
                        }
                }
                catch (const std::exception&)
                {
                }
                g.mLightingFilesRefract += any;
            }
            if (types.count("BSEffectShaderPropertyFloatController")
                || types.count("BSEffectShaderPropertyColorController"))
            {
                try
                {
                    Nif::ReadOptions ro;
                    ro.mAllowedVersions = Nif::VersionSet::SkyrimSE;
                    Nif::NIFFile file(VFS::Path::Normalized("s.nif"));
                    Nif::Reader reader(file);
                    reader.parse(std::make_unique<std::istringstream>(data, std::ios::binary), ro);
                    for (std::size_t i = 0; i < header.mBlockTypes.size(); ++i)
                    {
                        Nif::Record* r = reader.getRecord(i);
                        if (auto* f = dynamic_cast<Nif::BSEffectShaderPropertyFloatController*>(r))
                            ++g.mVariables["float var " + std::to_string(f->mControlledVariable)
                                + (f->mManagerControlled ? " managed" : " own-clock")];
                        else if (auto* c = dynamic_cast<Nif::BSEffectShaderPropertyColorController*>(r))
                            ++g.mVariables["colour var " + std::to_string(c->mControlledColor)
                                + (c->mManagerControlled ? " managed" : " own-clock")];
                    }
                }
                catch (const std::exception&)
                {
                }
            }
            if (!types.count("BSEffectShaderProperty"))
                continue;
            ++g.mWithEffectShader;
            try
            {
                NifScene::SceneOptions options;
                const NifScene::Scene scene
                    = NifScene::readScene(std::make_unique<std::istringstream>(data, std::ios::binary),
                        VFS::Path::Normalized("s.nif"), options);
                bool refract = false;
                for (const auto& geometry : scene.mGeometries)
                {
                    if (geometry.mMaterial >= scene.mMaterials.size())
                        continue;
                    const auto& m = scene.mMaterials[geometry.mMaterial];
                    if (m.mKind != NifScene::ShaderKind::Effect)
                        continue;
                    ++g.mEffectShapes;
                    const bool r = (m.mFlags1 & (1u << 15)) != 0, f = (m.mFlags1 & (1u << 16)) != 0;
                    g.mRefract += r;
                    g.mFireRefract += f;
                    refract = refract || r || f;
                    g.mPalette += (m.mFlags1 & (1u << 4)) != 0;
                    g.mFalloff += (m.mFalloff[0] != 0.f || m.mFalloff[1] != 0.f || m.mFalloff[2] != 1.f
                        || m.mFalloff[3] != 1.f);
                    g.mSoft += m.mSoftFalloffDepth > 0.f;
                    g.mVertexAlpha += (m.mFlags1 & (1u << 3)) != 0;
                }
                g.mAllRefract += refract;
            }
            catch (const std::exception&)
            {
            }
        }
    }
    for (const auto& [name, g] : groups)
    {
        line(name + ": files " + std::to_string(g.mFiles) + " with effect shader " + std::to_string(g.mWithEffectShader)
            + " effect shapes " + std::to_string(g.mEffectShapes) + " refraction " + std::to_string(g.mRefract)
            + " fire-refraction " + std::to_string(g.mFireRefract) + " files with any refraction "
            + std::to_string(g.mAllRefract) + " palette " + std::to_string(g.mPalette) + " falloff "
            + std::to_string(g.mFalloff) + " soft " + std::to_string(g.mSoft) + " vertex-alpha "
            + std::to_string(g.mVertexAlpha) + " LIGHTING-shader refraction shapes "
            + std::to_string(g.mLightingRefract) + " in files " + std::to_string(g.mLightingFilesRefract));
        for (const auto& [t, n] : g.mVariables)
            line(name + ": controller " + t + " blocks " + std::to_string(n));
        for (const auto& [t, n] : g.mFilesByType)
            line(name + ": block " + t + " files " + std::to_string(n));
    }
    EXPECT_FALSE(groups.empty());
}

// OPENEDEN_FIRE_DUMP=<part of a model path> prints that model's effect shader controllers (a developer aid for the
// research note: which variable, clock and keys). Prints no path.
TEST(SkyrimSeFireSurvey, dump_controllers)
{
    const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    const char* want = std::getenv("OPENEDEN_FIRE_DUMP");
    if (env == nullptr || want == nullptr || !fs::is_directory(env))
        GTEST_SKIP() << "OPENEDEN_FIRE_DUMP not set";
    for (const auto& e : fs::directory_iterator(env))
    {
        const std::string aname = lowerOf(e.path().filename().string());
        if (e.path().extension() != ".bsa" || aname.rfind("skyrim - meshes", 0) != 0)
            continue;
        Bsa::CompressedBSAFile archive;
        archive.open(e.path());
        for (const auto& entry : archive.getList())
        {
            std::string name = lowerOf(std::string(entry.name()));
            std::replace(name.begin(), name.end(), static_cast<char>(92), '/');
            if (name.find(want) == std::string::npos)
                continue;
            auto stream = archive.getFile(&entry);
            std::string data((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            Nif::ReadOptions ro;
            ro.mAllowedVersions = Nif::VersionSet::SkyrimSE;
            Nif::NIFFile file(VFS::Path::Normalized("s.nif"));
            Nif::Reader reader(file);
            reader.parse(std::make_unique<std::istringstream>(data, std::ios::binary), ro);
            const auto header = game_skyrimse::survey::readNifHeader(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()));
            line("model " + name);
            {
                NifScene::SceneOptions options;
                const NifScene::Scene scene
                    = NifScene::readScene(std::make_unique<std::istringstream>(data, std::ios::binary),
                        VFS::Path::Normalized("s.nif"), options);
                for (std::size_t i = 0; i < scene.mMaterials.size(); ++i)
                {
                    const auto& a = scene.mMaterials[i].mUvAnimation;
                    std::ostringstream o;
                    o << "  scene material " << i << " uv offset " << a.mBaseOffset[0] << "," << a.mBaseOffset[1]
                      << " scale " << a.mBaseScale[0] << "," << a.mBaseScale[1];
                    const char* names[4] = { "uOffset", "vOffset", "uScale", "vScale" };
                    const NifScene::FloatTrack* tracks[4]
                        = { &a.mOffset[0], &a.mOffset[1], &a.mScale[0], &a.mScale[1] };
                    for (int k = 0; k < 4; ++k)
                        if (!tracks[k]->empty())
                            o << " | " << names[k] << " keys " << tracks[k]->mKeys.size() << " interp "
                              << tracks[k]->mInterpolation << " window " << tracks[k]->mStart << ".."
                              << tracks[k]->mStop << " cycle " << tracks[k]->mCycle;
                    line(o.str());
                }
            }
            for (std::size_t i = 0; i < header.mBlockTypes.size(); ++i)
            {
                Nif::Record* r = reader.getRecord(i);
                std::ostringstream o;
                o << "  #" << i << " " << header.mTypeNames[header.mBlockTypes[i]];
                if (auto* c = dynamic_cast<Nif::NiTimeController*>(r))
                {
                    o << " freq " << c->mFrequency << " phase " << c->mPhase << " t " << c->mTimeStart << ".."
                      << c->mTimeStop << " flags " << c->mFlags << " target "
                      << (c->mTarget.empty() ? -1 : int(c->mTarget->mRecordIndex));
                    if (auto* f = dynamic_cast<Nif::BSEffectShaderPropertyFloatController*>(r))
                        o << " FLOATVAR " << f->mControlledVariable;
                    if (auto* f = dynamic_cast<Nif::BSEffectShaderPropertyColorController*>(r))
                        o << " COLORVAR " << f->mControlledColor;
                    if (auto* s = dynamic_cast<Nif::NiSingleInterpController*>(r))
                        if (!s->mInterpolator.empty())
                        {
                            if (auto* fi = dynamic_cast<Nif::NiFloatInterpolator*>(s->mInterpolator.getPtr()))
                            {
                                o << " floatInterp default " << fi->mDefaultValue;
                                if (!fi->mData.empty() && !fi->mData->mKeyList->mKeys.empty())
                                {
                                    o << " type " << fi->mData->mKeyList->mInterpolationType << " keys";
                                    int n = 0;
                                    for (const auto& k : fi->mData->mKeyList->mKeys)
                                        if (n++ < 12)
                                            o << " (" << k.first << "," << k.second.mValue << ")";
                                    o << " n=" << fi->mData->mKeyList->mKeys.size();
                                }
                            }
                            else
                                o << " interp record";
                        }
                }
                if (o.str().size() > 20 && (o.str().find("freq") != std::string::npos))
                    line(o.str());
            }
        }
    }
}

// OPENEDEN_FIRE_EXTRACT=<part of an archive path> with OPENEDEN_FIRE_EXTRACT_OUT=<folder> copies the matching files
// out of the textures archives (a developer aid for looking at an effect's palette and atlas by eye).
TEST(SkyrimSeFireSurvey, extract_files)
{
    const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    const char* want = std::getenv("OPENEDEN_FIRE_EXTRACT");
    const char* out = std::getenv("OPENEDEN_FIRE_EXTRACT_OUT");
    if (env == nullptr || want == nullptr || out == nullptr)
        GTEST_SKIP() << "OPENEDEN_FIRE_EXTRACT not set";
    for (const auto& e : fs::directory_iterator(env))
    {
        const std::string aname = lowerOf(e.path().filename().string());
        if (e.path().extension() != ".bsa" || aname.rfind("skyrim - textures", 0) != 0)
            continue;
        Bsa::CompressedBSAFile archive;
        archive.open(e.path());
        for (const auto& entry : archive.getList())
        {
            std::string name = lowerOf(std::string(entry.name()));
            std::replace(name.begin(), name.end(), static_cast<char>(92), '/');
            if (name.find(want) == std::string::npos)
                continue;
            auto stream = archive.getFile(&entry);
            std::string data((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            const fs::path path = fs::path(out) / fs::path(name).filename();
            std::ofstream(path, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    }
}
