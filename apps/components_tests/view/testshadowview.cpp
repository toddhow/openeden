#include <gtest/gtest.h>

#include <vsg/app/View.h>
#include <vsg/state/ViewDependentState.h>

#include <components/view/shadow.hpp>

namespace
{
    // `object shadows = true` crashed the game in vsg::TransferTask: vsg::View's copy constructor gives every copy a
    // default vsg::ViewDependentState, and a shadow cascade's view is a copy of the shared base view. The lights in
    // the objects' graph were then collected by that state, and the shadow pass worked on data never set up for it.
    // A shadow view must have no view dependent state at all (as MWRender::PrecipitationOccluder's).
    TEST(ViewShadowViewTest, a_plain_view_copy_does_get_a_view_dependent_state)
    {
        // The reason for the rule: if VSG stops doing this, the rule can be reviewed.
        auto base = vsg::View::create();
        base->viewDependentState = nullptr;
        auto copy = vsg::View::create(*base);
        EXPECT_TRUE(copy->viewDependentState.valid());
    }

    TEST(ViewShadowViewTest, the_shadow_view_has_no_view_dependent_state)
    {
        auto base = vsg::View::create();
        base->viewDependentState = nullptr;
        auto view = View::createShadowView(*base);
        ASSERT_TRUE(view.valid());
        EXPECT_FALSE(view->viewDependentState.valid());
    }

    TEST(ViewShadowViewTest, the_shadow_view_shares_the_base_views_id)
    {
        auto base = vsg::View::create();
        auto view = View::createShadowView(*base);
        EXPECT_EQ(view->viewID, base->viewID);
    }
}
