#ifndef VSGOPENMW_VIEW_COLLECTLIGHTS_H
#define VSGOPENMW_VIEW_COLLECTLIGHTS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>
#include <vsg/state/ViewDependentState.h>

namespace View
{
    /// A light as the last traversal saw it (task 0077): its place in the view's space, so that nothing needs to read
    /// the node again after the traversal, when the engine may have freed it. `id` is the node's address and is only
    /// compared, never followed.
    struct LightSnapshot
    {
        std::uintptr_t id = 0;
        vsg::dvec3 eyePosition;
        vsg::vec3 color;
        float intensity = 0.f;
    };

    /// A point light that has a shadow map (task 0077): which slot, and how far its shadow has faded in.
    struct ShadowEntry
    {
        std::uintptr_t id = 0;
        int slot = -1;
        float fade = 0.f;
    };

    class CollectLights : public vsg::ViewDependentState
    {
        vsg::ref_ptr<vsg::vec4Array> mData;
        bool mLinear = false;
        // Task 0077: the shadow data buffer's light entries are written here, in the loop that writes the lights, so
        // that an entry always belongs to the light at its index (the list changes from one traversal to the next).
        vsg::ref_ptr<vsg::vec4Array> mShadowData;
        std::size_t mShadowEntryOffset = 0;
        std::vector<ShadowEntry> mShadowEntries; // main thread, between frames; read by pack()
        mutable std::vector<LightSnapshot> mSnapshots; // written by pack(), read by the main thread between frames

    public:
        CollectLights(size_t maxLights);

        vsg::ref_ptr<vsg::vec4Array> data() { return mData; }

        /// Task 0077: makes pack() write the shadow entry (slot or -1, fade) of every light it packs into `data`, from
        /// element `entryOffset` on, and keep a snapshot of each light. Main thread, once, before the first frame.
        void setShadowTarget(vsg::ref_ptr<vsg::vec4Array> data, std::size_t entryOffset);
        /// The lights that have a slot now; main thread, between frames.
        void setShadowEntries(const std::vector<ShadowEntry>& entries) { mShadowEntries = entries; }
        /// The lights of the last pack(), in the order of the light buffer; main thread, between frames. Empty without
        /// a shadow target.
        const std::vector<LightSnapshot>& snapshots() const { return mSnapshots; }

        // The linear HDR path (task 0025): the lights' colours are converted from display-encoded to linear in pack(),
        // for the view that lights in linear space. Set once, before the first frame.
        void setLinear(bool linear) { mLinear = linear; }

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
