#ifndef OPENEDEN_MWRENDER_RAIN_H
#define OPENEDEN_MWRENDER_RAIN_H

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/Descriptor.h>

namespace Pipeline
{
    class Builder;
}
namespace vsgUtil
{
    class CompileContext;
}
namespace vsg
{
    class Options;
    class VertexDraw;
}

namespace MWRender
{
    /// The current weather's rain (Sky::getRainParameters; OpenMW's WeatherResult rain fields).
    struct RainParameters
    {
        bool enabled = false;
        float speed = 0.f;
        float diameter = 0.f;
        float minHeight = 0.f;
        float maxHeight = 0.f;
        float entranceSpeed = 1.f;
        int maxRaindrops = 0;
        float windSpeed = 0.f;
        float alpha = 1.f;
    };

    /**
     * @brief The raindrops of OpenMW's procedural rain, with no renderer (task 0012, V4): SkyManager::createRain and
     * its RainShooter, RainCounter and WrapAroundOperator at cf3dbd3aa7.
     *
     * Each step:
     * - Drops age by dt and are removed once they reach their one-second life.
     * - The survivors move by the rain velocity times dt, and against the camera's movement, so they keep their place
     *   in the world. The velocity is (0, speed * sin(a), -speed / cos(a)), with a = -atan(windSpeed / 50).
     * - Each coordinate is wrapped into the box around the camera: rain diameter by rain diameter by
     *   (min height + max height) / 2, centred on the camera.
     * - New drops are emitted at maxRaindrops / entranceSpeed * 20 per second, with the fraction carried over, at
     *   uniformly random places in the box. The emission time step is capped at 0.2 s, as OpenMW's RainCounter did, so
     *   a long frame does not release a burst. Emissions beyond the capacity are dropped and counted (dropped()).
     * - A drop's size grows linearly with its age, from 5 when emitted to 15 at the end of its life, as osgParticle
     *   interpolates a particle template's size range over its lifetime (OSG 3.6.5 Particle::update).
     *
     * Positions are relative to the camera. The random sequence is seeded, so a run is repeatable. step() allocates
     * nothing; the constructor allocates the capacity.
     */
    class RainSimulation
    {
    public:
        struct Drop
        {
            vsg::vec3 position; // relative to the camera
            float age = 0.f;
            float size = 0.f;
        };

        explicit RainSimulation(std::size_t capacity, unsigned seed = 1);

        void step(float dt, const vsg::vec3& cameraMove, const RainParameters& parameters);
        /// Removes every drop and the carried-over emission.
        void clear();

        std::size_t size() const { return mCount; }
        std::size_t capacity() const { return mDrops.size(); }
        /// Emissions dropped because the capacity was full, since construction.
        std::size_t dropped() const { return mDropped; }
        const Drop& operator[](std::size_t index) const { return mDrops[index]; }

    private:
        std::vector<Drop> mDrops;
        std::size_t mCount = 0;
        std::size_t mDropped = 0;
        float mEmitCarry = 0.f;
        std::minstd_rand mRandom;
    };

    /**
     * @brief Draws RainSimulation's drops around the camera (task 0012, V4). Each drop is a quad 0.2 x 2 times its
     * size, tilted by the wind, as OpenMW's fixed particle alignment, with the raindrop texture, lit by the ambient
     * light and the sun, fogged, and faded by 0.6 times the precipitation alpha. Without the raindrop texture, rain is
     * not drawn and a warning is logged.
     *
     * Owned by RenderManager; update() runs on the main thread. Storage for kMaxDrops drops is created on first use, so
     * updating allocates nothing.
     */
    class Rain
    {
    public:
        /// Drops alive at once; the rate OpenMW emits for one second of a drop's life stays below this.
        static constexpr std::size_t kMaxDrops = 4096;

        using Parameters = RainParameters;

        Rain(const Pipeline::Builder& builder, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            vsg::ref_ptr<const vsg::Options> textureOptions);
        ~Rain();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        /// Discards drops under the occlusion map's surface (PrecipitationOccluder). Set before the first update.
        void setOcclusionMap(vsg::ref_ptr<vsg::Descriptor> map) { mOcclusionMap = std::move(map); }

        /// Task 0040: draw the rain as streaks that scatter the scene's light (lib/light/precipitation.glsl), fade out with
        /// distance and lengthen with the rain's speed. Set before the first update; off by default.
        void setLit(bool lit) { mLit = lit; }

        /// Advances and redraws the rain; visible false (underwater, sky off) hides it and keeps it still.
        void update(float dt, const vsg::vec3& cameraPosition, const Parameters& parameters, bool visible);

    private:
        void create();
        void draw(const vsg::vec3& cameraPosition, const Parameters& parameters);

        const Pipeline::Builder& mBuilder;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<const vsg::Options> mTextureOptions;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<vsg::Descriptor> mOcclusionMap;
        vsg::ref_ptr<vsg::Node> mContents;
        vsg::ref_ptr<vsg::VertexDraw> mDraw;
        vsg::ref_ptr<vsg::vec3Array> mVertices;
        vsg::ref_ptr<vsg::vec4Array> mColors;
        vsg::ref_ptr<vsg::vec2Array> mUvs;

        std::unique_ptr<RainSimulation> mSimulation;
        vsg::vec3 mLastCameraPosition;
        bool mHasLastCameraPosition = false;
        bool mUnavailable = false;
        bool mReportedOverflow = false;
        bool mLit = false;
    };
}

#endif
