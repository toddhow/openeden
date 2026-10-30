// Tests for OpenMW's built-in NIF switch nodes restored on VSG (task 0012, V4). Written from
// apps/openmw/mwrender/switches.hpp and the spec only.
//
// Conventions these tests rely on:
// - The NIF reader stores a NiSwitchNode's name as the vsg::Switch's string value "switch".
// - vsg::Switch::Child is { Mask mask; ref_ptr<Node> node; } (vsg/nodes/Switch.h); MASK_ALL / MASK_OFF from
//   vsg/core/Mask.h.
// - Anim::Transform (components/animation/transform.hpp) is a vsg::Transform, hence a vsg::Group.

#include "apps/openmw/mwrender/switches.hpp"

#include <components/animation/transform.hpp>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/nodes/Switch.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using MWRender::findSwitch;
    using MWRender::makeSwitchesUnique;
    using MWRender::setSingleChildOn;

    vsg::ref_ptr<vsg::Switch> makeSwitch(const std::string& name, std::size_t numChildren, std::size_t onIndex = 0)
    {
        auto sw = vsg::Switch::create();
        sw->setValue("switch", name);
        for (std::size_t i = 0; i < numChildren; ++i)
            sw->addChild(i == onIndex, vsg::Node::create());
        return sw;
    }

    // ---- findSwitch ----

    TEST(MWRenderSwitchesTest, findSwitchReturnsNullptrWhenNoSwitch)
    {
        auto root = vsg::Group::create();
        root->addChild(vsg::Node::create());
        root->addChild(vsg::Group::create());
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), nullptr);
    }

    TEST(MWRenderSwitchesTest, findSwitchReturnsNullptrWhenOnlyOtherNames)
    {
        auto root = vsg::Group::create();
        root->addChild(makeSwitch("Other", 2));
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), nullptr);
    }

    TEST(MWRenderSwitchesTest, findSwitchFindsNodeItself)
    {
        auto sw = makeSwitch("NightDaySwitch", 2);
        EXPECT_EQ(findSwitch(*sw, "NightDaySwitch"), sw.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchFindsDirectChild)
    {
        auto root = vsg::Group::create();
        auto sw = makeSwitch("NightDaySwitch", 2);
        root->addChild(vsg::Node::create());
        root->addChild(sw);
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), sw.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchRequiresExactName)
    {
        auto root = vsg::Group::create();
        root->addChild(makeSwitch("NightDaySwitchX", 2));
        root->addChild(makeSwitch("nightdayswitch", 2));
        root->addChild(makeSwitch("NightDay", 2));
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), nullptr);
    }

    TEST(MWRenderSwitchesTest, findSwitchIgnoresSwitchWithoutName)
    {
        auto root = vsg::Group::create();
        auto unnamed = vsg::Switch::create();
        unnamed->addChild(true, vsg::Node::create());
        root->addChild(unnamed);
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), nullptr);
        EXPECT_EQ(findSwitch(*root, ""), nullptr);
    }

    TEST(MWRenderSwitchesTest, findSwitchTraversesNestedGroups)
    {
        auto root = vsg::Group::create();
        auto a = vsg::Group::create();
        auto b = vsg::Group::create();
        auto sw = makeSwitch("HerbalismSwitch", 2);
        root->addChild(a);
        a->addChild(b);
        b->addChild(sw);
        EXPECT_EQ(findSwitch(*root, "HerbalismSwitch"), sw.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchTraversesAnimTransform)
    {
        auto root = vsg::Group::create();
        auto t = Anim::Transform::create();
        auto sw = makeSwitch("HerbalismSwitch", 2);
        root->addChild(t);
        t->addChild(sw);
        EXPECT_EQ(findSwitch(*root, "HerbalismSwitch"), sw.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchTraversesIntoNonMatchingSwitch)
    {
        auto root = vsg::Group::create();
        auto outer = makeSwitch("Other", 0);
        auto inner = makeSwitch("NightDaySwitch", 2);
        outer->addChild(false, vsg::Node::create());
        outer->addChild(false, inner); // disabled child still searched
        root->addChild(outer);
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), inner.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchReturnsFirstDepthFirst)
    {
        auto root = vsg::Group::create();
        auto deep = vsg::Group::create();
        auto first = makeSwitch("NightDaySwitch", 2);
        auto second = makeSwitch("NightDaySwitch", 2);
        // first is deep inside child 0, second is a direct child at index 1: depth-first in order finds first.
        deep->addChild(vsg::Group::create());
        deep->addChild(first);
        root->addChild(deep);
        root->addChild(second);
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), first.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchPrefersOuterMatchOverNestedMatch)
    {
        auto root = vsg::Group::create();
        auto outer = makeSwitch("NightDaySwitch", 0);
        auto inner = makeSwitch("NightDaySwitch", 2);
        outer->addChild(true, inner);
        root->addChild(outer);
        EXPECT_EQ(findSwitch(*root, "NightDaySwitch"), outer.get());
    }

    TEST(MWRenderSwitchesTest, findSwitchConstOverloadMatchesNonConst)
    {
        auto root = vsg::Group::create();
        auto g = vsg::Group::create();
        auto sw = makeSwitch("HerbalismSwitch", 2);
        root->addChild(vsg::Node::create());
        root->addChild(g);
        g->addChild(sw);
        const vsg::Node& constRoot = *root;
        const vsg::Switch* found = findSwitch(constRoot, "HerbalismSwitch");
        EXPECT_EQ(found, sw.get());
        EXPECT_EQ(findSwitch(constRoot, "Missing"), nullptr);
    }

    // ---- setSingleChildOn ----

    TEST(MWRenderSwitchesTest, setSingleChildOnTurnsOnlyIndexOn)
    {
        auto sw = makeSwitch("NightDaySwitch", 3, 0);
        setSingleChildOn(*sw, 1);
        ASSERT_EQ(sw->children.size(), 3u);
        EXPECT_EQ(sw->children[0].mask, vsg::MASK_OFF);
        EXPECT_EQ(sw->children[1].mask, vsg::MASK_ALL);
        EXPECT_EQ(sw->children[2].mask, vsg::MASK_OFF);

        setSingleChildOn(*sw, 0);
        EXPECT_EQ(sw->children[0].mask, vsg::MASK_ALL);
        EXPECT_EQ(sw->children[1].mask, vsg::MASK_OFF);
        EXPECT_EQ(sw->children[2].mask, vsg::MASK_OFF);
    }

    TEST(MWRenderSwitchesTest, setSingleChildOnOverridesPartialMasks)
    {
        auto sw = vsg::Switch::create();
        sw->addChild(vsg::Mask(0x2), vsg::Node::create());
        sw->addChild(vsg::Mask(0x4), vsg::Node::create());
        setSingleChildOn(*sw, 1);
        EXPECT_EQ(sw->children[0].mask, vsg::MASK_OFF);
        EXPECT_EQ(sw->children[1].mask, vsg::MASK_ALL);
    }

    TEST(MWRenderSwitchesTest, setSingleChildOnPastEndTurnsAllOff)
    {
        auto sw = vsg::Switch::create();
        sw->addChild(true, vsg::Node::create());
        sw->addChild(true, vsg::Node::create());
        setSingleChildOn(*sw, 2);
        EXPECT_EQ(sw->children[0].mask, vsg::MASK_OFF);
        EXPECT_EQ(sw->children[1].mask, vsg::MASK_OFF);

        sw->children[0].mask = vsg::MASK_ALL;
        setSingleChildOn(*sw, 100);
        EXPECT_EQ(sw->children[0].mask, vsg::MASK_OFF);
        EXPECT_EQ(sw->children[1].mask, vsg::MASK_OFF);
    }

    TEST(MWRenderSwitchesTest, setSingleChildOnKeepsChildNodes)
    {
        auto sw = makeSwitch("NightDaySwitch", 2);
        auto n0 = sw->children[0].node;
        auto n1 = sw->children[1].node;
        setSingleChildOn(*sw, 1);
        ASSERT_EQ(sw->children.size(), 2u);
        EXPECT_EQ(sw->children[0].node, n0);
        EXPECT_EQ(sw->children[1].node, n1);
    }

    TEST(MWRenderSwitchesTest, setSingleChildOnEmptySwitchIsSafe)
    {
        auto sw = vsg::Switch::create();
        setSingleChildOn(*sw, 0);
        EXPECT_TRUE(sw->children.empty());
    }

    // ---- makeSwitchesUnique ----

    struct ChildSnapshot
    {
        vsg::Mask mask;
        vsg::Node* node;
    };

    std::vector<ChildSnapshot> snapshot(const vsg::Switch& sw)
    {
        std::vector<ChildSnapshot> result;
        for (const auto& child : sw.children)
            result.push_back({ child.mask, child.node.get() });
        return result;
    }

    void expectSameChildren(const vsg::Switch& sw, const std::vector<ChildSnapshot>& expected)
    {
        ASSERT_EQ(sw.children.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(sw.children[i].mask, expected[i].mask) << "child " << i;
            EXPECT_EQ(sw.children[i].node.get(), expected[i].node) << "child " << i;
        }
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueReplacesDirectChildSwitch)
    {
        auto shared = makeSwitch("NightDaySwitch", 2, 0);
        const auto before = snapshot(*shared);

        auto group = vsg::Group::create();
        group->addChild(shared);
        makeSwitchesUnique(*group, "NightDaySwitch");

        ASSERT_EQ(group->children.size(), 1u);
        vsg::Switch* copy = findSwitch(*group->children[0], "NightDaySwitch");
        ASSERT_NE(copy, nullptr);
        EXPECT_NE(copy, shared.get());
        expectSameChildren(*copy, before);
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueReplacesNestedSwitch)
    {
        // A shared model: Transform -> Group -> Switch.
        auto model = Anim::Transform::create();
        auto mid = vsg::Group::create();
        auto shared = makeSwitch("HerbalismSwitch", 2, 0);
        model->addChild(mid);
        mid->addChild(shared);
        const auto before = snapshot(*shared);

        auto group = vsg::Group::create();
        group->addChild(model);
        makeSwitchesUnique(*group, "HerbalismSwitch");

        ASSERT_EQ(group->children.size(), 1u);
        vsg::Switch* copy = findSwitch(*group->children[0], "HerbalismSwitch");
        ASSERT_NE(copy, nullptr);
        EXPECT_NE(copy, shared.get());
        expectSameChildren(*copy, before);

        // The original shared model still holds the original switch.
        EXPECT_EQ(findSwitch(*model, "HerbalismSwitch"), shared.get());
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueChangingCopyLeavesOriginalUnchanged)
    {
        auto model = vsg::Group::create();
        auto shared = makeSwitch("NightDaySwitch", 2, 0);
        model->addChild(shared);
        const auto before = snapshot(*shared);

        auto group = vsg::Group::create();
        group->addChild(model);
        makeSwitchesUnique(*group, "NightDaySwitch");

        vsg::Switch* copy = findSwitch(*group, "NightDaySwitch");
        ASSERT_NE(copy, nullptr);
        ASSERT_NE(copy, shared.get());
        setSingleChildOn(*copy, 1);
        EXPECT_EQ(copy->children[0].mask, vsg::MASK_OFF);
        EXPECT_EQ(copy->children[1].mask, vsg::MASK_ALL);

        expectSameChildren(*shared, before);
        EXPECT_EQ(findSwitch(*model, "NightDaySwitch"), shared.get());
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueMakesTwoInstancesIndependent)
    {
        // One shared model placed in two instance groups, as vsgopenmw shares static models.
        auto model = Anim::Transform::create();
        auto mid = vsg::Group::create();
        auto shared = makeSwitch("NightDaySwitch", 2, 0);
        model->addChild(mid);
        mid->addChild(shared);
        const auto before = snapshot(*shared);

        auto groupA = vsg::Group::create();
        auto groupB = vsg::Group::create();
        groupA->addChild(model);
        groupB->addChild(model);
        makeSwitchesUnique(*groupA, "NightDaySwitch");
        makeSwitchesUnique(*groupB, "NightDaySwitch");

        vsg::Switch* swA = findSwitch(*groupA, "NightDaySwitch");
        vsg::Switch* swB = findSwitch(*groupB, "NightDaySwitch");
        ASSERT_NE(swA, nullptr);
        ASSERT_NE(swB, nullptr);
        EXPECT_NE(swA, shared.get());
        EXPECT_NE(swB, shared.get());
        EXPECT_NE(swA, swB);
        expectSameChildren(*swA, before);
        expectSameChildren(*swB, before);

        setSingleChildOn(*swA, 1);
        EXPECT_EQ(swA->children[0].mask, vsg::MASK_OFF);
        EXPECT_EQ(swA->children[1].mask, vsg::MASK_ALL);
        // B and the original are unaffected.
        expectSameChildren(*swB, before);
        expectSameChildren(*shared, before);
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueHandlesEveryChildOfGroup)
    {
        auto sharedA = makeSwitch("NightDaySwitch", 2, 0);
        auto sharedB = makeSwitch("NightDaySwitch", 3, 2);
        auto modelA = vsg::Group::create();
        modelA->addChild(sharedA);
        auto modelB = Anim::Transform::create();
        modelB->addChild(sharedB);
        const auto beforeA = snapshot(*sharedA);
        const auto beforeB = snapshot(*sharedB);

        auto group = vsg::Group::create();
        group->addChild(modelA);
        group->addChild(vsg::Node::create());
        group->addChild(modelB);
        makeSwitchesUnique(*group, "NightDaySwitch");

        ASSERT_EQ(group->children.size(), 3u);
        vsg::Switch* copyA = findSwitch(*group->children[0], "NightDaySwitch");
        vsg::Switch* copyB = findSwitch(*group->children[2], "NightDaySwitch");
        ASSERT_NE(copyA, nullptr);
        ASSERT_NE(copyB, nullptr);
        EXPECT_NE(copyA, sharedA.get());
        EXPECT_NE(copyB, sharedB.get());
        expectSameChildren(*copyA, beforeA);
        expectSameChildren(*copyB, beforeB);
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueOnlyCopiesNamedSwitch)
    {
        // Only the named switch must be a new object; the other-named switch is not asserted on except that the
        // named one becomes unique and the original named switch is unaffected.
        auto model = vsg::Group::create();
        auto other = makeSwitch("Other", 2, 0);
        auto named = makeSwitch("HerbalismSwitch", 2, 0);
        model->addChild(other);
        model->addChild(named);
        const auto before = snapshot(*named);

        auto group = vsg::Group::create();
        group->addChild(model);
        makeSwitchesUnique(*group, "HerbalismSwitch");

        vsg::Switch* copy = findSwitch(*group, "HerbalismSwitch");
        ASSERT_NE(copy, nullptr);
        EXPECT_NE(copy, named.get());
        expectSameChildren(*copy, before);
        setSingleChildOn(*copy, 1);
        expectSameChildren(*named, before);
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueFindsSwitchInsideNonMatchingSwitch)
    {
        auto model = vsg::Group::create();
        auto outer = makeSwitch("Other", 0);
        auto inner = makeSwitch("NightDaySwitch", 2, 0);
        outer->addChild(true, inner);
        model->addChild(outer);
        const auto before = snapshot(*inner);

        auto group = vsg::Group::create();
        group->addChild(model);
        makeSwitchesUnique(*group, "NightDaySwitch");

        vsg::Switch* copy = findSwitch(*group, "NightDaySwitch");
        ASSERT_NE(copy, nullptr);
        EXPECT_NE(copy, inner.get());
        expectSameChildren(*copy, before);
        setSingleChildOn(*copy, 1);
        expectSameChildren(*inner, before);
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueWithoutSwitchKeepsChildren)
    {
        auto model = vsg::Group::create();
        model->addChild(vsg::Node::create());
        auto leaf = vsg::Node::create();

        auto group = vsg::Group::create();
        group->addChild(model);
        group->addChild(leaf);
        makeSwitchesUnique(*group, "NightDaySwitch");

        ASSERT_EQ(group->children.size(), 2u);
        EXPECT_EQ(group->children[0].get(), model.get());
        EXPECT_EQ(group->children[1].get(), leaf.get());
        EXPECT_EQ(findSwitch(*group, "NightDaySwitch"), nullptr);
    }

    TEST(MWRenderSwitchesTest, makeSwitchesUniqueEmptyGroupIsSafe)
    {
        auto group = vsg::Group::create();
        makeSwitchesUnique(*group, "NightDaySwitch");
        EXPECT_TRUE(group->children.empty());
    }
}
