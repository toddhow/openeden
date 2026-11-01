#include "rain.hpp"

#include <algorithm>
#include <cmath>

#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>

#include <components/debug/debuglog.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/precipitationmath.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/share.hpp>

#include <vsg/utils/SharedObjects.h>

namespace MWRender
{
    namespace
    {
        // OpenMW's rain (SkyManager::createRain, cf3dbd3aa7).
        constexpr float sDropLifetime = 1.f;
        constexpr float sMinDropSize = 5.f;
        constexpr float sMaxDropSize = 15.f;
        constexpr float sDropWidthFactor = 0.1f; // the particle alignment's X vector
        constexpr float sEmitRateFactor = 20.f; // per second: maxRaindrops / entranceSpeed * 20
        constexpr float sWindAngleDivisor = 50.f; // angle = -atan(windSpeed / 50)
        constexpr float sMaxEmitStep = 0.2f; // RainCounter caps the emission time step, as the engine's frame loop
        constexpr const char* sRaindropTexture = "textures/tx_raindrop_01.dds";
        constexpr std::size_t sVerticesPerDrop = 6;
        // WeatherAlphaOperator scaled rain, not snow, by this ("Rain_Threshold?" in OpenMW's sky.cpp).
        constexpr float sRainAlphaFactor = 0.6f;
        // NifOsg::ParticleSystem gave every particle this normal; the material took ambient and diffuse from the
        // vertex colour, so rain is dimmed by the ambient light and the sun.
        constexpr float sParticleNormal = 0.3f;
        // The default shader's colour mode with the vertex colour as ambient and diffuse.
        constexpr uint32_t sAmbientAndDiffuseColorMode = 2;

        float wrap(float value, float range)
        {
            // OpenMW's WrapAroundOperator: keep value within [-range / 2, range / 2].
            if (!(range > 0.f))
                return 0.f; // an empty box keeps every drop at the camera, rather than NaN from fmod
            const float half = range / 2.f;
            if (value < -half)
                return half + std::fmod(value - half, range);
            if (value > half)
                return std::fmod(value + half, range) - half;
            return value;
        }

        float sizeAtAge(float age)
        {
            // osgParticle interpolates the template's size range linearly over the particle's life.
            return sMinDropSize + (sMaxDropSize - sMinDropSize) * std::clamp(age / sDropLifetime, 0.f, 1.f);
        }
    }

    Rain::Rain(const Pipeline::Builder& builder, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> textureOptions)
        : mBuilder(builder)
        , mCompile(std::move(compile))
        , mTextureOptions(std::move(textureOptions))
        , mRoot(vsg::Group::create())
    {
    }

    Rain::~Rain() = default;

    void Rain::create()
    {
        const std::size_t vertexCount = kMaxDrops * sVerticesPerDrop;
        mSimulation = std::make_unique<RainSimulation>(kMaxDrops);
        mVertices = vsg::vec3Array::create(vertexCount);
        mColors = vsg::vec4Array::create(vertexCount);
        mUvs = vsg::vec2Array::create(vertexCount);
        auto normals
            = vsg::vec3Array::create(vertexCount, vsg::vec3(sParticleNormal, sParticleNormal, sParticleNormal));
        mVertices->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        mColors->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        // Corners of the two triangles of each drop: (0,0) (1,0) (1,1) and (0,0) (1,1) (0,1).
        const vsg::vec2 corners[sVerticesPerDrop] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
        for (std::size_t i = 0; i < vertexCount; ++i)
            (*mUvs)[i] = vsg::vec2(corners[i % sVerticesPerDrop].x, 1.f - corners[i % sVerticesPerDrop].y);

        Pipeline::Options options;
        options.shader.addMode(Pipeline::Mode::VERTEX);
        options.shader.addMode(Pipeline::Mode::NORMAL);
        options.shader.addMode(Pipeline::Mode::COLOR);
        options.shader.addMode(Pipeline::Mode::TEXCOORD);
        options.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
        options.numUvSets = 1;
        options.colorMode = sAmbientAndDiffuseColorMode;
        options.cullMode = VK_CULL_MODE_NONE;
        options.blend = true;
        if (mOcclusionMap)
            options.shader.addMode(Pipeline::Mode::PARTICLE_OCCLUSION);
        if (mLit)
            options.shader.addMode(Pipeline::Mode::PRECIPITATION_LIT);
        options.depthWrite = false;
        auto pipeline = mBuilder.graphics->getOrCreate(options);

        auto image = vsgUtil::readOptionalImage(sRaindropTexture, mTextureOptions);
        if (!image)
        {
            // Content without the raindrop texture gets no rain, rather than an exception mid-game.
            Log(Debug::Warning) << "Rain disabled: " << sRaindropTexture << " not found";
            mUnavailable = true;
            return;
        }
        auto sampler = mBuilder.createSampler();
        sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vsgUtil::share_if(mTextureOptions->sharedObjects, sampler);
        auto texture = vsg::DescriptorImage::create(sampler, image);
        auto textures = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout,
            Pipeline::TEXTURE_SET,
            mOcclusionMap ? vsg::Descriptors{ texture, mOcclusionMap } : vsg::Descriptors{ texture });

