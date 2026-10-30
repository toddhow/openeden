#include "waterripples.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/sets.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/share.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwworld/cellstore.hpp"

namespace MWRender
{
    namespace
    {
        // OpenMW's RippleSimulation (cf3dbd3aa7).
        constexpr float sMinRippleSize = 15.f;
        constexpr float sMaxRippleSize = 180.f;
        constexpr float sRippleAlpha = 0.7f; // the template's colour alpha, times the alpha range 1 to 0
        constexpr float sMaxHeightFromWater = 20.f; // emitRipple ignores points further from the surface
        constexpr float sEmitterWait = 1.5f;
        constexpr float sEmitterMoveDistance = 10.f;
        constexpr float sFlipCycle = 0.3f; // FlipController's delta is 0.3 / frame count
        constexpr std::size_t sVerticesPerRipple = 6;
        constexpr float sPi = 3.14159265358979323846f;

        RippleSettings readSettings()
        {
            RippleSettings settings;
            settings.maxNumberRipples = Fallback::Map::getInt("Water_MaxNumberRipples");
            settings.lifetime = Fallback::Map::getFloat("Water_RippleLifetime");
            settings.rotationSpeed = Fallback::Map::getFloat("Water_RippleRotSpeed");
            return settings;
        }
    }

    RippleParticles::RippleParticles(const RippleSettings& settings, unsigned seed)
        : mSettings(settings)
        , mRipples(settings.maxNumberRipples > 0 ? static_cast<std::size_t>(settings.maxNumberRipples) + 1 : 0)
        , mRandom(seed)
    {
    }

    void RippleParticles::emit(const vsg::vec3& position)
    {
        if (!(std::abs(position.z - mWaterHeight) < sMaxHeightFromWater) || mRipples.empty())
            return;

        std::size_t index = mCount;
        if (mCount > static_cast<std::size_t>(mSettings.maxNumberRipples))
        {
            // Replace the oldest live ripple, as RippleSimulation's reuseParticle did.
            index = 0;
            for (std::size_t i = 1; i < mCount; ++i)
                if (mRipples[i].age > mRipples[index].age)
                    index = i;
        }
        else
            ++mCount;

        std::uniform_real_distribution<float> unit(0.f, 1.f);
        Ripple& ripple = mRipples[index];
        ripple.position = vsg::vec2(position.x, position.y);
        ripple.age = 0.f;
        ripple.angle = unit(mRandom) * sPi * 2.f - sPi;
    }

    void RippleParticles::step(float dt)
    {
        std::size_t alive = 0;
        for (std::size_t i = 0; i < mCount; ++i)
        {
            Ripple ripple = mRipples[i];
            ripple.age += dt;
            if (mSettings.lifetime > 0.f && ripple.age >= mSettings.lifetime)
                continue;
            ripple.angle += mSettings.rotationSpeed * dt;
            mRipples[alive++] = ripple;
        }
        mCount = alive;
    }

    float RippleParticles::lifeFraction(const Ripple& ripple) const
    {
        // osgParticle interpolates over age / lifetime, and uses the start of each range for endless particles.
        if (!(mSettings.lifetime > 0.f))
            return 0.f;
        return std::clamp(ripple.age / mSettings.lifetime, 0.f, 1.f);
    }

    float RippleParticles::sizeOf(const Ripple& ripple) const
    {
        return sMinRippleSize + (sMaxRippleSize - sMinRippleSize) * lifeFraction(ripple);
    }

    float RippleParticles::alphaOf(const Ripple& ripple) const
    {
        return sRippleAlpha * (1.f - lifeFraction(ripple));
    }

    bool updateRippleEmitter(RippleEmitterTimer& timer, float dt, const vsg::vec3& position, bool inWater)
    {
        timer.timer -= dt;
        if (!inWater)
        {
            timer.timer = 0.f;
            return false;
        }
        if (timer.timer <= 0.f || vsg::length(position - timer.lastEmitPosition) > sEmitterMoveDistance)
        {
            timer.lastEmitPosition = position;
            timer.timer = sEmitterWait;
            return true;
        }
        return false;
    }

