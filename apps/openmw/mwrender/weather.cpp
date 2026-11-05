#include "weather.hpp"

#include <vsg/commands/BeginQuery.h>
#include <vsg/commands/Draw.h>
#include <vsg/commands/EndQuery.h>
#include <vsg/commands/ResetQueryPool.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/StateSwitch.h>
#include <vsg/utils/SharedObjects.h>

#include <algorithm>

#include <components/animation/clone.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/atmospheremath.hpp>
#include <components/render/cirrusmath.hpp>
#include <components/render/cloudmath.hpp>
#include <components/render/cloudvolumegpu.hpp>
#include <components/render/cloudvolumemath.hpp>
#include <components/render/sunmath.hpp>
#include <components/resource/fallback.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/readnode.hpp>
#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/searchbytype.hpp>
#include <components/vsgutil/setbin.hpp>
#include <components/vsgutil/share.hpp>
#include <components/vsgutil/viewrelative.hpp>

#include "../mwworld/weather.hpp"

#include "bin.hpp"
#include "weatherdata.hpp"

namespace
{
    // OpenEden (task 0038): how long the clouds take to take on the lit look of a new weather, in seconds of game time.
    // A short fade, by eye; no source.
    constexpr float cloudShareSeconds = 3.f;

    void depthOff(Pipeline::Options& o)
    {
        o.depthTest = false;
        o.depthWrite = false;
    }

    // OpenEden (task 0038): the coarse thickness grid of a cloud picture, for the clouds' shadows. Only the 8 bit four
    // channel pictures Morrowind's skies use are read (an RGBA or BGRA .dds or .tga); anything else gives no shadow.
    std::vector<float> buildCloudGrid(const std::string& file, const vsg::Data& data)
    {
        std::vector<float> grid(Render::Cloud::gridSize * Render::Cloud::gridSize, 0.f);
        bool bgra = false;
        switch (data.properties.format)
        {
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
                break;
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
                bgra = true;
                break;
            default:
                Log(Debug::Warning) << "Cloud shadows: the cloud picture " << file << " has format "
                                    << static_cast<int>(data.properties.format)
                                    << ", which is not read; it casts no shadow";
                return grid;
        }
        Render::Cloud::buildGrid(
            static_cast<const unsigned char*>(data.dataPointer()), data.width(), data.height(), bgra, grid.data());
        return grid;
    }

    template <class T>
    void share(vsg::ref_ptr<T>& obj, vsg::ref_ptr<const vsg::Options> options)
    {
        vsgUtil::share_if(options->sharedObjects, obj);
    }

    static const auto sDrawQuad = vsg::Draw::create(4, 1, 0, 0);
}

namespace MWRender
{
    struct Atmosphere
    {
        static void pipelineOptions(Pipeline::Options& o)
        {
            o.shader.path = "sky/atmosphere";
            depthOff(o);
        }
    };

    class AtmosphereNight : public Anim::Transform // vsgUtil::Composite<Anim::Transform> // ToggleComposite
    {
    public:
        Pipeline::Material material;
        static void pipelineOptions(Pipeline::Options& o)
        {
            o.shader.path = "sky/night";
            depthOff(o);
            o.blend = true;
        }
    };

    class Clouds : public Anim::Transform
    {
        vsg::ref_ptr<vsg::StateSwitch> mStateSwitch;
        vsg::ref_ptr<const vsg::BindDescriptorSet> mReplaceBds;
        std::map<vsg::ref_ptr<vsg::Descriptor>, vsg::ref_ptr<vsg::BindDescriptorSet>> mTextureMap;

    public:
        Pipeline::Material material;
        Pipeline::DynamicDescriptorValue<vsg::mat4> texmat{ Pipeline::Descriptors::TEXMAT_BINDING };
        Clouds(vsg::ref_ptr<vsg::Node> node)
        {
            mStateSwitch = vsg::StateSwitch::create();

            auto replaceBds = [this](const vsg::Node& n) -> vsg::ref_ptr<vsg::Node> {
                if (auto* bds = dynamic_cast<const vsg::BindDescriptorSet*>(&n))
                {
                    if (bds->pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
                        return {};
                    mReplaceBds = bds;
                    mStateSwitch->slot = bds->slot;
                    return mStateSwitch;
                }
                return {};
            };
            Anim::cloneIfRequired(node, replaceBds);
            addChild(node);
        }
        static void pipelineOptions(Pipeline::Options& o)
        {
            depthOff(o);
            o.blend = true;
            o.shader.path = "sky/clouds";
            o.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
            o.shader.addMode(Pipeline::Mode::TEXMAT);
            o.shader.addMode(Pipeline::Mode::MATERIAL);
        }
        bool textureValid() const { return !mStateSwitch->children.empty(); }
        void setTexture(vsg::ref_ptr<vsg::Descriptor> texture, vsgUtil::CompileContext& compile)
        {
            if (!mReplaceBds)
                return;
            auto found = mTextureMap.find(texture);
            if (found == mTextureMap.end())
            {
                auto bds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, mReplaceBds->layout,
                    mReplaceBds->firstSet, vsg::Descriptors{ texture, material.descriptor(), texmat.descriptor() });
                if (compile.compile(bds))
                    found = mTextureMap.emplace(texture, bds).first;
            }
            if (found != mTextureMap.end())
                mStateSwitch->children = { { vsg::MASK_ALL, found->second } };
        }
    };

    // OpenEden (task 0079): the cirrus layer, a dome drawn by the Slang-only pair `sky/cirrus`. The dome has no vertex
    // buffers: the shader builds a triangle list from the vertex index (Render::Cirrus::domeVertex), so this node is a
    // transform (the sky shaders take their modelview from one), one pipeline and one draw. The material holds
    // everything the shader needs; its layout is documented at the top of files/shaders/sky/cirrus/cirrus.slang. Owner:
    // Sky, through its switch (a vsg::ref_ptr); the material descriptor is a member by value, as the other layers'.
    class Cirrus : public Anim::Transform
    {
    public:
        struct Settings
        {
            float coverageScale = 1.f;
            float opacityScale = 1.f;
            float altitudeMeters = Render::Cirrus::defaultAltitudeKm * 1000.f;
            float windScale = 6.f;
            Render::Cirrus::Quality quality = Render::Cirrus::Quality::Medium;
            Render::Cirrus::DebugView debugView = Render::Cirrus::DebugView::None;
        };
        // What changes while the game runs.
        struct Frame
        {
            float share = 0.f;
            float driftMeters = 0.f;
            float morph = 0.f;
            float sunElevation = 0.f;
            vsg::vec3 tint{ 1.f, 1.f, 1.f };
        };

