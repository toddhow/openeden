#include "resourcesystem.hpp"

#include <cstdlib>

#include <vsgXchange/images.h>
// #include <vsgXchange/models.h>
#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/spirv.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/Sampler.h>
#include <vsg/utils/FindDynamicObjects.h>
#include <vsg/utils/PropagateDynamicObjects.h>
#include <vsg/utils/SharedObjects.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vsgadapters/dae/dae.hpp>
#include <components/vsgadapters/dae/daeclips.hpp>
#include <components/vsgadapters/nif/kf.hpp>
#include <components/vsgadapters/nif/nif.hpp>
#include <components/vsgadapters/vfs.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include "decompress.hpp"
#include "fallback.hpp"
#include "shadersettings.hpp"

namespace
{
    // openeden-vsg: vsgopenmw passes mesh paths relative to "meshes/", while OpenMW 0.51 code passes paths already
    // corrected to start with it (Class::getCorrectedModel, ResourceHelpers::correctMeshPath). Accept both.
    std::string inMeshesDirectory(const vsg::Path& file)
    {
        const VFS::Path::Normalized normalized(file.string());
        constexpr std::string_view meshes = "meshes/";
        if (normalized.value().starts_with(meshes))
            return normalized.value();
        return std::string(meshes) + normalized.value();
    }
}

namespace Resource
{
    vsg::ref_ptr<const vsg::Options> createShaderOptions(
        const std::string& shaderPath, const std::string& cacheDirectory)
    {
        auto options = vsg::Options::create();
        auto shaderSettings = vsg::ref_ptr{ new Resource::ShaderSettings };
        options->paths = { shaderPath };
        options->readerWriters = { shaderSettings, vsg::glsl::create(), vsg::spirv::create() };
        options->sharedObjects = vsg::SharedObjects::create();
        // openeden-vsg: VSG 1.1.x clones "dynamic" objects of shared reads by default, which turns vsgopenmw's
        // Anim::Transform roots into plain groups (losing Bip01); vsgopenmw clones what it needs itself
        // (mwanimation/clone.hpp).
        options->findDynamicObjects = {};
        options->propagateDynamicObjects = {};
        // Task 0072: `[Rendering] slang shaders`, for every reader of these options (pipelines, the terrain, the GUI).
        options->setValue(vsgUtil::preferSlangKey, Settings::rendering().mSlangShaders.get());
        // Task 0072, stage b: the `*.settings` values the Slang twins take as specialization constants.
        options->setObject(vsgUtil::settingsConstantsKey, shaderSettings->constants);
        // Task 0072, stage h: with the switch on, a define set with no prebuilt permutation is compiled at run time
        // and cached under the user data folder. Nothing is loaded here: the library starts at the first compile.
        if (Settings::rendering().mSlangShaders.get())
        {
            // OPENEDEN_SHADER_CACHE names another cache folder (for benchmarks and checks that start from an empty or
            // a kept cache); the setting's own place is the user data folder.
            const char* cacheOverride = std::getenv("OPENEDEN_SHADER_CACHE");
            vsgUtil::createSlangRuntime(
                options, std::filesystem::path(shaderPath), std::filesystem::path(shaderPath).parent_path() / "slang",
                std::filesystem::path(
                    cacheOverride != nullptr && cacheOverride[0] != '\0' ? cacheOverride : cacheDirectory),
                [](bool warning, const std::string& message) {
                    Log(warning ? Debug::Warning : Debug::Info) << message;
                },
                // One record per run-time compile, to match a stutter to its cause: in the log (Info with the load
                // trace on, else Verbose) and, with the load trace on, in load-trace.jsonl as "slang.compile".
                [](const vsgUtil::SlangCompileEvent& event) {
                    const bool trace = Misc::LoadTrace::enabled();
                    Log(trace ? Debug::Info : Debug::Verbose) << vsgUtil::formatSlangCompileEvent(event);
                    if (trace && event.mKind != vsgUtil::SlangEventKind::CacheHit)
                        Misc::LoadTrace::record("slang.compile", event.mBeginMicroseconds, event.mEndMicroseconds,
                            static_cast<std::int64_t>(event.mThread));
                });
        }
        return options;
    }
}

namespace
{
    vsg::ref_ptr<const vsg::Options> createImageOptions(const VFS::Manager* vfs, bool decompress)
    {
        auto options = vsg::Options::create();
        auto images = vsgXchange::images::create();
        vsg::ReaderWriters children;
        if (decompress)
        {
            auto wrapper = vsg::ref_ptr{ new Resource::Decompress() };
            wrapper->readerWriters = { images->readerWriters };
            children = { wrapper };
        }
        else
            children = images->readerWriters;

        options->readerWriters = { vsg::ref_ptr{ new vsgAdapters::vfs(*vfs, children) } };
        options->sharedObjects = vsg::SharedObjects::create();
        // openeden-vsg: VSG 1.1.x clones "dynamic" objects of shared reads by default, which turns vsgopenmw's
        // Anim::Transform roots into plain groups (losing Bip01); vsgopenmw clones what it needs itself
        // (mwanimation/clone.hpp).
        options->findDynamicObjects = {};
        options->propagateDynamicObjects = {};
        return options;
    }

