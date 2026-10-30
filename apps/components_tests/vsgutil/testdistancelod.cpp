#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/vsgutil/distancelod.hpp>

#include <vsg/all.h>

#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    using vsgUtil::DistanceLOD;

    vsg::ref_ptr<DistanceLOD> makeLod(std::size_t childCount, std::vector<DistanceLOD::Range> ranges)
    {
        auto lod = DistanceLOD::create();
        for (std::size_t i = 0; i < childCount; ++i)
            lod->addChild(vsg::Group::create());
        lod->ranges = std::move(ranges);
        return lod;
    }

    // Counts how often each node is visited, and how many DistanceLODs are seen as Groups.
    class CountingVisitor : public vsg::Visitor
    {
    public:
        std::vector<const vsg::Node*> mVisited;
        unsigned int mGroupCount = 0;

        void apply(vsg::Node& node) override
        {
            mVisited.push_back(&node);
            node.traverse(*this);
        }

        void apply(vsg::Group& group) override
        {
            ++mGroupCount;
            mVisited.push_back(&group);
            group.traverse(*this);
        }

        unsigned int count(const vsg::Node* node) const
        {
            unsigned int n = 0;
            for (const vsg::Node* visited : mVisited)
                if (visited == node)
                    ++n;
            return n;
        }
    };

    class CountingConstVisitor : public vsg::ConstVisitor
    {
    public:
        std::vector<const vsg::Node*> mVisited;

        void apply(const vsg::Node& node) override
        {
            mVisited.push_back(&node);
            node.traverse(*this);
        }

        void apply(const vsg::Group& group) override
        {
            mVisited.push_back(&group);
            group.traverse(*this);
        }

        unsigned int count(const vsg::Node* node) const
        {
            unsigned int n = 0;
            for (const vsg::Node* visited : mVisited)
                if (visited == node)
                    ++n;
            return n;
        }
    };

    // --- 1. isVisible ---

    TEST(VsgUtilDistanceLODTest, DefaultsAreEmpty)
    {
        auto lod = DistanceLOD::create();
        EXPECT_TRUE(lod->ranges.empty());
        EXPECT_TRUE(lod->children.empty());
        EXPECT_EQ(lod->center, vsg::vec3(0.f, 0.f, 0.f));
    }

    TEST(VsgUtilDistanceLODTest, IsAGroup)
    {
        auto lod = DistanceLOD::create();
        EXPECT_NE(lod->cast<vsg::Group>(), nullptr);
        EXPECT_TRUE(lod->is_compatible(typeid(vsg::Group)));
    }

    TEST(VsgUtilDistanceLODTest, DistanceInsideRangeIsVisible)
    {
        auto lod = makeLod(1, { { 10.f, 100.f } });
        EXPECT_TRUE(lod->isVisible(0, 50.0));
    }

    TEST(VsgUtilDistanceLODTest, DistanceExactlyMinIsVisible)
    {
        auto lod = makeLod(1, { { 10.f, 100.f } });
        EXPECT_TRUE(lod->isVisible(0, 10.0));
    }

    TEST(VsgUtilDistanceLODTest, DistanceExactlyMaxIsNotVisible)
    {
        auto lod = makeLod(1, { { 10.f, 100.f } });
        EXPECT_FALSE(lod->isVisible(0, 100.0));
    }

    TEST(VsgUtilDistanceLODTest, DistanceJustBelowMinIsNotVisible)
    {
        auto lod = makeLod(1, { { 10.f, 100.f } });
        EXPECT_FALSE(lod->isVisible(0, 9.999));
        EXPECT_FALSE(lod->isVisible(0, 0.0));
    }

    TEST(VsgUtilDistanceLODTest, DistanceJustBelowMaxIsVisible)
    {
        auto lod = makeLod(1, { { 10.f, 100.f } });
        EXPECT_TRUE(lod->isVisible(0, 99.999));
    }

    TEST(VsgUtilDistanceLODTest, DistanceAboveMaxIsNotVisible)
    {
        auto lod = makeLod(1, { { 10.f, 100.f } });
        EXPECT_FALSE(lod->isVisible(0, 1000.0));
    }

    TEST(VsgUtilDistanceLODTest, ZeroMinIncludesEyeAtCenter)
    {
        auto lod = makeLod(1, { { 0.f, 100.f } });
        EXPECT_TRUE(lod->isVisible(0, 0.0));
    }

    TEST(VsgUtilDistanceLODTest, EmptyRangeIsNeverVisible)
    {
        // min == max: [min, max) contains no distance.
        auto lod = makeLod(1, { { 50.f, 50.f } });
        EXPECT_FALSE(lod->isVisible(0, 50.0));
        EXPECT_FALSE(lod->isVisible(0, 49.0));
        EXPECT_FALSE(lod->isVisible(0, 51.0));
    }

    TEST(VsgUtilDistanceLODTest, InvertedRangeIsNeverVisible)
    {
        auto lod = makeLod(1, { { 100.f, 10.f } });
        EXPECT_FALSE(lod->isVisible(0, 10.0));
        EXPECT_FALSE(lod->isVisible(0, 50.0));
        EXPECT_FALSE(lod->isVisible(0, 100.0));
    }

    TEST(VsgUtilDistanceLODTest, AdjacentRangesSwitchAtBoundary)
    {
        auto lod = makeLod(2, { { 0.f, 100.f }, { 100.f, 1000.f } });
        EXPECT_TRUE(lod->isVisible(0, 99.9));
        EXPECT_FALSE(lod->isVisible(1, 99.9));

        // At exactly the shared boundary only the farther child is drawn.
        EXPECT_FALSE(lod->isVisible(0, 100.0));
        EXPECT_TRUE(lod->isVisible(1, 100.0));
    }

    TEST(VsgUtilDistanceLODTest, OverlappingRangesDrawBothChildren)
    {
        auto lod = makeLod(2, { { 0.f, 200.f }, { 100.f, 300.f } });
        EXPECT_TRUE(lod->isVisible(0, 150.0));
        EXPECT_TRUE(lod->isVisible(1, 150.0));

        EXPECT_TRUE(lod->isVisible(0, 50.0));
        EXPECT_FALSE(lod->isVisible(1, 50.0));

        EXPECT_FALSE(lod->isVisible(0, 250.0));
        EXPECT_TRUE(lod->isVisible(1, 250.0));
    }

    TEST(VsgUtilDistanceLODTest, IdenticalRangesDrawEveryMatchingChild)
    {
        auto lod = makeLod(3, { { 0.f, 100.f }, { 0.f, 100.f }, { 0.f, 100.f } });
        for (std::size_t i = 0; i < 3; ++i)
            EXPECT_TRUE(lod->isVisible(i, 42.0)) << "child " << i;
    }

    TEST(VsgUtilDistanceLODTest, ChildWithoutRangeIsNeverVisible)
    {
        // Three children but only two ranges: child 2 is never drawn.
        auto lod = makeLod(3, { { 0.f, 1000.f }, { 0.f, 1000.f } });
        EXPECT_TRUE(lod->isVisible(0, 10.0));
        EXPECT_TRUE(lod->isVisible(1, 10.0));
        for (double d : { 0.0, 10.0, 999.0, 1000.0, 1e9 })
            EXPECT_FALSE(lod->isVisible(2, d)) << "distance " << d;
    }

    TEST(VsgUtilDistanceLODTest, NoRangesMeansNothingIsVisible)
    {
        auto lod = makeLod(2, {});
        for (std::size_t i = 0; i < 4; ++i)
            for (double d : { 0.0, 1.0, 1000.0 })
                EXPECT_FALSE(lod->isVisible(i, d)) << "child " << i << " distance " << d;
    }

    TEST(VsgUtilDistanceLODTest, IndexFarPastRangesIsNotVisible)
    {
        auto lod = makeLod(1, { { 0.f, 1000.f } });
        EXPECT_FALSE(lod->isVisible(1, 10.0));
        EXPECT_FALSE(lod->isVisible(std::numeric_limits<std::size_t>::max(), 10.0));
    }

    TEST(VsgUtilDistanceLODTest, IsVisibleDoesNotDependOnChildren)
    {
        // The predicate is defined by ranges alone; a range with no matching child still answers.
        auto lod = makeLod(0, { { 0.f, 100.f } });
        EXPECT_TRUE(lod->isVisible(0, 50.0));
        EXPECT_FALSE(lod->isVisible(0, 150.0));
    }

    TEST(VsgUtilDistanceLODTest, MaxRangeAtFloatMaxIsVisibleForLargeDistances)
    {
        auto lod = makeLod(1, { { 0.f, std::numeric_limits<float>::max() } });
        EXPECT_TRUE(lod->isVisible(0, 1e30));
    }

    TEST(VsgUtilDistanceLODTest, CenterAndRangesAreStoredAsSet)
    {
        auto lod = makeLod(2, { { 1.f, 2.f }, { 3.f, 4.f } });
        lod->center = vsg::vec3(1.f, -2.f, 3.5f);
        EXPECT_EQ(lod->center, vsg::vec3(1.f, -2.f, 3.5f));
        ASSERT_EQ(lod->ranges.size(), 2u);
        EXPECT_FLOAT_EQ(lod->ranges[0].min, 1.f);
        EXPECT_FLOAT_EQ(lod->ranges[0].max, 2.f);
        EXPECT_FLOAT_EQ(lod->ranges[1].min, 3.f);
        EXPECT_FLOAT_EQ(lod->ranges[1].max, 4.f);
    }

    // --- 2. Non-record visitors see every child ---

    TEST(VsgUtilDistanceLODTest, VisitorSeesEveryChildIncludingUnrangedOnes)
    {
        // Ranges that would hide everything at any distance, plus children without a range.
        auto lod = makeLod(4, { { 50.f, 50.f }, { 100.f, 10.f } });
        lod->center = vsg::vec3(1000.f, 1000.f, 1000.f);

        CountingVisitor visitor;
        lod->accept(visitor);

        EXPECT_EQ(visitor.count(lod.get()), 1u);
        for (const auto& child : lod->children)
            EXPECT_EQ(visitor.count(child.get()), 1u);
        // The DistanceLOD itself is dispatched as a Group, plus its four Group children.
        EXPECT_EQ(visitor.mGroupCount, 5u);
        EXPECT_EQ(visitor.mVisited.size(), 5u);
    }

    TEST(VsgUtilDistanceLODTest, VisitorSeesEveryChildWithEmptyRanges)
    {
        auto lod = makeLod(3, {});

        CountingVisitor visitor;
        lod->accept(visitor);

        for (const auto& child : lod->children)
            EXPECT_EQ(visitor.count(child.get()), 1u);
    }

    TEST(VsgUtilDistanceLODTest, VisitorTraversesNestedDistanceLODs)
    {
        auto inner = makeLod(2, { { 0.f, 1.f } });
        auto outer = DistanceLOD::create();
        outer->addChild(inner);
        outer->addChild(vsg::Group::create());
        outer->ranges = { { 10.f, 20.f } };

        auto root = vsg::Group::create();
        root->addChild(outer);

        CountingVisitor visitor;
        root->accept(visitor);

        EXPECT_EQ(visitor.count(outer.get()), 1u);
        EXPECT_EQ(visitor.count(inner.get()), 1u);
        for (const auto& child : inner->children)
            EXPECT_EQ(visitor.count(child.get()), 1u);
        EXPECT_EQ(visitor.count(outer->children[1].get()), 1u);
        EXPECT_EQ(visitor.mVisited.size(), 6u);
    }

    TEST(VsgUtilDistanceLODTest, ConstVisitorSeesEveryChild)
    {
        auto lod = makeLod(3, { { 0.f, 1.f } });

        CountingConstVisitor visitor;
        const DistanceLOD& constLod = *lod;
        constLod.accept(visitor);

        EXPECT_EQ(visitor.count(lod.get()), 1u);
        for (const auto& child : lod->children)
            EXPECT_EQ(visitor.count(child.get()), 1u);
        EXPECT_EQ(visitor.mVisited.size(), 4u);
    }

    TEST(VsgUtilDistanceLODTest, ComputeBoundsIncludesEveryChild)
    {
        // Each child is a CullNode with a known bound; ComputeBounds uses node bounds by default.
        auto lod = DistanceLOD::create();
        lod->addChild(vsg::CullNode::create(vsg::dsphere(vsg::dvec3(-10.0, 0.0, 0.0), 1.0), vsg::Group::create()));
        lod->addChild(vsg::CullNode::create(vsg::dsphere(vsg::dvec3(10.0, 0.0, 0.0), 1.0), vsg::Group::create()));
        lod->addChild(vsg::CullNode::create(vsg::dsphere(vsg::dvec3(0.0, 0.0, 50.0), 1.0), vsg::Group::create()));
        // Only child 0 has a range, and it is empty; bounds must still cover all three.
        lod->ranges = { { 5.f, 5.f } };
        lod->center = vsg::vec3(0.f, 0.f, 0.f);

        vsg::ComputeBounds computeBounds;
        lod->accept(computeBounds);

        ASSERT_TRUE(computeBounds.bounds.valid());
        EXPECT_LE(computeBounds.bounds.min.x, -10.0);
        EXPECT_GE(computeBounds.bounds.max.x, 10.0);
        EXPECT_GE(computeBounds.bounds.max.z, 50.0);
    }

    TEST(VsgUtilDistanceLODTest, ComputeBoundsMatchesPlainGroup)
    {
        auto makeChildren = [](vsg::Group& group) {
            group.addChild(vsg::CullNode::create(vsg::dsphere(vsg::dvec3(1.0, 2.0, 3.0), 2.0), vsg::Group::create()));
            group.addChild(vsg::CullNode::create(vsg::dsphere(vsg::dvec3(-4.0, 5.0, -6.0), 0.5), vsg::Group::create()));
        };

        auto group = vsg::Group::create();
        makeChildren(*group);
        auto lod = DistanceLOD::create();
        makeChildren(*lod);
        lod->ranges = { { 0.f, 1.f } };

        vsg::ComputeBounds groupBounds;
        group->accept(groupBounds);
        vsg::ComputeBounds lodBounds;
        lod->accept(lodBounds);

        ASSERT_TRUE(groupBounds.bounds.valid());
        ASSERT_TRUE(lodBounds.bounds.valid());
        EXPECT_DOUBLE_EQ(lodBounds.bounds.min.x, groupBounds.bounds.min.x);
        EXPECT_DOUBLE_EQ(lodBounds.bounds.min.y, groupBounds.bounds.min.y);
        EXPECT_DOUBLE_EQ(lodBounds.bounds.min.z, groupBounds.bounds.min.z);
        EXPECT_DOUBLE_EQ(lodBounds.bounds.max.x, groupBounds.bounds.max.x);
        EXPECT_DOUBLE_EQ(lodBounds.bounds.max.y, groupBounds.bounds.max.y);
        EXPECT_DOUBLE_EQ(lodBounds.bounds.max.z, groupBounds.bounds.max.z);
    }
}

#endif