        Pipeline::Material material;
        Settings settings;

        Cirrus(const Pipeline::Builder& builder, const Settings& settingsIn)
            : settings(settingsIn)
        {
            auto options = Pipeline::Options{ .shader = { "sky/cirrus", Pipeline::modes({ Pipeline::Mode::MATERIAL }) },
                .blend = true,
                .cullMode = VK_CULL_MODE_NONE };
            depthOff(options);
            auto pipeline = builder.graphics->getOrCreate(options);
            auto bds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout,
                Pipeline::TEXTURE_SET, vsg::Descriptors{ material.descriptor() });
            auto sg = vsg::StateGroup::create();
            sg->stateCommands = { pipeline, bds };
            sg->children = { vsg::Draw::create(Render::Cirrus::domeVertexCount, 1, 0, 0) };
            // The vanilla cloud layers are blended shapes of a model, which the renderer draws in the depth sorted bin
            // after everything drawn in order, so the cirrus is always under them (a debug view hides them instead,
            // see Sky::setWeather).
            addChild(sg);

            // The values that never change: the layer's wind direction and the pattern's cover.
            const vsg::vec2 wind = Render::Cirrus::veeredWindDirection();
            material.update(material.value().pbr,
                vsg::vec4(wind.x, wind.y, Render::Cirrus::patternCoverage(settings.coverageScale), 0.f));
            setFrame(Frame{});
        }

