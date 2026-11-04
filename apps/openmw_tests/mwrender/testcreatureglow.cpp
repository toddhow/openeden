// Tests for task 0074e, the fresh review of task 0074d (finding 9): a weapon-carrying creature's part gets reflective
// glow (MWRender::addEnv, object data of its own on Pipeline::OBJECT_SET) only with a glow colour, as an NPC's part
// does. addEnv also wraps a part when the alpha is below 1, and with no colour it then dereferences the empty colour,
// so a part linked with no glow colour to a transparent creature must get no object data. Written from the task's test
// specification before the implementation and run red against it.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/transparency.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/mwanimation/context.hpp>
#include <components/pipeline/layout.hpp>
#include <components/pipeline/object.hpp>
#include <components/pipeline/sets.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/Visitor.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/BindDescriptorSet.h>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace
{
    constexpr float transparentAlpha = 0.5f;

    // A creature that wields (MWAnim::createCreature for a creature with weapons), with linkPart in reach.
    struct GlowCreature : public MWRender::WieldingCreature
    {
        using MWRender::WieldingCreature::linkPart;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    // Every BindDescriptorSet on the object set in a subgraph, the state commands of its state groups included, also
    // under switches whose children are off (Wielding's switches start off).
    class CollectObjectSets : public vsg::Visitor
    {
    public:
        CollectObjectSets() { overrideMask = vsg::MASK_ALL; }

        using vsg::Visitor::apply;

        void apply(vsg::Object& object) override { object.traverse(*this); }

        void apply(vsg::BindDescriptorSet& bind) override
        {
            if (bind.firstSet == Pipeline::OBJECT_SET)
                mFound.push_back(&bind);
        }

        std::vector<vsg::BindDescriptorSet*> mFound;
    };

    struct CreatureGlowTest : public ::testing::Test
    {
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<GlowCreature> mCreature;

        void SetUp() override
        {
            mContext.compileContext = PartsTestSupport::quietCompileContext();
            mCreature = std::make_unique<GlowCreature>(mContext);
            PartsTestSupport::addCreatureSkeleton(
                *mCreature, PartsTestSupport::makeSkeleton({ "Weapon Bone", "Weapon Bone Left", "Shield Bone" }));
            // Transparent, with the state command MWRender::setTransparency makes, without compiling it.
            vsg::StateGroup* stateGroup = mCreature->getOrCreateStateGroup();
            Pipeline::Object objectData;
            objectData.value().alpha = transparentAlpha;
            stateGroup->stateCommands = { vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,
                Pipeline::getCompatiblePipelineLayout(), Pipeline::OBJECT_SET,
                vsg::Descriptors{ objectData.descriptor() }) };
            ASSERT_EQ(MWRender::getTransparency(*mCreature), transparentAlpha);
        }

        // The object sets below the creature's own state group (its children, not its own state commands): those of
        // the parts linked to it.
        std::vector<vsg::BindDescriptorSet*> partObjectSets()
        {
            CollectObjectSets visitor;
            for (const auto& child : mCreature->getStateGroup()->children)
                child->accept(visitor);
            return visitor.mFound;
        }
    };

    // Finding 9: a weapon with no glow colour linked to a transparent creature gets no object data of its own.
    TEST_F(CreatureGlowTest, a_weapon_with_no_glow_colour_on_a_transparent_creature_gets_no_object_data)
    {
        mCreature->linkPart(MWWorld::InventoryStore::Slot_CarriedRight, PartsTestSupport::makePlainModel(), nullptr,
            "Weapon Bone", std::nullopt);

        EXPECT_TRUE(partObjectSets().empty());
    }

    // Finding 9, guard: with a glow colour the weapon on the same creature gets object data of its own, with that
    // colour and the creature's alpha.
    TEST_F(CreatureGlowTest, a_weapon_with_a_glow_colour_on_a_transparent_creature_gets_the_colour_and_the_alpha)
    {
        const vsg::vec4 glow(0.25f, 0.5f, 1.f, 1.f);
        mCreature->linkPart(MWWorld::InventoryStore::Slot_CarriedRight, PartsTestSupport::makePlainModel(), nullptr,
            "Weapon Bone", glow);

        const std::vector<vsg::BindDescriptorSet*> found = partObjectSets();
        ASSERT_EQ(found.size(), 1u);
        MWRender::GetObjectData data;
        found[0]->accept(data);
        ASSERT_TRUE(data.value.valid());
        const vsg::vec4 envColor = data.value->value().envColor;
        EXPECT_EQ(envColor.r, glow.r);
        EXPECT_EQ(envColor.g, glow.g);
        EXPECT_EQ(envColor.b, glow.b);
        EXPECT_EQ(envColor.a, glow.a);
        EXPECT_EQ(data.value->value().alpha, transparentAlpha);
    }
}