    vsg::ref_ptr<const vsg::Options> createTextureOptions(const VFS::Manager* vfs, const vsg::Options& imageOptions)
    {
        auto textureOptions = vsg::Options::create(imageOptions);
        textureOptions->paths = { "textures" };
        textureOptions->sharedObjects = vsg::SharedObjects::create();
        // openeden-vsg: VSG 1.1.x clones "dynamic" objects of shared reads by default, which turns vsgopenmw's
        // Anim::Transform roots into plain groups (losing Bip01); vsgopenmw clones what it needs itself
        // (mwanimation/clone.hpp).
        textureOptions->findDynamicObjects = {};
        textureOptions->propagateDynamicObjects = {};
        textureOptions->findFileCallback = [vfs](const vsg::Path& file, const vsg::Options* options) -> vsg::Path {
            for (auto& dir : options->paths)
            {
                std::string fileStr = file.string();
                VFS::Path::Normalized normalizedFile(fileStr);
                VFS::Path::Normalized normalizedDir(dir.string());
                VFS::Path::NormalizedView topDirs[] = { normalizedDir };
                // openeden-vsg: prefer the .dds of a texture named .tga, as OpenMW's correctTexturePath does (0.51 made
                // the extension an argument; without it, models naming a .tga that only exists as .dds got the
                // fallback).
                auto corrected = Misc::ResourceHelpers::correctResourcePath(
                    topDirs, normalizedFile, *vfs, VFS::Path::ExtensionView("dds"));
                if (!corrected.empty() && corrected.value() != fileStr)
                    return vsg::Path(corrected.value());
            }
            return file;
        };
        auto fallback = vsg::ref_ptr{ new Resource::Fallback };
        fallback->fallbackObject = vsg::ubvec4Array2D::create(
            1, 1, vsg::ubvec4(0xff, 0x00, 0xff, 0xff), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        textureOptions->readerWriters.push_back(fallback);
        return textureOptions;
    }

    vsg::ref_ptr<const vsg::Options> createNodeOptions(const VFS::Manager* vfs, const Pipeline::Builder& builder,
        vsg::ref_ptr<const vsg::Options> textureOptions, int computeBin, int depthSortedBin,
        vsg::ref_ptr<vsgAdapters::nif>& nifReader)
    {
        auto nodeOptions = vsg::Options::create();
        nodeOptions->sharedObjects = vsg::SharedObjects::create();
        // openeden-vsg: VSG 1.1.x clones "dynamic" objects of shared reads by default, which turns vsgopenmw's
        // Anim::Transform roots into plain groups (losing Bip01); vsgopenmw clones what it needs itself
        // (mwanimation/clone.hpp).
        nodeOptions->findDynamicObjects = {};
        nodeOptions->propagateDynamicObjects = {};
        nodeOptions->findFileCallback = [](const vsg::Path& file, const vsg::Options* options) -> vsg::Path {
            return vsg::Path(inMeshesDirectory(file));
        };

        auto fallback = vsg::ref_ptr{ new Resource::Fallback };
        static const char* const sMeshTypes[] = { "nif", "dae" };
        for (unsigned int i = 0; i < sizeof(sMeshTypes) / sizeof(sMeshTypes[0]); ++i)
            fallback->fallbackFiles.push_back("marker_error." + std::string(sMeshTypes[i]));
        fallback->fallbackObject = vsg::Node::create();

        auto vfsReader = vsg::ref_ptr{ new vsgAdapters::vfs(*vfs) };
        nifReader = vsg::ref_ptr{ new vsgAdapters::nif(builder, textureOptions, computeBin, depthSortedBin) };
        // openeden-vsg: normal and specular maps next to diffuse textures, off by default (task 0012, V4h).
        const auto& shaders = Settings::shaders();
        nifReader->autoMaps.autoUseNormalMaps = shaders.mAutoUseObjectNormalMaps;
        nifReader->autoMaps.autoUseSpecularMaps = shaders.mAutoUseObjectSpecularMaps;
        nifReader->autoMaps.normalMapPattern = shaders.mNormalMapPattern;
        nifReader->autoMaps.normalHeightMapPattern = shaders.mNormalHeightMapPattern;
        nifReader->autoMaps.specularMapPattern = shaders.mSpecularMapPattern;
        nifReader->softParticles = shaders.mSoftParticles;
        if (shaders.mWeatherParticleOcclusion)
            nifReader->occludedParticleModel = Settings::models().mWeathersnow.get().value();
        nifReader->textureExists = [vfs](std::string_view path) { return vfs->exists(VFS::Path::Normalized(path)); };
        vfsReader->add(nifReader);
        // vfsReader->add(vsgXchange::models::create());
        // openeden-vsg: COLLADA models, which OpenMW 0.51 read through OpenSceneGraph (task 0010).
        vfsReader->add(vsg::ref_ptr{ new vsgAdapters::dae(builder, textureOptions, depthSortedBin) });

        nodeOptions->readerWriters = { vfsReader, fallback };
        return nodeOptions;
    }

    vsg::ref_ptr<const vsg::Options> createAnimationOptions(const VFS::Manager* vfs)
    {
        auto vfsReader = vsg::ref_ptr{ new vsgAdapters::vfs(
            *vfs, { vsg::ref_ptr{ new vsgAdapters::kf }, vsg::ref_ptr{ new vsgAdapters::daeClips(*vfs) } }) };
        auto options = vsg::Options::create();
        options->findFileCallback = [vfs](const vsg::Path& in_file, const vsg::Options* /*options*/) -> vsg::Path {
            VFS::Path::Normalized fileNorm(inMeshesDirectory(in_file));
            auto corrected = Misc::ResourceHelpers::correctActorModelPath(fileNorm, vfs);
            std::string file = corrected.value();
            // openeden-vsg: a COLLADA model holds its own animations; OpenMW 0.51's Animation::addAnimSource read it
            // as is rather than a separate .kf.
            if (vsg::lowerCaseFileExtension(vsg::Path(file)) == ".dae")
                return vsg::Path(file);
            std::string basename = file;
            size_t slashpos = file.find_last_of("\\/");
            if (slashpos != std::string::npos && slashpos + 1 < file.size())
                basename = file.substr(slashpos + 1);
            if (basename.empty() || (basename[0] != 'x' && basename[0] != 'X'))
                return {};

            auto ext = vsg::lowerCaseFileExtension(vsg::Path(file));
            if (ext == ".nif")
                file.replace(file.size() - 4, 4, ".kf");
            return vsg::Path(file);
        };
        options->readerWriters = { vfsReader };
        options->sharedObjects = vsg::SharedObjects::create();
        // openeden-vsg: VSG 1.1.x clones "dynamic" objects of shared reads by default, which turns vsgopenmw's
        // Anim::Transform roots into plain groups (losing Bip01); vsgopenmw clones what it needs itself
        // (mwanimation/clone.hpp).
        options->findDynamicObjects = {};
        options->propagateDynamicObjects = {};
        return options;
    }
}

namespace Resource
{
    ResourceSystem::ResourceSystem(const VFS::Manager* vfs, const std::string& shaderPath,
        vsg::ref_ptr<vsg::Sampler> defaultSampler, bool supportsCompressedImages, int computeBin, int depthSortedBin,
        const std::string& shaderCacheDirectory)
        : mVFS(vfs)
        , shaderOptions(createShaderOptions(shaderPath, shaderCacheDirectory))
        , builder(new Pipeline::Builder(shaderOptions, defaultSampler))
        , imageOptions(createImageOptions(vfs, !supportsCompressedImages))
        , textureOptions(createTextureOptions(vfs, *imageOptions))
        , nodeOptions(createNodeOptions(vfs, *builder, textureOptions, computeBin, depthSortedBin, nifReader))
        , animationOptions(createAnimationOptions(vfs))
    {
        // openeden-vsg: alpha to coverage, as OpenMW's shouldAddMSAAIntermediateTarget and
        // setAdjustCoverageForAlphaTest (task 0012, V4j); off by default. Pipelines are created only after this.
        builder->graphics->alphaToCoverage
            = Settings::shaders().mAntialiasAlphaTest && Settings::video().mAntialiasing > 1;
        builder->graphics->adjustCoverage = Settings::shaders().mAdjustCoverageForAlphaTest;
        // openeden (task 0023, F1b): the shaders written in Slang, off by default. Task 0072, review of the stage f and
        // g rounds, finding 7: Pipeline::Graphics takes the switch from `shaderOptions` when it is made, before any
        // stage, not from an assignment here after the Builder (createShaderOptions sets it from the same setting).
    }

    void ResourceSystem::setOccludedParticleMap(vsg::ref_ptr<vsg::Descriptor> map)
    {
        nifReader->occludedParticleMap = std::move(map);
    }

    ResourceSystem::~ResourceSystem() {}

    void ResourceSystem::updateCache(double referenceTime)
    {
        std::vector<vsg::ref_ptr<const vsg::Options>> options
            = { imageOptions, textureOptions, nodeOptions, animationOptions };
        for (auto& o : options)
            if (auto& sharedObjects = o->sharedObjects)
                sharedObjects->prune();
    }

    void ResourceSystem::clearCache() {}

    const VFS::Manager* ResourceSystem::getVFS() const
    {
        return mVFS;
    }

    void ResourceSystem::reportStats(unsigned int frameNumber, Misc::Stats* stats) const {}
}
