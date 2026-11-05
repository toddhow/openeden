#include "deferredbin.hpp"

namespace vsgUtil
{
    void DeferredBin::replay(vsg::RecordTraversal& rt) const
    {
        // vsg::Bin::traverse sorts, pushes each element's matrix and state commands and visits its node.
        vsg::Bin::traverse(rt);
    }
}
