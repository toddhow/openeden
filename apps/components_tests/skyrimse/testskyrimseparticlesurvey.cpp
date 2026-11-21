// Task 0359 stage b: the gated survey of Skyrim SE's NIF particle systems (docs/research/0148-skyrim-fire-effects.md).
// Prints AGGREGATE lines (SKYRIMSE-PSYS): which emitters, modifiers and controllers the particle systems of each model
// class use. OPENEDEN_PSYS_DUMP=<part of a model path> prints the parameters of every particle system of the models
// whose path contains that text (the paths are not printed). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <gtest/gtest.h>

#include <components/bsa/compressedbsafile.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/data.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/nif/particle.hpp>
#include <components/vfs/pathutil.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

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
        std::cout << "SKYRIMSE-PSYS " << text << "\n";
    }

    std::vector<fs::path> meshArchives(const char* dir)
    {
        std::vector<fs::path> result;
        for (const auto& e : fs::directory_iterator(dir))
        {
            const std::string name = lowerOf(e.path().filename().string());
            if (e.path().extension() == ".bsa" && name.rfind("skyrim - meshes", 0) == 0)
                result.push_back(e.path());
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::string describe(const Nif::NIFFile& file, const Nif::NiParticleSystem& ps)
    {
        std::ostringstream o;
        o << "psys '" << ps.mName << "' worldSpace " << ps.mWorldSpace << " pos " << ps.mTransform.mTranslation.x << ","
          << ps.mTransform.mTranslation.y << "," << ps.mTransform.mTranslation.z << " scale " << ps.mTransform.mScale;
        if (const auto* data = dynamic_cast<const Nif::NiPSysData*>(ps.mData.getPtr()))
        {
            o << " data{vertices " << data->mNumVertices << " active " << data->mActiveCount << " subtex "
              << data->mSubtextureOffsets.size() << " aspect " << data->mAspectRatio << "}";
            for (const auto& s : data->mSubtextureOffsets)
                o << " [" << s.x << "," << s.y << "," << s.z << "," << s.w << "]";
        }
        o << " nearfar " << ps.mNearFar[0] << ".." << ps.mNearFar[1] << " shader "
          << (ps.mShaderProperty.empty() ? "none" : ps.mShaderProperty->mRecordName) << " alpha "
          << (ps.mAlphaProperty.empty() ? "none" : "yes") << "\n";
        for (const auto& m : ps.mModifiers)
        {
            if (m.empty())
                continue;
            o << "    " << m->mRecordName << " order " << static_cast<std::uint32_t>(m->mOrder) << " active "
              << m->mActive;
            if (const auto* e = dynamic_cast<const Nif::NiPSysEmitter*>(m.getPtr()))
            {
                o << " speed " << e->mSpeed << "+-" << e->mSpeedVariation << " decl " << e->mDeclination << "+-"
                  << e->mDeclinationVariation << " planar " << e->mPlanarAngle << "+-" << e->mPlanarAngleVariation
                  << " colour " << e->mInitialColor.x << "," << e->mInitialColor.y << "," << e->mInitialColor.z << ","
                  << e->mInitialColor.w << " radius " << e->mInitialRadius << "+-" << e->mRadiusVariation << " life "
                  << e->mLifespan << "+-" << e->mLifespanVariation;
                if (const auto* b = dynamic_cast<const Nif::NiPSysBoxEmitter*>(m.getPtr()))
                    o << " box " << b->mWidth << "x" << b->mHeight << "x" << b->mDepth;
                else if (const auto* c = dynamic_cast<const Nif::NiPSysCylinderEmitter*>(m.getPtr()))
                    o << " cylinder r " << c->mRadius << " h " << c->mHeight;
                else if (const auto* s = dynamic_cast<const Nif::NiPSysSphereEmitter*>(m.getPtr()))
                    o << " sphere r " << s->mRadius;
                if (const auto* v = dynamic_cast<const Nif::NiPSysVolumeEmitter*>(m.getPtr()))
                {
                    o << " emitterObject " << (v->mEmitterObject.empty() ? "none" : v->mEmitterObject->mName);
                    if (!v->mEmitterObject.empty())
                    {
                        const auto& t = v->mEmitterObject->mTransform;
                        o << " at " << t.mTranslation.x << "," << t.mTranslation.y << "," << t.mTranslation.z
                          << " scale " << t.mScale << " rot";
                        for (int r = 0; r < 3; ++r)
                            o << " [" << t.mRotation.mValues[r][0] << "," << t.mRotation.mValues[r][1] << ","
                              << t.mRotation.mValues[r][2] << "]";
                    }
                }
            }
            else if (const auto* g = dynamic_cast<const Nif::NiPSysGrowFadeModifier*>(m.getPtr()))
                o << " grow " << g->mGrowTime << " gen " << g->mGrowGeneration << " fade " << g->mFadeTime << " gen "
                  << g->mFadeGeneration << " base " << g->mBaseScale;
            else if (const auto* r = dynamic_cast<const Nif::NiPSysRotationModifier*>(m.getPtr()))
                o << " speed " << r->mRotationSpeed << "+-" << r->mRotationSpeedVariation << " angle "
                  << r->mRotationAngle << "+-" << r->mRotationAngleVariation << " randsign " << r->mRandomRotSpeedSign;
            else if (const auto* gr = dynamic_cast<const Nif::NiPSysGravityModifier*>(m.getPtr()))
                o << " axis " << gr->mGravityAxis.x << "," << gr->mGravityAxis.y << "," << gr->mGravityAxis.z
                  << " strength " << gr->mStrength << " decay " << gr->mDecay << " force "
                  << static_cast<int>(gr->mForceType) << " turbulence " << gr->mTurbulence << " worldAligned "
                  << gr->mWorldAligned;
            else if (const auto* d = dynamic_cast<const Nif::NiPSysDragModifier*>(m.getPtr()))
                o << " drag " << d->mPercentage << " range " << d->mRange;
            else if (const auto* sc = dynamic_cast<const Nif::BSPSysSimpleColorModifier*>(m.getPtr()))
            {
                o << " fadeIn " << sc->mFadeInPercent << " fadeOut " << sc->mFadeOutPercent << " c1 "
                  << sc->mColor1StartPercent << ".." << sc->mColor1EndPercent << " c2 " << sc->mColor2StartPercent
                  << ".." << sc->mColor2EndPercent;
                for (const auto& c : sc->mColors)
                    o << " (" << c.x << "," << c.y << "," << c.z << "," << c.w << ")";
            }
            else if (const auto* st = dynamic_cast<const Nif::BSPSysSubTexModifier*>(m.getPtr()))
                o << " start " << st->mStartFrame << "+-" << st->mStartFrameFudge << " end " << st->mEndFrame
                  << " loopStart " << st->mLoopStartFrame << "+-" << st->mLoopStartFrameFudge << " count "
                  << st->mFrameCount << "+-" << st->mFrameCountFudge;
            else if (const auto* sp = dynamic_cast<const Nif::NiPSysSpawnModifier*>(m.getPtr()))
                o << " generations " << sp->mNumSpawnGenerations << " pct " << sp->mPercentageSpawned;
            else if (const auto* sca = dynamic_cast<const Nif::BSPSysScaleModifier*>(m.getPtr()))
            {
                o << " scales";
                for (float s : sca->mScales)
                    o << " " << s;
            }
            o << "\n";
        }
        // controllers on the system
        for (auto ctrl = ps.mController; !ctrl.empty(); ctrl = ctrl->mNext)
            o << "    controller " << ctrl->mRecordName << "\n";
        // controllers that name this system (emitter birth rate)
        for (const auto& r : file.mRecords)
            if (const auto* c = dynamic_cast<const Nif::NiPSysEmitterCtlr*>(r.get()))
            {
                if (c->mInterpolator.empty() || c->mTarget.getPtr() != &ps)
                    continue;
                o << "    emitterCtlr '" << c->mModifierName << "' interpolator " << c->mInterpolator->mRecordName;
                if (const auto* fi = dynamic_cast<const Nif::NiFloatInterpolator*>(c->mInterpolator.getPtr()))
                {
                    o << " default " << fi->mDefaultValue;
                    if (!fi->mData.empty() && fi->mData->mKeyList)
                    {
                        o << " keys";
                        for (const auto& [t, k] : fi->mData->mKeyList->mKeys)
                            o << " (" << t << ":" << k.mValue << ")";
                    }
                }
                o << " freq " << c->mFrequency << " start " << c->mTimeStart << " stop " << c->mTimeStop << " cycle "
                  << (c->mFlags & 6) << "\n";
            }
        return o.str();
    }
}

TEST(SkyrimSeParticleSurvey, aggregate)
{
    const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    if (env == nullptr || !fs::is_directory(env))
        GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder";
    struct Group
    {
        std::uint64_t mFiles = 0, mFilesWithPsys = 0, mSystems = 0, mWorldSpace = 0, mNoShader = 0, mEffectShader = 0;
        std::map<std::string, std::uint64_t> mKinds; // record type -> systems or modifiers that use it
    };
    std::map<std::string, Group> groups;
    for (const fs::path& path : meshArchives(env))
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
            if (data.find("NiParticleSystem") == std::string::npos
                && data.find("BSStripParticleSystem") == std::string::npos)
                continue;
            Group& g = groups[classOf(name)];
            ++g.mFiles;
            try
            {
                Nif::ReadOptions ro;
                ro.mAllowedVersions = Nif::VersionSet::SkyrimSE;
                Nif::NIFFile file(VFS::Path::Normalized("s.nif"));
                Nif::Reader reader(file);
                reader.parse(std::make_unique<std::istringstream>(data, std::ios::binary), ro);
                bool any = false;
                for (const auto& r : file.mRecords)
                {
                    const auto* ps = dynamic_cast<const Nif::NiParticleSystem*>(r.get());
                    if (ps == nullptr)
                        continue;
                    any = true;
                    ++g.mSystems;
                    g.mWorldSpace += ps->mWorldSpace;
                    ++g.mKinds["system " + ps->mRecordName];
                    g.mNoShader += ps->mShaderProperty.empty();
                    g.mEffectShader
                        += !ps->mShaderProperty.empty() && ps->mShaderProperty->mRecordName == "BSEffectShaderProperty";
                    for (const auto& m : ps->mModifiers)
                        if (!m.empty())
                            ++g.mKinds["modifier " + m->mRecordName];
                }
                g.mFilesWithPsys += any;
            }
            catch (const std::exception&)
            {
                ++g.mKinds["READ FAILED"];
            }
        }
    }
    for (const auto& [name, g] : groups)
    {
        line(name + ": files " + std::to_string(g.mFilesWithPsys) + " systems " + std::to_string(g.mSystems)
            + " worldSpace " + std::to_string(g.mWorldSpace) + " effect-shader " + std::to_string(g.mEffectShader)
            + " no-shader " + std::to_string(g.mNoShader));
        for (const auto& [t, n] : g.mKinds)
            line(name + ": " + t + " " + std::to_string(n));
    }
    EXPECT_FALSE(groups.empty());
}

