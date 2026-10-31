#ifndef VSGOPENMW_VIEW_ENV_H
#define VSGOPENMW_VIEW_ENV_H

#include <vsg/core/ref_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/state/Descriptor.h>

namespace View
{
    /// The reflective glow's texture array for a view's descriptor set.
    vsg::ref_ptr<vsg::Descriptor> readEnv(vsg::ref_ptr<const vsg::Options> options);
}

#endif