        mDraw = vsg::VertexDraw::create();
        mDraw->assignArrays({ mVertices, normals, mColors, mUvs });
        mDraw->vertexCount = 0;
        mDraw->instanceCount = 1;

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->stateCommands = { pipeline, textures };
        // Rain takes no part in intersection tests.
        stateGroup->prototypeArrayState = vsg::NullArrayState::create();
        stateGroup->addChild(mDraw);
        mContents = stateGroup;
        mCompile->compile(mContents);
    }

    RainSimulation::RainSimulation(std::size_t capacity, unsigned seed)
        : mDrops(capacity)
        , mRandom(seed)
    {
    }

    void RainSimulation::clear()
    {
        mCount = 0;
        mEmitCarry = 0.f;
    }

    void RainSimulation::step(float dt, const vsg::vec3& cameraMove, const RainParameters& parameters)
    {
        const vsg::vec3 range(
            parameters.diameter, parameters.diameter, (parameters.minHeight + parameters.maxHeight) / 2.f);
        const float angle = -std::atan(parameters.windSpeed / sWindAngleDivisor);
        const vsg::vec3 velocity(0.f, parameters.speed * std::sin(angle), -parameters.speed / std::cos(angle));

        // Age, move and wrap the living drops, keeping them in place in the world as the camera moves.
        std::size_t alive = 0;
        for (std::size_t i = 0; i < mCount; ++i)
        {
            Drop drop = mDrops[i];
            drop.age += dt;
            if (drop.age >= sDropLifetime)
                continue;
            drop.position += velocity * dt - cameraMove;
            drop.position.x = wrap(drop.position.x, range.x);
            drop.position.y = wrap(drop.position.y, range.y);
            drop.position.z = wrap(drop.position.z, range.z);
            drop.size = sizeAtAge(drop.age);
            mDrops[alive++] = drop;
        }
        mCount = alive;

        // Emit new drops at random places in the box around the camera.
        if (parameters.entranceSpeed > 0.f)
            mEmitCarry
                += std::min(dt, sMaxEmitStep) * parameters.maxRaindrops / parameters.entranceSpeed * sEmitRateFactor;
        std::uniform_real_distribution<float> unit(-0.5f, 0.5f);
        while (mEmitCarry >= 1.f)
        {
            mEmitCarry -= 1.f;
            if (mCount == mDrops.size())
            {
                ++mDropped;
                continue;
            }
            Drop& drop = mDrops[mCount++];
            drop.position = vsg::vec3(unit(mRandom) * range.x, unit(mRandom) * range.y, unit(mRandom) * range.z);
            drop.age = 0.f;
            drop.size = sizeAtAge(0.f);
        }
    }

    void Rain::draw(const vsg::vec3& cameraPosition, const Parameters& parameters)
    {
        const float angle = -std::atan(parameters.windSpeed / sWindAngleDivisor);
        // The particle's angle turns it about X by -angle, as RainShooter set it.
        const vsg::vec3 up(0.f, std::sin(angle), std::cos(angle));
        const vsg::vec4 color(1.f, 1.f, 1.f, std::clamp(parameters.alpha, 0.f, 1.f) * sRainAlphaFactor);
        const vsg::vec2 corners[sVerticesPerDrop] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
        const float stretch = mLit ? Render::Precipitation::streakStretch(parameters.speed) : 1.f;
        const RainSimulation& drops = *mSimulation;
        for (std::size_t i = 0; i < drops.size(); ++i)
        {
            const RainSimulation::Drop& drop = drops[i];
            const vsg::vec3 centre = cameraPosition + drop.position;
            // osgParticle puts a FIXED particle's corners at centre +- alignX * size +- alignY * size, so the quad is
            // twice the size across (ParticleSystem::drawImplementation, OSG 3.6.5).
            const vsg::vec3 across(sDropWidthFactor * drop.size, 0.f, 0.f);
            // Task 0040: lit rain is drawn longer the faster it falls, so that fast rain reads as streaks.
            const vsg::vec3 along = up * (drop.size * stretch);
            for (std::size_t v = 0; v < sVerticesPerDrop; ++v)
            {
                const vsg::vec2& corner = corners[v];
                (*mVertices)[i * sVerticesPerDrop + v]
                    = centre + across * (corner.x * 2.f - 1.f) + along * (corner.y * 2.f - 1.f);
                (*mColors)[i * sVerticesPerDrop + v] = color;
            }
        }
        mDraw->vertexCount = static_cast<uint32_t>(drops.size() * sVerticesPerDrop);
        mVertices->dirty();
        mColors->dirty();
    }

    void Rain::update(float dt, const vsg::vec3& cameraPosition, const Parameters& parameters, bool visible)
    {
        const vsg::vec3 cameraMove = mHasLastCameraPosition ? cameraPosition - mLastCameraPosition : vsg::vec3();
        mLastCameraPosition = cameraPosition;
        mHasLastCameraPosition = true;

        if (!parameters.enabled)
        {
            if (mSimulation)
                mSimulation->clear();
            mRoot->children.clear();
            return;
        }
        if (!mContents && !mUnavailable)
            create();
        if (!mContents)
            return;
        if (!visible)
        {
            // Frozen while hidden, as OpenMW's underwater switch froze the particle system.
            mRoot->children.clear();
            return;
        }
        if (mRoot->children.empty())
            mRoot->children = { mContents };

        mSimulation->step(dt, cameraMove, parameters);
        if (mSimulation->dropped() > 0 && !mReportedOverflow)
        {
            Log(Debug::Warning) << "Rain: more than " << kMaxDrops << " drops alive at once; extra drops are not drawn";
            mReportedOverflow = true;
        }
        draw(cameraPosition, parameters);
    }
}
