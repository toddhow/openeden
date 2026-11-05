#ifndef OPENEDEN_VSGUTIL_DEFERREDBIN_H
#define OPENEDEN_VSGUTIL_DEFERREDBIN_H

#include <vsg/app/RecordTraversal.h>
#include <vsg/nodes/Bin.h>
#include <vsg/nodes/Node.h>

namespace vsgUtil
{
    /*
     * A bin that collects its elements like any bin but does not draw them where the view is recorded (task 0075,
     * transparents after the TAA resolve). The elements are drawn later, in another render pass, by `replay`, called
     * through a BinReplay node. Elements hold raw node pointers and the state of the traversal that added them, so
     * `replay` is only valid in the same frame, in the same record traversal, with no scene update between: the
     * main view and the replaying view are recorded one after the other by one command graph.
     * Owner: the view that lists it; the BinReplay holds a reference.
     */
    class DeferredBin : public vsg::Inherit<vsg::Bin, DeferredBin>
    {
    public:
        DeferredBin(int32_t number, SortOrder order)
            : Inherit(number, order)
        {
        }

        // Where the bin is recorded: nothing is drawn.
        void traverse(vsg::RecordTraversal&) const override {}

        // Draws the collected elements as vsg::Bin does, sorted as the bin says, with `rt`'s state.
        void replay(vsg::RecordTraversal& rt) const;
    };

    // A node that replays a DeferredBin when it is traversed.
    class BinReplay : public vsg::Inherit<vsg::Node, BinReplay>
    {
    public:
        explicit BinReplay(vsg::ref_ptr<const DeferredBin> bin)
            : mBin(std::move(bin))
        {
        }
        void traverse(vsg::RecordTraversal& rt) const override { mBin->replay(rt); }

    private:
        vsg::ref_ptr<const DeferredBin> mBin;
    };
}

#endif