    WaterRipples::WaterRipples(const Pipeline::Builder& builder, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> textureOptions)
        : mBuilder(builder)
        , mCompile(std::move(compile))
        , mTextureOptions(std::move(textureOptions))
        , mRoot(vsg::Group::create())
        , mTransform(new Anim::Transform)
        , mParticles(readSettings())
    {
    }

    WaterRipples::~WaterRipples() = default;

    void WaterRipples::create()
    {
        mCreated = true;
        const int frameCount = Fallback::Map::getInt("Water_RippleFrameCount");
        if (frameCount <= 0 || mParticles.capacity() == 0)
            return; // OpenMW drew no ripples then

        const std::size_t vertexCount = mParticles.capacity() * sVerticesPerRipple;
        mVertices = vsg::vec3Array::create(vertexCount);
        auto normals = vsg::vec3Array::create(vertexCount, vsg::vec3(0.f, 0.f, 1.f));
        mColors = vsg::vec4Array::create(vertexCount);
        mUvs = vsg::vec2Array::create(vertexCount);
        mVertices->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        mColors->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        // Corners of the two triangles of each ripple, as osgParticle's c0 c1 c2 and c0 c2 c3 with their texture
        // coordinates.
        const vsg::vec2 corners[sVerticesPerRipple] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
        for (std::size_t i = 0; i < vertexCount; ++i)
            (*mUvs)[i] = corners[i % sVerticesPerRipple];

        Pipeline::Options options;
        options.shader.addMode(Pipeline::Mode::VERTEX);
        options.shader.addMode(Pipeline::Mode::NORMAL);
        options.shader.addMode(Pipeline::Mode::COLOR);
        options.shader.addMode(Pipeline::Mode::TEXCOORD);
        options.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
        options.numUvSets = 1;
        // Lit with the vertex colour as diffuse and ambient: OpenMW's material had a white ambient and took the
        // diffuse from the (white) vertex colour.
        options.colorMode = 2;
        options.cullMode = VK_CULL_MODE_NONE;
        options.blend = true;
        options.depthWrite = false;
        auto pipeline = mBuilder.graphics->getOrCreate(options);

        mDraw = vsg::VertexDraw::create();
        mDraw->assignArrays({ mVertices, normals, mColors, mUvs });
        mDraw->vertexCount = 0;
        mDraw->instanceCount = 1;

        auto sampler = mBuilder.createSampler();
        vsgUtil::share_if(mTextureOptions->sharedObjects, sampler);
        const std::string texture(Fallback::Map::getString("Water_RippleTexture"));
        mFrames = vsg::Switch::create();
        for (int i = 0; i < frameCount; ++i)
        {
            char number[16];
            std::snprintf(number, sizeof(number), "%02d", i);
            const std::string path = "textures/water/" + texture + number + ".dds";
            auto image = vsgUtil::readOptionalImage(path, mTextureOptions);
            if (!image)
            {
                Log(Debug::Warning) << "Water ripples disabled: " << path << " not found";
                mDraw = nullptr;
                mFrames = nullptr;
                return;
            }
            auto textures = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline->layout,
                Pipeline::TEXTURE_SET, vsg::Descriptors{ vsg::DescriptorImage::create(sampler, image) });
            auto stateGroup = vsg::StateGroup::create();
            stateGroup->stateCommands = { pipeline, textures };
            // Ripples take no part in intersection tests.
            stateGroup->prototypeArrayState = vsg::NullArrayState::create();
            stateGroup->addChild(mDraw);
            mFrames->addChild(i == 0, stateGroup);
        }
        mTransform->addChild(mFrames);
        mCompile->compile(mFrames);
    }

    void WaterRipples::addEmitter(const MWWorld::ConstPtr& ptr)
    {
        mEmitters.push_back({ ptr, {} });
    }

    void WaterRipples::removeEmitter(const MWWorld::ConstPtr& ptr)
    {
        auto found = std::find_if(mEmitters.begin(), mEmitters.end(), [&](const Emitter& e) { return e.ptr == ptr; });
        if (found != mEmitters.end())
            mEmitters.erase(found);
    }

    void WaterRipples::updateEmitterPtr(const MWWorld::ConstPtr& old, const MWWorld::ConstPtr& ptr)
    {
        // OpenMW had this but never called it, so actors moving between loaded cells kept a stale cell.
        for (Emitter& emitter : mEmitters)
            if (emitter.ptr == old)
                emitter.ptr = ptr;
    }

    void WaterRipples::removeCell(const MWWorld::CellStore* store)
    {
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        std::erase_if(mEmitters,
            [&](const Emitter& e) { return e.ptr.isInCell() && e.ptr.getCell() == store && e.ptr != player; });
    }

    void WaterRipples::emitRipple(const vsg::vec3& position)
    {
        mParticles.emit(position);
    }

    void WaterRipples::clear()
    {
        mParticles.clear();
    }

    void WaterRipples::setWaterHeight(float height)
    {
        mParticles.setWaterHeight(height);
        mTransform->translation.z = height;
    }

    void WaterRipples::update(float dt, bool paused)
    {
        if (!mCreated)
            create();
        if (!mDraw)
            return;

        if (!paused)
        {
            MWBase::World& world = *MWBase::Environment::get().getWorld();
            for (Emitter& emitter : mEmitters)
            {
                // Fetch the player's Ptr again, as it changes with cells; other actors follow through updateEmitterPtr,
                // or leave through removeCell and come back through addEmitter.
                if (emitter.ptr == world.getPlayerPtr())
                    emitter.ptr = world.getPlayerPtr();
                const osg::Vec3f position = emitter.ptr.getRefData().getPosition().asVec3();
                const bool inWater
                    = (world.isUnderwater(emitter.ptr.getCell(), position) && !world.isSubmerged(emitter.ptr))
                    || world.isWalkingOnWater(emitter.ptr);
                const vsg::vec3 current(position.x(), position.y(), position.z());
                if (updateRippleEmitter(emitter.timer, dt, current, inWater))
                    mParticles.emit(vsg::vec3(current.x, current.y, mParticles.getWaterHeight()));
            }
            mParticles.step(dt);
            // Kept within one flip cycle, so the float does not lose precision over a long session.
            mTime = std::fmod(mTime + dt, sFlipCycle);
        }

        const std::size_t frameCount = mFrames->children.size();
        const std::size_t frame
            = static_cast<std::size_t>(mTime / (sFlipCycle / static_cast<float>(frameCount))) % frameCount;
        for (std::size_t i = 0; i < frameCount; ++i)
            mFrames->children[i].mask = i == frame ? vsg::MASK_ALL : vsg::MASK_OFF;

        draw();
        if (mRoot->children.empty())
            mRoot->addChild(mTransform);
    }

    void WaterRipples::draw()
    {
        const vsg::vec2 corners[sVerticesPerRipple] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
        for (std::size_t i = 0; i < mParticles.size(); ++i)
        {
            const RippleParticles::Ripple& ripple = mParticles[i];
            const float size = mParticles.sizeOf(ripple);
            // osgParticle's FIXED alignment with the align vectors (1, 0, 0) and (0, -1, 0), turned about Z by the
            // ripple's angle; corners at position +- x * size +- y * size.
            const float c = std::cos(ripple.angle);
            const float s = std::sin(ripple.angle);
            const vsg::vec3 xAxis(c * size, s * size, 0.f);
            const vsg::vec3 yAxis(s * size, -c * size, 0.f);
            const vsg::vec3 centre(ripple.position.x, ripple.position.y, 0.f);
            const vsg::vec4 color(1.f, 1.f, 1.f, mParticles.alphaOf(ripple));
            for (std::size_t v = 0; v < sVerticesPerRipple; ++v)
            {
                (*mVertices)[i * sVerticesPerRipple + v]
                    = centre + xAxis * (corners[v].x * 2.f - 1.f) + yAxis * (corners[v].y * 2.f - 1.f);
                (*mColors)[i * sVerticesPerRipple + v] = color;
            }
        }
        mDraw->vertexCount = static_cast<uint32_t>(mParticles.size() * sVerticesPerRipple);
        if (mParticles.size() == 0 && mDrawnCount == 0)
            return; // nothing new to upload
        mDrawnCount = mParticles.size();
        mVertices->dirty();
        mColors->dirty();
    }
}