TEST(SkyrimSeParticleSurvey, dump)
{
    const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    const char* want = std::getenv("OPENEDEN_PSYS_DUMP");
    if (env == nullptr || want == nullptr || !fs::is_directory(env))
        GTEST_SKIP() << "OPENEDEN_PSYS_DUMP not set";
    const std::string needle = lowerOf(want);
    for (const fs::path& path : meshArchives(env))
    {
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        for (const auto& entry : archive.getList())
        {
            std::string name = lowerOf(std::string(entry.name()));
            std::replace(name.begin(), name.end(), static_cast<char>(92), '/');
            if (name.find(needle) == std::string::npos || name.size() < 4
                || name.compare(name.size() - 4, 4, ".nif") != 0)
                continue;
            auto stream = archive.getFile(&entry);
            std::string data((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            try
            {
                Nif::ReadOptions ro;
                ro.mAllowedVersions = Nif::VersionSet::SkyrimSE;
                Nif::NIFFile file(VFS::Path::Normalized("s.nif"));
                Nif::Reader reader(file);
                reader.parse(std::make_unique<std::istringstream>(data, std::ios::binary), ro);
                line("model " + name);
                for (const auto& r : file.mRecords)
                    if (const auto* ps = dynamic_cast<const Nif::NiParticleSystem*>(r.get()))
                        std::cout << describe(file, *ps);
            }
            catch (const std::exception& e)
            {
                line(std::string("failed: ") + e.what());
            }
        }
    }
}
