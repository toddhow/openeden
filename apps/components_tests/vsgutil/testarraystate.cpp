#include <gtest/gtest.h>

#include <components/vsgutil/arraystate.hpp>

namespace
{
    // An intersector clones a StateGroup's prototypeArrayState through vsg::ArrayState's virtual functions (VSG 1.1,
    // Intersector::apply(const StateGroup&)). A clone that lost the derived class would lose its vertices, with no error.
    class TestState : public vsgUtil::ArrayState<TestState>
    {
    public:
        int mValue = 0;
    };

    TEST(VsgUtilArrayState, aCloneThroughTheBaseClassKeepsTheDerivedClass)
    {
        auto prototype = TestState::create();
        prototype->mValue = 7;
        vsg::ref_ptr<vsg::ArrayState> base = prototype;

        auto* clone = dynamic_cast<TestState*>(base->cloneArrayState().get());
        ASSERT_NE(clone, nullptr);
        EXPECT_EQ(clone->mValue, 7);
    }

    TEST(VsgUtilArrayState, aCloneOfAnotherStateKeepsTheDerivedClassAndTakesTheOthersTopology)
    {
        auto prototype = TestState::create();
        prototype->mValue = 7;
        vsg::ref_ptr<vsg::ArrayState> base = prototype;

        auto other = vsg::ArrayState::create();
        other->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        auto result = base->cloneArrayState(other);
        auto* clone = dynamic_cast<TestState*>(result.get());
        ASSERT_NE(clone, nullptr);
        EXPECT_EQ(clone->mValue, 7);
        EXPECT_EQ(clone->topology, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    }
}
