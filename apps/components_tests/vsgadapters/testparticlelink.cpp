#include <components/vsgadapters/nif/particle.hpp>

#include <gtest/gtest.h>

#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>

#include <components/animation/context.hpp>
#include <components/animation/transform.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/node.hpp>
#include <components/vsgutil/id.hpp>

namespace
{
    // The bug of 2026-11-01: an equipped torch's flame was drawn in the air, away from the torch. A part attached to an
    // actor is linked with a path that starts at the skeleton's root bone, and ParticleSystem::link searched that whole
    // tree for the emitter and particle node by their NIF record index. The skeleton has its own NIF, so one of its
    // bones can have the same index as the torch's emitter; the search then took the bone as the emitter and never
    // reached the torch.
    //
    //   bone (index 5, at x = 100)  ->  torch root (index 1, at z = 10)  ->  emitter (index 5, at z = 50)
    //                                                                    ->  particle node (index 38)
    constexpr int sEmitterIndex = 5;
    constexpr int sParticleIndex = 38;

    struct Scene
    {
        vsg::ref_ptr<Anim::Transform> bone = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> torchRoot = Anim::Transform::create();
        vsg::ref_ptr<Anim::Transform> emitter = Anim::Transform::create();
        vsg::ref_ptr<vsg::CullNode> particleNode;

        explicit Scene(int boneIndex)
        {
            bone->translation = vsg::vec3(100.f, 0.f, 0.f);
            vsgUtil::setID(*bone, boneIndex);
            torchRoot->translation = vsg::vec3(0.f, 0.f, 10.f);
            vsgUtil::setID(*torchRoot, 1);
            emitter->translation = vsg::vec3(0.f, 0.f, 50.f);
            vsgUtil::setID(*emitter, sEmitterIndex);
            particleNode = vsg::CullNode::create(vsg::dsphere(vsg::dvec3(), 1.0), vsg::Group::create());
            vsgUtil::setID(*particleNode, sParticleIndex);
            torchRoot->addChild(emitter);
            torchRoot->addChild(particleNode);
            bone->addChild(torchRoot);
        }
    };

    vsg::vec3 linkAndGetEmitPosition(Scene& scene)
    {
        Nif::NiNode emitterRecord;
        emitterRecord.mRecordIndex = sEmitterIndex;
        Nif::NiParticleSystemController partctrl;
        partctrl.mEmitter = Nif::NiAVObjectPtr(&emitterRecord);
        // Emitting for the whole run, so that the system is active and sets its emit matrix.
        partctrl.mTimeStart = 0.f;
        partctrl.mTimeStop = 10.f;
        partctrl.mEmitStartTime = 0.f;
        partctrl.mEmitStopTime = 10.f;
        vsgAdapters::ParticleSystem system(partctrl, sParticleIndex, false);

        // As Npc::insertBoundedPart: the path runs from the skeleton's root bone to the attached node.
        Anim::Context ctx{ { /* actor */ }, { scene.bone.get(), scene.torchRoot.get() }, nullptr, nullptr };
        auto holder = vsg::Group::create();
        system.link(ctx, *holder);

        auto frameArgs = vsg::Value<Pipeline::Data::FrameArgs>::create();
        system.apply(*frameArgs, 1.f);
        const vsg::mat4& m = frameArgs->value().emitMatrix;
        return vsg::vec3(m[3][0], m[3][1], m[3][2]);
    }

    TEST(VsgAdaptersParticleLinkTest, emitterIsFoundInTheAttachedPartNotInTheSkeleton)
    {
        Scene scene(sEmitterIndex); // the skeleton bone shares the emitter's index
        const vsg::vec3 p = linkAndGetEmitPosition(scene);
        EXPECT_NEAR(p.x, 0.f, 1e-4f);
        EXPECT_NEAR(p.y, 0.f, 1e-4f);
        EXPECT_NEAR(p.z, 50.f, 1e-4f);
    }

    TEST(VsgAdaptersParticleLinkTest, emitterIsFoundWhenNoBoneSharesItsIndex)
    {
        Scene scene(77);
        const vsg::vec3 p = linkAndGetEmitPosition(scene);
        EXPECT_NEAR(p.x, 0.f, 1e-4f);
        EXPECT_NEAR(p.y, 0.f, 1e-4f);
        EXPECT_NEAR(p.z, 50.f, 1e-4f);
    }
}
