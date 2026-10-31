// Tests for task 0012, step V4k (soft particles), "Size": vsgAdapters::handleInitialParticles stores the controller's
// initial size (NiParticleSystemController::mInitialSize) in each output particle's maxAge.y.
//
// Written from the spec alone, without reading the implementation. Assumptions:
// - The caller passes an array with one element per particle (partctrl.mNumParticles == partctrl.mParticles.size() ==
//   data.mNumParticles == data.mVertices.size()); the function writes into that array. Whatever size the array has
//   afterwards, every element in it must carry maxAge.y == mInitialSize.
// - A particle is "inactive" when its index is at or past the valid/active count (partctrl.mNumValid and
//   data.mActiveCount) or its lifespan is 0; inactive particles get maxAge.y too.
// - NiParticleInfo::mCode is the particle's vertex index, as in OpenMW's NIF particle loader; it is set to the index.
// - Per-particle sizes (data.mSizes) may be present or absent and never replace mInitialSize in maxAge.y.
// - maxAge.x is the lifespan; the test only checks it does not depend on mInitialSize (the same inputs with a
//   different mInitialSize give the same maxAge.x), not its exact value.
// - The array is prefilled with a sentinel so an unwritten maxAge.y is caught.

#include <components/nif/controller.hpp>
#include <components/nif/particle.hpp>
#include <components/vsgadapters/nif/particle.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace
{
    constexpr float sentinel = -12345.f;
    constexpr std::size_t numParticles = 6;
    constexpr std::size_t numActive = 3;

    struct Inputs
    {
        Nif::NiParticlesData data{};
        Nif::NiParticleSystemController ctrl{};
    };

    // Six particles: 0-2 within the active count (particle 1 with a lifespan of 0), 3-5 past it.
    Inputs makeInputs(float initialSize, bool withSizes)
    {
        Inputs in;

        in.data.mNumVertices = static_cast<uint16_t>(numParticles);
        in.data.mNumParticles = static_cast<uint16_t>(numParticles);
        in.data.mActiveCount = static_cast<uint16_t>(numActive);
        for (std::size_t i = 0; i < numParticles; ++i)
        {
            const float f = static_cast<float>(i);
            in.data.mVertices.emplace_back(f, 2.f * f, -f);
            in.data.mColors.emplace_back(0.5f, 0.25f, 1.f, 1.f);
            if (withSizes)
                // Deliberately different from each other and from either initial size used below.
                in.data.mSizes.push_back(0.5f + 3.f * f);
        }

        in.ctrl.mInitialSize = initialSize;
        in.ctrl.mInitialColor = vsg::vec4(1.f, 1.f, 1.f, 1.f);
        in.ctrl.mInitialNormal = vsg::vec3(0.f, 0.f, 1.f);
        in.ctrl.mLifetime = 4.f;
        in.ctrl.mNumParticles = static_cast<uint16_t>(numParticles);
        in.ctrl.mNumValid = static_cast<uint16_t>(numActive);
        for (std::size_t i = 0; i < numParticles; ++i)
        {
            Nif::NiParticleInfo info{};
            const float f = static_cast<float>(i);
            info.mVelocity = vsg::vec3(f, 0.f, 1.f);
            info.mRotationAxis = vsg::vec3(0.f, 0.f, 1.f);
            info.mAge = 0.25f * f;
            info.mLifespan = (i == 1) ? 0.f : 2.f + f;
            info.mLastUpdate = 0.f;
            info.mSpawnGeneration = 0;
            info.mCode = static_cast<uint16_t>(i);
            in.ctrl.mParticles.push_back(info);
        }
        return in;
    }

    vsg::ref_ptr<vsg::Array<Pipeline::Data::Particle>> run(const Inputs& in)
    {
        auto array = vsg::Array<Pipeline::Data::Particle>::create(numParticles);
        for (auto& p : *array)
        {
            p.positionSize = vsg::vec4(sentinel, sentinel, sentinel, sentinel);
            p.color = vsg::vec4(sentinel, sentinel, sentinel, sentinel);
            p.velocityAge = vsg::vec4(sentinel, sentinel, sentinel, sentinel);
            p.maxAge = vsg::vec4(sentinel, sentinel, sentinel, sentinel);
        }
        vsgAdapters::handleInitialParticles(*array, in.data, in.ctrl);
        return array;
    }

    void expectAllSizes(float initialSize, bool withSizes)
    {
        const Inputs in = makeInputs(initialSize, withSizes);
        auto array = run(in);
        ASSERT_GE(array->size(), numParticles);
        for (std::size_t i = 0; i < array->size(); ++i)
            EXPECT_EQ(array->at(i).maxAge.y, initialSize)
                << "particle " << i << (i < numActive ? " (active" : " (inactive")
                << (withSizes ? ", with sizes)" : ", without sizes)");
    }

    TEST(VsgAdaptersSoftParticleSizeTest, everyParticleGetsInitialSizeWithoutPerParticleSizes)
    {
        expectAllSizes(7.5f, false);
    }

    TEST(VsgAdaptersSoftParticleSizeTest, everyParticleGetsInitialSizeWithPerParticleSizes)
    {
        expectAllSizes(7.5f, true);
    }

    TEST(VsgAdaptersSoftParticleSizeTest, secondInitialSizeWithoutPerParticleSizes)
    {
        expectAllSizes(42.25f, false);
    }

    TEST(VsgAdaptersSoftParticleSizeTest, secondInitialSizeWithPerParticleSizes)
    {
        expectAllSizes(42.25f, true);
    }

    TEST(VsgAdaptersSoftParticleSizeTest, inactiveParticlesGetInitialSize)
    {
        const float initialSize = 3.f;
        auto array = run(makeInputs(initialSize, true));
        ASSERT_GE(array->size(), numParticles);
        // Past the active count, and the zero-lifespan particle within it.
        for (std::size_t i = numActive; i < numParticles; ++i)
            EXPECT_EQ(array->at(i).maxAge.y, initialSize) << "particle " << i;
        EXPECT_EQ(array->at(1).maxAge.y, initialSize);
    }

    TEST(VsgAdaptersSoftParticleSizeTest, lifespanDoesNotDependOnInitialSize)
    {
        for (bool withSizes : { false, true })
        {
            auto a = run(makeInputs(7.5f, withSizes));
            auto b = run(makeInputs(42.25f, withSizes));
            ASSERT_EQ(a->size(), b->size());
            for (std::size_t i = 0; i < a->size(); ++i)
            {
                EXPECT_EQ(a->at(i).maxAge.x, b->at(i).maxAge.x)
                    << "particle " << i << (withSizes ? " with sizes" : " without sizes");
                EXPECT_NE(a->at(i).maxAge.x, 7.5f) << "particle " << i;
                EXPECT_NE(b->at(i).maxAge.x, 42.25f) << "particle " << i;
            }
        }
    }
}
