#ifndef VSGOPENMW_VIEW_COLLECTLIGHTS_H
#define VSGOPENMW_VIEW_COLLECTLIGHTS_H

#include <vsg/state/ViewDependentState.h>

namespace View
{
    class CollectLights : public vsg::ViewDependentState
    {
        vsg::ref_ptr<vsg::vec4Array> mData;

    public:
        CollectLights(size_t maxLights);

        vsg::ref_ptr<vsg::vec4Array> data() { return mData; }

        void traverse(vsg::Visitor& visitor) override {}
        void traverse(vsg::ConstVisitor& visitor) const override {}

        void compile(vsg::Context&) override {}
        // openeden-vsg: VSG 1.1.16 calls these to build and record its own light and shadow-map descriptor
        // sets, which dereference ViewDependentState::view (unset here) and would replace vsgopenmw's light
        // buffer. CollectLights only collects point lights for pack(), so they do nothing.
        void init(vsg::ResourceRequirements&) override {}
        void update(vsg::ResourceRequirements&) override {}
        void bindDescriptorSets(vsg::CommandBuffer&, VkPipelineBindPoint, VkPipelineLayout, uint32_t) override {}
        // openeden-vsg: called by RecordTraversal after the view's lights are collected; it replaces the pack() hook
        // of the VSG version vsgopenmw was written for, so the light buffer is filled each frame.
        void traverse(vsg::RecordTraversal&) const override { pack(); }
        void pack() const;
    };
}

#endif