        void setFrame(const Frame& frame)
        {
            auto& data = material.value();
            material.update(data.ambient,
                vsg::vec4(frame.share, settings.coverageScale, settings.opacityScale, settings.altitudeMeters));
            material.update(data.diffuse, vsg::vec4(frame.tint.x, frame.tint.y, frame.tint.z, 1.f));
            material.update(data.specular,
                vsg::vec4(frame.driftMeters, frame.morph, frame.sunElevation, static_cast<float>(settings.quality)));
            const vsg::vec3 sunTint = Render::Atmosphere::sunGlowTint(frame.sunElevation);
            material.update(
                data.emissive, vsg::vec4(sunTint.x, sunTint.y, sunTint.z, static_cast<float>(settings.debugView)));
        }
    };

    class CelestialBody : public Anim::Transform
    {
    public:
        Pipeline::Material material;
        CelestialBody(float scaleFactor = 1.f) { scale = vsg::vec3(450, 450, 450) * scaleFactor; }
        Pipeline::Options pipelineOptions(const std::string& shader)
        {
            auto options = Pipeline::Options{ .shader
                = { shader, Pipeline::modes({ Pipeline::Mode::MATERIAL, Pipeline::Mode::DIFFUSE_MAP }) },
                .primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                .blend = true,
                .cullMode = VK_CULL_MODE_NONE };
            depthOff(options);
            return options;
        }

        void setDirection(vsg::vec3 dir)
        {
            dir = vsg::normalize(dir);
            const float distance = 1000.0f;
            translation = dir * distance;
            setAttitude(vsg::quat(vsg::vec3(0.0f, 0.0f, 1.0f), dir));
        }
    };

    class SunGlare : public vsgUtil::Composite<vsg::StateGroup>
    {
        float mFaderMax;

    public:
        Pipeline::Material material;
        SunGlare(const Pipeline::Builder& builder)
        {
            mNode = vsg::StateGroup::create();
            auto color = Fallback::Map::getColour("Weather_Sun_Glare_Fader_Color");
            // Replicating a design flaw in MW. The color was being set on both ambient and emissive properties, which
            // multiplies the result by two, then finally gets clamped by the fixed function pipeline. With the default
            // INI settings, only the red component gets clamped, so the resulting color looks more orange than red.
            color *= 2;
            for (int i = 0; i < 3; ++i)
                color[i] = std::min(1.f, color[i]);
            mFaderMax = Fallback::Map::getFloat("Weather_Sun_Glare_Fader_Max");
            auto faderAngleMax = Fallback::Map::getFloat("Weather_Sun_Glare_Fader_Angle_Max");
            /*material.faderAngleMax*/ color.a = vsg::radians(faderAngleMax);
            material.value().emissive = color;

            auto pipelineOptions
                = Pipeline::Options{ .shader = { "sky/sunglare", Pipeline::modes({ Pipeline::Mode::MATERIAL }) },
                      .blend = true,
                      .cullMode = VK_CULL_MODE_NONE };
            depthOff(pipelineOptions);

            auto pipeline = builder.graphics->getOrCreate(pipelineOptions);
            auto bds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout,
                Pipeline::TEXTURE_SET, vsg::Descriptors{ material.descriptor() });
            mNode->stateCommands = { pipeline, bds };
            mNode->children = { sDrawQuad };
        }
        void update(float alpha) { material.setAlpha(mFaderMax * alpha); }
    };

    // Task 0047: what the Sun needs to draw itself as a computed disc ([Rendering] procedural sun and its three
    // numbers). `enabled` is only true when the setting is on, hdr is on and the Slang shader was built.
    struct ProceduralSunParams
    {
        bool enabled = false;
        float size = 1.f;
        float intensity = Render::Sun::defaultIntensity;
        float glow = 1.f;
    };

    class Sun : public CelestialBody
    {
        Pipeline::Material sunFlashMaterial;
        vsg::ref_ptr<vsg::QueryPool> mQueryPool;
        const uint32_t mQueryCount = 2;
        vsg::ref_ptr<Anim::Transform> mFlash;
        vsg::ref_ptr<vsg::Switch> mSwitch;
        std::unique_ptr<SunGlare> mGlare;
        vsg::ref_ptr<vsg::Node> mResetQueryPool;
        bool mPendingQuery = false;

    public:
        float glareView = 1.f;
        float glareTimeOfDayFade = 1.f;
        float visibleRatio = 0.f;
        bool enabled = true;
        Sun(vsg::ref_ptr<const vsg::Options> textureOptions, const Pipeline::Builder& builder,
            bool occlusionQueryPrecise, const ProceduralSunParams& procedural)
        {
            if (procedural.enabled)
            {
                // The disc's numbers ride in the sun's own material (see proceduralsun.slang): the intensity, the glow
                // scale and the disc radius. The colour and alpha are set every frame, as for the textured sun.
                material.value().pbr = vsg::vec4(Render::Sun::clampIntensity(procedural.intensity),
                    std::max(0.f, procedural.glow), Render::Sun::discRadius(procedural.size), 0.f);
            }
            VkQueryControlFlags queryControlFlags{};
            if (occlusionQueryPrecise)
                queryControlFlags |= VK_QUERY_CONTROL_PRECISE_BIT;
            {
                mQueryPool = vsg::QueryPool::create();
                mQueryPool->queryType = VK_QUERY_TYPE_OCCLUSION;
                mQueryPool->queryCount = mQueryCount;
                addChild(vsg::BeginQuery::create(mQueryPool, 0, queryControlFlags));
                mResetQueryPool = vsg::ResetQueryPool::create(mQueryPool);
            }

            auto sampler = builder.createSampler();
            sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            share(sampler, textureOptions);

            auto sunTex = vsg::DescriptorImage::create(sampler, vsgUtil::readImage("tx_sun_05.dds", textureOptions));

            auto pipeline = builder.graphics->getOrCreate(pipelineOptions("sky/sun"));
            auto sunBds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout,
                Pipeline::TEXTURE_SET, vsg::Descriptors{ sunTex, material.descriptor() });
            {
                // The visible disc is the textured sun's pipeline or, with the setting, the computed one on the same
                // quad and with the same descriptor sets. The occlusion query and the flash below keep the textured
                // pipeline, so that what they count and draw is unchanged.
                auto drawPipeline
                    = procedural.enabled ? builder.graphics->getOrCreate(pipelineOptions("sky/sunproc")) : pipeline;
                auto sg = vsg::StateGroup::create();
                sg->stateCommands = { drawPipeline, sunBds };
                sg->children = { sDrawQuad };
                addChild(sg);
            }
            addChild(vsg::EndQuery::create(mQueryPool, 0));

            auto binGroup = vsg::Group::create();
            binGroup->addChild(vsg::BeginQuery::create(mQueryPool, 1, queryControlFlags));
            {
                auto queryPipeline = builder.graphics->getOrCreate(queryOptions());
                auto sg = vsg::StateGroup::create();
                sg->stateCommands = { queryPipeline, sunBds };
                sg->children = { sDrawQuad };
                binGroup->addChild(sg);
            }
            binGroup->addChild(vsg::EndQuery::create(mQueryPool, 1));

            mSwitch = vsg::Switch::create();
            {
                auto sg = vsg::StateGroup::create();
                auto flashTex = vsgUtil::readImage("tx_sun_flash_grey_05.dds", textureOptions);
                auto flashSampler = builder.createSampler();
                flashSampler->addressModeU = flashSampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                share(flashSampler, textureOptions);
                auto descriptorImage = vsg::DescriptorImage::create(flashSampler, flashTex);

                sunFlashMaterial.value().emissive = vsg::vec4(1, 1, 1, 1);
                auto bds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout,
                    Pipeline::TEXTURE_SET, vsg::Descriptors{ descriptorImage, sunFlashMaterial.descriptor() });
                sg->stateCommands = { pipeline, bds };
                mFlash = vsg::ref_ptr{ new Anim::Transform };
                mFlash->children = { sDrawQuad };
                sg->children = { mFlash };
                mSwitch->addChild(false, sg);
            }
            mGlare = std::make_unique<SunGlare>(builder);
            mSwitch->addChild(false, mGlare->node());
            binGroup->addChild(mSwitch);

            addChild(vsgUtil::createBinSetter(Bin_SunGlare, binGroup));
        }
        Pipeline::Options queryOptions()
        {
            auto options = pipelineOptions("sky/sun");
            options.colorWrite = false;
            options.depthTest = true;
            return options;
        }
        vsg::ref_ptr<vsg::Node> resetNode() { return mResetQueryPool; }
        void setColor(const vsg::vec4& color) { material.update(material.value().emissive, color); }
        void updateQuery()
        {
            std::vector<uint64_t> results(mQueryCount);
            if (mQueryPool->getResults(results, 0, 0) != VK_SUCCESS)
                return;
            auto total = results[0];
            auto visible = results[1];
            visibleRatio = total > 0 ? static_cast<float>(visible) / total : 0.f;
        }
        void updateFlash()
        {
            if (visibleRatio > 0.f)
            {
                float flashScale = 2.6;
                const float fadeThreshold = 0.1;
                if (visibleRatio < fadeThreshold)
                {
                    float fade = 1.f - (fadeThreshold - visibleRatio) / fadeThreshold;
                    sunFlashMaterial.setAlpha(fade);
                }
                else if (visibleRatio < 1.f)
                {
                    sunFlashMaterial.setAlpha(1);
                    const float threshold = 0.6;
                    flashScale *= visibleRatio * (1.f - threshold) + threshold;
                }
                mFlash->setScale(flashScale);
            }
        }
        void update()
        {
            if (!enabled)
            {
                mPendingQuery = false;
                return;
            }
            if (mPendingQuery)
                updateQuery();
            mPendingQuery = true;

            auto glare = glareView * visibleRatio;
            mSwitch->setAllChildren(glare > 0.f);

            mGlare->update(glare * glareTimeOfDayFade);
            updateFlash();
        }
    };

    class Moon : public CelestialBody
    {
    public:
        enum class Type
        {
            Masser = 0,
            Secunda
        };

        Moon(vsg::ref_ptr<const vsg::Options> options, float scaleFactor, Type type, const Pipeline::Builder& builder)
            : CelestialBody(scaleFactor)
            , mType(type)
        {
            auto circleImage = vsgUtil::readImage(
                std::string("tx_mooncircle_full_") + (type == Type::Masser ? "m" : "s") + ".dds", options);
            auto sampler = builder.createSampler();
            sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            share(sampler, options);

            mCircleTex = vsg::DescriptorImage::create(sampler, circleImage);

            auto po = pipelineOptions("sky/moon");
            po.shader.addMode(Pipeline::Mode::GLOW_MAP);
            auto sg = vsg::StateGroup::create();
            mPipeline = builder.graphics->getOrCreate(po);
            mStateSwitch = vsg::StateSwitch::create();
            for (uint32_t i = 0; i < static_cast<uint32_t>(MoonState::Phase::Count); ++i)
            {
                auto bds = createBindDescriptorSet(options, static_cast<MoonState::Phase>(i), builder);
                mStateSwitch->add(vsg::boolToMask(i == 0), bds);
                mStateSwitch->slot = bds->slot;
            }
            sg->stateCommands = { mPipeline, mStateSwitch };
            sg->children = { sDrawQuad };
            children = { sg };
        }
        std::string phaseName(MoonState::Phase phase)
        {
            switch (phase)
            {
                case MoonState::Phase::New:
                    return "new";
                case MoonState::Phase::WaxingCrescent:
                    return "one_wax";
                case MoonState::Phase::FirstQuarter:
                    return "half_wax";
                case MoonState::Phase::WaxingGibbous:
                    return "three_wax";
                case MoonState::Phase::WaningCrescent:
                    return "one_wan";
                case MoonState::Phase::ThirdQuarter:
                    return "half_wan";
                case MoonState::Phase::WaningGibbous:
                    return "three_wan";
                case MoonState::Phase::Full:
                    return "full";
                default:
                    return {};
            }
        }
        vsg::ref_ptr<vsg::BindDescriptorSet> createBindDescriptorSet(
            vsg::ref_ptr<const vsg::Options> options, MoonState::Phase phase, const Pipeline::Builder& builder)
        {
            std::string textureName = "tx_";
            if (mType == Moon::Type::Secunda)
                textureName += "secunda_";
            else
                textureName += "masser_";
            textureName += phaseName(phase) + ".dds";

            auto sampler = builder.createSampler();
            sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            share(sampler, options);
            auto descriptorImage = vsg::DescriptorImage::create(
                sampler, vsgUtil::readImage(textureName, options), Pipeline::Descriptors::GLOW_UNIT);

            auto descriptors = vsg::Descriptors{ descriptorImage, mCircleTex, material.descriptor() };
            return vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline->pipeline->layout, Pipeline::TEXTURE_SET, descriptors);
        }
        void setColor(const vsg::vec4& color)
        {
            material.value().emissive = vsg::vec4(color.x, color.y, color.z, material.value().emissive.a);
        }
        void setState(const MoonState state)
        {
            float radsX = ((state.mRotationFromHorizon) * static_cast<float>(Misc::VsgMath::PI)) / 180.0f;
            float radsZ = ((state.mRotationFromNorth) * static_cast<float>(Misc::VsgMath::PI)) / 180.0f;

            vsg::quat rotX(radsX, vsg::vec3(1.0f, 0.0f, 0.0f));
            vsg::quat rotZ(radsZ, vsg::vec3(0.0f, 0.0f, 1.0f));

            auto direction = rotX * rotZ * vsg::vec3(0.0f, 1.0f, 0.0f);
            setDirection(direction);

            // The moon quad is initially oriented facing down, so we need to offset its X-axis
            // rotation to rotate it to face the camera when sitting at the horizon.
            vsg::quat attX((-static_cast<float>(vsg::PI) / 2.0f) + radsX, vsg::vec3(1.0f, 0.0f, 0.0f));
            setAttitude(attX * rotZ);

            setPhase(state.mPhase);

            material.update(material.value().emissive.a, state.mShadowBlend);
            material.setAlpha(state.mMoonAlpha);
        }
        unsigned int getPhaseInt() const
        {
            switch (mPhase)
            {
                case MoonState::Phase::New:
                    return 0;
                case MoonState::Phase::WaxingCrescent:
                    return 1;
                case MoonState::Phase::WaningCrescent:
                    return 1;
                case MoonState::Phase::FirstQuarter:
                    return 2;
                case MoonState::Phase::ThirdQuarter:
                    return 2;
                case MoonState::Phase::WaxingGibbous:
                    return 3;
                case MoonState::Phase::WaningGibbous:
                    return 3;
                case MoonState::Phase::Full:
                    return 4;
                default:
                    return 0;
            }
        }
        void setPhase(const MoonState::Phase& phase)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(MoonState::Phase::Count); ++i)
                mStateSwitch->children[i].mask = vsg::boolToMask(static_cast<MoonState::Phase>(i) == phase);
        }

    private:
        vsg::ref_ptr<vsg::DescriptorImage> mCircleTex;
        vsg::ref_ptr<vsg::StateSwitch> mStateSwitch;
        vsg::ref_ptr<vsg::BindGraphicsPipeline> mPipeline;
        Type mType;
        MoonState::Phase mPhase;
    };

    Sky::Sky(Resource::ResourceSystem& resourceSystem, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        bool occlusionQueryPrecise)
        : mCompile(compile)
        , mTextureOptions(resourceSystem.textureOptions)
        , mStormParticleDirection(MWWorld::Weather::defaultDirection())
        , mStormDirection(MWWorld::Weather::defaultDirection())
        , mRainRipplesEnabled(Fallback::Map::getBool("Weather_Rain_Ripples"))
        , mSnowRipplesEnabled(Fallback::Map::getBool("Weather_Snow_Ripples"))
    {
        mNode = new vsgUtil::ViewRelative;
        // OpenEden (task 0038): the lit look of the clouds (the shader's own switch is `cCloudsLit` in cloud.settings).
        mCloudsLit = Render::Cloud::useClouds(Settings::rendering().mClouds, Settings::rendering().mHdr)
            && Render::Cloud::parseLook(Settings::rendering().mCloudsLook.get()) == Render::Cloud::Look::Lit;
        mCloudShadowsOn = Render::Cloud::useShadows(
            Settings::rendering().mCloudShadows, Settings::rendering().mHdr, Settings::rendering().mAtmosphere);
        mVolumeCloudsOn = Render::CloudVolume::useVolumetricClouds(
            Settings::rendering().mVolumetricClouds, Settings::rendering().mHdr, Settings::rendering().mAtmosphere);
        auto nodeOptions = vsg::Options::create(*resourceSystem.nodeOptions);
        // openeden-vsg: an unreadable sky model (the example suite's .osgt clouds and stars: an OpenSceneGraph format
        // the VSG build has no reader for) must stay empty. The world's fallback would draw the error marker, which can
        // be read now that .dae models load, with the sky's shaders around the camera, covering the view.
        for (auto& readerWriter : nodeOptions->readerWriters)
        {
            if (auto fallback = dynamic_cast<const Resource::Fallback*>(readerWriter.get()))
            {
                auto skyFallback = vsg::ref_ptr{ new Resource::Fallback(*fallback) };
                skyFallback->fallbackFiles.clear();
                readerWriter = skyFallback;
            }
        }
        auto override_ = vsg::ref_ptr{ new Pipeline::Override };
        override_->attachTo(*nodeOptions);
        mAtmosphere = std::make_unique<Atmosphere>();
        {
            override_->pipelineOptions = Atmosphere::pipelineOptions;
            mNode->addChild(vsgUtil::readNode(Settings::Manager::getString("skyatmosphere", "Models"), nodeOptions));
        }
        override_->material = {};
        mSwitch = vsg::Switch::create();
        mNode->addChild(mSwitch);
        {
            override_->pipelineOptions = AtmosphereNight::pipelineOptions;
            auto t = vsg::ref_ptr{ new AtmosphereNight };
            override_->material = t->material.descriptor();
            vsg::ref_ptr<vsg::Node> node;
            auto night02 = Settings::Manager::getString("skynight02", "Models");
            if (resourceSystem.getVFS()->exists("meshes/" + night02))
                node = vsgUtil::readNode(night02, nodeOptions);
            else
                node = vsgUtil::readNode(Settings::Manager::getString("skynight01", "Models"), nodeOptions);
            t->children = { node };
            mAtmosphereNight.setup(t, mSwitch, false);
            override_->material = {};
        }
        ProceduralSunParams proceduralSun;
        {
            const auto& rendering = Settings::rendering();
            if (Render::Sun::useProceduralSun(rendering.mProceduralSun, rendering.mHdr))
            {
                // The shader has no GLSL pair, so it exists only if slangc was there at build time.
                if (resourceSystem.builder->graphics->hasSlangShader("sky/sunproc"))
                {
                    auto& slangOnly = resourceSystem.builder->graphics->slangOnlyPaths;
                    if (std::find(slangOnly.begin(), slangOnly.end(), "sky/sunproc") == slangOnly.end())
                        slangOnly.push_back("sky/sunproc");
                    proceduralSun = { true, rendering.mProceduralSunSize, rendering.mProceduralSunIntensity,
                        rendering.mProceduralSunGlow };
                    Log(Debug::Info) << "Procedural sun: a computed disc in place of tx_sun_05.dds";
                }
                else
                    Log(Debug::Warning) << "[Rendering] procedural sun: shaders/sky/sunproc/main.*.spv were not built "
                                           "(this build has no slangc); the textured sun is drawn";
            }
            else if (rendering.mProceduralSun)
                Log(Debug::Warning) << "[Rendering] procedural sun needs hdr; the textured sun is drawn";
        }
        mSun.setup(vsg::ref_ptr{ new Sun(
                       resourceSystem.textureOptions, *resourceSystem.builder, occlusionQueryPrecise, proceduralSun) },
            mSwitch, true);

        mMasser.setup(
            vsg::ref_ptr{ new Moon(resourceSystem.textureOptions, Fallback::Map::getFloat("Moons_Masser_Size") / 125,
                Moon::Type::Masser, *resourceSystem.builder) },
            mSwitch, true);
        mSecunda.setup(
            vsg::ref_ptr{ new Moon(resourceSystem.textureOptions, Fallback::Map::getFloat("Moons_Secunda_Size") / 125,
                Moon::Type::Secunda, *resourceSystem.builder) },
            mSwitch, true);
        mMoonScriptColor = Fallback::Map::getColour("Moons_Script_Color");

        // OpenEden (task 0079): the cirrus layer, after the moons and before the low cloud layers. Nothing is made
        // unless the setting took effect, so with it off the sky is what it was.
        {
            const auto& rendering = Settings::rendering();
            if (rendering.mCirrus)
            {
                if (!Render::Cirrus::useCirrus(rendering.mCirrus, rendering.mHdr, rendering.mAtmosphere))
                    Log(Debug::Warning) << "[Rendering] cirrus needs hdr and atmosphere; no cirrus is drawn";
                else if (!resourceSystem.builder->graphics->hasSlangShader("sky/cirrus"))
                    Log(Debug::Warning) << "[Rendering] cirrus: shaders/sky/cirrus/main.*.spv were not built (this "
                                           "build has no slangc); no cirrus is drawn";
                else
                {
                    auto& slangOnly = resourceSystem.builder->graphics->slangOnlyPaths;
                    if (std::find(slangOnly.begin(), slangOnly.end(), "sky/cirrus") == slangOnly.end())
                        slangOnly.push_back("sky/cirrus");
                    Cirrus::Settings settings;
                    settings.coverageScale = rendering.mCirrusCoverage;
                    settings.opacityScale = rendering.mCirrusOpacity;
                    settings.altitudeMeters = rendering.mCirrusAltitude * 1000.f;
                    settings.windScale = rendering.mCirrusWindScale;
                    if (const auto quality = Render::Cirrus::parseQuality(rendering.mCirrusQuality.get()))
                        settings.quality = *quality;
                    else
                        Log(Debug::Warning) << "[Rendering] cirrus quality \"" << rendering.mCirrusQuality.get()
                                            << "\" is not low, medium or high; medium is used";
                    if (const auto view = Render::Cirrus::parseDebugView(rendering.mCirrusDebug.get()))
                        settings.debugView = *view;
                    else
                        Log(Debug::Warning)
                            << "[Rendering] cirrus debug \"" << rendering.mCirrusDebug.get()
                            << "\" is not none, density, share, light, footprint or pattern; none is used";
                    mCirrus.setup(vsg::ref_ptr{ new Cirrus(*resourceSystem.builder, settings) }, mSwitch, false);
                    mCirrusOn = true;
                    mCirrusDebugOn = settings.debugView != Render::Cirrus::DebugView::None;
                    Log(Debug::Info) << "Cirrus: a procedural high cloud layer at " << settings.altitudeMeters << " m";
                }
            }
        }

        {
            override_->pipelineOptions = Clouds::pipelineOptions;
            auto node = vsgUtil::readNode(Settings::Manager::getString("skyclouds", "Models"), nodeOptions);
            mClouds.setup(vsg::ref_ptr{ new Clouds(node) }, mSwitch, false);
            mNextClouds.setup(vsg::ref_ptr{ new Clouds(node) }, mSwitch, false);
            // shareDescriptorSet();
        }

        // mUnderwaterSwitch = new UnderwaterSwitchCallback(skyroot);
    }

    Sky::~Sky() {}

    vsg::ref_ptr<vsg::Node> Sky::resetNode()
    {
        return mSun->resetNode();
    }

    void Sky::createRain() {}

    void Sky::destroyRain()
    {
        /*
        if (!mRainNode)
            return;

        mRootNode->removeChild(mRainNode);
        mRainNode = nullptr;
        mPlacer = nullptr;
        mCounter = nullptr;
        mRainParticleSystem = nullptr;
        mRainShooter = nullptr;
        */
    }

    int Sky::getMasserPhase() const
    {
        return mMasser->getPhaseInt();
    }

    int Sky::getSecundaPhase() const
    {
        return mSecunda->getPhaseInt();
    }

    bool Sky::hasRain() const
    {
        return !mRainEffect.empty();
    }

    float Sky::getRainIntensity() const
    {
        // OpenMW 0.51 drew rain ripples on water only for rain (Weather_Rain_Ripples) and for the snow effect
        // (Weather_Snow_Ripples), at the precipitation alpha (SkyManager::getRainRipplesEnabled, cf3dbd3aa7).
        if (hasRain())
            return mRainRipplesEnabled ? mPrecipitationAlpha : 0.f;
        if (mCurrentParticleEffect == Settings::models().mWeathersnow.get().value())
            return mSnowRipplesEnabled ? mPrecipitationAlpha : 0.f;
        return 0.f;
    }

    void Sky::update(float duration)
    {
        switchUnderwaterRain();
        updateParticleOrientation();

        /*
        if (mIsStorm && mParticleNode)
        {
            vsg::dquat quat;
            quat.makeRotate(MWWorld::Weather::defaultDirection(), mStormParticleDirection);
            // Morrowind deliberately rotates the blizzard mesh, so so should we.
            if (mCurrentParticleEffect == Settings::Manager::getString("weatherblizzard", "Models"))
                quat.makeRotate(vsg::vec3(-1,0,0), mStormParticleDirection);
            mParticleNode->setAttitude(quat);
        }
        */

        // OpenEden (task 0038): the share of the lit look the weather has moves to its target over a few seconds of
        // game time, so a change of weather does not make the clouds jump. It rides in the clouds' material
        // (specular alpha, which the cloud shader does not otherwise use); 0, as created, when the look is off.
        if (mCloudsLit || mCloudShadowsOn)
        {
            const float step = duration / cloudShareSeconds;
            mCloudShare += std::clamp(mCloudShareTarget - mCloudShare, -step, step);
        }
        if (mCloudsLit)
        {
            for (auto* clouds : { &*mClouds, &*mNextClouds })
            {
                clouds->material.setCloudShare(mCloudShare);
            }
        }

        // OpenEden (task 0042): the volume's share eases to its weather's value, and its wind blows with the weather's
        // cloud speed, in simulation time.
        if (mVolumeImage)
        {
            mVolumeShare = Render::CloudVolume::easeShare(mVolumeShare, mVolumeShareTarget, duration);
            const float step = Render::CloudVolume::windStep(mCloudSpeed, duration);
            mVolumeWind[0] += static_cast<double>(Render::CloudVolume::windDirection[0]) * step;
            mVolumeWind[1] += static_cast<double>(Render::CloudVolume::windDirection[1]) * step;
        }

        if (mCirrusOn)
            updateCirrus(duration);

        // UV Scroll the clouds
        mCloudAnimationTimer += duration * mCloudSpeed * 0.003;
        vsg::mat4 texmat = vsg::translate(vsg::vec3(0, -mCloudAnimationTimer, 0));
        mClouds->texmat.value() = texmat;
        mClouds->texmat.dirty();
        if (mCloudBlendFactor > 0)
        {
            mNextClouds->texmat.value() = texmat;
            mNextClouds->texmat.dirty();
        }

        // morrowind rotates each cloud mesh independently
        vsg::dquat rotation = Misc::VsgMath::makeRotate(MWWorld::Weather::defaultDirection(), mStormDirection);
        mClouds->setAttitude(vsg::quat(rotation));

        rotation = Misc::VsgMath::makeRotate(MWWorld::Weather::defaultDirection(), mNextStormDirection);
        mNextClouds->setAttitude(vsg::quat(rotation));
        if (mCloudShadowsOn)
        {
            // The same rotations as the layers above, as the shader's matrices (dome space to world).
            const vsg::dquat layerRotation[2]
                = { Misc::VsgMath::makeRotate(MWWorld::Weather::defaultDirection(), mStormDirection),
                      Misc::VsgMath::makeRotate(MWWorld::Weather::defaultDirection(), mNextStormDirection) };
            for (int layer = 0; layer < 2; ++layer)
            {
                const vsg::mat4 m = vsg::rotate(vsg::quat(layerRotation[layer]));
                for (int column = 0; column < 3; ++column)
                    for (int row = 0; row < 3; ++row)
                        mCloudRotation[layer][column * 3 + row] = m[column][row];
            }
        }

        mSun->update();
    }

    void Sky::setAtmosphereNightRoll(float roll)
    {
        mAtmosphereNight->setAttitude(vsg::quat(roll, vsg::vec3(0, 0, 1)));
    }

    void Sky::setMoonColour(bool red)
    {
        mSecunda->setColor(red ? mMoonScriptColor : vsg::vec4(1, 1, 1, 1));
    }

    void Sky::updateRainParameters()
    {
        /*
        if (mRainShooter)
        {
            float angle = -std::atan(mWindSpeed/50.f);
            mRainShooter->setVelocity(vsg::vec3(0, mRainSpeed*std::sin(angle), -mRainSpeed/std::cos(angle)));
            mRainShooter->setAngle(angle);

            vsg::vec3 rainRange = vsg::vec3(mRainDiameter, mRainDiameter, (mRainMinHeight+mRainMaxHeight)/2.f);

            mPlacer->setXRange(-rainRange.x / 2, rainRange.x / 2);
            mPlacer->setYRange(-rainRange.y / 2, rainRange.y / 2);
            mPlacer->setZRange(-rainRange.z / 2, rainRange.z / 2);

            mCounter->setNumberOfParticlesPerSecondToCreate(mRainMaxRaindrops/mRainEntranceSpeed*20);
        }
        */
    }

    void Sky::switchUnderwaterRain()
    {
        /*
        if (!mRainParticleSystem)
            return;

        bool freeze = mUnderwaterSwitch->isUnderwater();
        mRainParticleSystem->setFrozen(freeze);
        */
    }

    void Sky::setWeather(const WeatherResult& weather)
    {
        mRainEntranceSpeed = weather.mRainEntranceSpeed;
        mRainMaxRaindrops = weather.mRainMaxRaindrops;
        mRainDiameter = weather.mRainDiameter;
        mRainMinHeight = weather.mRainMinHeight;
        mRainMaxHeight = weather.mRainMaxHeight;
        mRainSpeed = weather.mRainSpeed;
        mWindSpeed = weather.mWindSpeed;
        mBaseWindSpeed = weather.mBaseWindSpeed;

        if (mRainEffect != weather.mRainEffect)
        {
            mRainEffect = weather.mRainEffect;
            if (!mRainEffect.empty())
            {
                // createRain();
            }
            else
            {
                // destroyRain();
            }
        }

        // updateRainParameters();

        mIsStorm = weather.mIsStorm;
        mStormDirection = weather.mStormDirection;

        mNextStormDirection = weather.mNextStormDirection;

        // openeden-vsg: the effect itself is drawn by WeatherEffects in the main scene (task 0012, V4a).
        mCurrentParticleEffect = weather.mParticleEffect;

        mClouds->setTexture(getOrCreateCloudTexture(weather.mCloudTexture), *mCompile);
        mClouds.setEnabled(mClouds->textureValid() && !mCirrusDebugOn);
        mCloudBlendFactor = std::clamp(weather.mCloudBlendFactor, 0.f, 1.f);
        mClouds->material.setAlpha(1.f - mCloudBlendFactor);
        mClouds->material.setTint(weather.mCloudTint);
        if (!weather.mNextCloudTexture.empty())
        {
            mNextClouds->setTexture(getOrCreateCloudTexture(weather.mNextCloudTexture), *mCompile);
            mNextClouds->material.setAlpha(mCloudBlendFactor);
            mNextClouds->material.setTint(weather.mCloudTint);
        }
        mNextClouds.setEnabled(mCloudBlendFactor > 0.f && mNextClouds->textureValid() && !mCirrusDebugOn);
        mCloudSpeed = weather.mCloudSpeed;
        if (mCloudsLit || mCloudShadowsOn)
        {
            mCloudShareTarget = Render::Cloud::weatherShare(weather.mGlareView, weather.mIsStorm);
            // The first weather (a new game or a load) starts at its share instead of fading in.
            if (!mCloudShareSet)
                mCloudShare = mCloudShareTarget;
            mCloudShareSet = true;
        }
        if (mCloudShadowsOn)
        {
            const auto gridOf = [this](const std::string& file) -> const std::vector<float>* {
                const auto found = mCloudGrids.find(file);
                return found != mCloudGrids.end() ? &found->second : nullptr;
            };
            mCloudGrid[0] = gridOf(weather.mCloudTexture);
            mCloudGrid[1] = weather.mNextCloudTexture.empty() ? nullptr : gridOf(weather.mNextCloudTexture);
        }

        if (mVolumeCloudsOn)
            applyVolumeClouds(weather);
        if (mCirrusOn)
            applyCirrus(weather);

        mSkyColor = weather.mSkyColor;
        mSkyColor.a = weather.mGlareView;

        mSun->setColor(weather.mSunDiscColor);
        mSun->material.setAlpha(weather.mSunDiscColor.a);
        mSun->glareView = weather.mGlareView;

        if (weather.mNight)
            mAtmosphereNight->material.setAlpha(weather.mNightFade);
        mAtmosphereNight.setEnabled(weather.mNight);

        mPrecipitationAlpha = weather.mPrecipitationAlpha;
        mLightSize = 8 + (weather.mNightFade * 20);
    }

    void Sky::sunEnable()
    {
        mSun.setEnabled(true);
        mSun->enabled = true;
    }

    void Sky::sunDisable()
    {
        mSun.setEnabled(false);
        mSun->enabled = false;
    }

    void Sky::setStormParticleDirection(const vsg::vec3& direction)
    {
        mStormParticleDirection = direction;
    }

    void Sky::getRainParameters(Rain::Parameters& out) const
    {
        out.enabled = !mRainEffect.empty();
        out.speed = mRainSpeed;
        out.diameter = mRainDiameter;
        out.minHeight = mRainMinHeight;
        out.maxHeight = mRainMaxHeight;
        out.entranceSpeed = mRainEntranceSpeed;
        out.maxRaindrops = mRainMaxRaindrops;
        out.windSpeed = mWindSpeed;
        out.alpha = mPrecipitationAlpha;
    }

    void Sky::updateParticleOrientation()
    {
        // As OpenMW's SkyManager::update: only storms turn the effect, and it keeps its last turn afterwards.
        if (!mIsStorm)
            return;
        vsg::dquat quat = Misc::VsgMath::makeRotate(MWWorld::Weather::defaultDirection(), mStormParticleDirection);
        // Morrowind deliberately rotates the blizzard mesh, so so should we.
        if (mCurrentParticleEffect == Settings::models().mWeatherblizzard.get().value())
            quat = Misc::VsgMath::makeRotate(vsg::vec3(-1, 0, 0), mStormParticleDirection);
        mParticleOrientation = vsg::quat(quat);
    }

    void Sky::setSunDirection(const vsg::vec3& direction)
    {
        mSun->setDirection(direction);
    }

    void Sky::setMasserState(const MoonState& state)
    {
        mMasser->setState(state);
        // mMasser.setEnabled(isBelowHorizon);
        // CullScreenSpaceQuad(
    }

    void Sky::setSecundaState(const MoonState& state)
    {
        mSecunda->setState(state);
        // mSecunda.setEnabled(isBelowHorizon);
    }

    void Sky::setDate(int day, int month)
    {
        mDay = day;
        mMonth = month;
    }

    void Sky::setGlareTimeOfDayFade(float val)
    {
        mSun->glareTimeOfDayFade = val;
    }

    void Sky::setVolumeClouds(vsg::ref_ptr<vsg::Descriptor> image)
    {
        mVolumeImage = std::move(image);
    }

    Sky::VolumeCloudInput Sky::getVolumeClouds() const
    {
        VolumeCloudInput input;
        input.valid = mVolumeImage != nullptr;
        input.coverage = mVolumeCoverage;
        input.windX = Render::CloudVolume::wrapWind(mVolumeWind[0]);
        input.windY = Render::CloudVolume::wrapWind(mVolumeWind[1]);
        input.share = mVolumeShare;
        return input;
    }

    // Task 0042: the weather's coverage from its cloud pictures, and the cloud dome showing the volume's picture where
    // the weather lets the volume draw. The vanilla picture stays on the second dome, fading out as the share rises.
    void Sky::applyVolumeClouds(const WeatherResult& weather)
    {
        const auto meanThickness = [this](const std::string& file) {
            const auto found = mCloudMeanThickness.find(file);
            return found != mCloudMeanThickness.end() ? found->second : 0.f;
        };
        const float current = Render::CloudVolume::coverageFromThickness(meanThickness(weather.mCloudTexture));
        const float next = weather.mNextCloudTexture.empty()
            ? current
            : Render::CloudVolume::coverageFromThickness(meanThickness(weather.mNextCloudTexture));
        mVolumeCoverage = Render::CloudVolume::blendCoverage(current, next, mCloudBlendFactor);
        mVolumeShareTarget = Render::CloudVolume::volumeShareTarget(weather.mIsStorm, weather.mFogDepth);
        if (std::abs(mVolumeCoverage - mVolumeCoverageLogged) > 0.01f)
        {
            Log(Debug::Info) << "Volumetric clouds: coverage " << mVolumeCoverage << " (" << weather.mCloudTexture
                             << " thickness " << meanThickness(weather.mCloudTexture) << "), share target "
                             << mVolumeShareTarget;
            mVolumeCoverageLogged = mVolumeCoverage;
        }
        // The first weather (a new game or a load) starts at its share instead of fading in.
        if (!mVolumeShareSet)
            mVolumeShare = mVolumeShareTarget;
        mVolumeShareSet = true;
        if (!mVolumeImage)
            return;

        mClouds->material.setVolumeClouds(mVolumeShare > 0.f);
        if (mVolumeShare <= 0.f)
            return;
        mClouds->setTexture(mVolumeImage, *mCompile);
        mClouds->material.setAlpha(mVolumeShare);
        mClouds.setEnabled(mClouds->textureValid());
        // The vanilla picture (the next weather's once it is more than half in) on the second dome, for the part of the
        // sky the volume does not take.
        const std::string& picture = (mCloudBlendFactor > 0.5f && !weather.mNextCloudTexture.empty())
            ? weather.mNextCloudTexture
            : weather.mCloudTexture;
        if (mVolumeShare < 1.f)
        {
            mNextClouds->setTexture(getOrCreateCloudTexture(picture), *mCompile);
            mNextClouds->material.setAlpha(1.f - mVolumeShare);
            mNextClouds->material.setTint(weather.mCloudTint);
            mNextClouds.setEnabled(mNextClouds->textureValid());
        }
        else
            mNextClouds.setEnabled(false);
    }

    // Task 0079: the cirrus's share of the sky from the weather, as the volumetric clouds' coverage is worked out: the
    // mean thickness of the cloud pictures (built whenever the cirrus is on, see getOrCreateCloudTexture) through
    // coverageFromThickness, blended by the manager's factor. The layer thins as the low clouds thicken, and is gone in
    // storms and fog. The share eases to its target in updateCirrus.
    void Sky::applyCirrus(const WeatherResult& weather)
    {
        const auto meanThickness = [this](const std::string& file) {
            const auto found = mCloudMeanThickness.find(file);
            return found != mCloudMeanThickness.end() ? found->second : 0.f;
        };
        const float current = Render::CloudVolume::coverageFromThickness(meanThickness(weather.mCloudTexture));
        const float next = weather.mNextCloudTexture.empty()
            ? current
            : Render::CloudVolume::coverageFromThickness(meanThickness(weather.mNextCloudTexture));
        const float lowCoverage = Render::CloudVolume::blendCoverage(current, next, mCloudBlendFactor);
        mCirrusShareTarget = Render::Cirrus::shareTarget(lowCoverage, weather.mIsStorm, weather.mFogDepth)
            * Render::Cirrus::dayFactor(mDay, mMonth);
        mSunElevation = weather.mSunElevation;
        mCirrusTint = weather.mCloudTint;
        if (std::abs(lowCoverage - mCirrusCoverageLogged) > 0.01f)
        {
            Log(Debug::Info) << "Cirrus: low cloud coverage " << lowCoverage << " (" << weather.mCloudTexture
                             << " thickness " << meanThickness(weather.mCloudTexture) << "), share target "
                             << mCirrusShareTarget;
            mCirrusCoverageLogged = lowCoverage;
        }
        // The first weather (a new game or a load) starts at its share instead of fading in.
        if (!mCirrusShareSet)
            mCirrusShare = mCirrusShareTarget;
        mCirrusShareSet = true;
    }

    // The share eases to its target; the wind's drift and the morph sum in simulation time (never a clock) and are
    // wrapped by the pattern's period, so the floats the shader gets stay small and the noise shows no jump.
    void Sky::updateCirrus(float duration)
    {
        mCirrusShare = Render::Cirrus::easedShare(mCirrusShare, mCirrusShareTarget, duration);
        mCirrusDrift = Render::Cirrus::wrapDrift(
            mCirrusDrift + Render::Cirrus::driftDistance(mCloudSpeed, mCirrus->settings.windScale, duration));
        // One period of the evolution coordinate is 76,800 s; the sum stays exact in a double for a whole session.
        constexpr double secondsPerPeriod
            = static_cast<double>(Render::Cirrus::morphSeconds) * Render::Cirrus::noisePeriodCells;
        mCirrusSeconds = std::fmod(mCirrusSeconds + std::max(duration, 0.f), secondsPerPeriod);

        Cirrus::Frame frame;
        frame.share = mCirrusShare;
        frame.driftMeters = static_cast<float>(mCirrusDrift);
        frame.morph = Render::Cirrus::wrapMorph(mCirrusSeconds);
        frame.sunElevation = mSunElevation;
        frame.tint = mCirrusTint;
        mCirrus->setFrame(frame);
        if (mCirrusShare <= 0.f && mCirrusShareLogged)
        {
            Log(Debug::Info) << "Cirrus: the share reached 0 (the layer is not drawn)";
            mCirrusShareLogged = false;
        }
        else if (mCirrusShare > 0.f)
            mCirrusShareLogged = true;
        // Nothing to draw at a share of 0: the node leaves the traversal (the alpha falls to 0 with the share, so
        // there is no pop).
        mCirrus.setEnabled(mCirrusShare > 0.f);
    }

    Sky::CloudShadowInput Sky::getCloudShadow() const
    {
        CloudShadowInput input;
        if (!mCloudShadowsOn || mCloudGrid[0] == nullptr)
            return input;
        input.valid = true;
        input.grid[0] = mCloudGrid[0]->data();
        input.grid[1] = mCloudGrid[1] != nullptr && mCloudBlendFactor > 0.f ? mCloudGrid[1]->data() : nullptr;
        input.scroll = mCloudAnimationTimer;
        input.blend = input.grid[1] != nullptr ? mCloudBlendFactor : 0.f;
        input.share = mCloudShare;
        input.rotation[0] = mCloudRotation[0];
        input.rotation[1] = mCloudRotation[1];
        return input;
    }

    vsg::ref_ptr<vsg::Descriptor> Sky::getOrCreateCloudTexture(const std::string& file)
    {
        auto found = mCloudTextures.find(file);
        if (found == mCloudTextures.end())
        {
            auto data = vsgUtil::readImage(file, mTextureOptions);
            auto sampler = vsg::Sampler::create();
            share(sampler, mTextureOptions);
            auto descriptor = vsg::DescriptorImage::create(sampler, data);
            if (Render::Cirrus::needsCloudGrids(mCloudShadowsOn, mVolumeCloudsOn, mCirrusOn))
            {
                const auto grid = mCloudGrids.emplace(file, buildCloudGrid(file, *data)).first;
                // The mean thickness gives the volumetric clouds' coverage (see applyVolumeClouds).
                float sum = 0.f;
                for (float v : grid->second)
                    sum += v;
                mCloudMeanThickness[file] = grid->second.empty() ? 0.f : sum / static_cast<float>(grid->second.size());
            }
            found = mCloudTextures.emplace(file, descriptor).first;
        }
        return found->second;
    }
}
